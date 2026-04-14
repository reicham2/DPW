#pragma once
#include <string>
#include <vector>
#include <optional>
#include "json.hpp"

// ---- Program ----------------------------------------------------------------

struct Program
{
    std::string id;
    std::string activity_id;
    std::string time;
    std::string title;
    std::string description;
    std::vector<std::string> responsible;
};

struct ProgramInput
{
    std::string time;
    std::string title;
    std::string description;
    std::vector<std::string> responsible;
};

// ---- Material ---------------------------------------------------------------

struct MaterialItem
{
    std::string name;
    std::vector<std::string> responsible; // optional — may be empty
};

// ---- Activity ---------------------------------------------------------------

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
    std::optional<std::string> siko_text;
    std::optional<std::string> bad_weather_info;
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
    std::optional<std::string> siko_text;
    std::optional<std::string> bad_weather_info;
    std::vector<ProgramInput> programs;
};

// ---- Attachment -------------------------------------------------------------

struct Attachment
{
    std::string id;
    std::string activity_id;
    std::string filename;
    std::string content_type;
    std::string created_at;
};

struct AttachmentData
{
    std::string filename;
    std::string content_type;
    std::vector<uint8_t> data;
};

// ---- Serialisers ------------------------------------------------------------

inline nlohmann::json program_to_json(const Program &p)
{
    return {
        {"id", p.id},
        {"activity_id", p.activity_id},
        {"time", p.time},
        {"title", p.title},
        {"description", p.description},
        {"responsible", p.responsible}};
}

inline nlohmann::json attachment_to_json(const Attachment &att)
{
    return {
        {"id", att.id},
        {"activity_id", att.activity_id},
        {"filename", att.filename},
        {"content_type", att.content_type},
        {"created_at", att.created_at}};
}

// Full activity serialiser
inline nlohmann::json to_json(const Activity &a)
{
    nlohmann::json mat = nlohmann::json::array();
    for (const auto &m : a.material)
    {
        nlohmann::json item = {{"name", m.name}};
        if (!m.responsible.empty())
            item["responsible"] = m.responsible;
        mat.push_back(item);
    }

    nlohmann::json j = {
        {"id", a.id},
        {"title", a.title},
        {"date", a.date},
        {"start_time", a.start_time},
        {"end_time", a.end_time},
        {"goal", a.goal},
        {"location", a.location},
        {"responsible", a.responsible},
        {"material", mat},
        {"created_at", a.created_at},
        {"updated_at", a.updated_at}};
    j["department"] = a.department ? nlohmann::json(*a.department) : nlohmann::json(nullptr);
    j["siko_text"] = a.siko_text ? nlohmann::json(*a.siko_text) : nlohmann::json(nullptr);
    j["bad_weather_info"] = a.bad_weather_info ? nlohmann::json(*a.bad_weather_info) : nlohmann::json(nullptr);

    nlohmann::json progs = nlohmann::json::array();
    for (const auto &p : a.programs)
        progs.push_back(program_to_json(p));
    j["programs"] = progs;
    return j;
}
