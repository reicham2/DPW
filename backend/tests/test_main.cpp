// Minimal test framework — no external dependencies needed.
// Each TEST() registers a function; main() runs them all and reports results.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>

#include "utils.hpp"
#include "auth.hpp"
#include "models.hpp"

// ── Minimal test harness ────────────────────────────────────────────────────

struct TestCase
{
    const char *name;
    std::function<void()> fn;
};
static std::vector<TestCase> &tests()
{
    static std::vector<TestCase> t;
    return t;
}

#define TEST(name)                                                         \
    static void test_##name();                                             \
    static int _reg_##name = (tests().push_back({#name, test_##name}), 0); \
    static void test_##name()

#define ASSERT_EQ(a, b)                                                                                                                                                   \
    do                                                                                                                                                                    \
    {                                                                                                                                                                     \
        auto _a = (a);                                                                                                                                                    \
        auto _b = (b);                                                                                                                                                    \
        if (_a != _b)                                                                                                                                                     \
        {                                                                                                                                                                 \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": ASSERT_EQ failed: " + std::to_string(_a) + " != " + std::to_string(_b)); \
        }                                                                                                                                                                 \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                                                                                                                       \
    do                                                                                                                                                                            \
    {                                                                                                                                                                             \
        std::string _a = (a);                                                                                                                                                     \
        std::string _b = (b);                                                                                                                                                     \
        if (_a != _b)                                                                                                                                                             \
        {                                                                                                                                                                         \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": ASSERT_STR_EQ failed:\n  got:      \"" + _a + "\"\n  expected: \"" + _b + "\""); \
        }                                                                                                                                                                         \
    } while (0)

#define ASSERT_TRUE(expr)                                                                                                      \
    do                                                                                                                         \
    {                                                                                                                          \
        if (!(expr))                                                                                                           \
        {                                                                                                                      \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": ASSERT_TRUE failed: " #expr); \
        }                                                                                                                      \
    } while (0)

#define ASSERT_FALSE(expr)                                                                                                      \
    do                                                                                                                          \
    {                                                                                                                           \
        if ((expr))                                                                                                             \
        {                                                                                                                       \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": ASSERT_FALSE failed: " #expr); \
        }                                                                                                                       \
    } while (0)

// ── Helpers ─────────────────────────────────────────────────────────────────

static Activity make_activity()
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
    a.programs = {{"prog-1", "act-1", 60, "Einstieg", "Begrüssung", {"Anna"}}};
    return a;
}

static UserRecord make_user(const std::string &role = "Mitglied",
                            const std::string &dept = "Pfadi")
{
    UserRecord u;
    u.id = "u-1";
    u.microsoft_oid = "oid-1";
    u.email = "test@example.com";
    u.display_name = "Test User";
    u.department = dept;
    u.role = role;
    return u;
}

static RolePermission make_perm(const std::string &read = "none",
                                const std::string &create = "none",
                                const std::string &edit = "none")
{
    RolePermission p{};
    p.role = "Mitglied";
    p.activity_read_scope = read;
    p.activity_create_scope = create;
    p.activity_edit_scope = edit;
    return p;
}

// ═══════════════════════════════════════════════════════════════════════════
// Tests
// ═══════════════════════════════════════════════════════════════════════════

// ── status_text ─────────────────────────────────────────────────────────────

TEST(status_text_known_codes)
{
    ASSERT_STR_EQ(status_text(200), "200 OK");
    ASSERT_STR_EQ(status_text(201), "201 Created");
    ASSERT_STR_EQ(status_text(400), "400 Bad Request");
    ASSERT_STR_EQ(status_text(401), "401 Unauthorized");
    ASSERT_STR_EQ(status_text(403), "403 Forbidden");
    ASSERT_STR_EQ(status_text(404), "404 Not Found");
    ASSERT_STR_EQ(status_text(500), "500 Internal Server Error");
}

TEST(status_text_unknown_defaults_200)
{
    ASSERT_STR_EQ(status_text(999), "200 OK");
}

// ── url_decode ──────────────────────────────────────────────────────────────

TEST(url_decode_plain)
{
    ASSERT_STR_EQ(url_decode("hello"), "hello");
}

TEST(url_decode_percent)
{
    ASSERT_STR_EQ(url_decode("W%C3%B6lfe"), "Wölfe");
}

TEST(url_decode_plus_to_space)
{
    ASSERT_STR_EQ(url_decode("hello+world"), "hello world");
}

TEST(url_decode_empty)
{
    ASSERT_STR_EQ(url_decode(""), "");
}

TEST(url_decode_mixed)
{
    ASSERT_STR_EQ(url_decode("a%20b+c"), "a b c");
}

// ── extract_bearer_token ────────────────────────────────────────────────────

TEST(extract_bearer_valid)
{
    ASSERT_STR_EQ(extract_bearer_token("Bearer abc123"), "abc123");
}

TEST(extract_bearer_empty)
{
    ASSERT_STR_EQ(extract_bearer_token(""), "");
}

TEST(extract_bearer_wrong_prefix)
{
    ASSERT_STR_EQ(extract_bearer_token("Basic abc123"), "");
}

TEST(extract_bearer_only_prefix)
{
    ASSERT_STR_EQ(extract_bearer_token("Bearer"), "");
}

TEST(extract_bearer_with_spaces_in_token)
{
    // Should return everything after "Bearer "
    ASSERT_STR_EQ(extract_bearer_token("Bearer token with spaces"), "token with spaces");
}

// ── format_date_short_de ────────────────────────────────────────────────────

TEST(format_date_short_normal)
{
    ASSERT_STR_EQ(format_date_short_de("2026-04-15"), "15.04.2026");
}

TEST(format_date_short_january)
{
    ASSERT_STR_EQ(format_date_short_de("2026-01-03"), "03.01.2026");
}

TEST(format_date_short_too_short)
{
    ASSERT_STR_EQ(format_date_short_de("short"), "short");
}

// ── format_date_long_de ─────────────────────────────────────────────────────

TEST(format_date_long_wednesday)
{
    // 2026-04-15 is a Wednesday
    ASSERT_STR_EQ(format_date_long_de("2026-04-15"), "Mittwoch, 15. April 2026");
}

TEST(format_date_long_sunday)
{
    // 2026-01-04 is a Sunday
    ASSERT_STR_EQ(format_date_long_de("2026-01-04"), "Sonntag, 4. Januar 2026");
}

TEST(format_date_long_too_short)
{
    ASSERT_STR_EQ(format_date_long_de("x"), "x");
}

// ── replace_template_vars ───────────────────────────────────────────────────

TEST(template_vars_no_placeholders)
{
    Activity a = make_activity();
    ASSERT_STR_EQ(replace_template_vars("Keine Variablen", a), "Keine Variablen");
}

TEST(template_vars_titel)
{
    Activity a = make_activity();
    ASSERT_STR_EQ(replace_template_vars("Betreff: {{titel}}", a), "Betreff: Pfadilager");
}

TEST(template_vars_datum_kurz)
{
    Activity a = make_activity();
    ASSERT_STR_EQ(replace_template_vars("Am {{datum_kurz}}", a), "Am 15.07.2026");
}

TEST(template_vars_multiple)
{
    Activity a = make_activity();
    std::string tpl = "{{titel}} am {{datum_kurz}} in {{ort}}";
    ASSERT_STR_EQ(replace_template_vars(tpl, a), "Pfadilager am 15.07.2026 in Waldhaus");
}

TEST(template_vars_verantwortlich)
{
    Activity a = make_activity();
    ASSERT_STR_EQ(replace_template_vars("{{verantwortlich}}", a), "Anna, Ben");
}

TEST(template_vars_material)
{
    Activity a = make_activity();
    ASSERT_STR_EQ(replace_template_vars("{{material}}", a), "Seil, Karte");
}

TEST(template_vars_no_department)
{
    Activity a = make_activity();
    a.department = std::nullopt;
    std::string result = replace_template_vars("{{abteilung}}", a);
    // Should show em-dash for missing department
    ASSERT_STR_EQ(result, "\xe2\x80\x94");
}

TEST(template_vars_schlechtwetter)
{
    Activity a = make_activity();
    ASSERT_STR_EQ(replace_template_vars("{{schlechtwetter}}", a), "Drinnen");
}

TEST(template_vars_repeated)
{
    Activity a = make_activity();
    ASSERT_STR_EQ(replace_template_vars("{{titel}} {{titel}}", a), "Pfadilager Pfadilager");
}

// ── is_admin ────────────────────────────────────────────────────────────────

TEST(is_admin_true)
{
    auto u = make_user("admin");
    ASSERT_TRUE(is_admin(u));
}

TEST(is_admin_false)
{
    auto u = make_user("Mitglied");
    ASSERT_FALSE(is_admin(u));
}

// ── has_dept_access ─────────────────────────────────────────────────────────

TEST(has_dept_access_read)
{
    std::vector<RoleDeptAccess> access = {{"Mitglied", "Pfadi", true, false}};
    ASSERT_TRUE(has_dept_access(access, "Pfadi", false));
    ASSERT_FALSE(has_dept_access(access, "Pfadi", true));
}

TEST(has_dept_access_write)
{
    std::vector<RoleDeptAccess> access = {{"Mitglied", "Pfadi", true, true}};
    ASSERT_TRUE(has_dept_access(access, "Pfadi", true));
}

TEST(has_dept_access_wrong_dept)
{
    std::vector<RoleDeptAccess> access = {{"Mitglied", "Pfadi", true, true}};
    ASSERT_FALSE(has_dept_access(access, "Wölfe", false));
}

TEST(has_dept_access_empty)
{
    std::vector<RoleDeptAccess> access;
    ASSERT_FALSE(has_dept_access(access, "Pfadi", false));
}

// ── can_read_activity ───────────────────────────────────────────────────────

TEST(can_read_activity_admin)
{
    auto u = make_user("admin");
    auto p = make_perm("none");
    Activity a = make_activity();
    ASSERT_TRUE(can_read_activity(p, u, {}, a));
}

TEST(can_read_activity_scope_all)
{
    auto u = make_user();
    auto p = make_perm("all");
    Activity a = make_activity();
    ASSERT_TRUE(can_read_activity(p, u, {}, a));
}

TEST(can_read_activity_same_dept_match)
{
    auto u = make_user("Mitglied", "Pfadi");
    auto p = make_perm("same_dept");
    Activity a = make_activity();
    a.department = "Pfadi";
    ASSERT_TRUE(can_read_activity(p, u, {}, a));
}

TEST(can_read_activity_same_dept_mismatch)
{
    auto u = make_user("Mitglied", "Wölfe");
    auto p = make_perm("same_dept");
    Activity a = make_activity();
    a.department = "Pfadi";
    ASSERT_FALSE(can_read_activity(p, u, {}, a));
}

TEST(can_read_activity_no_department)
{
    auto u = make_user();
    auto p = make_perm("same_dept");
    Activity a = make_activity();
    a.department = std::nullopt;
    // Activities without department are readable unless scope is "none"
    ASSERT_TRUE(can_read_activity(p, u, {}, a));
}

TEST(can_read_activity_via_dept_access)
{
    auto u = make_user("Mitglied", "Wölfe");
    auto p = make_perm("same_dept");
    Activity a = make_activity();
    a.department = "Pfadi";
    std::vector<RoleDeptAccess> access = {{"Mitglied", "Pfadi", true, false}};
    ASSERT_TRUE(can_read_activity(p, u, access, a));
}

// ── can_edit_activity ───────────────────────────────────────────────────────

TEST(can_edit_activity_admin)
{
    auto u = make_user("admin");
    auto p = make_perm("none", "none", "none");
    Activity a = make_activity();
    ASSERT_TRUE(can_edit_activity(p, u, a, ""));
}

TEST(can_edit_activity_scope_all)
{
    auto u = make_user();
    auto p = make_perm("none", "none", "all");
    Activity a = make_activity();
    ASSERT_TRUE(can_edit_activity(p, u, a, ""));
}

TEST(can_edit_activity_same_dept)
{
    auto u = make_user("Mitglied", "Pfadi");
    auto p = make_perm("none", "none", "same_dept");
    Activity a = make_activity();
    a.department = "Pfadi";
    ASSERT_TRUE(can_edit_activity(p, u, a, ""));
}

TEST(can_edit_activity_same_dept_mismatch)
{
    auto u = make_user("Mitglied", "Wölfe");
    auto p = make_perm("none", "none", "same_dept");
    Activity a = make_activity();
    a.department = "Pfadi";
    ASSERT_FALSE(can_edit_activity(p, u, a, ""));
}

TEST(can_edit_activity_own_responsible)
{
    auto u = make_user();
    u.display_name = "Anna";
    auto p = make_perm("none", "none", "own");
    Activity a = make_activity();
    a.responsible = {"Anna", "Ben"};
    ASSERT_TRUE(can_edit_activity(p, u, a, "other@example.com"));
}

TEST(can_edit_activity_own_by_email)
{
    auto u = make_user();
    u.display_name = "Nobody";
    auto p = make_perm("none", "none", "own");
    Activity a = make_activity();
    a.responsible = {"anna@test.com"};
    ASSERT_TRUE(can_edit_activity(p, u, a, "anna@test.com"));
}

TEST(can_edit_activity_own_not_responsible)
{
    auto u = make_user();
    u.display_name = "Nobody";
    auto p = make_perm("none", "none", "own");
    Activity a = make_activity();
    a.responsible = {"Anna"};
    ASSERT_FALSE(can_edit_activity(p, u, a, "other@test.com"));
}

// ── can_create_dept ─────────────────────────────────────────────────────────

TEST(can_create_dept_admin)
{
    auto u = make_user("admin");
    auto p = make_perm();
    ASSERT_TRUE(can_create_dept(p, u, {}, "Pfadi"));
}

TEST(can_create_dept_scope_all)
{
    auto u = make_user();
    auto p = make_perm("none", "all");
    ASSERT_TRUE(can_create_dept(p, u, {}, "Wölfe"));
}

TEST(can_create_dept_own_dept_match)
{
    auto u = make_user("Mitglied", "Pfadi");
    auto p = make_perm("none", "own_dept");
    ASSERT_TRUE(can_create_dept(p, u, {}, "Pfadi"));
}

TEST(can_create_dept_own_dept_mismatch)
{
    auto u = make_user("Mitglied", "Pfadi");
    auto p = make_perm("none", "own_dept");
    ASSERT_FALSE(can_create_dept(p, u, {}, "Wölfe"));
}

// ── str_field ───────────────────────────────────────────────────────────────

TEST(str_field_present)
{
    nlohmann::json j = {{"name", "Test"}};
    ASSERT_STR_EQ(str_field(j, "name"), "Test");
}

TEST(str_field_missing)
{
    nlohmann::json j = {{"other", 42}};
    ASSERT_STR_EQ(str_field(j, "name", "default"), "default");
}

TEST(str_field_wrong_type)
{
    nlohmann::json j = {{"name", 42}};
    ASSERT_STR_EQ(str_field(j, "name", "fallback"), "fallback");
}

// ── parse_activity_input ────────────────────────────────────────────────────

TEST(parse_activity_input_minimal)
{
    nlohmann::json j = {
        {"title", "Test"},
        {"date", "2026-06-01"},
        {"start_time", "10:00"},
        {"end_time", "12:00"},
        {"goal", "Ziel"},
        {"location", "Ort"}};
    auto input = parse_activity_input(j);
    ASSERT_STR_EQ(input.title, "Test");
    ASSERT_STR_EQ(input.date, "2026-06-01");
    ASSERT_STR_EQ(input.location, "Ort");
    ASSERT_TRUE(input.programs.empty());
    ASSERT_TRUE(input.material.empty());
}

TEST(parse_activity_input_with_programs)
{
    nlohmann::json j = {
        {"title", "Test"}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"programs", {{{"duration_minutes", 30}, {"title", "Block 1"}, {"description", "Intro"}, {"responsible", {"Anna"}}}}}};
    auto input = parse_activity_input(j);
    ASSERT_EQ((int)input.programs.size(), 1);
    ASSERT_STR_EQ(input.programs[0].title, "Block 1");
    ASSERT_EQ(input.programs[0].duration_minutes, 30);
    ASSERT_EQ((int)input.programs[0].responsible.size(), 1);
}

TEST(parse_activity_input_with_material_objects)
{
    nlohmann::json j = {
        {"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"material", {{{"name", "Seil"}, {"responsible", {"Ben"}}}}}};
    auto input = parse_activity_input(j);
    ASSERT_EQ((int)input.material.size(), 1);
    ASSERT_STR_EQ(input.material[0].name, "Seil");
    ASSERT_EQ((int)input.material[0].responsible.size(), 1);
}

TEST(parse_activity_input_with_material_strings)
{
    nlohmann::json j = {
        {"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"material", {"Seil", "Karte"}}};
    auto input = parse_activity_input(j);
    ASSERT_EQ((int)input.material.size(), 2);
    ASSERT_STR_EQ(input.material[0].name, "Seil");
}

TEST(parse_activity_input_optional_fields)
{
    nlohmann::json j = {
        {"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"department", "Pfadi"}, {"siko_text", "Sicherheit ok"}, {"bad_weather_info", "Drinnen"}};
    auto input = parse_activity_input(j);
    ASSERT_TRUE(input.department.has_value());
    ASSERT_STR_EQ(*input.department, "Pfadi");
    ASSERT_TRUE(input.siko_text.has_value());
    ASSERT_STR_EQ(*input.siko_text, "Sicherheit ok");
    ASSERT_TRUE(input.bad_weather_info.has_value());
    ASSERT_STR_EQ(*input.bad_weather_info, "Drinnen");
}

TEST(parse_activity_input_empty_optional_not_set)
{
    nlohmann::json j = {
        {"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"siko_text", ""}, {"bad_weather_info", ""}};
    auto input = parse_activity_input(j);
    // Empty strings should NOT set the optional
    ASSERT_FALSE(input.siko_text.has_value());
    ASSERT_FALSE(input.bad_weather_info.has_value());
}

// ── Model JSON serialisation ────────────────────────────────────────────────

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

TEST(program_to_json_fields)
{
    Program p{"prog-1", "act-1", 45, "Block", "Beschreibung", {"Anna"}};
    auto j = program_to_json(p);
    ASSERT_STR_EQ(j["id"].get<std::string>(), "prog-1");
    ASSERT_EQ(j["duration_minutes"].get<int>(), 45);
    ASSERT_EQ((int)j["responsible"].size(), 1);
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

// ═══════════════════════════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════════════════════════

int main()
{
    int passed = 0, failed = 0;
    for (auto &tc : tests())
    {
        try
        {
            tc.fn();
            printf("  \033[32m✓\033[0m %s\n", tc.name);
            ++passed;
        }
        catch (std::exception &e)
        {
            printf("  \033[31m✗\033[0m %s\n    %s\n", tc.name, e.what());
            ++failed;
        }
    }
    printf("\n%d passed, %d failed (%d total)\n", passed, failed, passed + failed);
    return failed > 0 ? 1 : 0;
}
