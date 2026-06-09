#pragma once
#include <string>
#include <vector>
#include "json.hpp"

struct FormQuestion
{
    std::string id;
    std::string form_id;
    std::string question_text;
    std::string question_type;
    int position{0};
    bool is_required{true};
    nlohmann::json metadata;
    std::string created_at;
};

struct SignupForm
{
    std::string id;
    std::string activity_id;
    std::string public_slug;
    std::string form_type;
    std::string title;
    std::string created_by;
    std::string created_at;
    std::string updated_at;
    std::vector<FormQuestion> questions;
};

struct FormResponse
{
    std::string id;
    std::string form_id;
    std::string submission_mode;
    std::string submitted_at;
    std::string user_agent;
    std::string ip_address;
    std::vector<std::pair<std::string, std::string>> answers;
};

struct FormTemplate
{
    std::string id;
    std::string name;
    std::string department;
    std::string form_type;
    nlohmann::json template_config;
    bool is_default{false};
    std::string created_by;
    std::string created_at;
    std::string updated_at;
};
