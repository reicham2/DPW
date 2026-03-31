#pragma once
#include <string>
#include "json.hpp"

struct Activity {
    std::string id;
    std::string text;
    std::string created_at;
    std::string updated_at;
};

inline nlohmann::json to_json(const Activity& a) {
    return {
        {"id",         a.id},
        {"text",       a.text},
        {"created_at", a.created_at},
        {"updated_at", a.updated_at}
    };
}
