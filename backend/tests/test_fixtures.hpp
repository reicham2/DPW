#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/models/all_models.hpp"

inline Activity make_activity()
{
    Activity a;
    a.id = "act-1";
    a.title = "Pfadilager";
    a.date = "2026-07-15";
    a.start_time = "09:00";
    a.end_time = "17:00";
    a.goal = "Spass haben";
    a.location = "Waldhaus";
    a.responsible = {"Anna", "Ben"};
    a.department = "Pfadi";
    a.material = {{"Seil", {"Anna"}}, {"Karte", {}}};
    a.siko_text = "Alles sicher";
    a.bad_weather_info = "Drinnen";
    a.created_at = "2026-01-01T00:00:00Z";
    a.updated_at = "2026-01-01T00:00:00Z";
    a.programs = {{"prog-1", "act-1", 60, "Einstieg", "Begruessung", {"Anna"}}};
    return a;
}
