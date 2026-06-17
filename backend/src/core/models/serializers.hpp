#pragma once
#include "json.hpp"
#include "core/models/activity.hpp"
#include "core/models/attachment.hpp"
#include "core/models/ideenkiste.hpp"
#include "core/models/form.hpp"
#include "core/models/mail.hpp"
#include "core/models/notification.hpp"
#include "core/models/camp.hpp"

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

// ── Camp Planning serializers ────────────────────────────────────────────────

inline nlohmann::json to_json(const Camp &c)
{
    nlohmann::json j = {
        {"id", c.id},
        {"title", c.title},
        {"short_title", c.short_title},
        {"motto", c.motto},
        {"kind", c.kind},
        {"organizer", c.organizer},
        {"address_name", c.address_name},
        {"address_street", c.address_street},
        {"address_zipcode", c.address_zipcode},
        {"address_city", c.address_city},
        {"coach_name", c.coach_name},
        {"course_number", c.course_number},
        {"color", c.color},
        {"is_prototype", c.is_prototype},
        {"created_at", c.created_at},
        {"updated_at", c.updated_at}};
    j["department"] = c.department ? nlohmann::json(*c.department) : nlohmann::json(nullptr);
    j["created_by"] = c.created_by ? nlohmann::json(*c.created_by) : nlohmann::json(nullptr);
    return j;
}

inline nlohmann::json to_json(const CampCollaboration &c)
{
    nlohmann::json j = {
        {"id", c.id},
        {"camp_id", c.camp_id},
        {"display_name", c.display_name},
        {"role", c.role},
        {"camp_role", c.camp_role},
        {"abbreviation", c.abbreviation},
        {"color", c.color},
        {"status", c.status},
        {"created_at", c.created_at},
        {"updated_at", c.updated_at}};
    j["user_id"] = c.user_id ? nlohmann::json(*c.user_id) : nlohmann::json(nullptr);
    return j;
}

inline nlohmann::json to_json(const CampDayResponsible &d)
{
    return {
        {"id", d.id},
        {"period_id", d.period_id},
        {"day_offset", d.day_offset},
        {"responsible", d.responsible}};
}

inline nlohmann::json to_json(const CampCategory &c)
{
    return {
        {"id", c.id},
        {"camp_id", c.camp_id},
        {"short_name", c.short_name},
        {"name", c.name},
        {"color", c.color},
        {"numbering_style", c.numbering_style},
        {"position", c.position},
        {"created_at", c.created_at},
        {"updated_at", c.updated_at}};
}

inline nlohmann::json to_json(const CampPeriod &p)
{
    return {
        {"id", p.id},
        {"camp_id", p.camp_id},
        {"description", p.description},
        {"start_date", p.start_date},
        {"end_date", p.end_date},
        {"position", p.position},
        {"created_at", p.created_at},
        {"updated_at", p.updated_at}};
}

inline nlohmann::json to_json(const ScheduleEntry &s)
{
    return {
        {"id", s.id},
        {"activity_id", s.activity_id},
        {"period_id", s.period_id},
        {"period_offset", s.period_offset},
        {"length", s.length},
        {"left_fraction", s.left_fraction},
        {"width_fraction", s.width_fraction},
        {"created_at", s.created_at},
        {"updated_at", s.updated_at}};
}

inline nlohmann::json to_json(const ContentNode &n)
{
    nlohmann::json j = {
        {"id", n.id},
        {"activity_id", n.activity_id},
        {"slot", n.slot},
        {"position", n.position},
        {"content_type", n.content_type},
        {"instance_name", n.instance_name},
        {"is_root", n.is_root},
        {"data", n.data}};
    j["parent_id"] = n.parent_id ? nlohmann::json(*n.parent_id) : nlohmann::json(nullptr);
    return j;
}

inline nlohmann::json to_json(const CampMaterialItem &m)
{
    nlohmann::json j = {
        {"id", m.id},
        {"material_list_id", m.material_list_id},
        {"article_name", m.article_name},
        {"unit", m.unit},
        {"created_at", m.created_at},
        {"updated_at", m.updated_at}};
    j["content_node_id"] = m.content_node_id ? nlohmann::json(*m.content_node_id) : nlohmann::json(nullptr);
    j["period_id"] = m.period_id ? nlohmann::json(*m.period_id) : nlohmann::json(nullptr);
    j["quantity"] = m.quantity ? nlohmann::json(*m.quantity) : nlohmann::json(nullptr);
    return j;
}

inline nlohmann::json to_json(const CampMaterialList &l)
{
    nlohmann::json items = nlohmann::json::array();
    for (const auto &it : l.items)
        items.push_back(to_json(it));
    nlohmann::json j = {
        {"id", l.id},
        {"camp_id", l.camp_id},
        {"name", l.name},
        {"items", items},
        {"created_at", l.created_at},
        {"updated_at", l.updated_at}};
    j["collaboration_id"] = l.collaboration_id ? nlohmann::json(*l.collaboration_id) : nlohmann::json(nullptr);
    return j;
}

inline nlohmann::json to_json(const CampActivity &a)
{
    nlohmann::json sched = nlohmann::json::array();
    for (const auto &s : a.schedule_entries)
        sched.push_back(to_json(s));
    nlohmann::json nodes = nlohmann::json::array();
    for (const auto &n : a.content_nodes)
        nodes.push_back(to_json(n));
    nlohmann::json progs = nlohmann::json::array();
    for (const auto &p : a.programs)
        progs.push_back(program_to_json(p));
    nlohmann::json j = {
        {"id", a.id},
        {"camp_id", a.camp_id},
        {"title", a.title},
        {"location", a.location},
        {"responsible", a.responsible},
        {"programs", progs},
        {"schedule_entries", sched},
        {"responsible_collaboration_ids", a.responsible_collaboration_ids},
        {"content_nodes", nodes},
        {"created_at", a.created_at},
        {"updated_at", a.updated_at}};
    j["category_id"] = a.category_id ? nlohmann::json(*a.category_id) : nlohmann::json(nullptr);
    return j;
}
