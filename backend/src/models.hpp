#pragma once
#include <string>
#include "json.hpp"

struct Activity {
    std::string id;
    std::string text;
    std::string title;
    std::string description;
    std::string responsible;
    std::string created_at;
    std::string updated_at;
};

struct ActivityInput {
    std::string text;
    std::string title;
    std::string description;
    std::string responsible;
};

inline nlohmann::json to_json(const Activity& a) {
    return {
        {"id",          a.id},
        {"text",        a.text},
        {"title",       a.title},
        {"description", a.description},
        {"responsible", a.responsible},
        {"created_at",  a.created_at},
        {"updated_at",  a.updated_at}
    };
}
