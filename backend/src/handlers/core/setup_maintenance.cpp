#include "handlers/internal/shared.hpp"

static bool azure_auth_config_ready(Database &db)
{
    auto tenant = app_config::get(db, app_config::kAzureTenantId);
    auto client = app_config::get(db, app_config::kAzureClientId);
    auto secret = app_config::get(db, app_config::kAzureClientSecret);
    return tenant.has_value() && !tenant->empty() && client.has_value() && !client->empty() && secret;
}

static bool setup_contact_email_configured(Database &db, std::string *out_email = nullptr)
{
    auto vapid_subject = app_config::get(db, app_config::kVapidSubject);
    if (!vapid_subject || vapid_subject->empty() || vapid_subject->rfind("mailto:", 0) != 0)
        return false;

    std::string email = vapid_subject->substr(7);
    if (email.empty() || email == "admin@example.com")
        return false;

    if (out_email)
        *out_email = email;
    return true;
}

static bool setup_config_ready(Database &db)
{
    return azure_auth_config_ready(db) && setup_contact_email_configured(db);
}

static void apply_azure_runtime_env(const std::string &tenant,
                                    const std::string &client,
                                    const std::string &secret)
{
    setenv("AZURE_TENANT_ID", tenant.c_str(), 1);
    setenv("AZURE_CLIENT_ID", client.c_str(), 1);
    setenv("AZURE_CLIENT_SECRET", secret.c_str(), 1);
}

void sync_azure_runtime_env_from_db(Database &db)
{
    auto tenant = app_config::get(db, app_config::kAzureTenantId);
    auto client = app_config::get(db, app_config::kAzureClientId);
    auto secret = app_config::get(db, app_config::kAzureClientSecret);

    if (tenant && !tenant->empty())
        setenv("AZURE_TENANT_ID", tenant->c_str(), 1);
    else
        unsetenv("AZURE_TENANT_ID");

    if (client && !client->empty())
        setenv("AZURE_CLIENT_ID", client->c_str(), 1);
    else
        unsetenv("AZURE_CLIENT_ID");

    if (secret && !secret->empty())
        setenv("AZURE_CLIENT_SECRET", secret->c_str(), 1);
    else
        unsetenv("AZURE_CLIENT_SECRET");
}

MaintenanceState get_maintenance_state(Database &db);

void handle_get_setup_auth_config(HttpRes *res, HttpReq *req, Database &db)
{
    (void)req;
    try
    {
        auto tenant = app_config::get(db, app_config::kAzureTenantId);
        auto client = app_config::get(db, app_config::kAzureClientId);
        auto secret = app_config::get(db, app_config::kAzureClientSecret);
        const int autosave_interval_ms = app_config::get_int_or(db, app_config::kAutosaveIntervalMs, 1500, 250, 600000);
        const bool autosave_debounce = app_config::get_or(db, app_config::kAutosaveDebounce, "true") != "false";
        const int midata_weather_refresh_interval_ms = app_config::get_int_or(db, app_config::kMidataWeatherRefreshIntervalMs, 900000, 10000, 86400000);
        const std::string wp_url = app_config::get_or(db, app_config::kWpUrl, "");
        const std::string public_base_url_value = app_config::get_or(db, app_config::kPublicBaseUrl, "");
        const bool midata_configured = !app_config::get_or(db, app_config::kMidataApiKey, "").empty();
        const bool wp_configured_flag = wp_configured(db);
        const bool github_bug_report_configured = !app_config::get_or(db, app_config::kGitHubToken, "").empty();
        const bool tenant_configured = tenant.has_value() && !tenant->empty();
        const bool client_configured = client.has_value() && !client->empty();
        const bool client_secret_configured = secret.has_value() && !secret->empty();
        std::string contact_email;
        const bool contact_email_configured = setup_contact_email_configured(db, &contact_email);
        const bool configured = setup_config_ready(db);
        const auto maint = get_maintenance_state(db);

        nlohmann::json j = {
            {"configured", configured},
            {"tenant_id", (tenant && !tenant->empty()) ? nlohmann::json(*tenant) : nlohmann::json("")},
            {"tenant_id_configured", tenant_configured},
            {"client_id", (client && !client->empty()) ? nlohmann::json(*client) : nlohmann::json("")},
            {"client_id_configured", client_configured},
            {"client_secret_configured", client_secret_configured},
            {"contact_email", contact_email},
            {"contact_email_configured", contact_email_configured},
            {"autosave_interval_ms", autosave_interval_ms},
            {"autosave_debounce", autosave_debounce},
            {"midata_weather_refresh_interval_ms", midata_weather_refresh_interval_ms},
            {"wp_url", wp_url},
            {"public_base_url", public_base_url_value},
            {"midata_configured", midata_configured},
            {"wp_configured", wp_configured_flag},
            {"github_bug_report_configured", github_bug_report_configured},
            {"maintenance_active", maint.active},
            {"maintenance_message", maint.message},
            {"maintenance_scheduled_start", maint.scheduled_start},
            {"maintenance_scheduled_end", maint.scheduled_end},
        };
        send_json(res, 200, j.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_post_setup_auth_config(HttpRes *res, HttpReq *req, Database &db)
{
    (void)req;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})");
            return;
        }

        if (setup_config_ready(db)) {
            send_json(res, 409, R"({"error":"Auth-Konfiguration ist bereits gesetzt"})");
            return;
        }

        auto existing_tenant = app_config::get(db, app_config::kAzureTenantId);
        auto existing_client = app_config::get(db, app_config::kAzureClientId);
        auto existing_secret = app_config::get(db, app_config::kAzureClientSecret);
        std::string existing_contact_email;
        bool existing_contact_email_configured = setup_contact_email_configured(db, &existing_contact_email);

        std::string tenant = j.value("tenant_id", "");
        std::string client = j.value("client_id", "");
        std::string secret = j.value("client_secret", "");
        std::string contact_email = j.value("contact_email", "");

        if (tenant.empty() && existing_tenant && !existing_tenant->empty())
            tenant = *existing_tenant;
        if (client.empty() && existing_client && !existing_client->empty())
            client = *existing_client;
        if (secret.empty() && existing_secret && !existing_secret->empty())
            secret = *existing_secret;
        if (contact_email.empty() && existing_contact_email_configured)
            contact_email = existing_contact_email;

        if (tenant.empty() || client.empty() || secret.empty() || contact_email.empty()) {
            send_json(res, 400, R"({"error":"Bitte alle fehlenden Setup-Felder ausfuellen"})");
            return;
        }

        try {
            std::string err;
            auto set_if_missing = [&](const char *key,
                                      const std::string &value,
                                      bool already_configured,
                                      bool is_secret_value) -> bool {
                if (already_configured) return true;
                if (is_secret_value) {
                    if (value.empty()) return false;
                } else {
                    if (value.empty()) return false;
                }
                return app_config::set_from_admin(db, key, value, err);
            };

            const bool tenant_configured = existing_tenant && !existing_tenant->empty();
            const bool client_configured = existing_client && !existing_client->empty();
            const bool secret_configured = existing_secret && !existing_secret->empty();

            if (!set_if_missing(app_config::kAzureTenantId, tenant, tenant_configured, false) ||
                !set_if_missing(app_config::kAzureClientId, client, client_configured, false) ||
                !set_if_missing(app_config::kAzureClientSecret, secret, secret_configured, true) ||
                !set_if_missing(app_config::kVapidSubject, contact_email, existing_contact_email_configured, false)) {
                if (err == "invalid-email") {
                    send_json(res, 400, R"({"error":"Ungueltige E-Mail-Adresse"})");
                    return;
                }
                if (err == "locked-by-env") {
                    send_json(res, 409, R"({"error":"Mindestens ein Setup-Wert wird durch ENV vorgegeben"})");
                    return;
                }
                send_json(res, 500, R"({"error":"Konfiguration konnte nicht gespeichert werden"})");
                return;
            }

            bool generated_vapid = false;
            if (!app_config::ensure_generated_vapid(db, err, generated_vapid)) {
                send_json(res, 500, R"({"error":"VAPID-Konfiguration konnte nicht erzeugt werden"})");
                return;
            }

            apply_azure_runtime_env(tenant, client, secret);
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// ── Maintenance mode ──────────────────────────────────────────────────────────

static time_t parse_iso8601(const std::string &s);

namespace
{
    struct MaintenanceWindow
    {
        std::string id;
        std::string start;
        std::string end;
        std::string message;
        std::string recurrence{"none"}; // none|daily|weekly|monthly
        int interval{1};
        std::string until;
    };

    struct MaintenanceOccurrence
    {
        std::string window_id;
        time_t start_ts{0};
        time_t end_ts{0};
        std::string start;
        std::string end;
        std::string message;
        std::string recurrence{"none"};
    };

    bool is_valid_recurrence(const std::string &freq)
    {
        return freq == "none" || freq == "daily" || freq == "weekly" || freq == "monthly";
    }

    std::string iso8601_local_minute(time_t ts)
    {
        std::tm tm{};
        localtime_r(&ts, &tm);
        char buf[20];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M", &tm);
        return std::string(buf);
    }

    time_t add_months_local(time_t ts, int months)
    {
        std::tm tm{};
        localtime_r(&ts, &tm);
        tm.tm_mon += months;
        tm.tm_isdst = -1;
        return mktime(&tm);
    }

    void push_occurrence(const MaintenanceWindow &w,
                         time_t start_ts,
                         time_t end_ts,
                         std::vector<MaintenanceOccurrence> &out)
    {
        if (end_ts <= start_ts)
            return;
        MaintenanceOccurrence occ;
        occ.window_id = w.id;
        occ.start_ts = start_ts;
        occ.end_ts = end_ts;
        occ.start = iso8601_local_minute(start_ts);
        occ.end = iso8601_local_minute(end_ts);
        occ.message = w.message;
        occ.recurrence = w.recurrence;
        out.push_back(std::move(occ));
    }

    std::vector<MaintenanceWindow> parse_maintenance_windows_raw(const std::string &raw)
    {
        std::vector<MaintenanceWindow> out;
        auto j = nlohmann::json::parse(raw, nullptr, false);
        if (!j.is_array())
            return out;

        size_t idx = 0;
        for (const auto &entry : j)
        {
            if (!entry.is_object())
                continue;
            MaintenanceWindow w;
            w.id = trim_ascii(entry.value("id", ""));
            if (w.id.empty())
                w.id = std::string("w-") + std::to_string(++idx);
            w.start = trim_ascii(entry.value("start", ""));
            w.end = trim_ascii(entry.value("end", ""));
            w.message = trim_ascii(entry.value("message", ""));
            w.recurrence = to_lower_ascii(trim_ascii(entry.value("recurrence", "none")));
            if (!is_valid_recurrence(w.recurrence))
                w.recurrence = "none";
            w.interval = std::max(1, entry.value("interval", 1));
            w.until = trim_ascii(entry.value("until", ""));
            if (!w.start.empty() && !w.end.empty())
                out.push_back(std::move(w));
        }
        return out;
    }

    bool parse_windows_payload(const nlohmann::json &payload,
                               std::vector<MaintenanceWindow> &out,
                               std::string &error)
    {
        out.clear();
        if (!payload.is_array())
        {
            error = "windows-must-be-array";
            return false;
        }

        size_t idx = 0;
        for (const auto &entry : payload)
        {
            if (!entry.is_object())
            {
                error = "window-entry-must-be-object";
                return false;
            }

            MaintenanceWindow w;
            w.id = trim_ascii(entry.value("id", ""));
            if (w.id.empty())
                w.id = std::string("w-") + std::to_string(++idx);
            w.start = trim_ascii(entry.value("start", ""));
            w.end = trim_ascii(entry.value("end", ""));
            w.message = trim_ascii(entry.value("message", ""));
            w.recurrence = to_lower_ascii(trim_ascii(entry.value("recurrence", "none")));
            w.interval = std::max(1, entry.value("interval", 1));
            w.until = trim_ascii(entry.value("until", ""));

            if (w.start.empty() || w.end.empty())
            {
                error = "window-start-end-required";
                return false;
            }
            const time_t t_start = parse_iso8601(w.start);
            const time_t t_end = parse_iso8601(w.end);
            if (t_start < 0 || t_end < 0 || t_end <= t_start)
            {
                error = "window-invalid-range";
                return false;
            }
            if (!is_valid_recurrence(w.recurrence))
            {
                error = "window-invalid-recurrence";
                return false;
            }
            if (w.interval < 1 || w.interval > 365)
            {
                error = "window-invalid-interval";
                return false;
            }
            if (!w.until.empty())
            {
                const time_t t_until = parse_iso8601(w.until);
                if (t_until < 0)
                {
                    error = "window-invalid-until";
                    return false;
                }
            }

            out.push_back(std::move(w));
        }
        return true;
    }

    void expand_window_occurrences(const MaintenanceWindow &w,
                                   time_t now,
                                   time_t horizon,
                                   std::vector<MaintenanceOccurrence> &out);

    bool should_prune_window(const MaintenanceWindow &w, time_t cutoff)
    {
        if (w.recurrence == "none")
        {
            const time_t t_end = parse_iso8601(w.end);
            return t_end > 0 && t_end < cutoff;
        }
        if (!w.until.empty())
        {
            const time_t t_until = parse_iso8601(w.until);
            return t_until > 0 && t_until < cutoff;
        }
        return false;
    }

    std::unordered_map<std::string, MaintenanceOccurrence> active_occurrences_by_window(
        const std::vector<MaintenanceWindow> &windows,
        time_t now)
    {
        std::vector<MaintenanceOccurrence> occs;
        occs.reserve(128);
        for (const auto &w : windows)
            expand_window_occurrences(w, now, now, occs);

        std::unordered_map<std::string, MaintenanceOccurrence> active_by_id;
        for (const auto &occ : occs)
        {
            if (now >= occ.start_ts && now < occ.end_ts)
                active_by_id[occ.window_id] = occ;
        }
        return active_by_id;
    }

    void mark_window_ended_now(MaintenanceWindow &w, const MaintenanceOccurrence *active_occ, time_t now)
    {
        const time_t minute_floor = now - (now % 60);
        const time_t ended_start = minute_floor - 60;
        if (active_occ)
            w.start = iso8601_local_minute(std::min(active_occ->start_ts, ended_start));
        else
            w.start = iso8601_local_minute(ended_start);
        w.end = iso8601_local_minute(minute_floor);
        w.recurrence = "none";
        w.interval = 1;
        w.until.clear();
    }

    std::string window_runtime_status(const MaintenanceWindow &w,
                                      time_t now,
                                      const std::unordered_set<std::string> &active_window_ids)
    {
        if (active_window_ids.find(w.id) != active_window_ids.end())
            return "active";

        const time_t t_end = parse_iso8601(w.end);
        if (w.recurrence == "none")
        {
            if (t_end > 0 && t_end <= now)
                return "ended";
            return "planned";
        }

        if (!w.until.empty())
        {
            const time_t t_until = parse_iso8601(w.until);
            if (t_until > 0 && t_until <= now)
                return "ended";
        }
        return "planned";
    }

    nlohmann::json windows_to_json(const std::vector<MaintenanceWindow> &windows,
                                   time_t now,
                                   const std::unordered_set<std::string> &active_window_ids)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &w : windows)
        {
            arr.push_back({
                {"id", w.id},
                {"start", w.start},
                {"end", w.end},
                {"message", w.message},
                {"recurrence", w.recurrence},
                {"interval", w.interval},
                {"until", w.until},
                {"status", window_runtime_status(w, now, active_window_ids)},
            });
        }
        return arr;
    }

    nlohmann::json windows_to_storage_json(const std::vector<MaintenanceWindow> &windows)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &w : windows)
        {
            arr.push_back({
                {"id", w.id},
                {"start", w.start},
                {"end", w.end},
                {"message", w.message},
                {"recurrence", w.recurrence},
                {"interval", w.interval},
                {"until", w.until},
            });
        }
        return arr;
    }

    void expand_window_occurrences(const MaintenanceWindow &w,
                                   time_t now,
                                   time_t horizon,
                                   std::vector<MaintenanceOccurrence> &out)
    {
        time_t t_start = parse_iso8601(w.start);
        time_t t_end = parse_iso8601(w.end);
        if (t_start < 0 || t_end < 0 || t_end <= t_start)
            return;

        if (w.recurrence == "none")
        {
            if (t_end >= now && t_start <= horizon)
                push_occurrence(w, t_start, t_end, out);
            return;
        }

        time_t recurrence_until = horizon;
        if (!w.until.empty())
        {
            const time_t t_until = parse_iso8601(w.until);
            if (t_until > 0)
                recurrence_until = std::min(recurrence_until, t_until);
        }

        int guard = 0;
        while (t_start <= horizon && t_start <= recurrence_until && guard++ < 5000)
        {
            if (t_end >= now)
                push_occurrence(w, t_start, t_end, out);

            if (w.recurrence == "daily")
            {
                const time_t step = static_cast<time_t>(w.interval) * 24 * 60 * 60;
                t_start += step;
                t_end += step;
            }
            else if (w.recurrence == "weekly")
            {
                const time_t step = static_cast<time_t>(w.interval) * 7 * 24 * 60 * 60;
                t_start += step;
                t_end += step;
            }
            else // monthly
            {
                const time_t next_start = add_months_local(t_start, w.interval);
                const time_t next_end = add_months_local(t_end, w.interval);
                if (next_start <= t_start || next_end <= t_end)
                    break;
                t_start = next_start;
                t_end = next_end;
            }
        }
    }
}

// Parse ISO 8601 timestamp (YYYY-MM-DDTHH:MM or YYYY-MM-DDTHH:MM:SS) to time_t.
// Returns -1 on failure.
static time_t parse_iso8601(const std::string &s)
{
    if (s.size() < 16)
        return -1;
    std::tm tm{};
    if (sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d",
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min) != 5)
        return -1;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

MaintenanceState get_maintenance_state(Database &db)
{
    MaintenanceState s;
    const time_t now = time(nullptr);
    time_t horizon = add_months_local(now, 6);
    const time_t prune_cutoff = add_months_local(now, -1);
    if (horizon <= now)
        horizon = now + static_cast<time_t>(183) * 24 * 60 * 60;

    s.enabled = app_config::get_or(db, app_config::kMaintenanceEnabled, "false") == "true";
    const std::string default_message = app_config::get_or(db, app_config::kMaintenanceMessage, "");
    s.message = default_message;

    std::vector<MaintenanceWindow> windows = parse_maintenance_windows_raw(
        app_config::get_or(db, app_config::kMaintenanceWindowsJson, "[]"));

    const size_t before_prune_count = windows.size();
    windows.erase(
        std::remove_if(windows.begin(), windows.end(), [&](const MaintenanceWindow &w)
                       { return should_prune_window(w, prune_cutoff); }),
        windows.end());
    if (windows.size() != before_prune_count)
    {
        std::string err;
        (void)app_config::set_from_admin(db, app_config::kMaintenanceWindowsJson, windows_to_storage_json(windows).dump(), err);
    }

    const std::string legacy_start = app_config::get_or(db, app_config::kMaintenanceScheduledStart, "");
    const std::string legacy_end = app_config::get_or(db, app_config::kMaintenanceScheduledEnd, "");
    if (!legacy_start.empty() && !legacy_end.empty())
    {
        windows.push_back(MaintenanceWindow{
            "legacy-single",
            legacy_start,
            legacy_end,
            default_message,
            "none",
            1,
            "",
        });
    }

    std::vector<MaintenanceOccurrence> occs;
    occs.reserve(256);
    for (const auto &w : windows)
        expand_window_occurrences(w, now, horizon, occs);

    std::sort(occs.begin(), occs.end(), [](const MaintenanceOccurrence &a, const MaintenanceOccurrence &b)
              {
        if (a.start_ts != b.start_ts)
            return a.start_ts < b.start_ts;
        return a.end_ts < b.end_ts; });

    for (const auto &occ : occs)
    {
        s.upcoming_windows.push_back({
            {"window_id", occ.window_id},
            {"start", occ.start},
            {"end", occ.end},
            {"message", occ.message},
            {"recurrence", occ.recurrence},
        });
    }

    const MaintenanceOccurrence *active_occ = nullptr;
    const MaintenanceOccurrence *next_occ = nullptr;
    std::unordered_set<std::string> active_window_ids;
    for (const auto &occ : occs)
    {
        if (now >= occ.start_ts && now < occ.end_ts)
        {
            active_window_ids.insert(occ.window_id);
            if (!active_occ)
                active_occ = &occ;
            continue;
        }
        if (!next_occ && occ.start_ts >= now)
            next_occ = &occ;
    }

    s.windows = windows_to_json(windows, now, active_window_ids);

    if (active_occ)
    {
        s.scheduled_now = true;
        s.scheduled_start = active_occ->start;
        s.scheduled_end = active_occ->end;
    }
    else if (next_occ)
    {
        s.scheduled_start = next_occ->start;
        s.scheduled_end = next_occ->end;
    }

    s.active = s.enabled || s.scheduled_now;
    return s;
}

static nlohmann::json maintenance_state_to_json(const MaintenanceState &s, bool admin_view = false)
{
    nlohmann::json j = {
        {"active", s.active},
        {"message", s.message},
        {"scheduled_start", s.scheduled_start},
        {"scheduled_end", s.scheduled_end},
    };
    if (admin_view)
    {
        j["enabled"] = s.enabled;
        j["scheduled_now"] = s.scheduled_now;
        j["windows"] = s.windows;
        j["upcoming_windows"] = s.upcoming_windows;
    }
    return j;
}

// ---- GET /maintenance (public) ---------------------------------------------

void handle_get_maintenance(HttpRes *res, HttpReq *req, Database &db)
{
    (void)req;
    try
    {
        auto s = get_maintenance_state(db);
        send_json(res, 200, maintenance_state_to_json(s).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handle_get_maintenance", e);
    }
}

// ---- GET /admin/maintenance -------------------------------------------------

void handle_get_admin_maintenance(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto user = resolve_user(db, claims);
        if (!user || !is_admin(*user))
        {
            send_json(res, 403, R"({"error":"Nur Admins dürfen die Wartungskonfiguration abrufen"})");
            return;
        }
        auto s = get_maintenance_state(db);
        send_json(res, 200, maintenance_state_to_json(s, true).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handle_get_admin_maintenance", e);
    }
}

// ---- PUT /admin/maintenance -------------------------------------------------

void handle_put_admin_maintenance(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string auth_header{req->getHeader("authorization")};
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, auth_header, &db, &wm](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last)
            return;

        std::string token = extract_bearer_token(auth_header);
        if (token.empty())
        {
            send_json(res, 401, R"({"error":"Nicht autorisiert"})");
            return;
        }
        TokenClaims claims;
        try
        {
            claims = validate_token(token);
        }
        catch (std::exception &e)
        {
            send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
            return;
        }
        auto user = resolve_user(db, claims);
        if (!user || !is_admin(*user))
        {
            send_json(res, 403, R"({"error":"Nur Admins dürfen den Wartungsmodus ändern"})");
            return;
        }

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object())
        {
            send_json(res, 400, R"({"error":"Ungültiges JSON"})");
            return;
        }

        try
        {
            std::string err;
            bool windows_changed = false;
            bool windows_loaded = false;
            std::vector<MaintenanceWindow> windows;
            const time_t now = time(nullptr);

            auto ensure_windows_loaded = [&]() {
                if (windows_loaded)
                    return;
                windows = parse_maintenance_windows_raw(app_config::get_or(db, app_config::kMaintenanceWindowsJson, "[]"));
                windows_loaded = true;
            };

            if (j.contains("enabled"))
            {
                bool enabled = j["enabled"].get<bool>();
                if (!app_config::set_from_admin(db, app_config::kMaintenanceEnabled,
                                                 enabled ? std::string("true") : std::string("false"), err))
                {
                    send_json(res, 500, R"({"error":"Konnte maintenance.enabled nicht speichern"})");
                    return;
                }

                if (!enabled)
                {
                    ensure_windows_loaded();
                    const auto active_by_id = active_occurrences_by_window(windows, now);
                    for (auto &w : windows)
                    {
                        auto it = active_by_id.find(w.id);
                        if (it == active_by_id.end())
                            continue;
                        mark_window_ended_now(w, &it->second, now);
                        windows_changed = true;
                    }
                }
            }

            if (j.contains("message"))
            {
                std::string msg = trim_ascii(j["message"].get<std::string>());
                if (!app_config::set_from_admin(db, app_config::kMaintenanceMessage, msg, err))
                {
                    send_json(res, 500, R"({"error":"Konnte maintenance.message nicht speichern"})");
                    return;
                }
            }

            if (j.contains("scheduled_start"))
            {
                std::string ts = trim_ascii(j["scheduled_start"].get<std::string>());
                if (!ts.empty() && parse_iso8601(ts) < 0)
                {
                    send_json(res, 400, nlohmann::json{{"error", "Ungültiges Format für scheduled_start (erwartet YYYY-MM-DDTHH:MM)"}}.dump());
                    return;
                }
                if (!app_config::set_from_admin(db, app_config::kMaintenanceScheduledStart, ts, err))
                {
                    send_json(res, 500, R"({"error":"Konnte maintenance.scheduled_start nicht speichern"})");
                    return;
                }
            }

            if (j.contains("scheduled_end"))
            {
                std::string ts = trim_ascii(j["scheduled_end"].get<std::string>());
                if (!ts.empty() && parse_iso8601(ts) < 0)
                {
                    send_json(res, 400, nlohmann::json{{"error", "Ungültiges Format für scheduled_end (erwartet YYYY-MM-DDTHH:MM)"}}.dump());
                    return;
                }
                if (!app_config::set_from_admin(db, app_config::kMaintenanceScheduledEnd, ts, err))
                {
                    send_json(res, 500, R"({"error":"Konnte maintenance.scheduled_end nicht speichern"})");
                    return;
                }
            }

            if (j.contains("windows"))
            {
                if (!parse_windows_payload(j["windows"], windows, err))
                {
                    send_json(res, 400, nlohmann::json{{"error", "Ungültige Wartungsfenster-Konfiguration"}, {"detail", err}}.dump());
                    return;
                }
                windows_loaded = true;
                windows_changed = true;
            }

            if (j.contains("end_window_id"))
            {
                const std::string target_id = trim_ascii(j["end_window_id"].get<std::string>());
                if (target_id.empty())
                {
                    send_json(res, 400, R"({"error":"end_window_id darf nicht leer sein"})");
                    return;
                }
                ensure_windows_loaded();
                const auto active_by_id = active_occurrences_by_window(windows, now);
                auto target_it = std::find_if(windows.begin(), windows.end(), [&](const MaintenanceWindow &w) {
                    return w.id == target_id;
                });
                if (target_it == windows.end())
                {
                    send_json(res, 404, R"({"error":"Wartungsfenster nicht gefunden"})");
                    return;
                }
                auto occ_it = active_by_id.find(target_id);
                if (occ_it == active_by_id.end())
                {
                    send_json(res, 409, R"({"error":"Das Wartungsfenster ist aktuell nicht aktiv"})");
                    return;
                }
                mark_window_ended_now(*target_it, &occ_it->second, now);
                windows_changed = true;
            }

            if (windows_changed)
            {
                if (!app_config::set_from_admin(db, app_config::kMaintenanceWindowsJson, windows_to_storage_json(windows).dump(), err))
                {
                    send_json(res, 500, R"({"error":"Konnte maintenance.windows_json nicht speichern"})");
                    return;
                }
                // Keep legacy single-window fields empty once structured windows are used.
                if (!app_config::set_from_admin(db, app_config::kMaintenanceScheduledStart, std::string(""), err) ||
                    !app_config::set_from_admin(db, app_config::kMaintenanceScheduledEnd, std::string(""), err))
                {
                    send_json(res, 500, R"({"error":"Konnte alte Wartungsfenster-Felder nicht zurücksetzen"})");
                    return;
                }
            }

            auto s = get_maintenance_state(db);
            const auto payload = maintenance_state_to_json(s, true);
            send_json(res, 200, payload.dump());

            nlohmann::json ws_msg = {
                {"event", "maintenance_status"},
                {"active", s.active},
                {"enabled", s.enabled},
                {"scheduled_now", s.scheduled_now},
                {"message", s.message},
                {"scheduled_start", s.scheduled_start},
                {"scheduled_end", s.scheduled_end},
            };
            wm.broadcast(ws_msg.dump());
        }
        catch (std::exception &)
        {
            send_json(res, 500, R"({"error":"Interner Serverfehler"})");
        } });
}
