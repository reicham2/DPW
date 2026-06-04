#pragma once

#include "handlers.hpp"
#include "app_config.hpp"
#include "models.hpp"
#include "json.hpp"
#include "graph.hpp"
#include "cache.hpp"
#include "utils.hpp"
#include "wp_client.hpp"

#include <string>
#include <memory>
#include <algorithm>
#include <ctime>
#include <map>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <limits>
#include <vector>
#include <utility>
#include <array>
#include <cstdio>
#include <sys/wait.h>

// Shared primitives (formerly implicit via handlers.cpp inclusion)
TokenClaims validate_token(const std::string &token);
std::optional<UserRecord> resolve_user(Database &db, const TokenClaims &claims);
void send_internal_error(HttpRes *res, const char *context, const std::exception &e);

// Shared setup/maintenance state
struct MaintenanceState
{
    bool enabled{false};        // explicitly toggled on
    bool scheduled_now{false};  // current time falls within any scheduled window
    bool active{false};         // enabled || scheduled_now
    std::string message;
    std::string scheduled_start; // currently active or next scheduled start
    std::string scheduled_end;   // currently active or next scheduled end
    nlohmann::json windows = nlohmann::json::array();
    nlohmann::json upcoming_windows = nlohmann::json::array();
};

MaintenanceState get_maintenance_state(Database &db);
void sync_azure_runtime_env_from_db(Database &db);

// Cross-module helper used for websocket notification payloads
nlohmann::json notification_to_json(const NotificationRecord &n);

// Shared activity/weather helpers
struct WeatherResult
{
    std::string mode; // forecast | seasonal-average
    double temperature_c = 0.0;
    std::optional<double> temperature_min_c;
    std::optional<double> temperature_max_c;
    std::vector<std::pair<time_t, double>> hourly_temps;
    std::optional<int> rain_probability_percent;
    std::vector<std::pair<time_t, int>> hourly_rain_probability;
    std::string weather_symbol;
    std::string season;
    std::string point_name;
    std::string postal_code;
    std::string source;
    std::string note;
};

std::optional<int> shared_fetch_midata_children_count_cached(Database &db,
                                                             const std::string &group_id,
                                                             std::string &error);
std::optional<WeatherResult> shared_fetch_expected_weather_for_activity(const Activity &activity,
                                                                        const std::string &location_input,
                                                                        std::string &error);
bool shared_should_use_frozen_values(const Activity &activity);

std::string shared_activity_absolute_link(Database &db, const std::string &activity_id);
void shared_deliver_web_push_for_user(Database &db, const UserRecord &user, const NotificationRecord &notification);
std::string shared_format_date_ddmmyyyy(const std::string &iso);
std::string shared_join_display(const std::vector<std::string> &items);
std::vector<std::string> shared_unique_names_from_material(const std::vector<MaterialItem> &material);
bool shared_user_in_assignee_list(const std::vector<std::string> &assignees, const UserRecord &user);
bool shared_contains_name_ci(const std::vector<std::string> &names, const std::string &needle);
void shared_clear_cached_midata_children_counts();
std::string shared_normalize_assignee_key(const std::string &name);
bool shared_user_matches_assignee(const UserRecord &user, const std::string &assignee);
std::vector<std::string> shared_newly_assigned_users_from_material_delta(const std::vector<MaterialItem> &old_material,
                                                                          const std::vector<MaterialItem> &new_material);
