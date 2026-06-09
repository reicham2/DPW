#include "db/database.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <curl/curl.h>
#include "json.hpp"

// ---- Mail template helpers --------------------------------------------------

MailTemplate Database::row_to_mail_template(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    MailTemplate t;
    t.id = col("id") ? col("id") : "";
    t.department = col("department") ? col("department") : "";
    t.subject = col("subject") ? col("subject") : "";
    t.body = col("body") ? col("body") : "";
    if (col("recipients"))
        t.recipients = parse_pg_array(col("recipients"));
    if (col("cc"))
        t.cc = parse_pg_array(col("cc"));
    t.created_at = col("created_at") ? col("created_at") : "";
    t.updated_at = col("updated_at") ? col("updated_at") : "";
    return t;
}

std::vector<MailTemplate> Database::list_mail_templates()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT id, department, subject, body, recipients, cc, created_at, updated_at "
                           "FROM mail_templates ORDER BY department");

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_mail_templates: " + err);
    }
    std::vector<MailTemplate> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_mail_template(res, i));
    PQclear(res);
    return out;
}

std::optional<MailTemplate> Database::get_mail_template_by_department(const std::string &department)
{
    ensure_connected();
    const char *params[1] = {department.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, department, subject, body, recipients, cc, created_at, updated_at "
                                 "FROM mail_templates WHERE department = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    MailTemplate t = row_to_mail_template(res, 0);
    PQclear(res);
    return t;
}

std::optional<MailTemplate> Database::upsert_mail_template(const std::string &department,
                                                           const std::string &subject,
                                                           const std::string &body,
                                                           const std::vector<std::string> &recipients,
                                                           const std::vector<std::string> &cc)
{
    ensure_connected();
    auto build_arr = [](const std::vector<std::string> &v)
    {
        std::string s = "{";
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i > 0)
                s += ",";
            s += "\"" + v[i] + "\"";
        }
        s += "}";
        return s;
    };
    std::string recipients_arr = build_arr(recipients);
    std::string cc_arr = build_arr(cc);
    const char *params[5] = {department.c_str(), subject.c_str(), body.c_str(), recipients_arr.c_str(), cc_arr.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO mail_templates (department, subject, body, recipients, cc) "
                                 "VALUES ($1, $2, $3, $4::text[], $5::text[]) "
                                 "ON CONFLICT (department) DO UPDATE SET subject = EXCLUDED.subject, body = EXCLUDED.body, recipients = EXCLUDED.recipients, cc = EXCLUDED.cc "
                                 "RETURNING id, department, subject, body, recipients, cc, created_at, updated_at",
                                 5, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    MailTemplate t = row_to_mail_template(res, 0);
    PQclear(res);
    return t;
}

// ---- Event templates ---------------------------------------------------------

EventTemplate Database::row_to_event_template(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    EventTemplate t;
    t.id = col("id") ? col("id") : "";
    t.department = col("department") ? col("department") : "";
    t.title = col("title") ? col("title") : "";
    t.body = col("body") ? col("body") : "";
    t.created_at = col("created_at") ? col("created_at") : "";
    t.updated_at = col("updated_at") ? col("updated_at") : "";
    return t;
}

std::vector<EventTemplate> Database::list_event_templates()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT id, department, title, body, created_at, updated_at "
                           "FROM event_templates ORDER BY department");
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    std::vector<EventTemplate> out;
    for (int i = 0; i < PQntuples(res); ++i)
        out.push_back(row_to_event_template(res, i));
    PQclear(res);
    return out;
}

std::optional<EventTemplate> Database::get_event_template_by_department(const std::string &department)
{
    ensure_connected();
    const char *params[1] = {department.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, department, title, body, created_at, updated_at "
                                 "FROM event_templates WHERE department = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    EventTemplate t = row_to_event_template(res, 0);
    PQclear(res);
    return t;
}

std::optional<EventTemplate> Database::upsert_event_template(const std::string &department,
                                                             const std::string &title,
                                                             const std::string &body)
{
    ensure_connected();
    const char *params[3] = {department.c_str(), title.c_str(), body.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO event_templates (department, title, body) "
                                 "VALUES ($1, $2, $3) "
                                 "ON CONFLICT (department) DO UPDATE SET title = EXCLUDED.title, body = EXCLUDED.body "
                                 "RETURNING id, department, title, body, created_at, updated_at",
                                 3, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    EventTemplate t = row_to_event_template(res, 0);
    PQclear(res);
    return t;
}

// ---- Event publications -----------------------------------------------------

EventPublication Database::row_to_event_publication(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    EventPublication p;
    p.id = col("id") ? col("id") : "";
    p.activity_id = col("activity_id") ? col("activity_id") : "";
    p.published_by = col("published_by") ? col("published_by") : "";
    p.title = col("title") ? col("title") : "";
    p.body_html = col("body_html") ? col("body_html") : "";
    p.wp_event_id = col("wp_event_id") ? col("wp_event_id") : "";
    p.published_at = col("published_at") ? col("published_at") : "";
    return p;
}

std::optional<EventPublication> Database::get_event_publication(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, published_by, title, body_html, wp_event_id, published_at "
                                 "FROM event_publications WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    EventPublication p = row_to_event_publication(res, 0);
    PQclear(res);
    return p;
}

std::optional<EventPublication> Database::upsert_event_publication(const std::string &activity_id,
                                                                   const std::string &published_by,
                                                                   const std::string &title,
                                                                   const std::string &body_html)
{
    ensure_connected();
    const char *params[4] = {activity_id.c_str(), published_by.c_str(), title.c_str(), body_html.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO event_publications (activity_id, published_by, title, body_html) "
                                 "VALUES ($1, $2, $3, $4) "
                                 "ON CONFLICT (activity_id) DO UPDATE SET "
                                 "published_by = EXCLUDED.published_by, title = EXCLUDED.title, "
                                 "body_html = EXCLUDED.body_html, published_at = NOW() "
                                 "RETURNING id, activity_id, published_by, title, body_html, wp_event_id, published_at",
                                 4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    EventPublication p = row_to_event_publication(res, 0);
    PQclear(res);
    return p;
}

bool Database::update_event_publication_wp_id(const std::string &activity_id, const std::string &wp_event_id)
{
    ensure_connected();
    const char *params[2] = {wp_event_id.c_str(), activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE event_publications SET wp_event_id = $1 WHERE activity_id = $2",
                                 2, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

bool Database::delete_event_publication(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM event_publications WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

// ---- Sent mails log ---------------------------------------------------------

SentMail Database::row_to_sent_mail(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    SentMail m;
    m.id = col("id") ? col("id") : "";
    m.activity_id = col("activity_id") ? col("activity_id") : "";
    m.sender_id = col("sender_id") ? col("sender_id") : "";
    m.sender_email = col("sender_email") ? col("sender_email") : "";
    if (col("to_emails"))
        m.to_emails = parse_pg_array(col("to_emails"));
    if (col("cc_emails"))
        m.cc_emails = parse_pg_array(col("cc_emails"));
    m.subject = col("subject") ? col("subject") : "";
    m.body_html = col("body_html") ? col("body_html") : "";
    m.sent_at = col("sent_at") ? col("sent_at") : "";
    return m;
}

std::optional<SentMail> Database::log_sent_mail(const std::string &activity_id,
                                                const std::string &sender_id,
                                                const std::string &sender_email,
                                                const std::vector<std::string> &to_emails,
                                                const std::vector<std::string> &cc_emails,
                                                const std::string &subject,
                                                const std::string &body_html)
{
    ensure_connected();
    auto build_arr = [](const std::vector<std::string> &v)
    {
        std::string s = "{";
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i > 0)
                s += ",";
            s += "\"" + v[i] + "\"";
        }
        s += "}";
        return s;
    };
    std::string to_arr = build_arr(to_emails);
    std::string cc_arr = build_arr(cc_emails);
    const char *params[7] = {activity_id.c_str(), sender_id.c_str(), sender_email.c_str(),
                             to_arr.c_str(), cc_arr.c_str(), subject.c_str(), body_html.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO sent_mails (activity_id, sender_id, sender_email, to_emails, cc_emails, subject, body_html) "
                                 "VALUES ($1, $2, $3, $4::text[], $5::text[], $6, $7) "
                                 "RETURNING id, activity_id, sender_id, sender_email, to_emails, cc_emails, subject, body_html, sent_at",
                                 7, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        fprintf(stderr, "[log_sent_mail] %s\n", err.c_str());
        return std::nullopt;
    }
    SentMail m = row_to_sent_mail(res, 0);
    PQclear(res);
    return m;
}

std::vector<SentMail> Database::list_sent_mails(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, sender_id, sender_email, to_emails, cc_emails, subject, body_html, sent_at "
                                 "FROM sent_mails WHERE activity_id = $1 ORDER BY sent_at DESC",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    std::vector<SentMail> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_sent_mail(res, i));
    PQclear(res);
    return out;
}

// ---- send_mail via Microsoft Graph ------------------------------------------

static size_t graph_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *resp = static_cast<std::string *>(userdata);
    resp->append(ptr, size * nmemb);
    return size * nmemb;
}

bool Database::send_mail(const std::string &access_token,
                         const std::string &from_email,
                         const std::vector<std::string> &to_emails,
                         const std::vector<std::string> &cc_emails,
                         const std::string &subject,
                         const std::string &body_html)
{
    (void)from_email; // Graph /me/sendMail sends as the authenticated user

    // Build Graph API JSON payload
    nlohmann::json to_arr = nlohmann::json::array();
    for (auto &email : to_emails)
    {
        to_arr.push_back({{"emailAddress", {{"address", email}}}});
    }
    nlohmann::json cc_arr = nlohmann::json::array();
    for (auto &email : cc_emails)
    {
        cc_arr.push_back({{"emailAddress", {{"address", email}}}});
    }

    nlohmann::json message = {
        {"subject", subject},
        {"body", {{"contentType", "HTML"}, {"content", body_html}}},
        {"toRecipients", to_arr}};
    if (!cc_arr.empty())
        message["ccRecipients"] = cc_arr;
    nlohmann::json payload = {
        {"message", message},
        {"saveToSentItems", true}};

    std::string url = "https://graph.microsoft.com/v1.0/me/sendMail";
    std::string json_body = payload.dump();
    std::string response_body;

    CURL *curl = curl_easy_init();
    if (!curl)
        return false;

    struct curl_slist *headers = nullptr;
    std::string auth_hdr = "Authorization: Bearer " + access_token;
    headers = curl_slist_append(headers, auth_hdr.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, graph_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || (http_code != 202 && http_code != 200))
    {
        fprintf(stderr, "[send_mail] HTTP %ld: %s\n", http_code, response_body.c_str());
        return false;
    }
    return true;
}

// ---- Mail drafts ------------------------------------------------------------

MailDraft Database::row_to_mail_draft(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    MailDraft d;
    d.id = col("id") ? col("id") : "";
    d.activity_id = col("activity_id") ? col("activity_id") : "";
    if (col("recipients"))
        d.recipients = parse_pg_array(col("recipients"));
    if (col("cc"))
        d.cc = parse_pg_array(col("cc"));
    d.subject = col("subject") ? col("subject") : "";
    d.body_html = col("body_html") ? col("body_html") : "";
    d.updated_by = col("updated_by") ? col("updated_by") : "";
    d.updated_at = col("updated_at") ? col("updated_at") : "";
    return d;
}

std::optional<MailDraft> Database::get_mail_draft(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, recipients, cc, subject, body_html, updated_by, updated_at "
                                 "FROM mail_drafts WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    MailDraft d = row_to_mail_draft(res, 0);
    PQclear(res);
    return d;
}

std::optional<MailDraft> Database::upsert_mail_draft(const std::string &activity_id,
                                                     const std::vector<std::string> &recipients,
                                                     const std::vector<std::string> &cc,
                                                     const std::string &subject,
                                                     const std::string &body_html,
                                                     const std::string &updated_by)
{
    ensure_connected();
    auto build_arr = [](const std::vector<std::string> &v)
    {
        std::string s = "{";
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i > 0)
                s += ",";
            s += "\"" + v[i] + "\"";
        }
        s += "}";
        return s;
    };
    std::string recip_arr = build_arr(recipients);
    std::string cc_arr = build_arr(cc);
    const char *params[6] = {activity_id.c_str(), recip_arr.c_str(), cc_arr.c_str(), subject.c_str(),
                             body_html.c_str(), updated_by.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO mail_drafts (activity_id, recipients, cc, subject, body_html, updated_by) "
                                 "VALUES ($1, $2::text[], $3::text[], $4, $5, $6) "
                                 "ON CONFLICT (activity_id) DO UPDATE SET recipients = EXCLUDED.recipients, cc = EXCLUDED.cc, "
                                 "subject = EXCLUDED.subject, body_html = EXCLUDED.body_html, updated_by = EXCLUDED.updated_by "
                                 "RETURNING id, activity_id, recipients, cc, subject, body_html, updated_by, updated_at",
                                 6, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        fprintf(stderr, "[upsert_mail_draft] %s\n", err.c_str());
        return std::nullopt;
    }
    MailDraft d = row_to_mail_draft(res, 0);
    PQclear(res);
    return d;
}

bool Database::delete_mail_draft(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM mail_drafts WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

FormDraft Database::row_to_form_draft(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    FormDraft d;
    d.id = col("id") ? col("id") : "";
    d.activity_id = col("activity_id") ? col("activity_id") : "";
    d.form_type = col("form_type") ? col("form_type") : "";
    d.title = col("title") ? col("title") : "";
    d.questions_json = col("questions_json") ? col("questions_json") : "[]";
    d.updated_by = col("updated_by") ? col("updated_by") : "";
    d.updated_at = col("updated_at") ? col("updated_at") : "";
    return d;
}

std::optional<FormDraft> Database::get_form_draft(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, form_type, title, questions_json, updated_by, updated_at "
                                 "FROM form_drafts WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    FormDraft d = row_to_form_draft(res, 0);
    PQclear(res);
    return d;
}

std::optional<FormDraft> Database::upsert_form_draft(const std::string &activity_id,
                                                     const std::string &form_type,
                                                     const std::string &title,
                                                     const std::string &questions_json,
                                                     const std::string &updated_by)
{
    ensure_connected();
    const char *params[5] = {activity_id.c_str(), form_type.c_str(), title.c_str(),
                             questions_json.c_str(), updated_by.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO form_drafts (activity_id, form_type, title, questions_json, updated_by) "
                                 "VALUES ($1, $2, $3, $4::jsonb, $5) "
                                 "ON CONFLICT (activity_id) DO UPDATE SET form_type = EXCLUDED.form_type, "
                                 "title = EXCLUDED.title, questions_json = EXCLUDED.questions_json, updated_by = EXCLUDED.updated_by "
                                 "RETURNING id, activity_id, form_type, title, questions_json, updated_by, updated_at",
                                 5, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        fprintf(stderr, "[upsert_form_draft] %s\n", err.c_str());
        return std::nullopt;
    }
    FormDraft d = row_to_form_draft(res, 0);
    PQclear(res);
    return d;
}

bool Database::delete_form_draft(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM form_drafts WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}
