#pragma once
#include "json.hpp"
#include "core/models/activity.hpp"
#include "core/models/attachment.hpp"
#include "core/models/ideenkiste.hpp"
#include "core/models/form.hpp"
#include "core/models/mail.hpp"
#include "core/models/notification.hpp"

inline nlohmann::json ideenkiste_to_json(const IdeenkisteItem &item)
{
    nlohmann::json j = {
        {"id", item.id},
        {"title", item.title},
        {"duration_minutes", item.duration_minutes},
        {"description", item.description},
        {"created_at", item.created_at},
        {"updated_at", item.updated_at}};
    j["department"] = item.department ? nlohmann::json(*item.department) : nlohmann::json(nullptr);
    return j;
}

inline nlohmann::json program_to_json(const Program &p)
{
    return {
        {"id", p.id},
        {"activity_id", p.activity_id},
        {"duration_minutes", p.duration_minutes},
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
    j["tn_material"] = a.tn_material;
    j["siko_text"] = a.siko_text ? nlohmann::json(*a.siko_text) : nlohmann::json(nullptr);
    j["bad_weather_info"] = a.bad_weather_info ? nlohmann::json(*a.bad_weather_info) : nlohmann::json(nullptr);
    j["planned_participants_estimate"] = a.planned_participants_estimate
                                             ? nlohmann::json(*a.planned_participants_estimate)
                                             : nlohmann::json(nullptr);

    nlohmann::json progs = nlohmann::json::array();
    for (const auto &p : a.programs)
        progs.push_back(program_to_json(p));
    j["programs"] = progs;
    return j;
}
