#include "models.hpp"
#include "test_harness.hpp"
#include "test_fixtures.hpp"

TEST(program_to_json_fields)
{
    Program p{"prog-1", "act-1", 45, "Block", "Beschreibung", {"Anna"}};
    auto j = program_to_json(p);
    ASSERT_STR_EQ(j["id"].get<std::string>(), "prog-1");
    ASSERT_EQ(j["duration_minutes"].get<int>(), 45);
    ASSERT_EQ((int)j["responsible"].size(), 1);
}

TEST(program_to_json_empty_responsible)
{
    Program p{"prog-2", "act-1", 0, "Ohne Team", "", {}};
    auto j = program_to_json(p);
    ASSERT_TRUE(j["responsible"].is_array());
    ASSERT_EQ((int)j["responsible"].size(), 0);
}

TEST(activity_to_json_fields)
{
    Activity a = make_activity();
    auto j = to_json(a);
    ASSERT_STR_EQ(j["id"].get<std::string>(), "act-1");
    ASSERT_STR_EQ(j["title"].get<std::string>(), "Pfadilager");
    ASSERT_STR_EQ(j["date"].get<std::string>(), "2026-07-15");
    ASSERT_STR_EQ(j["department"].get<std::string>(), "Pfadi");
    ASSERT_EQ((int)j["responsible"].size(), 2);
    ASSERT_EQ((int)j["programs"].size(), 1);
    ASSERT_EQ((int)j["material"].size(), 2);
}

TEST(activity_to_json_material_without_responsible_omits_field)
{
    Activity a = make_activity();
    a.material = {{"Seil", {}}};
    auto j = to_json(a);
    ASSERT_TRUE(!j["material"][0].contains("responsible"));
}

TEST(activity_to_json_null_optionals)
{
    Activity a = make_activity();
    a.department = std::nullopt;
    a.siko_text = std::nullopt;
    a.bad_weather_info = std::nullopt;
    auto j = to_json(a);
    ASSERT_TRUE(j["department"].is_null());
    ASSERT_TRUE(j["siko_text"].is_null());
    ASSERT_TRUE(j["bad_weather_info"].is_null());
}

TEST(activity_to_json_participant_estimate)
{
    Activity a = make_activity();
    a.planned_participants_estimate = 23;
    auto j = to_json(a);
    ASSERT_EQ(j["planned_participants_estimate"].get<int>(), 23);
}

TEST(activity_to_json_default_participant_estimate_null)
{
    Activity a = make_activity();
    a.planned_participants_estimate = std::nullopt;
    auto j = to_json(a);
    ASSERT_TRUE(j["planned_participants_estimate"].is_null());
}

TEST(activity_to_json_includes_tn_material_array)
{
    Activity a = make_activity();
    a.tn_material = {"Trinkflasche"};
    auto j = to_json(a);
    ASSERT_TRUE(j["tn_material"].is_array());
    ASSERT_EQ((int)j["tn_material"].size(), 1);
    ASSERT_STR_EQ(j["tn_material"][0].get<std::string>(), "Trinkflasche");
}

TEST(attachment_to_json_fields)
{
    Attachment att{"a1", "act-1", "plan.pdf", "application/pdf", "2026-01-01"};
    auto j = attachment_to_json(att);
    ASSERT_STR_EQ(j["id"].get<std::string>(), "a1");
    ASSERT_STR_EQ(j["filename"].get<std::string>(), "plan.pdf");
}

TEST(attachment_to_json_content_type)
{
    Attachment att{"a2", "act-1", "image.png", "image/png", "2026-01-01"};
    auto j = attachment_to_json(att);
    ASSERT_STR_EQ(j["content_type"].get<std::string>(), "image/png");
}

TEST(ideenkiste_to_json_null_department)
{
    IdeenkisteItem item{"id-1", "Idee", 30, "Beschreibung", std::nullopt, "2026-01-01", "2026-01-02"};
    auto j = ideenkiste_to_json(item);
    ASSERT_TRUE(j["department"].is_null());
}

TEST(ideenkiste_to_json_with_department)
{
    IdeenkisteItem item{"id-2", "Idee", 15, "Kurz", std::optional<std::string>("Pfadi"), "2026-01-01", "2026-01-02"};
    auto j = ideenkiste_to_json(item);
    ASSERT_STR_EQ(j["department"].get<std::string>(), "Pfadi");
}

TEST(form_question_to_json_fields)
{
    FormQuestion q;
    q.id = "q1";
    q.form_id = "f1";
    q.question_text = "Wer?";
    q.question_type = "text_input";
    q.position = 1;
    q.is_required = true;
    q.metadata = nlohmann::json::object();
    q.created_at = "2026-01-01";

    auto j = form_question_to_json(q);
    ASSERT_STR_EQ(j["id"].get<std::string>(), "q1");
    ASSERT_STR_EQ(j["question_type"].get<std::string>(), "text_input");
}

TEST(signup_form_to_json_with_questions)
{
    SignupForm f;
    f.id = "f1";
    f.activity_id = "act-1";
    f.public_slug = "slug";
    f.form_type = "registration";
    f.title = "Anmeldung";
    f.created_by = "u1";
    f.created_at = "2026-01-01";
    f.updated_at = "2026-01-02";

    FormQuestion q;
    q.id = "q1";
    q.form_id = "f1";
    q.question_text = "Name";
    q.question_type = "text_input";
    q.position = 1;
    q.is_required = true;
    q.metadata = nlohmann::json::object();
    q.created_at = "2026-01-01";
    f.questions.push_back(q);

    auto j = signup_form_to_json(f);
    ASSERT_EQ((int)j["questions"].size(), 1);
    ASSERT_STR_EQ(j["questions"][0]["id"].get<std::string>(), "q1");
}

TEST(signup_form_to_json_without_questions)
{
    SignupForm f;
    f.id = "f2";
    f.activity_id = "act-1";
    f.public_slug = "slug2";
    f.form_type = "registration";
    f.title = "Leer";
    f.created_by = "u1";
    f.created_at = "2026-01-01";
    f.updated_at = "2026-01-02";

    auto j = signup_form_to_json(f);
    ASSERT_TRUE(j["questions"].is_array());
    ASSERT_EQ((int)j["questions"].size(), 0);
}

TEST(form_response_to_json_without_answers)
{
    FormResponse r;
    r.id = "r-1";
    r.form_id = "f-1";
    r.submission_mode = "registration";
    r.submitted_at = "2026-01-01";
    r.answers = {{"q1", "answer1"}};
    auto j = form_response_to_json(r, false);
    ASSERT_TRUE(!j.contains("answers"));
}

TEST(form_response_to_json_with_answers)
{
    FormResponse r;
    r.id = "r-1";
    r.form_id = "f-1";
    r.submission_mode = "registration";
    r.submitted_at = "2026-01-01";
    r.answers = {{"q1", "Ja"}, {"q2", "Nein"}};
    auto j = form_response_to_json(r, true);
    ASSERT_TRUE(j.contains("answers"));
    ASSERT_EQ((int)j["answers"].size(), 2);
}

TEST(form_response_to_json_with_empty_answers_array)
{
    FormResponse r;
    r.id = "r-2";
    r.form_id = "f-1";
    r.submission_mode = "registration";
    r.submitted_at = "2026-01-01";
    auto j = form_response_to_json(r, true);
    ASSERT_TRUE(j.contains("answers"));
    ASSERT_TRUE(j["answers"].is_array());
    ASSERT_EQ((int)j["answers"].size(), 0);
}

TEST(form_template_to_json_fields)
{
    FormTemplate t;
    t.id = "t1";
    t.name = "Standard";
    t.department = "Pfadi";
    t.form_type = "registration";
    t.template_config = nlohmann::json::array();
    t.is_default = true;
    t.created_by = "u1";
    t.created_at = "2026-01-01";
    t.updated_at = "2026-01-02";

    auto j = form_template_to_json(t);
    ASSERT_STR_EQ(j["id"].get<std::string>(), "t1");
    ASSERT_TRUE(j["is_default"].get<bool>());
}

TEST(form_template_to_json_template_config_passthrough)
{
    FormTemplate t;
    t.id = "t2";
    t.name = "Spezial";
    t.department = "Wolfe";
    t.form_type = "deregistration";
    t.template_config = nlohmann::json::array({{{"question_type", "text_input"}, {"question_text", "Warum?"}}});
    t.is_default = false;
    t.created_by = "u2";
    t.created_at = "2026-01-01";
    t.updated_at = "2026-01-02";

    auto j = form_template_to_json(t);
    ASSERT_TRUE(j["template_config"].is_array());
    ASSERT_EQ((int)j["template_config"].size(), 1);
    ASSERT_STR_EQ(j["template_config"][0]["question_type"].get<std::string>(), "text_input");
}
