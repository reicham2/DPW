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

// ---- Location ---------------------------------------------------------------

struct LocationRecord
{
    std::string id;
    std::string name;
    std::string created_at;
    std::string updated_at;
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
    std::string activity_id;
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

// ---- Forms ------------------------------------------------------------------

struct FormQuestion
{
    std::string id;
    std::string form_id;
    std::string question_text;
    std::string question_type; // section | text_input | single_choice | multiple_choice | dropdown
    int position{0};
    bool is_required{true};
    nlohmann::json metadata; // options[], section title/subtitle, multiline, max_length
    std::string created_at;
};

struct SignupForm
{
    std::string id;
    std::string activity_id;
    std::string public_slug;
    std::string form_type; // registration | deregistration
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
    std::string submission_mode; // registration | deregistration
    std::string submitted_at;
    std::string user_agent;
    std::string ip_address;
    // answers: question_id -> answer_value (filled when fetching single response)
    std::vector<std::pair<std::string, std::string>> answers;
};

struct FormTemplate
{
    std::string id;
    std::string name;
    std::string department;
    std::string form_type;
    nlohmann::json template_config; // array of question-like objects
    bool is_default{false};
    std::string created_by;
    std::string created_at;
    std::string updated_at;
};

// ---- Forms serialisers -------------------------------------------------------

inline nlohmann::json form_question_to_json(const FormQuestion &q)
{
    return {
        {"id", q.id},
        {"form_id", q.form_id},
        {"question_text", q.question_text},
        {"question_type", q.question_type},
        {"position", q.position},
        {"is_required", q.is_required},
        {"metadata", q.metadata},
        {"created_at", q.created_at}};
}

inline nlohmann::json signup_form_to_json(const SignupForm &f)
{
    nlohmann::json qs = nlohmann::json::array();
    for (const auto &q : f.questions)
        qs.push_back(form_question_to_json(q));
    return {
        {"id", f.id},
        {"activity_id", f.activity_id},
        {"public_slug", f.public_slug},
        {"form_type", f.form_type},
        {"title", f.title},
        {"created_by", f.created_by},
        {"created_at", f.created_at},
        {"updated_at", f.updated_at},
        {"questions", qs}};
}

inline nlohmann::json form_response_to_json(const FormResponse &r, bool include_answers = false)
{
    nlohmann::json j = {
        {"id", r.id},
        {"form_id", r.form_id},
        {"submission_mode", r.submission_mode},
        {"submitted_at", r.submitted_at}};
    if (include_answers)
    {
        nlohmann::json ans = nlohmann::json::array();
        for (const auto &[qid, val] : r.answers)
            ans.push_back({{"question_id", qid}, {"answer_value", val}});
        j["answers"] = ans;
    }
    return j;
}

inline nlohmann::json form_template_to_json(const FormTemplate &t)
{
    return {
        {"id", t.id},
        {"name", t.name},
        {"is_default", t.is_default},
        {"department", t.department},
        {"form_type", t.form_type},
        {"template_config", t.template_config},
        {"created_by", t.created_by},
        {"created_at", t.created_at},
        {"updated_at", t.updated_at}};
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
