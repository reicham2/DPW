#include "db/database.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include "json.hpp"

// ── Forms ─────────────────────────────────────────────────────────────────────

FormQuestion Database::row_to_form_question(PGresult *res, int row)
{
    FormQuestion q;
    q.id = PQgetvalue(res, row, 0);
    q.form_id = PQgetvalue(res, row, 1);
    q.question_text = PQgetvalue(res, row, 2);
    q.question_type = PQgetvalue(res, row, 3);
    q.position = std::stoi(PQgetvalue(res, row, 4));
    q.is_required = std::string(PQgetvalue(res, row, 5)) == "t";
    const char *meta = PQgetvalue(res, row, 6);
    q.metadata = nlohmann::json::parse(meta && *meta ? meta : "{}", nullptr, false);
    if (q.metadata.is_discarded())
        q.metadata = nlohmann::json::object();
    q.created_at = PQgetvalue(res, row, 7);
    return q;
}

SignupForm Database::row_to_signup_form(PGresult *res, int row)
{
    SignupForm f;
    f.id = PQgetvalue(res, row, 0);
    f.activity_id = PQgetvalue(res, row, 1);
    f.public_slug = PQgetvalue(res, row, 2);
    f.form_type = PQgetvalue(res, row, 3);
    f.title = PQgetvalue(res, row, 4);
    f.created_by = PQgetvalue(res, row, 5);
    f.created_at = PQgetvalue(res, row, 6);
    f.updated_at = PQgetvalue(res, row, 7);
    return f;
}

FormResponse Database::row_to_form_response(PGresult *res, int row)
{
    FormResponse r;
    r.id = PQgetvalue(res, row, 0);
    r.form_id = PQgetvalue(res, row, 1);
    r.submission_mode = PQgetvalue(res, row, 2);
    r.submitted_at = PQgetvalue(res, row, 3);
    r.user_agent = PQnfields(res) > 4 ? PQgetvalue(res, row, 4) : "";
    r.ip_address = PQnfields(res) > 5 ? PQgetvalue(res, row, 5) : "";
    return r;
}

FormTemplate Database::row_to_form_template(PGresult *res, int row)
{
    FormTemplate t;
    t.id = PQgetvalue(res, row, 0);
    t.name = PQgetvalue(res, row, 1);
    t.department = PQgetvalue(res, row, 2);
    t.form_type = PQgetvalue(res, row, 3);
    const char *cfg = PQgetvalue(res, row, 4);
    t.template_config = nlohmann::json::parse(cfg && *cfg ? cfg : "[]", nullptr, false);
    if (t.template_config.is_discarded())
        t.template_config = nlohmann::json::array();
    t.is_default = std::string(PQgetvalue(res, row, 5)) == "t";
    t.created_by = PQgetvalue(res, row, 6);
    t.created_at = PQgetvalue(res, row, 7);
    t.updated_at = PQgetvalue(res, row, 8);
    return t;
}

void Database::attach_questions_single(SignupForm &f)
{
    ensure_connected();
    const char *params[1] = {f.id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, form_id, question_text, question_type, position, is_required, metadata, created_at "
                                 "FROM form_questions WHERE form_id = $1 ORDER BY position",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return;
    }
    int n = PQntuples(res);
    f.questions.reserve(n);
    for (int i = 0; i < n; ++i)
        f.questions.push_back(row_to_form_question(res, i));
    PQclear(res);
}

std::optional<SignupForm> Database::get_form_for_activity(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, public_slug, form_type, title, created_by::text, created_at, updated_at "
                                 "FROM signup_forms WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    SignupForm f = row_to_signup_form(res, 0);
    PQclear(res);
    attach_questions_single(f);
    return f;
}

std::optional<SignupForm> Database::get_form_by_id(const std::string &form_id)
{
    ensure_connected();
    const char *params[1] = {form_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, public_slug, form_type, title, created_by::text, created_at, updated_at "
                                 "FROM signup_forms WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    SignupForm f = row_to_signup_form(res, 0);
    PQclear(res);
    attach_questions_single(f);
    return f;
}

std::optional<SignupForm> Database::get_form_for_public_slug(const std::string &public_slug)
{
    ensure_connected();
    const char *params[1] = {public_slug.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, public_slug, form_type, title, created_by::text, created_at, updated_at "
                                 "FROM signup_forms WHERE public_slug = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    SignupForm f = row_to_signup_form(res, 0);
    PQclear(res);
    attach_questions_single(f);
    return f;
}

std::optional<SignupForm> Database::create_form(const std::string &activity_id,
                                                const std::string &form_type,
                                                const std::string &title,
                                                const std::string &created_by,
                                                const std::vector<FormQuestion> &questions)
{
    ensure_connected();
    const char *params[4] = {activity_id.c_str(), form_type.c_str(), title.c_str(), created_by.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO signup_forms (activity_id, form_type, title, created_by) "
                                 "VALUES ($1, $2, $3, $4::uuid) "
                                 "RETURNING id, activity_id, public_slug, form_type, title, created_by::text, created_at, updated_at",
                                 4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return std::nullopt;
    }
    SignupForm f = row_to_signup_form(res, 0);
    PQclear(res);

    for (const auto &q : questions)
    {
        std::string meta_str = q.metadata.dump();
        std::string pos_str = std::to_string(q.position);
        const char *req_str = q.is_required ? "true" : "false";
        const char *qparams[6] = {
            f.id.c_str(), q.question_text.c_str(), q.question_type.c_str(),
            pos_str.c_str(), req_str, meta_str.c_str()};
        PGresult *qres = PQexecParams(conn_,
                                      "INSERT INTO form_questions (form_id, question_text, question_type, position, is_required, metadata) "
                                      "VALUES ($1, $2, $3, $4::int, $5::boolean, $6::jsonb) "
                                      "RETURNING id, form_id, question_text, question_type, position, is_required, metadata, created_at",
                                      6, nullptr, qparams, nullptr, nullptr, 0);
        if (PQresultStatus(qres) == PGRES_TUPLES_OK)
            f.questions.push_back(row_to_form_question(qres, 0));
        PQclear(qres);
    }
    return f;
}

std::optional<SignupForm> Database::update_form(const std::string &activity_id,
                                                const std::string &form_type,
                                                const std::string &title,
                                                const std::vector<FormQuestion> &questions)
{
    ensure_connected();
    const char *params[3] = {activity_id.c_str(), form_type.c_str(), title.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE signup_forms SET form_type=$2, title=$3 WHERE activity_id=$1 "
                                 "RETURNING id, activity_id, public_slug, form_type, title, created_by::text, created_at, updated_at",
                                 3, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    SignupForm f = row_to_signup_form(res, 0);
    PQclear(res);

    const char *dparams[1] = {f.id.c_str()};
    PGresult *dres = PQexecParams(conn_,
                                  "DELETE FROM form_questions WHERE form_id = $1",
                                  1, nullptr, dparams, nullptr, nullptr, 0);
    PQclear(dres);

    for (const auto &q : questions)
    {
        std::string meta_str = q.metadata.dump();
        std::string pos_str = std::to_string(q.position);
        const char *req_str = q.is_required ? "true" : "false";
        const char *qparams[6] = {
            f.id.c_str(), q.question_text.c_str(), q.question_type.c_str(),
            pos_str.c_str(), req_str, meta_str.c_str()};
        PGresult *qres = PQexecParams(conn_,
                                      "INSERT INTO form_questions (form_id, question_text, question_type, position, is_required, metadata) "
                                      "VALUES ($1, $2, $3, $4::int, $5::boolean, $6::jsonb) "
                                      "RETURNING id, form_id, question_text, question_type, position, is_required, metadata, created_at",
                                      6, nullptr, qparams, nullptr, nullptr, 0);
        if (PQresultStatus(qres) == PGRES_TUPLES_OK)
            f.questions.push_back(row_to_form_question(qres, 0));
        PQclear(qres);
    }
    return f;
}

bool Database::delete_form(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM signup_forms WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

std::optional<FormResponse> Database::submit_response(const std::string &form_id,
                                                      const std::string &submission_mode,
                                                      const std::string &user_agent,
                                                      const std::string &ip_address,
                                                      const std::vector<std::pair<std::string, std::string>> &answers)
{
    ensure_connected();
    const char *params[4] = {form_id.c_str(), submission_mode.c_str(),
                             user_agent.empty() ? nullptr : user_agent.c_str(),
                             ip_address.empty() ? nullptr : ip_address.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO form_responses (form_id, submission_mode, user_agent, ip_address) "
                                 "VALUES ($1, $2, $3, $4) "
                                 "RETURNING id, form_id, submission_mode, submitted_at",
                                 4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return std::nullopt;
    }
    FormResponse r = row_to_form_response(res, 0);
    PQclear(res);

    for (const auto &[qid, val] : answers)
    {
        const char *aparams[3] = {r.id.c_str(), qid.c_str(), val.c_str()};
        PGresult *ares = PQexecParams(conn_,
                                      "INSERT INTO response_answers (response_id, question_id, answer_value) VALUES ($1, $2, $3)",
                                      3, nullptr, aparams, nullptr, nullptr, 0);
        PQclear(ares);
    }
    r.answers = answers;
    return r;
}

std::vector<FormResponse> Database::list_responses(const std::string &form_id)
{
    ensure_connected();
    const char *params[1] = {form_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, form_id, submission_mode, submitted_at FROM form_responses "
                                 "WHERE form_id = $1 ORDER BY submitted_at DESC",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    int n = PQntuples(res);
    std::vector<FormResponse> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_form_response(res, i));
    PQclear(res);
    return out;
}

std::optional<FormResponse> Database::get_response(const std::string &response_id)
{
    ensure_connected();
    const char *params[1] = {response_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, form_id, submission_mode, submitted_at FROM form_responses WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    FormResponse r = row_to_form_response(res, 0);
    PQclear(res);

    const char *aparams[1] = {response_id.c_str()};
    PGresult *ares = PQexecParams(conn_,
                                  "SELECT question_id::text, answer_value FROM response_answers WHERE response_id = $1",
                                  1, nullptr, aparams, nullptr, nullptr, 0);
    if (PQresultStatus(ares) == PGRES_TUPLES_OK)
    {
        int n = PQntuples(ares);
        r.answers.reserve(n);
        for (int i = 0; i < n; ++i)
            r.answers.emplace_back(PQgetvalue(ares, i, 0), PQgetvalue(ares, i, 1));
    }
    PQclear(ares);
    return r;
}

bool Database::delete_response(const std::string &response_id)
{
    ensure_connected();
    const char *params[1] = {response_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM form_responses WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

nlohmann::json Database::get_form_stats(const std::string &form_id)
{
    ensure_connected();
    nlohmann::json stats;

    const char *params[1] = {form_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT submission_mode, COUNT(*) FROM form_responses WHERE form_id=$1 GROUP BY submission_mode",
                                 1, nullptr, params, nullptr, nullptr, 0);
    int total = 0;
    nlohmann::json by_mode = nlohmann::json::object();
    if (PQresultStatus(res) == PGRES_TUPLES_OK)
    {
        for (int i = 0; i < PQntuples(res); ++i)
        {
            std::string mode = PQgetvalue(res, i, 0);
            int cnt = std::stoi(PQgetvalue(res, i, 1));
            by_mode[mode] = cnt;
            total += cnt;
        }
    }
    PQclear(res);
    stats["total"] = total;
    stats["by_mode"] = by_mode;
    int registration_count = by_mode.contains("registration") ? by_mode["registration"].get<int>() : 0;
    int deregistration_count = by_mode.contains("deregistration") ? by_mode["deregistration"].get<int>() : 0;
    stats["registration_count"] = registration_count;
    stats["deregistration_count"] = deregistration_count;
    stats["expected_current"] = registration_count - deregistration_count;

    PGresult *qres = PQexecParams(conn_,
                                  "SELECT fq.id::text, fq.question_text, fq.question_type, ra.answer_value, COUNT(*) "
                                  "FROM form_questions fq "
                                  "JOIN response_answers ra ON ra.question_id = fq.id "
                                  "JOIN form_responses fr ON fr.id = ra.response_id "
                                  "WHERE fq.form_id = $1 "
                                  "GROUP BY fq.id, fq.question_text, fq.question_type, ra.answer_value "
                                  "ORDER BY fq.position",
                                  1, nullptr, params, nullptr, nullptr, 0);
    nlohmann::json questions_stats = nlohmann::json::object();
    if (PQresultStatus(qres) == PGRES_TUPLES_OK)
    {
        for (int i = 0; i < PQntuples(qres); ++i)
        {
            std::string qid = PQgetvalue(qres, i, 0);
            std::string qtext = PQgetvalue(qres, i, 1);
            std::string qtype = PQgetvalue(qres, i, 2);
            std::string val = PQgetvalue(qres, i, 3);
            int cnt = std::stoi(PQgetvalue(qres, i, 4));
            if (!questions_stats.contains(qid))
            {
                questions_stats[qid] = {
                    {"question_text", qtext},
                    {"question_type", qtype},
                    {"answers", nlohmann::json::object()}};
            }
            questions_stats[qid]["answers"][val] = cnt;
        }
    }
    PQclear(qres);
    stats["questions"] = questions_stats;
    return stats;
}

// ── Form Templates ────────────────────────────────────────────────────────────

std::vector<FormTemplate> Database::list_form_templates(const std::string &department)
{
    ensure_connected();
    const char *params[1] = {department.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, name, department, form_type, template_config, is_default, created_by::text, created_at, updated_at "
                                 "FROM form_templates WHERE department = $1 ORDER BY name",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    int n = PQntuples(res);
    std::vector<FormTemplate> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_form_template(res, i));
    PQclear(res);
    return out;
}

std::optional<FormTemplate> Database::create_form_template(const std::string &name,
                                                           const std::string &department,
                                                           const std::string &form_type,
                                                           const nlohmann::json &template_config,
                                                           const std::string &created_by,
                                                           bool is_default)
{
    ensure_connected();
    // If setting as default, clear any existing default for this department
    if (is_default)
    {
        const char *dp[1] = {department.c_str()};
        PGresult *dr = PQexecParams(conn_,
                                    "UPDATE form_templates SET is_default = false WHERE department = $1 AND is_default = true",
                                    1, nullptr, dp, nullptr, nullptr, 0);
        PQclear(dr);
    }
    std::string cfg_str = template_config.dump();
    const char *def_str = is_default ? "true" : "false";
    const char *params[6] = {name.c_str(), department.c_str(), form_type.c_str(), cfg_str.c_str(), created_by.c_str(), def_str};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO form_templates (name, department, form_type, template_config, created_by, is_default) "
                                 "VALUES ($1, $2, $3, $4::jsonb, $5::uuid, $6::boolean) "
                                 "RETURNING id, name, department, form_type, template_config, is_default, created_by::text, created_at, updated_at",
                                 6, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return std::nullopt;
    }
    FormTemplate t = row_to_form_template(res, 0);
    PQclear(res);
    return t;
}

std::optional<FormTemplate> Database::update_form_template(const std::string &id,
                                                           const std::string &name,
                                                           const std::string &form_type,
                                                           const nlohmann::json &template_config,
                                                           bool is_default)
{
    ensure_connected();
    // If setting as default, clear any existing default for this department
    if (is_default)
    {
        const char *ip[1] = {id.c_str()};
        PGresult *dr = PQexecParams(conn_,
                                    "UPDATE form_templates SET is_default = false "
                                    "WHERE department = (SELECT department FROM form_templates WHERE id = $1) "
                                    "AND is_default = true AND id != $1",
                                    1, nullptr, ip, nullptr, nullptr, 0);
        PQclear(dr);
    }
    std::string cfg_str = template_config.dump();
    const char *def_str = is_default ? "true" : "false";
    const char *params[5] = {id.c_str(), name.c_str(), form_type.c_str(), cfg_str.c_str(), def_str};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE form_templates SET name=$2, form_type=$3, template_config=$4::jsonb, is_default=$5::boolean WHERE id=$1 "
                                 "RETURNING id, name, department, form_type, template_config, is_default, created_by::text, created_at, updated_at",
                                 5, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    FormTemplate t = row_to_form_template(res, 0);
    PQclear(res);
    return t;
}

bool Database::delete_form_template(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM form_templates WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

// ── Ideenkiste ───────────────────────────────────────────────────────────────

IdeenkisteItem Database::row_to_ideenkiste(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    IdeenkisteItem item;
    item.id = col("id") ? col("id") : "";
    item.title = col("title") ? col("title") : "";
    item.duration_minutes = col("duration_minutes") ? std::stoi(col("duration_minutes")) : 0;
    item.description = col("description") ? col("description") : "";
    if (col("department"))
        item.department = col("department");
    item.created_at = col("created_at") ? col("created_at") : "";
    item.updated_at = col("updated_at") ? col("updated_at") : "";
    return item;
}

std::vector<IdeenkisteItem> Database::list_ideenkiste(const std::string &dept_filter)
{
    ensure_connected();
    PGresult *res;
    if (dept_filter.empty())
    {
        res = PQexec(conn_,
                     "SELECT id, title, duration_minutes, description, department, created_at, updated_at "
                     "FROM ideenkiste ORDER BY created_at DESC");
    }
    else
    {
        const char *params[] = {dept_filter.c_str()};
        res = PQexecParams(conn_,
                           "SELECT id, title, duration_minutes, description, department, created_at, updated_at "
                           "FROM ideenkiste WHERE department = $1 OR department IS NULL ORDER BY created_at DESC",
                           1, nullptr, params, nullptr, nullptr, 0);
    }
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    std::vector<IdeenkisteItem> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_ideenkiste(res, i));
    PQclear(res);
    return out;
}

std::optional<IdeenkisteItem> Database::create_ideenkiste_item(const IdeenkisteInput &input)
{
    ensure_connected();
    std::string dur = std::to_string(input.duration_minutes);
    const char *dept = input.department && !input.department->empty() ? input.department->c_str() : nullptr;
    const char *params[] = {input.title.c_str(), dur.c_str(), input.description.c_str(), dept};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO ideenkiste (title, duration_minutes, description, department) "
                                 "VALUES ($1, $2::int, $3, $4) "
                                 "RETURNING id, title, duration_minutes, description, department, created_at, updated_at",
                                 4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto item = row_to_ideenkiste(res, 0);
    PQclear(res);
    return item;
}

std::optional<IdeenkisteItem> Database::update_ideenkiste_item(const std::string &id, const IdeenkisteInput &input)
{
    ensure_connected();
    std::string dur = std::to_string(input.duration_minutes);
    const char *dept = input.department && !input.department->empty() ? input.department->c_str() : nullptr;
    const char *params[] = {id.c_str(), input.title.c_str(), dur.c_str(), input.description.c_str(), dept};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE ideenkiste SET title=$2, duration_minutes=$3::int, description=$4, department=$5 "
                                 "WHERE id=$1 "
                                 "RETURNING id, title, duration_minutes, description, department, created_at, updated_at",
                                 5, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto item = row_to_ideenkiste(res, 0);
    PQclear(res);
    return item;
}

bool Database::delete_ideenkiste_item(const std::string &id)
{
    ensure_connected();
    const char *params[] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM ideenkiste WHERE id=$1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = false;
    if (PQresultStatus(res) == PGRES_COMMAND_OK)
    {
        const char *affected = PQcmdTuples(res);
        ok = affected && std::string(affected) != "0";
    }
    PQclear(res);
    return ok;
}
