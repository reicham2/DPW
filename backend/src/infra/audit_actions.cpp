#include "infra/audit_actions.hpp"
#include <unordered_map>

std::optional<AuditAction> audit_action(const std::string &method, const std::string &route)
{
    // No raw URL, query, dynamic name, body or public submission content is stored.
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
        {"/admin/reset-azure-auth", {"Anmeldekonfiguration zurücksetzen", "system"}},
        {"/admin/activities/:id/restore", {"Aktivität wiederherstellen", "activity"}},
        {"/admin/activities/:id/trash", {"Aktivität endgültig löschen", "activity"}},
        {"/activities", {"Aktivität", "activity"}},
        {"/activities/:id", {"Aktivität", "activity"}},
        {"/admin/locations", {"Ort", "location"}},
        {"/admin/locations/:id", {"Ort", "location"}},
        {"/admin/materials", {"Materialverantwortliche", "material"}},
        {"/admin/materials/:id", {"Materialverantwortliche", "material"}},
        {"/activities/:id/attachments", {"Anhang", "activity"}},
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
