#pragma once
#include <string>
#include <vector>
#include <optional>
#include <ctime>
#include "json.hpp"
#include "core/models/activity.hpp"

struct WeatherResult
{
    std::string mode;
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

std::optional<WeatherResult> fetch_expected_weather_for_activity(const Activity &activity,
                                                                 const std::string &location_input,
                                                                 std::string &error);
bool should_use_frozen_values(const Activity &activity);

std::optional<std::string> select_latest_weather_asset(const nlohmann::json &payload,
                                                        const std::string &asset_suffix,
                                                        std::string &error);
std::optional<std::vector<std::pair<time_t, double>>> parse_weather_series(
    const std::string &csv, int point_id, std::string &error);
