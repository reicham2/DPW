#include "utils/activity_helpers.hpp"
#include "infra/app_config.hpp"
#include "db/database.hpp"
#include "utils/strings.hpp"
#include "json.hpp"
#include <unordered_set>
#include <cstdlib>

std::string activity_absolute_link(Database &db, const std::string &activity_id)
{
    std::string base = app_config::get_or(db, app_config::kPublicBaseUrl, "");
    base = trim_ascii(base);
    while (!base.empty() && base.back() == '/')
        base.pop_back();
    if (base.empty())
        throw std::runtime_error("Öffentliche Basis-URL ist nicht konfiguriert");
    return base + "/activities/" + activity_id;
}

std::string format_date_ddmmyyyy(const std::string &iso)
{
    if (iso.size() >= 10 && iso[4] == '-' && iso[7] == '-')
        return iso.substr(8, 2) + "." + iso.substr(5, 2) + "." + iso.substr(0, 4);
    return iso;
}

std::string join_display(const std::vector<std::string> &items)
{
    std::string out;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].empty())
            continue;
        if (!out.empty())
            out += ", ";
        out += items[i];
    }
    if (out.empty())
        return "-";
    return out;
}

std::vector<std::string> unique_names_from_material(const std::vector<MaterialItem> &material)
{
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto &m : material)
    {
        for (const auto &name : m.responsible)
        {
            std::string t = trim_ascii(name);
            if (t.empty())
                continue;
            std::string key = to_lower_ascii(t);
            if (seen.insert(key).second)
                out.push_back(t);
        }
    }
    return out;
}

std::string normalize_assignee_key(const std::string &name)
{
    return to_lower_ascii(trim_ascii(name));
}

bool user_matches_assignee(const UserRecord &user, const std::string &assignee)
{
    const std::string assignee_key = normalize_assignee_key(assignee);
    if (assignee_key.empty())
        return false;

    if (assignee_key == normalize_assignee_key(user.display_name))
        return true;
    if (assignee_key == normalize_assignee_key(user.email))
        return true;

    const auto at = user.email.find('@');
    if (at != std::string::npos)
    {
        const std::string mail_local = user.email.substr(0, at);
        if (assignee_key == normalize_assignee_key(mail_local))
            return true;
    }

    return false;
}

bool user_in_assignee_list(const std::vector<std::string> &assignees, const UserRecord &user)
{
    for (const auto &assignee : assignees)
    {
        if (user_matches_assignee(user, assignee))
            return true;
    }
    return false;
}

bool contains_name_ci(const std::vector<std::string> &names, const std::string &needle)
{
    std::string key = to_lower_ascii(trim_ascii(needle));
    if (key.empty())
        return false;
    for (const auto &n : names)
    {
        if (to_lower_ascii(trim_ascii(n)) == key)
            return true;
    }
    return false;
}

std::vector<std::string> newly_assigned_users_from_material_delta(const std::vector<MaterialItem> &old_material,
                                                                   const std::vector<MaterialItem> &new_material)
{
    std::unordered_set<std::string> old_pairs;
    for (const auto &m : old_material)
    {
        const std::string material_key = to_lower_ascii(trim_ascii(m.name));
        if (material_key.empty())
            continue;
        for (const auto &name : m.responsible)
        {
            const std::string assignee_key = normalize_assignee_key(name);
            if (assignee_key.empty())
                continue;
            old_pairs.insert(material_key + "\n" + assignee_key);
        }
    }

    std::vector<std::string> out;
    std::unordered_set<std::string> seen_assignees;
    for (const auto &m : new_material)
    {
        const std::string material_key = to_lower_ascii(trim_ascii(m.name));
        if (material_key.empty())
            continue;
        for (const auto &name : m.responsible)
        {
            const std::string trimmed_name = trim_ascii(name);
            const std::string assignee_key = normalize_assignee_key(trimmed_name);
            if (assignee_key.empty())
                continue;

            const std::string pair_key = material_key + "\n" + assignee_key;
            if (old_pairs.find(pair_key) != old_pairs.end())
                continue;

            if (seen_assignees.insert(assignee_key).second)
                out.push_back(trimmed_name);
        }
    }
    return out;
}

nlohmann::json notification_to_json(const NotificationRecord &n)
{
    nlohmann::json j = {
        {"id", n.id},
        {"user_id", n.user_id},
        {"category", n.category},
        {"title", n.title},
        {"message", n.message},
        {"payload", n.payload},
        {"is_read", n.is_read},
        {"created_at", n.created_at},
    };
    j["link"] = n.link ? nlohmann::json(*n.link) : nlohmann::json(nullptr);
    return j;
}
