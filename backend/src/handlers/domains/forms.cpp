#include "handlers.hpp"

#include "models.hpp"
#include "json.hpp"
#include "utils.hpp"

#include <cstdio>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

#if !defined(DPW_ENABLE_DEBUG_AUTH)
#define DPW_ENABLE_DEBUG_AUTH 0
#endif

namespace
{
    static TokenClaims validate_token(const std::string &token)
    {
#if DPW_ENABLE_DEBUG_AUTH
        if (token.rfind("debug:", 0) == 0)
        {
            std::string user_id = token.substr(6);
            if (user_id.empty())
                throw std::runtime_error("debug token: user_id missing");
            TokenClaims c;
            c.oid = "debug:" + user_id;
            c.email = "debug@local";
            c.display_name = "Debug";
            c.tid = "debug";
            return c;
        }
#endif
        return validate_microsoft_token(token);
    }

    static std::optional<UserRecord> resolve_user(Database &db, const TokenClaims &claims)
    {
        if (claims.oid.rfind("debug:", 0) == 0)
            return db.get_user_by_id(claims.oid.substr(6));
        return db.get_user_by_oid(claims.oid);
    }

    static void send_internal_error(HttpRes *res, const char *context, const std::exception &e)
    {
        fprintf(stderr, "[error] %s: %s\n", context, e.what());
        if (std::string(e.what()) == "Öffentliche Basis-URL ist nicht konfiguriert")
        {
            send_json(res, 409, R"({"error":"Öffentliche Basis-URL ist nicht konfiguriert"})");
            return;
        }
        send_json(res, 500, R"({"error":"Interner Serverfehler"})");
    }

    constexpr size_t kMaxPublicFormPayloadBytes = 128 * 1024;
    constexpr size_t kPublicSubmitBurstLimit = 3;
    constexpr size_t kPublicSubmitSustainedLimit = 12;
    constexpr auto kPublicSubmitBurstWindow = std::chrono::seconds(30);
    constexpr auto kPublicSubmitSustainedWindow = std::chrono::minutes(10);
    constexpr size_t kMaxPublicAnswerLength = 4000;

    struct PublicSubmitBucket
    {
        std::deque<std::chrono::steady_clock::time_point> attempts;
    };

    std::mutex public_submit_rate_limit_mutex;
    std::unordered_map<std::string, PublicSubmitBucket> public_submit_rate_limit;

    std::string normalize_public_submit_client_key(const std::string &ip_address, const std::string &user_agent)
    {
        if (!ip_address.empty())
            return ip_address;
        if (user_agent.empty())
            return "anonymous";
        return user_agent.substr(0, std::min<size_t>(user_agent.size(), 160));
    }

    bool is_public_submit_rate_limited(const std::string &public_slug, const std::string &client_key)
    {
        auto now = std::chrono::steady_clock::now();
        auto cutoff = now - kPublicSubmitSustainedWindow;
        std::lock_guard<std::mutex> lock(public_submit_rate_limit_mutex);
        auto &bucket = public_submit_rate_limit[public_slug + "|" + client_key];

        while (!bucket.attempts.empty() && bucket.attempts.front() < cutoff)
            bucket.attempts.pop_front();

        size_t burst_count = 0;
        auto burst_cutoff = now - kPublicSubmitBurstWindow;
        for (auto it = bucket.attempts.rbegin(); it != bucket.attempts.rend(); ++it)
        {
            if (*it < burst_cutoff)
                break;
            ++burst_count;
        }

        if (burst_count >= kPublicSubmitBurstLimit || bucket.attempts.size() >= kPublicSubmitSustainedLimit)
            return true;

        bucket.attempts.push_back(now);
        return false;
    }

    // Helper: check form_scope-based read access for an activity
    bool can_form_access(Database &db, const RolePermission &perm, const UserRecord &user,
                         const TokenClaims &claims, const std::string &activity_id, bool /*write*/)
    {
        if (is_admin(user))
            return true;
        if (perm.form_scope == "none")
            return false;
        auto act = db.get_activity_by_id(activity_id);
        if (!act)
            return false;
        if (perm.form_scope == "all")
            return true;
        if (perm.form_scope == "same_dept" && act->department && user.department && *act->department == *user.department)
            return true;
        if (perm.form_scope == "own" && is_activity_responsible(*act, user, claims.email))
            return true;
        return false;
    }

    // Helper: resolve activity department from activity_id
    std::optional<std::string> get_activity_dept(Database &db, const std::string &activity_id)
    {
        auto act = db.get_activity_by_id(activity_id);
        if (!act)
            return std::nullopt;
        return act->department;
    }

    // Helper: parse questions array from JSON
    std::vector<FormQuestion> parse_questions(const nlohmann::json &arr)
    {
        std::vector<FormQuestion> questions;
        if (!arr.is_array())
            return questions;
        int pos = 0;
        for (auto &q : arr)
        {
            FormQuestion fq;
            fq.question_text = q.value("question_text", "");
            fq.question_type = q.value("question_type", "text_input");
            fq.position = q.contains("position") ? q["position"].get<int>() : pos;
            fq.is_required = q.value("is_required", true);
            fq.metadata = q.contains("metadata") ? q["metadata"] : nlohmann::json::object();
            questions.push_back(fq);
            ++pos;
        }
        return questions;
    }
}

// ---- GET /activities/:id/form -----------------------------------------------

void handle_get_activity_form(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, false))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto form = db.get_form_for_activity(activity_id);
        if (!form)
        {
            send_json(res, 200, R"({"exists":false,"form":null})");
            return;
        }
        send_json(res, 200, nlohmann::json{{"exists", true}, {"form", signup_form_to_json(*form)}}.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- POST /activities/:id/form ----------------------------------------------

void handle_post_activity_form(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
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

    std::string activity_id{req->getParameter(0)};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    // Check: exactly one form per activity
    auto existing = db.get_form_for_activity(activity_id);
    if (existing)
    {
        send_json(res, 409, R"({"error":"Formular existiert bereits f\u00fcr diese Aktivit\u00e4t"})");
        return;
    }

    std::string user_id = current_user->id;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, activity_id, user_id, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }
        std::string form_type = j.value("form_type", "registration");
        std::string title = j.value("title", "");
        if (title.empty()) { send_json(res, 400, R"({"error":"Titel erforderlich"})"); return; }
        auto questions = parse_questions(j.value("questions", nlohmann::json::array()));
        try {
            auto form = db.create_form(activity_id, form_type, title, user_id, questions);
            if (!form) { send_json(res, 500, R"({"error":"Datenbankfehler"})"); return; }
            send_json(res, 201, signup_form_to_json(*form).dump());
        } catch (std::exception &e) { send_internal_error(res, "handler", e); } });
}

// ---- PUT /activities/:id/form -----------------------------------------------

void handle_put_activity_form(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
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

    std::string activity_id{req->getParameter(0)};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, activity_id, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }
        std::string form_type = j.value("form_type", "registration");
        std::string title = j.value("title", "");
        if (title.empty()) { send_json(res, 400, R"({"error":"Titel erforderlich"})"); return; }
        auto questions = parse_questions(j.value("questions", nlohmann::json::array()));
        try {
            auto form = db.update_form(activity_id, form_type, title, questions);
            if (!form) { send_json(res, 404, R"({"error":"Formular nicht gefunden"})"); return; }
            send_json(res, 200, signup_form_to_json(*form).dump());
        } catch (std::exception &e) { send_internal_error(res, "handler", e); } });
}

// ---- DELETE /activities/:id/form --------------------------------------------

void handle_delete_activity_form(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        bool ok = db.delete_form(activity_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Formular nicht gefunden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /activities/:id/form/responses -------------------------------------

void handle_get_form_responses(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, false))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto form = db.get_form_for_activity(activity_id);
        if (!form)
        {
            send_json(res, 404, R"({"error":"Kein Formular gefunden"})");
            return;
        }

        auto responses = db.list_responses(form->id);
        nlohmann::json arr = nlohmann::json::array();
        for (auto &r : responses)
            arr.push_back(form_response_to_json(r));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /activities/:id/form/responses/:rid --------------------------------

void handle_get_form_response(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    std::string response_id{req->getParameter(1)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, false))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto response = db.get_response(response_id);
        if (!response)
        {
            send_json(res, 404, R"({"error":"Antwort nicht gefunden"})");
            return;
        }
        send_json(res, 200, form_response_to_json(*response, true).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- DELETE /activities/:id/form/responses/:rid -----------------------------

void handle_delete_form_response(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    std::string response_id{req->getParameter(1)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        bool ok = db.delete_response(response_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Antwort nicht gefunden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /activities/:id/form/stats -----------------------------------------

void handle_get_form_stats(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        auto dept_access = db.list_role_dept_access(current_user->role);

        auto act_dept = get_activity_dept(db, activity_id);
        bool allowed = false;
        if (perm)
        {
            if (act_dept)
                allowed = can_read_dept(*perm, *current_user, dept_access, *act_dept);
            else
                allowed = perm->can_read_all_depts || perm->can_read_own_dept;
        }
        if (!allowed)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto form = db.get_form_for_activity(activity_id);
        if (!form)
        {
            send_json(res, 200, R"({"exists":false,"stats":null})");
            return;
        }

        auto stats = db.get_form_stats(form->id);
        stats["form_type"] = form->form_type;
        send_json(res, 200, nlohmann::json{{"exists", true}, {"stats", stats}}.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /forms/:slug (public, no auth) --------------------------------

void handle_get_public_form(HttpRes *res, HttpReq *req, Database &db)
{
    std::string public_slug{req->getParameter(0)};
    try
    {
        auto form = db.get_form_for_public_slug(public_slug);
        if (!form)
        {
            send_json(res, 404, R"({"error":"Formular nicht gefunden"})");
            return;
        }

        // Resolve template variables in question texts using activity data
        auto act = db.get_activity_by_id(form->activity_id);
        if (act)
        {
            form->title = replace_template_vars(form->title, *act);
            for (auto &q : form->questions)
            {
                q.question_text = replace_template_vars(q.question_text, *act);
                // Also resolve subtitle in section metadata
                if (q.metadata.contains("subtitle") && q.metadata["subtitle"].is_string())
                {
                    q.metadata["subtitle"] = replace_template_vars(q.metadata["subtitle"].get<std::string>(), *act);
                }
                // Resolve choice labels
                if (q.metadata.contains("choices") && q.metadata["choices"].is_array())
                {
                    for (auto &c : q.metadata["choices"])
                    {
                        if (c.contains("label") && c["label"].is_string())
                        {
                            c["label"] = replace_template_vars(c["label"].get<std::string>(), *act);
                        }
                    }
                }
            }
        }

        send_json(res, 200, signup_form_to_json(*form).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- POST /forms/:slug/submit (public, no auth) -----------------------

void handle_post_form_submit(HttpRes *res, HttpReq *req, Database &db)
{
    std::string public_slug{req->getParameter(0)};

    std::string user_agent{req->getHeader("user-agent")};
    std::string ip_address{req->getHeader("x-real-ip")};

    auto buf = std::make_shared<std::string>();
    auto handled = std::make_shared<bool>(false);
    res->onAborted([] {});
    res->onData([res, buf, handled, public_slug, user_agent, ip_address, &db](std::string_view chunk, bool last)
                {
        if (*handled) return;
        if (buf->size() + chunk.size() > kMaxPublicFormPayloadBytes)
        {
            *handled = true;
            send_json(res, 413, R"({"error":"Anfrage zu gross"})");
            return;
        }

        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        *handled = true;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }

        try {
            auto form = db.get_form_for_public_slug(public_slug);
            if (!form) { send_json(res, 404, R"({"error":"Formular nicht gefunden"})"); return; }

            auto client_key = normalize_public_submit_client_key(ip_address, user_agent);
            if (is_public_submit_rate_limited(public_slug, client_key))
            {
                send_json(res, 429, R"({"error":"Zu viele Einsendungen in kurzer Zeit"})");
                return;
            }

            // Parse answers: array of { question_id, answer_value }
            std::vector<std::pair<std::string, std::string>> answers;
            if (j.contains("answers") && j["answers"].is_array())
            {
                for (auto &a : j["answers"])
                {
                    std::string qid = a.value("question_id", "");
                    std::string val = a.value("answer_value", "");
                    if (val.size() > kMaxPublicAnswerLength)
                    {
                        send_json(res, 400, R"({"error":"Antwort ist zu lang"})");
                        return;
                    }
                    if (!qid.empty())
                        answers.emplace_back(qid, val);
                }
            }

            // Validate required questions
            for (const auto &q : form->questions)
            {
                if (q.question_type == "section" || !q.is_required) continue;
                bool answered = false;
                for (auto &[qid, val] : answers)
                    if (qid == q.id && !val.empty()) { answered = true; break; }
                if (!answered)
                {
                    send_json(res, 400, nlohmann::json{{"error", "Pflichtfeld fehlt: " + q.question_text}}.dump());
                    return;
                }
            }

            auto resp = db.submit_response(form->id, form->form_type, user_agent, ip_address, answers);
            if (!resp) { send_json(res, 500, R"({"error":"Datenbankfehler"})"); return; }
            send_json(res, 201, form_response_to_json(*resp, true).dump());
        } catch (std::exception &e) { send_internal_error(res, "handler", e); } });
}

// ---- GET /form-templates?department=... -------------------------------------

void handle_get_form_templates(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string department = url_decode(std::string{req->getQuery("department")});
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto templates = db.list_form_templates(department);
        nlohmann::json arr = nlohmann::json::array();
        for (auto &t : templates)
            arr.push_back(form_template_to_json(t));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- POST /form-templates ---------------------------------------------------

void handle_post_form_template(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
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

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->form_templates_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string user_id = current_user->id;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, user_id, current_user, perm, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }

        std::string name = j.value("name", "");
        std::string department = j.value("department", "");
        std::string form_type = j.value("form_type", "registration");
        nlohmann::json config = j.contains("template_config") ? j["template_config"] : nlohmann::json::array();
        bool is_default = j.value("is_default", false);

        if (name.empty() || department.empty()) {
            send_json(res, 400, R"({"error":"name und department erforderlich"})");
            return;
        }
        // Scope check: own_dept only allows own department
        if (!is_admin(*current_user) && perm->form_templates_scope == "own_dept")
        {
            if (!current_user->department || *current_user->department != department) {
                send_json(res, 403, R"({"error":"Keine Berechtigung f\u00fcr dieses Department"})");
                return;
            }
        }
        try {
            auto tpl = db.create_form_template(name, department, form_type, config, user_id, is_default);
            if (!tpl) { send_json(res, 500, R"({"error":"Datenbankfehler"})"); return; }
            send_json(res, 201, form_template_to_json(*tpl).dump());
        } catch (std::exception &e) { send_internal_error(res, "handler", e); } });
}

// ---- PUT /form-templates/:id ------------------------------------------------

void handle_put_form_template(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
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

    std::string tpl_id{req->getParameter(0)};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->form_templates_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, tpl_id, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }

        std::string name = j.value("name", "");
        std::string form_type = j.value("form_type", "registration");
        nlohmann::json config = j.contains("template_config") ? j["template_config"] : nlohmann::json::array();
        bool is_default = j.value("is_default", false);

        try {
            auto tpl = db.update_form_template(tpl_id, name, form_type, config, is_default);
            if (!tpl) { send_json(res, 404, R"({"error":"Vorlage nicht gefunden"})"); return; }
            send_json(res, 200, form_template_to_json(*tpl).dump());
        } catch (std::exception &e) { send_internal_error(res, "handler", e); } });
}

// ---- DELETE /form-templates/:id ---------------------------------------------

void handle_delete_form_template(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string tpl_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!is_admin(*current_user) && (!perm || perm->form_templates_scope == "none"))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        bool ok = db.delete_form_template(tpl_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Vorlage nicht gefunden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}
