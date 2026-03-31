#pragma once
#include <string>
#include <vector>
#include <optional>
#include "json.hpp"

// ---- Program ----------------------------------------------------------------

struct Program {
    std::string id;
    std::string activity_id;
    std::string time;
    std::string title;
    std::string description;
    std::string responsible;
};

struct ProgramInput {
    std::string time;
    std::string title;
    std::string description;
    std::string responsible;
};

// ---- Activity ---------------------------------------------------------------

struct Activity {
    std::string id;
    std::string title;
    std::string date;
    std::string start_time;
    std::string end_time;
    std::string goal;
    std::string location;
    std::string responsible;
    std::optional<std::string>  department;
    std::vector<std::string>    material;
    bool                        needs_siko{false};
    std::vector<uint8_t>        siko;          // raw bytes — never serialised
    std::optional<std::string>  bad_weather_info;
    std::string created_at;
    std::string updated_at;
    std::vector<Program>        programs;
};

struct ActivityInput {
    std::string title;
    std::string date;
    std::string start_time;
    std::string end_time;
    std::string goal;
    std::string location;
    std::string responsible;
    std::optional<std::string>  department;
    std::vector<std::string>    material;
    bool                        needs_siko{false};
    std::optional<std::string>  siko_base64;   // base64 string from frontend
    std::optional<std::string>  bad_weather_info;
    std::vector<ProgramInput>   programs;
};

// ---- Serialisers ------------------------------------------------------------

inline nlohmann::json program_to_json(const Program& p) {
    return {
        {"id",          p.id},
        {"activity_id", p.activity_id},
        {"time",        p.time},
        {"title",       p.title},
        {"description", p.description},
        {"responsible", p.responsible}
    };
}

// Full activity serialiser — includes has_siko flag, NEVER includes raw bytes
inline nlohmann::json to_json(const Activity& a) {
    nlohmann::json j = {
        {"id",         a.id},
        {"title",      a.title},
        {"date",       a.date},
        {"start_time", a.start_time},
        {"end_time",   a.end_time},
        {"goal",       a.goal},
        {"location",   a.location},
        {"responsible",a.responsible},
        {"material",   a.material},
        {"needs_siko", a.needs_siko},
        {"has_siko",   !a.siko.empty()},
        {"created_at", a.created_at},
        {"updated_at", a.updated_at}
    };
    j["department"]       = a.department       ? nlohmann::json(*a.department)       : nlohmann::json(nullptr);
    j["bad_weather_info"] = a.bad_weather_info ? nlohmann::json(*a.bad_weather_info) : nlohmann::json(nullptr);

    nlohmann::json progs = nlohmann::json::array();
    for (const auto& p : a.programs) progs.push_back(program_to_json(p));
    j["programs"] = progs;
    return j;
}
