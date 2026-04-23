#include "utils.hpp"
#include <ctime>
#include <algorithm>
#include <cctype>

namespace
{
    std::string to_lower_ascii(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string trim_ascii(std::string s)
    {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c)
                                        { return !std::isspace(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c)
                             { return !std::isspace(c); })
                    .base(),
                s.end());
        return s;
    }
} // namespace

// ── HTTP status text ────────────────────────────────────────────────────────

const char *status_text(int code)
{
    switch (code)
    {
    case 200:
        return "200 OK";
    case 201:
        return "201 Created";
    case 204:
        return "204 No Content";
    case 400:
        return "400 Bad Request";
    case 401:
        return "401 Unauthorized";
    case 403:
        return "403 Forbidden";
    case 404:
        return "404 Not Found";
    case 409:
        return "409 Conflict";
    case 413:
        return "413 Payload Too Large";
    case 429:
        return "429 Too Many Requests";
    case 502:
        return "502 Bad Gateway";
    case 503:
        return "503 Service Unavailable";
    case 500:
        return "500 Internal Server Error";
    default:
        return "200 OK";
    }
}

// ── URL decoding ────────────────────────────────────────────────────────────

std::string url_decode(const std::string &src)
{
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i)
    {
        if (src[i] == '%' && i + 2 < src.size())
        {
            unsigned int ch = 0;
            if (sscanf(src.c_str() + i + 1, "%2x", &ch) == 1)
            {
                out += static_cast<char>(ch);
                i += 2;
                continue;
            }
        }
        out += (src[i] == '+') ? ' ' : src[i];
    }
    return out;
}

// ── Date formatting (German) ────────────────────────────────────────────────

std::string format_date_long_de(const std::string &iso_date)
{
    if (iso_date.size() < 10)
        return iso_date;
    struct tm t{};
    t.tm_year = std::stoi(iso_date.substr(0, 4)) - 1900;
    t.tm_mon = std::stoi(iso_date.substr(5, 2)) - 1;
    t.tm_mday = std::stoi(iso_date.substr(8, 2));
    std::mktime(&t);
    static const char *wdays[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                                  "Donnerstag", "Freitag", "Samstag"};
    static const char *months[] = {"Januar", "Februar", "März", "April", "Mai",
                                   "Juni", "Juli", "August", "September",
                                   "Oktober", "November", "Dezember"};
    std::string r = wdays[t.tm_wday];
    r += ", ";
    r += std::to_string(t.tm_mday);
    r += ". ";
    r += months[t.tm_mon];
    r += " ";
    r += iso_date.substr(0, 4);
    return r;
}

std::string format_date_short_de(const std::string &iso_date)
{
    if (iso_date.size() < 10)
        return iso_date;
    return iso_date.substr(8, 2) + "." + iso_date.substr(5, 2) + "." + iso_date.substr(0, 4);
}

// ── Template variable substitution ──────────────────────────────────────────

std::string replace_template_vars(const std::string &text, const Activity &act)
{
    if (text.find("{{") == std::string::npos)
        return text;
    std::map<std::string, std::string> vars;
    vars["titel"] = act.title;
    vars["datum"] = format_date_long_de(act.date);
    vars["datum_kurz"] = format_date_short_de(act.date);
    vars["startzeit"] = act.start_time;
    vars["endzeit"] = act.end_time;
    vars["ort"] = act.location;
    {
        std::string r;
        for (size_t i = 0; i < act.responsible.size(); ++i)
        {
            if (i)
                r += ", ";
            r += act.responsible[i];
        }
        vars["verantwortlich"] = r;
    }
    vars["abteilung"] = act.department.value_or("\xe2\x80\x94");
    vars["ziel"] = act.goal;
    {
        std::string m;
        for (size_t i = 0; i < act.material.size(); ++i)
        {
            if (i)
                m += ", ";
            m += act.material[i].name;
        }
        vars["material"] = m.empty() ? "\xe2\x80\x94" : m;
    }
    vars["schlechtwetter"] = act.bad_weather_info.value_or("\xe2\x80\x94");
    {
        std::string p;
        for (auto &pr : act.programs)
        {
            if (!p.empty())
                p += "\n";
            p += (pr.duration_minutes > 0 ? std::to_string(pr.duration_minutes) + " min" : std::string("\xe2\x80\x94"));
            p += " \xe2\x80\x93 " + pr.title;
            if (!pr.responsible.empty())
            {
                std::string rj;
                for (size_t ri = 0; ri < pr.responsible.size(); ++ri)
                {
                    if (ri)
                        rj += ", ";
                    rj += pr.responsible[ri];
                }
                p += " (" + rj + ")";
            }
            if (!pr.description.empty())
                p += ": " + pr.description;
        }
        vars["programm"] = p.empty() ? "\xe2\x80\x94" : p;
    }

    std::string result = text;
    for (auto &[key, val] : vars)
    {
        std::string placeholder = "{{" + key + "}}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos)
        {
            result.replace(pos, placeholder.size(), val);
            pos += val.size();
        }
    }
    return result;
}

// ── Permission helpers ──────────────────────────────────────────────────────

bool is_admin(const UserRecord &user) { return user.role == "admin"; }

bool has_dept_access(const std::vector<RoleDeptAccess> &dept_access,
                     const std::string &dept, bool write)
{
    for (const auto &da : dept_access)
    {
        if (da.department == dept && (write ? da.can_write : da.can_read))
            return true;
    }
    return false;
}

bool can_create_dept(const RolePermission &perm, const UserRecord &user,
                     const std::vector<RoleDeptAccess> &dept_access,
                     const std::string &dept)
{
    if (is_admin(user))
        return true;
    if (perm.activity_create_scope == "all")
        return true;
    if (perm.activity_create_scope == "own_dept" && user.department && *user.department == dept)
        return true;
    return has_dept_access(dept_access, dept, true);
}

bool can_read_dept(const RolePermission &perm, const UserRecord &user,
                   const std::vector<RoleDeptAccess> &dept_access,
                   const std::string &dept)
{
    if (is_admin(user))
        return true;
    if (perm.activity_read_scope == "all")
        return true;
    if (perm.activity_read_scope == "same_dept" && user.department && *user.department == dept)
        return true;
    return has_dept_access(dept_access, dept, false);
}

bool is_activity_responsible(const Activity &activity, const UserRecord &user,
                             const std::string &email)
{
    auto normalize = [](const std::string &value)
    {
        return to_lower_ascii(trim_ascii(value));
    };

    const std::string user_name = normalize(user.display_name);
    const std::string user_email = normalize(email);
    std::string user_email_local;
    auto at = user_email.find('@');
    if (at != std::string::npos)
        user_email_local = user_email.substr(0, at);

    for (const auto &responsible : activity.responsible)
    {
        const std::string candidate = normalize(responsible);
        if (candidate.empty())
            continue;
        if (candidate == user_name || candidate == user_email || (!user_email_local.empty() && candidate == user_email_local))
            return true;
    }
    return false;
}

bool can_read_activity(const RolePermission &perm, const UserRecord &user,
                       const std::vector<RoleDeptAccess> &dept_access,
                       const Activity &activity)
{
    if (is_admin(user))
        return true;
    if (perm.activity_read_scope == "all")
        return true;
    if (!activity.department)
        return perm.activity_read_scope != "none";
    const std::string &dept = *activity.department;
    if (perm.activity_read_scope == "same_dept" && user.department && *user.department == dept)
        return true;
    return has_dept_access(dept_access, dept, false);
}

bool can_edit_activity(const RolePermission &perm, const UserRecord &user,
                       const Activity &activity, const std::string &email)
{
    if (is_admin(user))
        return true;
    if (perm.activity_edit_scope == "all")
        return true;
    if (perm.activity_edit_scope == "same_dept")
        return (activity.department && user.department && *activity.department == *user.department) ||
               is_activity_responsible(activity, user, email);
    if (perm.activity_edit_scope == "own")
        return is_activity_responsible(activity, user, email);
    return false;
}

// ── JSON parsing helpers ────────────────────────────────────────────────────

std::string str_field(const nlohmann::json &j, const char *key, const std::string &def)
{
    if (j.contains(key) && j[key].is_string())
        return j[key].get<std::string>();
    return def;
}

ActivityInput parse_activity_input(const nlohmann::json &j)
{
    ActivityInput input;
    input.title = str_field(j, "title");
    input.date = str_field(j, "date");
    input.start_time = str_field(j, "start_time");
    input.end_time = str_field(j, "end_time");
    input.goal = str_field(j, "goal");
    input.location = str_field(j, "location");
    if (j.contains("responsible") && j["responsible"].is_array())
    {
        for (auto &r : j["responsible"])
            if (r.is_string())
                input.responsible.push_back(r.get<std::string>());
    }
    if (j.contains("department") && j["department"].is_string())
        input.department = j["department"].get<std::string>();
    if (j.contains("bad_weather_info") && j["bad_weather_info"].is_string())
    {
        std::string bwi = j["bad_weather_info"].get<std::string>();
        if (!bwi.empty())
            input.bad_weather_info = bwi;
    }
    if (j.contains("siko_text") && j["siko_text"].is_string())
    {
        std::string st = j["siko_text"].get<std::string>();
        if (!st.empty())
            input.siko_text = st;
    }
    if (j.contains("planned_participants_estimate"))
    {
        if (j["planned_participants_estimate"].is_number_integer())
        {
            int value = j["planned_participants_estimate"].get<int>();
            if (value >= 0)
                input.planned_participants_estimate = value;
        }
        else if (j["planned_participants_estimate"].is_number())
        {
            int value = static_cast<int>(j["planned_participants_estimate"].get<double>());
            if (value >= 0)
                input.planned_participants_estimate = value;
        }
    }
    if (j.contains("material") && j["material"].is_array())
    {
        for (auto &m : j["material"])
        {
            MaterialItem mi;
            if (m.is_string())
            {
                mi.name = m.get<std::string>();
            }
            else if (m.is_object())
            {
                mi.name = str_field(m, "name");
                if (m.contains("responsible") && m["responsible"].is_array())
                {
                    for (auto &r : m["responsible"])
                        if (r.is_string())
                            mi.responsible.push_back(r.get<std::string>());
                }
                else if (m.contains("responsible") && m["responsible"].is_string())
                {
                    std::string rs = m["responsible"].get<std::string>();
                    if (!rs.empty())
                        mi.responsible.push_back(rs);
                }
            }
            if (!mi.name.empty())
                input.material.push_back(mi);
        }
    }
    if (j.contains("programs") && j["programs"].is_array())
    {
        for (auto &p : j["programs"])
        {
            ProgramInput pi;
            if (p.contains("duration_minutes") && p["duration_minutes"].is_number())
                pi.duration_minutes = p["duration_minutes"].get<int>();
            pi.title = str_field(p, "title");
            pi.description = str_field(p, "description");
            if (p.contains("responsible") && p["responsible"].is_array())
            {
                for (auto &r : p["responsible"])
                    if (r.is_string())
                        pi.responsible.push_back(r.get<std::string>());
            }
            else if (p.contains("responsible") && p["responsible"].is_string())
            {
                std::string rs = p["responsible"].get<std::string>();
                if (!rs.empty())
                    pi.responsible.push_back(rs);
            }
            input.programs.push_back(pi);
        }
    }
    return input;
}
