#pragma once
#include <string>
#include <vector>
#include "core/models/activity.hpp"
#include "core/models/user.hpp"
#include "json.hpp"

// ── Template variable substitution ──────────────────────────────────────────

std::string replace_template_vars(const std::string &text, const Activity &act,
                                  const std::vector<UserRecord> &all_users = {});

// ── JSON parsing helpers ────────────────────────────────────────────────────

std::string str_field(const nlohmann::json &j, const char *key,
                      const std::string &def = "");
ActivityInput parse_activity_input(const nlohmann::json &j);
