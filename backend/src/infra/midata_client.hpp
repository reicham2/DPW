#pragma once
#include <string>
#include <optional>

class Database;

std::optional<int> fetch_midata_children_count_cached(Database &db,
                                                      const std::string &group_id,
                                                      std::string &error);
void clear_cached_midata_children_counts();
