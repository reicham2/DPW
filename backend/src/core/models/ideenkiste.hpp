#pragma once
#include <string>
#include <optional>

struct IdeenkisteItem
{
    std::string id;
    std::string title;
    int duration_minutes{0};
    std::string description;
    std::optional<std::string> department;
    std::string created_at;
    std::string updated_at;
};

struct IdeenkisteInput
{
    std::string title;
    int duration_minutes{0};
    std::string description;
    std::optional<std::string> department;
};
