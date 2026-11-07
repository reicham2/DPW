#include "db/database.hpp"
#include "utils/program_images.hpp"
#include <stdexcept>

namespace
{
    void require_rows(PGconn *conn, PGresult *res)
    {
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            const std::string error = PQerrorMessage(conn);
            PQclear(res);
            throw std::runtime_error("program_images: " + error);
        }
    }
}

std::optional<Attachment> Database::add_program_image(const std::string &activity_id,
                                                      const std::string &filename,
                                                      const std::string &content_type,
                                                      const std::string &data_base64)
{
    ensure_connected();
    const char *params[] = {activity_id.c_str(), filename.c_str(), content_type.c_str(), data_base64.c_str()};
    PGresult *res = PQexecParams(conn_,
        "INSERT INTO program_images (activity_id, filename, content_type, data) "
        "VALUES ($1, $2, $3, decode($4, 'base64')) RETURNING id, activity_id, filename, content_type, created_at",
        4, nullptr, params, nullptr, nullptr, 0);
    require_rows(conn_, res);
    auto image = row_to_attachment(res, 0);
    PQclear(res);
    return image;
}

std::optional<ProgramImageData> Database::get_program_image_data(const std::string &id, bool include_data)
{
    ensure_connected();
    const char *params[] = {id.c_str(), include_data ? "true" : "false"};
    PGresult *res = PQexecParams(conn_,
        "SELECT activity_id, ideenkiste_id, filename, content_type, CASE WHEN $2::boolean THEN data ELSE NULL END "
        "FROM program_images WHERE id = $1", 2, nullptr, params, nullptr, nullptr, 0);
    require_rows(conn_, res);
    if (PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    ProgramImageData image;
    if (!PQgetisnull(res, 0, 0)) image.activity_id = PQgetvalue(res, 0, 0);
    if (!PQgetisnull(res, 0, 1)) image.ideenkiste_id = PQgetvalue(res, 0, 1);
    image.filename = PQgetvalue(res, 0, 2);
    image.content_type = PQgetvalue(res, 0, 3);
    if (include_data)
    {
        size_t length = 0;
        unsigned char *bytes = PQunescapeBytea(reinterpret_cast<const unsigned char *>(PQgetvalue(res, 0, 4)), &length);
        if (!bytes)
        {
            PQclear(res);
            throw std::runtime_error("program_images: failed to decode stored image");
        }
        image.data.assign(bytes, bytes + length);
        PQfreemem(bytes);
    }
    PQclear(res);
    return image;
}

bool Database::delete_program_image(const std::string &id)
{
    ensure_connected();
    const char *params[] = {id.c_str()};
    PGresult *res = PQexecParams(conn_, "DELETE FROM program_images WHERE id = $1",
        1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        const std::string error = PQerrorMessage(conn_);
        PQclear(res);
        throw std::runtime_error("delete_program_image: " + error);
    }
    const bool deleted = std::string(PQcmdTuples(res)) == "1";
    PQclear(res);
    return deleted;
}

std::string Database::copy_program_image_references(const std::string &description,
                                                    const std::optional<std::string> &activity_id,
                                                    const std::optional<std::string> &ideenkiste_id,
                                                    std::map<std::string, std::string> &copies)
{
    for (const auto &id : program_image_ids(description))
    {
        if (copies.contains(id)) continue;
        const char *params[] = {id.c_str(), activity_id ? activity_id->c_str() : nullptr,
            ideenkiste_id ? ideenkiste_id->c_str() : nullptr};
        PGresult *res = PQexecParams(conn_,
            "SELECT id FROM program_images WHERE id=$1 "
            "AND activity_id IS NOT DISTINCT FROM $2::uuid AND ideenkiste_id IS NOT DISTINCT FROM $3::uuid",
            3, nullptr, params, nullptr, nullptr, 0);
        require_rows(conn_, res);
        const bool same_parent = PQntuples(res) != 0;
        PQclear(res);
        if (same_parent)
        {
            copies.emplace(id, id);
            continue;
        }
        res = PQexecParams(conn_,
            "INSERT INTO program_images (activity_id, ideenkiste_id, filename, content_type, data) "
            "SELECT $2::uuid, $3::uuid, filename, content_type, data FROM program_images WHERE id=$1 RETURNING id",
            3, nullptr, params, nullptr, nullptr, 0);
        require_rows(conn_, res);
        if (PQntuples(res) == 0)
        {
            PQclear(res);
            throw std::runtime_error("program_images: source image no longer exists");
        }
        copies.emplace(id, PQgetvalue(res, 0, 0));
        PQclear(res);
    }
    return replace_program_image_ids(description, copies);
}
