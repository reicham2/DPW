#include "infra/audit_actions.hpp"
#include <unordered_map>
#include <vector>
#include <charconv>
#include <stdexcept>

std::optional<AuditAction> audit_action(const std::string &method, const std::string &route)
{
    // Public submissions and routine notification traffic are not journaled.
    if (route.rfind("/notifications", 0) == 0 || route.rfind("/push/", 0) == 0 ||
        route == "/forms/:slug/submit")
        return std::nullopt;

    static const std::unordered_map<std::string, AuditAction> actions = {
        {"/auth/me", {"Anmeldung", "user"}},
        {"/me", {"Profil", "user"}},
        {"/setup/auth-config", {"Ersteinrichtung", "system"}},
        {"/admin/maintenance", {"Wartungsfenster", "system"}},
        {"/admin/users/:id", {"Benutzerverwaltung", "user"}},
        {"/admin/roles", {"Rolle", "role"}},
        {"/admin/roles/reorder", {"Rollenreihenfolge", "role"}},
        {"/admin/roles/:name/move", {"Rollenreihenfolge", "role"}},
        {"/admin/roles/:name", {"Rolle", "role"}},
        {"/admin/departments", {"Abteilung", "department"}},
        {"/admin/departments/:name", {"Abteilung", "department"}},
        {"/admin/role-permissions", {"Rollenberechtigungen", "role"}},
        {"/admin/role-dept-access", {"Abteilungszugriff", "role"}},
        {"/admin/app-settings/:key", {"Systemeinstellung", "system"}},
        {"/admin/cron-jobs/:id", {"Cron-Job", "system"}},
        {"/admin/reset-azure-auth", {"Anmeldekonfiguration zurücksetzen", "system"}},
        {"/admin/activities/:id/restore", {"Aktivität wiederherstellen", "activity"}},
        {"/admin/activities/:id/trash", {"Aktivität endgültig löschen", "activity"}},
        {"/activities", {"Aktivität", "activity"}},
        {"/activities/:id", {"Aktivität", "activity"}},
        {"/activities/:id/evaluation", {"Aktivitätsauswertung", "activity"}},
        {"/admin/locations", {"Ort", "location"}},
        {"/admin/locations/:id", {"Ort", "location"}},
        {"/admin/materials", {"Materialverantwortliche", "material"}},
        {"/admin/materials/:id", {"Materialverantwortliche", "material"}},
        {"/activities/:id/attachments", {"Anhang", "activity"}},
        {"/activities/:id/program-images", {"Programmbild", "activity"}},
        {"/program-images/:id", {"Programmbild", "attachment"}},
        {"/attachments/:id", {"Anhang", "attachment"}},
        {"/activities/:id/share", {"Freigabelink", "activity"}},
        {"/activities/:id/weather-location", {"Wetterort", "activity"}},
        {"/mail-templates/:department", {"Mailvorlage", "template"}},
        {"/event-templates/:department", {"Eventvorlage", "template"}},
        {"/activities/:id/event-publication", {"Eventveröffentlichung", "activity"}},
        {"/send-mail", {"Mailversand", "mail"}},
        {"/activities/:id/mail-draft", {"Mailentwurf", "activity"}},
        {"/activities/:id/form-draft", {"Formularentwurf", "activity"}},
        {"/bug-report", {"Fehlermeldung", "system"}},
        {"/activities/:id/form", {"Anmeldeformular", "activity"}},
        {"/activities/:id/form/responses/:rid", {"Formularantwort", "activity"}},
        {"/form-templates", {"Formularvorlage", "template"}},
        {"/form-templates/:id", {"Formularvorlage", "template"}},
        {"/ideaBox", {"Ideenkiste", "idea"}},
        {"/ideaBox/:id", {"Ideenkiste", "idea"}},
    };
    auto found = actions.find(route);
    if (found == actions.end())
        return AuditAction{"Änderung", "system"};
    auto action = found->second;
    if (route != "/auth/me")
        action.label += method == "DELETE" ? " — Löschen" :
                        method == "POST" ? " — Ausführen/Erstellen" : " — Ändern";
    return action;
}

std::string audit_outcome(int status, bool uncertain)
{
    if (uncertain)
        return "unknown";
    if (status >= 200 && status < 300)
        return "completed";
    if (status >= 400 && status < 500)
        return "rejected";
    return "unknown";
}

nlohmann::json audit_change_details(const std::string &resource_type,
                                   const nlohmann::json &before, const nlohmann::json &after)
{
    auto scalar = [](const nlohmann::json &value) -> nlohmann::json
    {
        if (value.is_string())
        {
            auto text = value.get<std::string>();
            if (text.size() > 300)
            {
                size_t end = 300;
                while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
                text.resize(end);
                text += "…";
            }
            return text;
        }
        return value.is_boolean() || value.is_number() || value.is_null() ? value : nlohmann::json(nullptr);
    };
    struct Field { const char *key; const char *label; bool redact = false; };
    static const std::unordered_map<std::string, std::vector<Field>> fields = {
        {"user", {{"display_name", "Anzeigename"}, {"department", "Stufe"}, {"role", "Rolle"},
                  {"time_display_mode", "Zeitanzeige"}, {"avatar_color", "Avatarfarbe"},
                  {"proofreading_enabled", "Integrierte Rechtschreibprüfung"},
                  {"notify_material_assigned", "Materialbenachrichtigungen"},
                  {"notify_activity_assigned", "Aktivitätsbenachrichtigungen"},
                  {"notify_program_assigned", "Programmbenachrichtigungen"},
                  {"notify_mail_own_activity", "Mails zu eigenen Aktivitäten"},
                  {"notify_mail_department", "Mails zur Stufe"},
                  {"notify_channel_websocket", "Webbenachrichtigungen"},
                  {"notify_channel_email", "E-Mail-Benachrichtigungen"}}},
        {"activity", {{"title", "Titel"}, {"date", "Datum"}, {"start_time", "Startzeit"},
                      {"end_time", "Endzeit"}, {"department", "Stufe"}, {"location", "Ort"},
                      {"responsible", "Verantwortliche", true}, {"programs", "Programmblöcke", true},
                      {"material", "Material / Zuweisungen", true}, {"tn_material", "Teilnehmendenmaterial", true},
                      {"goal", "Ziel", true}, {"guiding_thread", "Roter Faden", true},
                      {"siko_text", "Sicherheitskonzept", true}, {"bad_weather_info", "Schlechtwetter", true},
                      {"planned_participants_estimate", "Geplante Teilnehmendenzahl"},
                      {"actual_participants", "Tatsächliche Teilnehmendenzahl"},
                      {"form_type", "Formulartyp"}, {"questions", "Formularfragen", true},
                      {"subject", "Betreff", true}, {"body_html", "Inhalt", true},
                      {"recipients", "Empfänger", true}, {"cc", "CC-Empfänger", true},
                      {"filename", "Dateiname", true}, {"content_type", "Dateityp"}}},
        {"location", {{"name", "Name"}}},
        {"material", {{"name", "Name"}}},
        {"role", {{"name", "Name"}, {"role", "Rolle"}, {"color", "Farbe"},
                  {"sort_order", "Reihenfolge"}, {"order", "Rollenreihenfolge", true},
                  {"direction", "Verschieberichtung"}, {"department", "Stufe"},
                  {"can_read", "Lesen"}, {"can_write", "Schreiben"},
                  {"activity_read_scope", "Aktivitäten lesen"}, {"activity_create_scope", "Aktivitäten erstellen"},
                  {"activity_edit_scope", "Aktivitäten bearbeiten"}, {"mail_send_scope", "Mailversand"},
                  {"mail_templates_scope", "Mailvorlagen"}, {"form_scope", "Formulare"},
                  {"form_templates_scope", "Formularvorlagen"}, {"event_templates_scope", "Eventvorlagen"},
                  {"event_publish_scope", "Events veröffentlichen"}, {"user_dept_scope", "Benutzerstufe verwalten"},
                  {"user_role_scope", "Benutzerrolle verwalten"}, {"locations_manage_scope", "Orte verwalten"},
                  {"materials_manage_scope", "Materialverantwortliche verwalten"},
                  {"ideenkiste_scope", "Ideenkiste lesen"}, {"ideenkiste_add_scope", "Ideen hinzufügen"},
                  {"ideenkiste_delete_scope", "Ideen löschen"},
                  {"transfer_users_to", "Benutzer übertragen nach"}, {"delete_users", "Benutzer löschen"}}},
        {"department", {{"name", "Name"}, {"color", "Farbe"}, {"midata_group_id", "MiData-Gruppen-ID"},
                        {"transfer_activities_to", "Aktivitäten übertragen nach"}, {"delete_activities", "Aktivitäten löschen"},
                        {"transfer_users_to", "Benutzer übertragen nach"}, {"delete_users", "Benutzer löschen"}}},
        {"system", {{"enabled", "Aktiviert"}, {"setting_operation", "Einstellung"},
                    {"retention_days", "Aufbewahrung (Tage)"}}},
        {"template", {{"name", "Name"}, {"department", "Stufe"},
                      {"subject", "Betreff", true}, {"body", "Vorlageninhalt", true},
                      {"title", "Vorlagentitel", true}, {"body_html", "Vorlageninhalt", true},
                      {"template_config", "Vorlagenkonfiguration", true}, {"is_default", "Standardvorlage"}}},
        {"attachment", {{"filename", "Dateiname", true}, {"content_type", "Dateityp"}}},
        {"mail", {{"recipient_count", "Empfängeranzahl"}, {"cc_count", "CC-Anzahl"}, {"status", "Versandstatus"}}},
        {"idea", {{"title", "Titel", true}, {"description", "Beschreibung", true}, {"department", "Stufe"}}},
    };
    auto changes = nlohmann::json::array();
    auto found = fields.find(resource_type);
    if (found != fields.end())
        for (const auto &field : found->second)
        {
            auto old_value = before.is_object() ? before.value(field.key, nlohmann::json(nullptr)) : nlohmann::json(nullptr);
            auto new_value = after.is_object() ? after.value(field.key, nlohmann::json(nullptr)) : nlohmann::json(nullptr);
            if (resource_type == "activity" && std::string(field.key) == "programs")
            {
                // Program rows are replaced on save; regenerated IDs are not content changes.
                for (auto *value : {&old_value, &new_value})
                    if (value->is_array())
                        for (auto &program : *value)
                            if (program.is_object())
                            {
                                program.erase("id");
                                program.erase("activity_id");
                            }
            }
            if (old_value == new_value) continue;
            auto empty_value = [](const nlohmann::json &value)
            {
                return value.empty() || (value.is_string() && value.get_ref<const std::string &>().empty());
            };
            if ((old_value.is_null() && empty_value(new_value)) || (new_value.is_null() && empty_value(old_value)))
                continue;
            nlohmann::json change = {{"field", field.key}, {"label", field.label}, {"redacted", field.redact}};
            if (!field.redact)
            {
                // Only short scalar metadata is eligible; nested content is never copied.
                change["before"] = scalar(old_value);
                change["after"] = scalar(new_value);
            }
            changes.push_back(std::move(change));
        }
    nlohmann::json details = {{"kind", before.is_null() ? "created" : after.is_null() ? "deleted" : "changed"},
                              {"changes", changes}};
    const auto &resource = after.is_object() ? after : before;
    for (const char *key : {"display_name", "title", "name"})
        if (resource.is_object() && resource.contains(key) && resource[key].is_string())
        {
            if (found != fields.end())
                for (const auto &field : found->second)
                    if (std::string(field.key) == key && !field.redact)
                        details["resource_name"] = scalar(resource[key]);
            break;
        }
    if (resource_type == "role" && resource.is_object() && resource.contains("role") && resource["role"].is_string())
        details["resource_name"] = scalar(resource["role"]);
    return details;
}

int audit_retention_days(const std::string &value)
{
    int days = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), days);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || days < 1 || days > 3650)
        throw std::invalid_argument("Audit retention must be an integer between 1 and 3650 days");
    return days;
}
