#include "handlers/handler_deps.hpp"
#include "handlers/program_images.hpp"
#include "core/models/serializers.hpp"
#include "infra/audit.hpp"
#include "utils/program_images.hpp"

#include <cstdio>
#include <array>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace
{
    std::optional<UserRecord> resolve_user_local(Database &db, const TokenClaims &claims)
    {
        if (claims.oid.rfind("debug:", 0) == 0)
            return db.get_user_by_id(claims.oid.substr(6));
        return db.get_user_by_oid(claims.oid);
    }

    std::optional<std::string> configured_public_base_url(Database &db)
    {
        std::string out = app_config::get_or(db, app_config::kPublicBaseUrl, "");
        out = trim_ascii(out);
        while (!out.empty() && out.back() == '/')
            out.pop_back();
        if (out.empty())
            return std::nullopt;
        return out;
    }

    void send_internal_error_local(HttpRes *res, const char *context, const std::exception &e)
    {
        fprintf(stderr, "[error] %s: %s\n", context, e.what());
        if (std::string(e.what()) == "Öffentliche Basis-URL ist nicht konfiguriert")
        {
            send_json(res, 409, R"({"error":"Öffentliche Basis-URL ist nicht konfiguriert"})");
            return;
        }
        send_json(res, 500, R"({"error":"Interner Serverfehler"})");
    }

    enum class IdeenkisteOp
    {
        View,
        Add,
        Delete
    };

    std::optional<std::string> ideenkiste_check(HttpRes *res, HttpReq *req, Database &db,
                                                UserRecord &out_user, IdeenkisteOp op)
    {
        TokenClaims claims;
        if (!require_auth(res, req, claims))
            return std::nullopt;
        auto user = resolve_user_local(db, claims);
        if (!user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return std::nullopt;
        }
        auto perm = is_admin(*user) ? std::optional<RolePermission>{} : db.get_role_permission(user->role);
        std::string scope;
        if (is_admin(*user))
        {
            scope = "all";
        }
        else if (perm)
        {
            switch (op)
            {
            case IdeenkisteOp::View:
                scope = perm->ideenkiste_scope;
                break;
            case IdeenkisteOp::Add:
                scope = perm->ideenkiste_add_scope;
                break;
            case IdeenkisteOp::Delete:
                scope = perm->ideenkiste_delete_scope;
                break;
            }
        }
        else
        {
            scope = "none";
        }
        if (scope == "none")
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return std::nullopt;
        }
        out_user = *user;
        return scope;
    }

    bool ideenkiste_item_in_scope(Database &db,
                                  const std::string &item_id,
                                  const std::string &scope,
                                  const std::optional<std::string> &user_dept)
    {
        if (scope == "all")
            return true;

        if (scope == "own_dept")
        {
            if (!user_dept)
                return false;

            auto all_items = db.list_ideenkiste("");
            for (const auto &item : all_items)
            {
                if (item.id == item_id)
                    return item.department.has_value() && *item.department == *user_dept;
            }
        }

        return false;
    }

    std::optional<UserRecord> require_share_access(HttpRes *res, Database &db,
                                                   const TokenClaims &claims, const std::string &activity_id)
    {
        auto access = require_activity_access(res, db, claims);
        if (!access)
            return std::nullopt;
        auto activity = db.get_activity_by_id(activity_id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Aktivität nicht gefunden"})");
            return std::nullopt;
        }
        if (!can_read_activity(access->permission, access->user, access->dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return std::nullopt;
        }
        return std::move(access->user);
    }

    std::string guest_token_hash(const std::string &token)
    {
        std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
        unsigned int digest_size = 0;
        if (EVP_Digest(token.data(), token.size(), digest.data(), &digest_size, EVP_sha256(), nullptr) != 1)
            throw std::runtime_error("guest edit token hashing failed");
        static constexpr char hex[] = "0123456789abcdef";
        std::string hash;
        hash.reserve(digest_size * 2);
        for (unsigned int i = 0; i < digest_size; ++i)
        {
            hash.push_back(hex[digest[i] >> 4]);
            hash.push_back(hex[digest[i] & 0x0f]);
        }
        return hash;
    }

    std::string create_guest_token()
    {
        std::array<unsigned char, 32> bytes{};
        if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
            throw std::runtime_error("guest edit token generation failed");
        static constexpr char hex[] = "0123456789abcdef";
        std::string token;
        token.reserve(bytes.size() * 2);
        for (unsigned char byte : bytes)
        {
            token.push_back(hex[byte >> 4]);
            token.push_back(hex[byte & 0x0f]);
        }
        return token;
    }

    bool valid_guest_token(const std::string &token)
    {
        return token.size() == 64 && std::all_of(token.begin(), token.end(), [](unsigned char ch) {
            return std::isxdigit(ch) != 0;
        });
    }

    void guest_no_store(HttpRes *res)
    {
        res->writeHeader("Cache-Control", "no-store");
        res->writeHeader("Referrer-Policy", "no-referrer");
    }

    void send_guest_json(HttpRes *res, int status, const std::string &body)
    {
        if (!audit_finish(res, status, body))
        {
            send_guest_json(res, 500, R"({"error":"Audit Log konnte nicht abgeschlossen werden; Aktion möglicherweise ausgeführt. Vor Wiederholung Zustand prüfen."})");
            return;
        }
        res->writeStatus(status_text(status));
        set_cors(res);
        guest_no_store(res);
        res->writeHeader("Content-Type", "application/json");
        res->end(body);
    }

    void send_guest_internal_error(HttpRes *res, const char *context, const std::exception &error)
    {
        fprintf(stderr, "[error] %s: %s\n", context, error.what());
        send_guest_json(res, 500, R"({"error":"Interner Serverfehler"})");
    }

    std::optional<ActivityGuestEditLink> require_guest_edit_link(HttpRes *res, Database &db,
                                                                  const std::string &token_hash)
    {
        auto link = db.get_guest_edit_link_by_token_hash(token_hash);
        if (!link)
        {
            send_guest_json(res, 404, R"({"error":"Gastlink ungültig oder abgelaufen"})");
            return std::nullopt;
        }
        return link;
    }

    std::optional<std::string> require_guest_edit_management_access(HttpRes *res, Database &db,
                                                                    const TokenClaims &claims,
                                                                    const std::string &activity_id)
    {
        auto access = require_activity_access(res, db, claims);
        if (!access)
            return std::nullopt;
        auto activity = db.get_activity_by_id(activity_id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Aktivität nicht gefunden"})");
            return std::nullopt;
        }
        if (!can_edit_activity(access->permission, access->user, *activity, claims.email))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return std::nullopt;
        }
        return access->user.id;
    }

    bool valid_guest_edit_payload(const nlohmann::json &j)
    {
        if (!j.is_object() || !j.contains("updated_at") || !j["updated_at"].is_string()
            || !j.contains("guiding_thread")
            || (!j["guiding_thread"].is_string() && !j["guiding_thread"].is_null())
            || !j.contains("goal") || !j["goal"].is_string()
            || !j.contains("bad_weather_info")
            || (!j["bad_weather_info"].is_string() && !j["bad_weather_info"].is_null())
            || !j.contains("siko_text")
            || (!j["siko_text"].is_string() && !j["siko_text"].is_null())
            || !j.contains("programs") || !j["programs"].is_array()
            || !j.contains("material") || !j["material"].is_array()
            || !j.contains("tn_material") || !j["tn_material"].is_array())
            return false;

        for (const auto &program : j["programs"])
        {
            if (!program.is_object() || !program.contains("duration_minutes")
                || !program["duration_minutes"].is_number_integer()
                || !program.contains("title") || !program["title"].is_string()
                || !program.contains("description") || !program["description"].is_string()
                || !program.contains("responsible") || !program["responsible"].is_array())
                return false;
            const auto &duration = program["duration_minutes"];
            if (duration.is_number_unsigned())
            {
                if (duration.get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int>::max()))
                    return false;
            }
            else if (duration.get<int64_t>() < 0
                     || duration.get<int64_t>() > std::numeric_limits<int>::max())
                return false;
            for (const auto &person : program["responsible"])
                if (!person.is_string()) return false;
        }
        for (const auto &material : j["material"])
        {
            if (material.is_string())
                continue;
            if (!material.is_object() || !material.contains("name") || !material["name"].is_string())
                return false;
            if (material.contains("responsible"))
            {
                if (!material["responsible"].is_array()) return false;
                for (const auto &person : material["responsible"])
                    if (!person.is_string()) return false;
            }
        }
        for (const auto &item : j["tn_material"])
            if (!item.is_string()) return false;
        return true;
    }

    bool valid_guest_program_image_id(const std::string &id)
    {
        const auto ids = program_image_ids("/api/program-images/" + id);
        return ids.size() == 1 && ids.front() == id;
    }

    void guest_send_image(HttpRes *res, const ProgramImageData &image)
    {
        if (!valid_program_image(image.content_type, image.data))
            throw std::runtime_error("program_images: invalid stored image");
        res->writeStatus("200 OK");
        set_cors(res);
        guest_no_store(res);
        res->writeHeader("Content-Type", image.content_type);
        res->writeHeader("Content-Disposition", "inline");
        res->writeHeader("X-Content-Type-Options", "nosniff");
        res->end(std::string_view(reinterpret_cast<const char *>(image.data.data()), image.data.size()));
    }

    void handle_post_guest_edit_upload(HttpRes *res, HttpReq *req, Database &db, bool program_image)
    {
        const std::string token{req->getHeader("x-guest-edit-token")};
        if (!valid_guest_token(token))
        {
            send_guest_json(res, 404, R"({"error":"Gastlink ungültig oder abgelaufen"})");
            return;
        }
        const std::string token_hash = guest_token_hash(token);
        const auto link = require_guest_edit_link(res, db, token_hash);
        if (!link)
            return;

        auto body = std::make_shared<std::string>();
        res->onAborted([] {});
        res->onData([res, body, &db, token_hash, activity_id = link->activity_id, program_image,
                     rejected = false](std::string_view chunk, bool last) mutable {
            const size_t max_body = program_image ? kMaxProgramImageBodyBytes : 64 * 1024 * 1024;
            if (rejected) return;
            if (chunk.size() > max_body - body->size())
            {
                rejected = true;
                send_guest_json(res, 413, R"({"error":"Datei ist zu gross"})");
                return;
            }
            body->append(chunk.data(), chunk.size());
            if (!last) return;
            try
            {
                const auto active_link = require_guest_edit_link(res, db, token_hash);
                if (!active_link || active_link->activity_id != activity_id)
                    return;
                const auto payload = body->empty()
                    ? nlohmann::json::object()
                    : nlohmann::json::parse(*body, nullptr, false);
                if (!payload.is_object() || !payload.contains("filename") || !payload["filename"].is_string()
                    || !payload.contains("data") || !payload["data"].is_string()
                    || (payload.contains("content_type") && !payload["content_type"].is_string()))
                {
                    send_guest_json(res, 400, R"({"error":"filename, content_type and data must be text"})");
                    return;
                }
                const std::string filename = payload["filename"].get<std::string>();
                const std::string content_type = payload.value("content_type", "application/octet-stream");
                const std::string data = payload["data"].get<std::string>();
                if (filename.empty() || filename.size() > 255 || data.empty()
                    || content_type.find_first_of("\r\n") != std::string::npos)
                {
                    send_guest_json(res, 400, R"({"error":"Ungültige Datei"})");
                    return;
                }
                if (program_image)
                {
                    std::vector<uint8_t> bytes;
                    if (!decode_program_image(data, content_type, bytes))
                    {
                        send_guest_json(res, 400, R"({"error":"Ungültiges Bild. Erlaubt: PNG, JPEG, GIF oder WebP bis 10 MiB."})");
                        return;
                    }
                }
                auto attachment = program_image
                    ? db.add_program_image(activity_id, filename, content_type, data)
                    : db.add_attachment(activity_id, filename, content_type, data);
                if (!attachment)
                {
                    send_guest_json(res, 500, R"({"error":"Datei konnte nicht gespeichert werden"})");
                    return;
                }
                send_guest_json(res, 201, attachment_to_json(*attachment).dump());
            }
            catch (const std::exception &error)
            {
                send_guest_internal_error(res, "guest_edit_upload", error);
            }
        });
    }
}

void handle_post_share_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = require_share_access(res, db, claims, activity_id);
        if (!current_user)
            return;

        auto base_url = configured_public_base_url(db);
        if (!base_url)
        {
            send_json(res, 409, R"({"error":"Öffentliche Basis-URL ist nicht konfiguriert"})");
            return;
        }

        auto link = db.create_share_link(activity_id, current_user->id);
        if (!link)
        {
            send_json(res, 500, R"({"error":"Share-Link konnte nicht erstellt werden"})");
            return;
        }
        nlohmann::json j = {
            {"id", link->id},
            {"activity_id", link->activity_id},
            {"share_token", link->share_token},
            {"share_url", *base_url + "/shared/" + link->share_token},
            {"created_at", link->created_at}};
        send_json(res, 201, j.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}

void handle_get_share_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string activity_id{req->getParameter(0)};
    try
    {
        if (!require_share_access(res, db, claims, activity_id))
            return;

        auto base_url = configured_public_base_url(db);
        if (!base_url)
        {
            send_json(res, 409, R"({"error":"Öffentliche Basis-URL ist nicht konfiguriert"})");
            return;
        }

        auto link = db.get_share_link(activity_id);
        if (!link)
        {
            send_json(res, 200, R"({"share_token":null})");
            return;
        }
        nlohmann::json j = {
            {"id", link->id},
            {"activity_id", link->activity_id},
            {"share_token", link->share_token},
            {"share_url", *base_url + "/shared/" + link->share_token},
            {"created_at", link->created_at}};
        send_json(res, 200, j.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}

void handle_delete_share_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string activity_id{req->getParameter(0)};
    try
    {
        if (!require_share_access(res, db, claims, activity_id))
            return;
        bool ok = db.delete_share_link(activity_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Kein Share-Link vorhanden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}

void handle_get_shared_activity(HttpRes *res, HttpReq *req, Database &db)
{
    std::string token{req->getParameter(0)};
    try
    {
        auto activity = db.get_activity_by_share_token(token);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        // Resolve user IDs to display names for public view
        auto all_users = db.list_users();
        activity->responsible = resolve_ids_to_display_names(activity->responsible, all_users);
        for (auto &p : activity->programs)
            p.responsible = resolve_ids_to_display_names(p.responsible, all_users);
        for (auto &m : activity->material)
            m.responsible = resolve_ids_to_display_names(m.responsible, all_users);
        send_json(res, 200, to_json(*activity).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}

void handle_post_guest_edit_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    const std::string activity_id{req->getParameter(0)};
    const auto created_by = require_guest_edit_management_access(res, db, claims, activity_id);
    if (!created_by)
        return;
    const auto base_url = configured_public_base_url(db);
    if (!base_url)
    {
        send_json(res, 409, R"({"error":"Öffentliche Basis-URL ist nicht konfiguriert"})");
        return;
    }

    auto body = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, body, &db, activity_id, created_by = *created_by, base_url = *base_url,
                 rejected = false](std::string_view chunk, bool last) mutable {
        constexpr size_t max_body = 4096;
        if (rejected) return;
        if (chunk.size() > max_body - body->size())
        {
            rejected = true;
            send_json(res, 413, R"({"error":"Anfrage ist zu gross"})");
            return;
        }
        body->append(chunk.data(), chunk.size());
        if (!last) return;
        const auto payload = nlohmann::json::parse(*body, nullptr, false);
        if (!payload.is_object())
        {
            send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})");
            return;
        }
        const int max_days = app_config::get_int_or(db, app_config::kGuestEditMaxDays, 30, 1, 30);
        int expires_in_days = max_days < 7 ? max_days : 7;
        if (payload.contains("expires_in_days"))
        {
            if (!payload["expires_in_days"].is_number_integer())
            {
                send_json(res, 400, nlohmann::json{
                    {"error", "expires_in_days muss eine ganze Zahl zwischen 1 und " +
                                  std::to_string(max_days) + " sein"}}.dump());
                return;
            }
            const auto &requested_days = payload["expires_in_days"];
            if (requested_days < 1 || requested_days > max_days)
            {
                send_json(res, 400, nlohmann::json{
                    {"error", "expires_in_days muss eine ganze Zahl zwischen 1 und " +
                                  std::to_string(max_days) + " sein"}}.dump());
                return;
            }
            expires_in_days = requested_days.get<int>();
        }
        try
        {
            const std::string token = create_guest_token();
            const auto link = db.create_guest_edit_link(activity_id, created_by,
                                                        guest_token_hash(token), expires_in_days);
            if (!link)
            {
                send_json(res, 500, R"({"error":"Gastlink konnte nicht erstellt werden"})");
                return;
            }
            send_json(res, 201, nlohmann::json{
                {"activity_id", link->activity_id},
                {"share_url", base_url + "/guest-edit#" + token},
                {"expires_at", link->expires_at},
                {"expires_in_days", expires_in_days},
                {"max_expires_in_days", max_days}}.dump());
        }
        catch (const std::exception &error)
        {
            send_internal_error_local(res, "create_guest_edit_link", error);
        }
    });
}

void handle_get_guest_edit_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    const std::string activity_id{req->getParameter(0)};
    if (!require_guest_edit_management_access(res, db, claims, activity_id))
        return;
    try
    {
        const auto link = db.get_guest_edit_link(activity_id);
        send_json(res, 200, nlohmann::json{
            {"expires_at", link ? nlohmann::json(link->expires_at) : nlohmann::json(nullptr)},
            {"max_expires_in_days", app_config::get_int_or(
                db, app_config::kGuestEditMaxDays, 30, 1, 30)}}.dump());
    }
    catch (const std::exception &error)
    {
        send_internal_error_local(res, "get_guest_edit_link", error);
    }
}

void handle_delete_guest_edit_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    const std::string activity_id{req->getParameter(0)};
    if (!require_guest_edit_management_access(res, db, claims, activity_id))
        return;
    try
    {
        if (!db.delete_guest_edit_link(activity_id))
        {
            send_json(res, 404, R"({"error":"Kein Gastlink vorhanden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (const std::exception &error)
    {
        send_internal_error_local(res, "delete_guest_edit_link", error);
    }
}

void handle_get_guest_edit_activity(HttpRes *res, HttpReq *req, Database &db)
{
    const std::string token{req->getHeader("x-guest-edit-token")};
    if (!valid_guest_token(token))
    {
        send_guest_json(res, 404, R"({"error":"Gastlink ungültig oder abgelaufen"})");
        return;
    }
    try
    {
        const auto link = require_guest_edit_link(res, db, guest_token_hash(token));
        if (!link)
            return;
        const auto activity = db.get_activity_by_id(link->activity_id);
        if (!activity)
        {
            send_guest_json(res, 404, R"({"error":"Gastlink ungültig oder abgelaufen"})");
            return;
        }
        send_guest_json(res, 200, nlohmann::json{
            {"activity", to_json(*activity)},
            {"expires_at", link->expires_at}}.dump());
    }
    catch (const std::exception &error)
    {
        send_guest_internal_error(res, "get_guest_edit_activity", error);
    }
}

void handle_patch_guest_edit_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    const std::string token{req->getHeader("x-guest-edit-token")};
    if (!valid_guest_token(token))
    {
        send_guest_json(res, 404, R"({"error":"Gastlink ungültig oder abgelaufen"})");
        return;
    }
    const std::string token_hash = guest_token_hash(token);
    const auto link = require_guest_edit_link(res, db, token_hash);
    if (!link)
        return;
    const std::string activity_id = link->activity_id;

    auto body = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, body, &db, &wm, token_hash, activity_id, rejected = false](std::string_view chunk, bool last) mutable {
        constexpr size_t max_body = 5 * 1024 * 1024;
        if (rejected) return;
        if (chunk.size() > max_body - body->size())
        {
            rejected = true;
            send_guest_json(res, 413, R"({"error":"Änderungen sind zu gross"})");
            return;
        }
        body->append(chunk.data(), chunk.size());
        if (!last) return;
        try
        {
            const auto active_link = require_guest_edit_link(res, db, token_hash);
            if (!active_link || active_link->activity_id != activity_id)
                return;
            const auto payload = nlohmann::json::parse(*body, nullptr, false);
            if (!valid_guest_edit_payload(payload))
            {
                send_guest_json(res, 400, R"({"error":"Ungültige Aktivitätsänderung"})");
                return;
            }
            const auto activity = db.get_activity_by_id(activity_id);
            if (!activity)
            {
                send_guest_json(res, 404, R"({"error":"Gastlink ungültig oder abgelaufen"})");
                return;
            }
            std::vector<std::string> descriptions;
            for (const auto &program : payload["programs"])
                descriptions.push_back(program["description"].get<std::string>());
            for (const auto &description : descriptions)
            {
                for (const auto &image_id : program_image_ids(description))
                {
                    const auto image = db.get_program_image_data(image_id, false);
                    if (!image || image->activity_id != std::optional<std::string>(activity_id))
                    {
                        send_guest_json(res, 403, R"({"error":"Keine Berechtigung für ein Programmbild"})");
                        return;
                    }
                }
            }

            const ActivityInput input = parse_activity_input(payload);
            bool conflict = false;
            const auto updated = db.update_activity(activity_id, input,
                                                    payload["updated_at"].get<std::string>(),
                                                    true, &conflict);
            if (!updated)
            {
                send_guest_json(res, conflict ? 409 : 404, conflict
                    ? R"({"error":"Die Aktivität wurde zwischenzeitlich geändert. Bitte neu laden und Änderungen erneut anwenden."})"
                    : R"({"error":"Aktivität nicht gefunden"})");
                return;
            }
            wm.broadcast_activity_updated(updated->first, updated->second);
            cache_bump_version(db, "activities");
            cache_bump_version(db, "activity");
            send_guest_json(res, 200, to_json(updated->second).dump());
        }
        catch (const std::exception &error)
        {
            send_guest_internal_error(res, "patch_guest_edit_activity", error);
        }
    });
}

void handle_get_guest_edit_attachments(HttpRes *res, HttpReq *req, Database &db)
{
    const std::string token{req->getHeader("x-guest-edit-token")};
    if (!valid_guest_token(token))
    {
        send_guest_json(res, 404, R"({"error":"Gastlink ungültig oder abgelaufen"})");
        return;
    }
    try
    {
        const auto link = require_guest_edit_link(res, db, guest_token_hash(token));
        if (!link)
            return;
        nlohmann::json attachments = nlohmann::json::array();
        for (const auto &attachment : db.list_attachments(link->activity_id))
            attachments.push_back(attachment_to_json(attachment));
        send_guest_json(res, 200, attachments.dump());
    }
    catch (const std::exception &error)
    {
        send_guest_internal_error(res, "get_guest_edit_attachments", error);
    }
}

void handle_post_guest_edit_attachment(HttpRes *res, HttpReq *req, Database &db)
{
    handle_post_guest_edit_upload(res, req, db, false);
}

void handle_post_guest_edit_program_image(HttpRes *res, HttpReq *req, Database &db)
{
    handle_post_guest_edit_upload(res, req, db, true);
}

void handle_get_guest_edit_attachment(HttpRes *res, HttpReq *req, Database &db)
{
    const std::string token{req->getHeader("x-guest-edit-token")};
    if (!valid_guest_token(token))
    {
        send_guest_json(res, 404, R"({"error":"Gastlink ungültig oder abgelaufen"})");
        return;
    }
    const std::string attachment_id{req->getParameter(0)};
    try
    {
        const auto link = require_guest_edit_link(res, db, guest_token_hash(token));
        if (!link)
            return;
        const auto attachment = db.get_attachment_data(attachment_id);
        if (!attachment || attachment->activity_id != link->activity_id || attachment->data.empty())
        {
            send_guest_json(res, 404, R"({"error":"Datei nicht gefunden"})");
            return;
        }
        std::string safe_name;
        for (char ch : attachment->filename)
        {
            if (ch == '"' || ch == '\\' || ch == '\r' || ch == '\n'
                || static_cast<unsigned char>(ch) < 0x20)
                continue;
            safe_name += ch;
        }
        if (safe_name.empty()) safe_name = "attachment";
        set_cors(res);
        res->writeStatus("200 OK");
        guest_no_store(res);
        res->writeHeader("Content-Type", attachment->content_type);
        res->writeHeader("Content-Disposition", "inline; filename=\"" + safe_name + "\"");
        res->writeHeader("X-Content-Type-Options", "nosniff");
        res->end(std::string_view(reinterpret_cast<const char *>(attachment->data.data()),
                                  attachment->data.size()));
    }
    catch (const std::exception &error)
    {
        send_guest_internal_error(res, "get_guest_edit_attachment", error);
    }
}

void handle_delete_guest_edit_attachment(HttpRes *res, HttpReq *req, Database &db)
{
    const std::string token{req->getHeader("x-guest-edit-token")};
    if (!valid_guest_token(token))
    {
        send_guest_json(res, 404, R"({"error":"Gastlink ungültig oder abgelaufen"})");
        return;
    }
    const std::string attachment_id{req->getParameter(0)};
    try
    {
        const auto link = require_guest_edit_link(res, db, guest_token_hash(token));
        if (!link)
            return;
        const auto attachment = db.get_attachment_data(attachment_id);
        if (!attachment || attachment->activity_id != link->activity_id)
        {
            send_guest_json(res, 404, R"({"error":"Datei nicht gefunden"})");
            return;
        }
        if (!db.delete_attachment(attachment_id))
        {
            send_guest_json(res, 404, R"({"error":"Datei nicht gefunden"})");
            return;
        }
        send_guest_json(res, 200, R"({"ok":true})");
    }
    catch (const std::exception &error)
    {
        send_guest_internal_error(res, "delete_guest_edit_attachment", error);
    }
}

void handle_get_guest_edit_program_image(HttpRes *res, HttpReq *req, Database &db)
{
    const std::string token{req->getHeader("x-guest-edit-token")};
    if (!valid_guest_token(token))
    {
        send_guest_json(res, 404, R"({"error":"Gastlink ungültig oder abgelaufen"})");
        return;
    }
    const std::string image_id{req->getParameter(0)};
    if (!valid_guest_program_image_id(image_id))
    {
        send_guest_json(res, 404, R"({"error":"Bild nicht gefunden"})");
        return;
    }
    try
    {
        const auto link = require_guest_edit_link(res, db, guest_token_hash(token));
        if (!link)
            return;
        const auto image = db.get_program_image_data(image_id);
        if (!image || image->activity_id != std::optional<std::string>(link->activity_id))
        {
            send_guest_json(res, 404, R"({"error":"Bild nicht gefunden"})");
            return;
        }
        guest_send_image(res, *image);
    }
    catch (const std::exception &error)
    {
        send_guest_internal_error(res, "get_guest_edit_program_image", error);
    }
}

void handle_delete_guest_edit_program_image(HttpRes *res, HttpReq *req, Database &db)
{
    const std::string token{req->getHeader("x-guest-edit-token")};
    if (!valid_guest_token(token))
    {
        send_guest_json(res, 404, R"({"error":"Gastlink ungültig oder abgelaufen"})");
        return;
    }
    const std::string image_id{req->getParameter(0)};
    if (!valid_guest_program_image_id(image_id))
    {
        send_guest_json(res, 404, R"({"error":"Bild nicht gefunden"})");
        return;
    }
    try
    {
        const auto link = require_guest_edit_link(res, db, guest_token_hash(token));
        if (!link)
            return;
        const auto image = db.get_program_image_data(image_id, false);
        const auto activity = db.get_activity_by_id(link->activity_id);
        if (!image || image->activity_id != std::optional<std::string>(link->activity_id) || !activity)
        {
            send_guest_json(res, 404, R"({"error":"Bild nicht gefunden"})");
            return;
        }
        for (const auto &program : activity->programs)
        {
            const auto ids = program_image_ids(program.description);
            if (std::find(ids.begin(), ids.end(), image_id) != ids.end())
            {
                send_guest_json(res, 409, R"({"error":"Das Bild wird noch in einem Programmpunkt verwendet"})");
                return;
            }
        }
        if (!db.delete_program_image(image_id))
        {
            send_guest_json(res, 404, R"({"error":"Bild nicht gefunden"})");
            return;
        }
        send_guest_json(res, 204, "");
    }
    catch (const std::exception &error)
    {
        send_guest_internal_error(res, "delete_guest_edit_program_image", error);
    }
}

void handle_get_ideenkiste(HttpRes *res, HttpReq *req, Database &db)
{
    UserRecord user;
    auto scope = ideenkiste_check(res, req, db, user, IdeenkisteOp::View);
    if (!scope)
        return;
    try
    {
        std::string dept_filter = (*scope == "own_dept" && user.department) ? *user.department : "";
        auto items = db.list_ideenkiste(dept_filter);
        nlohmann::json arr = nlohmann::json::array();
        for (auto &item : items)
            arr.push_back(ideenkiste_to_json(item));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}

void handle_post_ideenkiste(HttpRes *res, HttpReq *req, Database &db)
{
    UserRecord user;
    auto scope = ideenkiste_check(res, req, db, user, IdeenkisteOp::Add);
    if (!scope)
        return;
    res->onAborted([]() {});
    res->onData([res, &db, user, scope = *scope, body = std::string{}](std::string_view chunk, bool last) mutable
                {
        body.append(chunk);
        if (!last) return;
        try
        {
            auto j = nlohmann::json::parse(body);
            IdeenkisteInput input;
            input.title = j.value("title", "");
            input.duration_minutes = j.value("duration_minutes", 0);
            input.description = j.value("description", "");
            if (j.contains("department") && !j["department"].is_null())
                input.department = j["department"].get<std::string>();
            if (input.title.empty())
            {
                send_json(res, 400, R"({"error":"Titel fehlt"})");
                return;
            }
            if (scope == "own_dept")
                input.department = user.department;
            if (!validate_program_image_references(res, db, user, {input.description})) return;
            auto item = db.create_ideenkiste_item(input);
            if (!item)
            {
                send_json(res, 500, R"({"error":"Fehler beim Erstellen"})");
                return;
            }
            send_json(res, 201, ideenkiste_to_json(*item).dump());
        }
        catch (std::exception &e)
        {
            send_internal_error_local(res, "handler", e);
        } });
}

void handle_put_ideenkiste(HttpRes *res, HttpReq *req, Database &db)
{
    UserRecord user;
    auto scope = ideenkiste_check(res, req, db, user, IdeenkisteOp::Add);
    if (!scope)
        return;
    std::string id{req->getParameter(0)};
    res->onAborted([]() {});
    res->onData([res, &db, id, user, scope = *scope, body = std::string{}](std::string_view chunk, bool last) mutable
                {
        body.append(chunk);
        if (!last) return;
        try
        {
            if (!ideenkiste_item_in_scope(db, id, scope, user.department))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }

            auto j = nlohmann::json::parse(body);
            IdeenkisteInput input;
            input.title = j.value("title", "");
            input.duration_minutes = j.value("duration_minutes", 0);
            input.description = j.value("description", "");
            if (j.contains("department") && !j["department"].is_null())
                input.department = j["department"].get<std::string>();
            if (input.title.empty())
            {
                send_json(res, 400, R"({"error":"Titel fehlt"})");
                return;
            }
            if (scope == "own_dept")
                input.department = user.department;
            if (!validate_program_image_references(res, db, user, {input.description}, std::nullopt, id)) return;
            auto item = db.update_ideenkiste_item(id, input);
            if (!item)
            {
                send_json(res, 404, R"({"error":"Nicht gefunden"})");
                return;
            }
            send_json(res, 200, ideenkiste_to_json(*item).dump());
        }
        catch (std::exception &e)
        {
            send_internal_error_local(res, "handler", e);
        } });
}

void handle_delete_ideenkiste(HttpRes *res, HttpReq *req, Database &db)
{
    UserRecord user;
    auto scope = ideenkiste_check(res, req, db, user, IdeenkisteOp::Delete);
    if (!scope)
        return;
    std::string id{req->getParameter(0)};
    try
    {
        if (!ideenkiste_item_in_scope(db, id, *scope, user.department))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        bool ok = db.delete_ideenkiste_item(id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}
