#pragma once
#include <string>
#include <vector>
#include <optional>

struct MaterialItem
{
    std::string name;
    std::vector<std::string> responsible;
};

struct Program
{
    std::string id;
    std::string activity_id;
    int duration_minutes{0};
    std::string title;
    std::string description;
    std::vector<std::string> responsible;
};

struct ProgramInput
{
    int duration_minutes{0};
    std::string title;
    std::string description;
    std::vector<std::string> responsible;
};

struct Activity
{
    std::string id;
    std::string title;
    std::string date;
    std::string start_time;
    std::string end_time;
    std::string goal;
    std::string location;
    std::vector<std::string> responsible;
    std::optional<std::string> department;
    std::vector<MaterialItem> material;
    std::vector<std::string> tn_material;
    std::optional<std::string> siko_text;
    std::optional<std::string> bad_weather_info;
    std::optional<int> planned_participants_estimate;
    std::string created_at;
    std::string updated_at;
    std::vector<Program> programs;
};

struct ActivityInput
{
    std::string title;
    std::string date;
    std::string start_time;
    std::string end_time;
    std::string goal;
    std::string location;
    std::vector<std::string> responsible;
    std::optional<std::string> department;
    std::vector<MaterialItem> material;
    std::vector<std::string> tn_material;
    std::optional<std::string> siko_text;
    std::optional<std::string> bad_weather_info;
    std::optional<int> planned_participants_estimate;
    std::vector<ProgramInput> programs;
};
