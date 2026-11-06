#include "handlers/handler_deps.hpp"
#include "handlers/program_images.hpp"
#include "utils/program_images.hpp"
#include <set>

namespace
{
    bool valid_image_id(const std::string &id)
    {
        const auto ids = program_image_ids("/api/program-images/" + id);
        return ids.size() == 1 && ids.front() == id;
    }

    bool can_read_image(Database &db, const UserRecord &user, const std::optional<RolePermission> &permission,
                        const std::vector<RoleDeptAccess> &dept_access, const ProgramImageData &image)
    {
        if (image.activity_id)
        {
            const auto activity = db.get_activity_by_id(*image.activity_id);
            return activity && permission && can_read_activity(*permission, user, dept_access, *activity);
        }
        if (!image.ideenkiste_id) return false;
        const auto idea = db.get_ideenkiste_item(*image.ideenkiste_id);
        if (!idea) return false;
        if (is_admin(user)) return true;
        if (!permission) return false;
        return permission->ideenkiste_scope == "all"
            || (permission->ideenkiste_scope == "own_dept"
                && (!idea->department || (user.department && idea->department == user.department)));
    }

    bool references_image(const Activity &activity, const std::string &id)
    {
        for (const auto &program : activity.programs)
            for (const auto &reference : program_image_ids(program.description))
                if (reference == id) return true;
        return false;
    }

    void send_image(HttpRes *res, const ProgramImageData &image)
    {
        if (!valid_program_image(image.content_type, image.data))
            throw std::runtime_error("program_images: invalid stored image");
        set_cors(res);
        res->writeStatus("200 OK");
        res->writeHeader("Content-Type", image.content_type);
        res->writeHeader("Content-Disposition", "inline");
        res->writeHeader("X-Content-Type-Options", "nosniff");
        res->writeHeader("Cache-Control", "private, no-store");
        res->end(std::string_view(reinterpret_cast<const char *>(image.data.data()), image.data.size()));
    }
}

bool validate_program_image_references(HttpRes *res, Database &db, const UserRecord &user,
                                      const std::vector<std::string> &descriptions,
                                      const std::optional<std::string> &activity_id,
                                      const std::optional<std::string> &ideenkiste_id)
{
    std::set<std::string> ids;
    for (const auto &description : descriptions)
        for (const auto &id : program_image_ids(description)) ids.insert(id);
    if (ids.empty()) return true;
    const auto permission = db.get_role_permission(user.role);
    const auto dept_access = db.list_role_dept_access(user.role);
    for (const auto &id : ids)
    {
        const auto image = db.get_program_image_data(id, false);
        if (!image)
        {
            send_json(res, 400, R"({"error":"Ein Bild existiert nicht mehr. Bitte erneut hochladen."})");
            return false;
        }
        // The caller has already authorized editing the target parent.
        if ((activity_id && image->activity_id == activity_id)
            || (ideenkiste_id && image->ideenkiste_id == ideenkiste_id))
            continue;
        if (!can_read_image(db, user, permission, dept_access, *image))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung zum Kopieren dieses Bildes"})");
            return false;
        }
    }
    return true;
}

void handle_get_program_image(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims)) return;
    const std::string id{req->getParameter(0)};
    if (!valid_image_id(id))
    {
        send_json(res, 400, R"({"error":"Ung\u00fcltige Bild-ID"})");
        return;
    }
    try
    {
        const auto image = db.get_program_image_data(id, false);
        if (!image)
        {
            send_json(res, 404, R"({"error":"Bild nicht gefunden"})");
            return;
        }
        const auto user = resolve_user(db, claims);
        if (!user || !can_read_image(db, *user, db.get_role_permission(user->role),
                                   db.list_role_dept_access(user->role), *image))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        const auto data = db.get_program_image_data(id);
        if (!data)
        {
            send_json(res, 404, R"({"error":"Bild nicht gefunden"})");
            return;
        }
        send_image(res, *data);
    }
    catch (const std::exception &e)
    {
        send_internal_error(res, "program_image", e);
    }
}

void handle_get_shared_program_image(HttpRes *res, HttpReq *req, Database &db)
{
    const std::string token{req->getParameter(0)};
    const std::string id{req->getParameter(1)};
    if (!valid_image_id(id))
    {
        send_json(res, 404, R"({"error":"Bild nicht gefunden"})");
        return;
    }
    try
    {
        const auto activity = db.get_activity_by_share_token(token);
        const auto image = db.get_program_image_data(id);
        if (!activity || !image || image->activity_id != std::optional<std::string>(activity->id)
            || !references_image(*activity, id))
        {
            send_json(res, 404, R"({"error":"Bild nicht gefunden"})");
            return;
        }
        send_image(res, *image);
    }
    catch (const std::exception &e)
    {
        send_internal_error(res, "shared_program_image", e);
    }
}

void handle_delete_program_image(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims)) return;
    const std::string id{req->getParameter(0)};
    if (!valid_image_id(id))
    {
        send_json(res, 400, R"({"error":"Ung\u00fcltige Bild-ID"})");
        return;
    }
    try
    {
        const auto image = db.get_program_image_data(id, false);
        if (!image)
        {
            send_json(res, 404, R"({"error":"Bild nicht gefunden"})");
            return;
        }
        const auto user = resolve_user(db, claims);
        const auto activity = image->activity_id ? db.get_activity_by_id(*image->activity_id) : std::nullopt;
        const auto permission = user ? db.get_role_permission(user->role) : std::nullopt;
        if (!user || !activity || !permission || !can_edit_activity(*permission, *user, *activity, claims.email))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        if (references_image(*activity, id))
        {
            send_json(res, 409, R"({"error":"Das Bild wird noch in einem Programmpunkt verwendet"})");
            return;
        }
        send_json(res, db.delete_program_image(id) ? 204 : 404, "");
    }
    catch (const std::exception &e)
    {
        send_internal_error(res, "delete_program_image", e);
    }
}
