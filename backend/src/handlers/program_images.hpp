#pragma once
#include "core/http.hpp"
#include "core/models/user.hpp"
#include <optional>
#include <string>
#include <vector>

class Database;

bool validate_program_image_references(HttpRes *res, Database &db, const UserRecord &user,
                                      const std::vector<std::string> &descriptions,
                                      const std::optional<std::string> &activity_id = std::nullopt,
                                      const std::optional<std::string> &ideenkiste_id = std::nullopt);
