#include "utils/templates.hpp"
#include "utils/strings.hpp"
#include "utils/activity_helpers.hpp"
#include <map>

// ── Template variable substitution ──────────────────────────────────────────

std::string replace_template_vars(const std::string &text, const Activity &act,
                                  const std::vector<UserRecord> &all_users)
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
        auto resolved = resolve_ids_to_display_names(act.responsible, all_users);
        std::string r;
        for (size_t i = 0; i < resolved.size(); ++i)
        {
            if (i)
                r += ", ";
            r += resolved[i];
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
    {
        std::string tn;
        for (size_t i = 0; i < act.tn_material.size(); ++i)
        {
            if (i)
                tn += ", ";
            tn += act.tn_material[i];
        }
        vars["tn_material"] = tn.empty() ? "\xe2\x80\x94" : tn;
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
                auto resolved_pr = resolve_ids_to_display_names(pr.responsible, all_users);
                std::string rj;
                for (size_t ri = 0; ri < resolved_pr.size(); ++ri)
                {
                    if (ri)
                        rj += ", ";
                    rj += resolved_pr[ri];
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

    // Date variables with a day offset, e.g. {{datum|-2}} or {{datum_kurz|7}}.
    // The offset shifts the activity date so templates can express a deadline
    // (Anmeldefrist) relative to the activity. Plain {{datum}}/{{datum_kurz}}
    // are already handled above.
    auto replace_offset_date = [&](const std::string &name, bool long_fmt)
    {
        std::string token = "{{" + name + "|";
        size_t pos = 0;
        while ((pos = result.find(token, pos)) != std::string::npos)
        {
            size_t end = result.find("}}", pos + token.size());
            if (end == std::string::npos)
                break;
            std::string num = result.substr(pos + token.size(), end - (pos + token.size()));
            int days = 0;
            bool ok = !num.empty();
            try
            {
                size_t idx = 0;
                days = std::stoi(num, &idx);
                if (idx != num.size())
                    ok = false;
            }
            catch (...)
            {
                ok = false;
            }
            if (!ok)
            {
                pos = end + 2;
                continue;
            }
            std::string shifted = shift_iso_date(act.date, days);
            std::string val = long_fmt ? format_date_long_de(shifted)
                                       : format_date_short_de(shifted);
            result.replace(pos, end + 2 - pos, val);
            pos += val.size();
        }
    };
    replace_offset_date("datum", true);
    replace_offset_date("datum_kurz", false);

    return result;
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
    if (j.contains("tn_material") && j["tn_material"].is_array())
    {
        for (auto &m : j["tn_material"])
        {
            if (m.is_string())
            {
                std::string name = m.get<std::string>();
                if (!name.empty())
                    input.tn_material.push_back(name);
            }
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
