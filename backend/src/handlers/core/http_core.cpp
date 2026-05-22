#include "handlers/internal/shared.hpp"

void set_cors(HttpRes *res)
{
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET,POST,PUT,PATCH,DELETE,OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "Content-Type,Authorization");
}

// ── Debug token support ─────────────────────────────────────────────────────
TokenClaims validate_token(const std::string &token)
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

bool require_auth(HttpRes *res, HttpReq *req, TokenClaims &out_claims)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return false;
    }
    try
    {
        out_claims = validate_token(token);
        return true;
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return false;
    }
}

std::string auth_token_from_header(HttpRes *res, const std::string &auth_header)
{
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
    }
    return token;
}

std::optional<UserRecord> resolve_user(Database &db, const TokenClaims &claims)
{
    if (claims.oid.rfind("debug:", 0) == 0)
        return db.get_user_by_id(claims.oid.substr(6));
    return db.get_user_by_oid(claims.oid);
}

void send_json(HttpRes *res, int status, const std::string &body)
{
    res->writeStatus(status_text(status));
    set_cors(res);
    res->writeHeader("Content-Type", "application/json");
    res->end(body);
}

void send_internal_error(HttpRes *res, const char *context, const std::exception &e)
{
    fprintf(stderr, "[error] %s: %s\n", context, e.what());
    if (std::string(e.what()) == "Öffentliche Basis-URL ist nicht konfiguriert")
    {
        send_json(res, 409, R"({"error":"Öffentliche Basis-URL ist nicht konfiguriert"})");
        return;
    }
    send_json(res, 500, R"({"error":"Interner Serverfehler"})");
}
