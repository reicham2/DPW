#include "utils.hpp"
#include "test_harness.hpp"
#include "test_fixtures.hpp"

#include <string>
#include <vector>

namespace
{
    UserRecord make_user(const std::string &role = "Mitglied",
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

    RolePermission make_perm(const std::string &read = "none",
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
} // namespace

TEST(status_text_known_codes)
{
    ASSERT_STR_EQ(status_text(200), "200 OK");
    ASSERT_STR_EQ(status_text(201), "201 Created");
    ASSERT_STR_EQ(status_text(204), "204 No Content");
    ASSERT_STR_EQ(status_text(409), "409 Conflict");
    ASSERT_STR_EQ(status_text(413), "413 Payload Too Large");
    ASSERT_STR_EQ(status_text(429), "429 Too Many Requests");
    ASSERT_STR_EQ(status_text(500), "500 Internal Server Error");
}

TEST(status_text_unknown_defaults_200)
{
    ASSERT_STR_EQ(status_text(999), "200 OK");
}

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

TEST(url_decode_invalid_percent_keeps_literal)
{
    ASSERT_STR_EQ(url_decode("abc%zzdef"), "abc%zzdef");
}

TEST(url_decode_truncated_percent_keeps_literal)
{
    ASSERT_STR_EQ(url_decode("abc%"), "abc%");
    ASSERT_STR_EQ(url_decode("abc%2"), "abc%2");
}

TEST(to_lower_ascii_basic)
{
    ASSERT_STR_EQ(to_lower_ascii("AbC123"), "abc123");
}

TEST(to_lower_ascii_non_ascii_unchanged)
{
    ASSERT_STR_EQ(to_lower_ascii("ÄÖÜ"), "ÄÖÜ");
}

TEST(trim_ascii_whitespace)
{
    ASSERT_STR_EQ(trim_ascii(" \t  hello  \n"), "hello");
}

TEST(trim_ascii_only_spaces)
{
    ASSERT_STR_EQ(trim_ascii("   \t\n  "), "");
}

TEST(format_date_short_normal)
{
    ASSERT_STR_EQ(format_date_short_de("2026-04-15"), "15.04.2026");
}

TEST(format_date_short_too_short)
{
    ASSERT_STR_EQ(format_date_short_de("x"), "x");
}

TEST(format_date_long_wednesday)
{
    ASSERT_STR_EQ(format_date_long_de("2026-04-15"), "Mittwoch, 15. April 2026");
}

TEST(format_date_long_too_short)
{
    ASSERT_STR_EQ(format_date_long_de("x"), "x");
}

TEST(template_vars_no_placeholders)
{
    Activity a = make_activity();
    ASSERT_STR_EQ(replace_template_vars("Keine Variablen", a), "Keine Variablen");
}

TEST(template_vars_multiple)
{
    Activity a = make_activity();
    ASSERT_STR_EQ(replace_template_vars("{{titel}} am {{datum_kurz}} in {{ort}}", a), "Pfadilager am 15.07.2026 in Waldhaus");
}

TEST(template_vars_no_department)
{
    Activity a = make_activity();
    a.department = std::nullopt;
    ASSERT_STR_EQ(replace_template_vars("{{abteilung}}", a), "\xe2\x80\x94");
}

TEST(template_vars_tn_material)
{
    Activity a = make_activity();
    a.tn_material = {"Trinkflasche", "Regenschutz"};
    ASSERT_STR_EQ(replace_template_vars("{{tn_material}}", a), "Trinkflasche, Regenschutz");
}

TEST(template_vars_programm_placeholder)
{
    Activity a = make_activity();
    std::string out = replace_template_vars("{{programm}}", a);
    ASSERT_TRUE(out.find("60 min") != std::string::npos);
    ASSERT_TRUE(out.find("Einstieg") != std::string::npos);
}

TEST(is_admin_true)
{
    ASSERT_TRUE(is_admin(make_user("admin")));
}

TEST(is_admin_false)
{
    ASSERT_FALSE(is_admin(make_user("Mitglied")));
}

TEST(has_dept_access_read)
{
    std::vector<RoleDeptAccess> access = {{"Mitglied", "Pfadi", true, false}};
    ASSERT_TRUE(has_dept_access(access, "Pfadi", false));
    ASSERT_FALSE(has_dept_access(access, "Pfadi", true));
}

TEST(can_read_dept_denied)
{
    auto u = make_user("Mitglied", "Wölfe");
    auto p = make_perm("none");
    ASSERT_FALSE(can_read_dept(p, u, {}, "Pfadi"));
}

TEST(is_activity_responsible_email_local_part)
{
    auto u = make_user();
    u.display_name = "Nobody";
    Activity a = make_activity();
    a.responsible = {"anna"};
    ASSERT_TRUE(is_activity_responsible(a, u, "anna@example.com"));
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

TEST(can_edit_activity_own_responsible)
{
    auto u = make_user();
    u.display_name = "Anna";
    auto p = make_perm("none", "none", "own");
    Activity a = make_activity();
    a.responsible = {"Anna", "Ben"};
    ASSERT_TRUE(can_edit_activity(p, u, a, "other@example.com"));
}

TEST(can_create_dept_via_dept_access_write)
{
    auto u = make_user("Mitglied", "Wölfe");
    auto p = make_perm("none", "none");
    std::vector<RoleDeptAccess> access = {{"Mitglied", "Pfadi", true, true}};
    ASSERT_TRUE(can_create_dept(p, u, access, "Pfadi"));
}

TEST(can_publish_event_own_only_denied)
{
    auto u = make_user("Mitglied", "Pfadi");
    u.display_name = "Chris";
    RolePermission p{};
    p.event_publish_scope = "own";
    Activity a = make_activity();
    a.responsible = {"Anna"};
    ASSERT_FALSE(can_publish_event(p, u, a, "chris@example.com"));
}

TEST(str_field_wrong_type)
{
    nlohmann::json j = {{"name", 42}};
    ASSERT_STR_EQ(str_field(j, "name", "fallback"), "fallback");
}

TEST(parse_activity_input_minimal)
{
    nlohmann::json j = {{"title", "Test"}, {"date", "2026-06-01"}, {"start_time", "10:00"}, {"end_time", "12:00"}, {"goal", "Ziel"}, {"location", "Ort"}};
    auto input = parse_activity_input(j);
    ASSERT_STR_EQ(input.title, "Test");
    ASSERT_TRUE(input.programs.empty());
}

TEST(parse_activity_input_material_responsible_as_string)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"material", {{{"name", "Kocher"}, {"responsible", "Lea"}}}}};
    auto input = parse_activity_input(j);
    ASSERT_EQ((int)input.material.size(), 1);
    ASSERT_EQ((int)input.material[0].responsible.size(), 1);
}

TEST(parse_activity_input_program_responsible_as_string)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"programs", {{{"duration_minutes", 15}, {"title", "Start"}, {"responsible", "Nina"}}}}};
    auto input = parse_activity_input(j);
    ASSERT_EQ((int)input.programs.size(), 1);
    ASSERT_EQ((int)input.programs[0].responsible.size(), 1);
}

TEST(parse_activity_input_planned_participants)
{
    nlohmann::json j_int = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"planned_participants_estimate", 42}};
    auto input_int = parse_activity_input(j_int);
    ASSERT_TRUE(input_int.planned_participants_estimate.has_value());
    ASSERT_EQ(*input_int.planned_participants_estimate, 42);

    nlohmann::json j_float = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"planned_participants_estimate", 12.9}};
    auto input_float = parse_activity_input(j_float);
    ASSERT_TRUE(input_float.planned_participants_estimate.has_value());
    ASSERT_EQ(*input_float.planned_participants_estimate, 12);

    nlohmann::json j_neg = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"planned_participants_estimate", -1}};
    auto input_neg = parse_activity_input(j_neg);
    ASSERT_FALSE(input_neg.planned_participants_estimate.has_value());
}

TEST(parse_activity_input_tn_material_filters_empty)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"tn_material", {"Sitzunterlage", "", "Becher"}}};
    auto input = parse_activity_input(j);
    ASSERT_EQ((int)input.tn_material.size(), 2);
}

TEST(parse_activity_input_empty_optional_not_set)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"siko_text", ""}, {"bad_weather_info", ""}};
    auto input = parse_activity_input(j);
    ASSERT_FALSE(input.siko_text.has_value());
    ASSERT_FALSE(input.bad_weather_info.has_value());
}

TEST(status_text_additional_known_codes)
{
    ASSERT_STR_EQ(status_text(400), "400 Bad Request");
    ASSERT_STR_EQ(status_text(401), "401 Unauthorized");
    ASSERT_STR_EQ(status_text(403), "403 Forbidden");
    ASSERT_STR_EQ(status_text(404), "404 Not Found");
    ASSERT_STR_EQ(status_text(502), "502 Bad Gateway");
    ASSERT_STR_EQ(status_text(503), "503 Service Unavailable");
}

TEST(url_decode_uppercase_hex)
{
    ASSERT_STR_EQ(url_decode("A%2FB%3A"), "A/B:");
}

TEST(url_decode_mixed_sequences)
{
    ASSERT_STR_EQ(url_decode("a+b%20c%2Fz"), "a b c/z");
}

TEST(url_decode_no_conversions_needed)
{
    ASSERT_STR_EQ(url_decode("-_.~"), "-_.~");
}

TEST(to_lower_ascii_with_symbols)
{
    ASSERT_STR_EQ(to_lower_ascii("Admin-TEAM_42"), "admin-team_42");
}

TEST(trim_ascii_noop)
{
    ASSERT_STR_EQ(trim_ascii("hello"), "hello");
}

TEST(trim_ascii_with_carriage_return)
{
    ASSERT_STR_EQ(trim_ascii("\r\n hello \r\n"), "hello");
}

TEST(format_date_short_leap_day)
{
    ASSERT_STR_EQ(format_date_short_de("2024-02-29"), "29.02.2024");
}

TEST(format_date_long_leap_day)
{
    ASSERT_STR_EQ(format_date_long_de("2024-02-29"), "Donnerstag, 29. Februar 2024");
}

TEST(template_vars_unknown_placeholder_unchanged)
{
    Activity a = make_activity();
    ASSERT_STR_EQ(replace_template_vars("{{unknown}}", a), "{{unknown}}");
}

TEST(template_vars_repeated_placeholder)
{
    Activity a = make_activity();
    ASSERT_STR_EQ(replace_template_vars("{{titel}} / {{titel}}", a), "Pfadilager / Pfadilager");
}

TEST(template_vars_empty_program_uses_dash)
{
    Activity a = make_activity();
    a.programs.clear();
    ASSERT_STR_EQ(replace_template_vars("{{programm}}", a), "\xe2\x80\x94");
}

TEST(template_vars_empty_material_uses_dash)
{
    Activity a = make_activity();
    a.material.clear();
    ASSERT_STR_EQ(replace_template_vars("{{material}}", a), "\xe2\x80\x94");
}

TEST(template_vars_program_without_duration_and_responsible)
{
    Activity a = make_activity();
    a.programs = {{"p2", "act-1", 0, "Freispiel", "", {}}};
    std::string out = replace_template_vars("{{programm}}", a);
    ASSERT_TRUE(out.find("\xe2\x80\x94 \xe2\x80\x93 Freispiel") != std::string::npos);
    ASSERT_TRUE(out.find("(") == std::string::npos);
}

TEST(has_dept_access_no_match)
{
    std::vector<RoleDeptAccess> access = {{"Mitglied", "Pfadi", true, false}};
    ASSERT_FALSE(has_dept_access(access, "Wolfe", false));
}

TEST(can_create_dept_admin_always_true)
{
    auto u = make_user("admin", "Wolfe");
    auto p = make_perm("none", "none", "none");
    ASSERT_TRUE(can_create_dept(p, u, {}, "Pfadi"));
}

TEST(can_create_dept_own_dept_scope_true)
{
    auto u = make_user("Mitglied", "Pfadi");
    auto p = make_perm("none", "own_dept", "none");
    ASSERT_TRUE(can_create_dept(p, u, {}, "Pfadi"));
}

TEST(can_create_dept_own_dept_scope_false_without_user_dept)
{
    auto u = make_user("Mitglied", "Pfadi");
    u.department = std::nullopt;
    auto p = make_perm("none", "own_dept", "none");
    ASSERT_FALSE(can_create_dept(p, u, {}, "Pfadi"));
}

TEST(can_read_dept_all_scope)
{
    auto u = make_user("Mitglied", "Wolfe");
    auto p = make_perm("all", "none", "none");
    ASSERT_TRUE(can_read_dept(p, u, {}, "Pfadi"));
}

TEST(can_read_dept_same_dept_scope)
{
    auto u = make_user("Mitglied", "Pfadi");
    auto p = make_perm("same_dept", "none", "none");
    ASSERT_TRUE(can_read_dept(p, u, {}, "Pfadi"));
    ASSERT_FALSE(can_read_dept(p, u, {}, "Wolfe"));
}

TEST(is_activity_responsible_display_name_case_insensitive)
{
    auto u = make_user();
    u.display_name = "Anna Muster";
    Activity a = make_activity();
    a.responsible = {"  aNNa MUsTer  "};
    ASSERT_TRUE(is_activity_responsible(a, u, "other@example.com"));
}

TEST(is_activity_responsible_email_match_case_insensitive)
{
    auto u = make_user();
    u.display_name = "Nobody";
    Activity a = make_activity();
    a.responsible = {"anna@example.com"};
    ASSERT_TRUE(is_activity_responsible(a, u, "ANNA@EXAMPLE.COM"));
}

TEST(is_activity_responsible_false_when_empty_responsibles)
{
    auto u = make_user();
    Activity a = make_activity();
    a.responsible = {"", "   "};
    ASSERT_FALSE(is_activity_responsible(a, u, "test@example.com"));
}

TEST(can_read_activity_without_department_scope_dependent)
{
    auto u = make_user("Mitglied", "Pfadi");
    Activity a = make_activity();
    a.department = std::nullopt;

    auto p_none = make_perm("none", "none", "none");
    ASSERT_FALSE(can_read_activity(p_none, u, {}, a));

    auto p_same = make_perm("same_dept", "none", "none");
    ASSERT_TRUE(can_read_activity(p_same, u, {}, a));
}

TEST(can_edit_activity_same_dept_scope_by_department_match)
{
    auto u = make_user("Mitglied", "Pfadi");
    u.display_name = "Nicht Verantwortlich";
    auto p = make_perm("none", "none", "same_dept");
    Activity a = make_activity();
    a.department = "Pfadi";
    a.responsible = {"Jemand Anders"};
    ASSERT_TRUE(can_edit_activity(p, u, a, "user@example.com"));
}

TEST(can_edit_activity_own_scope_false_when_not_responsible)
{
    auto u = make_user("Mitglied", "Pfadi");
    u.display_name = "Chris";
    auto p = make_perm("none", "none", "own");
    Activity a = make_activity();
    a.responsible = {"Anna"};
    ASSERT_FALSE(can_edit_activity(p, u, a, "chris@example.com"));
}

TEST(can_publish_event_all_scope)
{
    auto u = make_user("Mitglied", "Wolfe");
    RolePermission p{};
    p.event_publish_scope = "all";
    Activity a = make_activity();
    a.department = "Pfadi";
    ASSERT_TRUE(can_publish_event(p, u, a, "x@example.com"));
}

TEST(can_publish_event_own_dept_via_responsible)
{
    auto u = make_user("Mitglied", "Wolfe");
    u.display_name = "Anna";
    RolePermission p{};
    p.event_publish_scope = "own_dept";
    Activity a = make_activity();
    a.department = "Pfadi";
    a.responsible = {"Anna"};
    ASSERT_TRUE(can_publish_event(p, u, a, "anna@example.com"));
}

TEST(str_field_missing_returns_default)
{
    nlohmann::json j = {{"other", "value"}};
    ASSERT_STR_EQ(str_field(j, "name", "fallback"), "fallback");
}

TEST(str_field_string_present)
{
    nlohmann::json j = {{"name", "Alice"}};
    ASSERT_STR_EQ(str_field(j, "name", "fallback"), "Alice");
}

TEST(parse_activity_input_filters_non_string_responsible)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"responsible", {"Anna", 7, true}}};
    auto input = parse_activity_input(j);
    ASSERT_EQ((int)input.responsible.size(), 1);
    ASSERT_STR_EQ(input.responsible[0], "Anna");
}

TEST(parse_activity_input_department_non_string_ignored)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"department", 99}};
    auto input = parse_activity_input(j);
    ASSERT_FALSE(input.department.has_value());
}

TEST(parse_activity_input_material_mixed_filters_empty_names)
{
    nlohmann::json j = {{"title", ""},
                        {"date", ""},
                        {"start_time", ""},
                        {"end_time", ""},
                        {"goal", ""},
                        {"location", ""},
                        {"material", {"Seil", "", {{"name", "Kocher"}, {"responsible", nlohmann::json::array({"Lea", 3})}}, {{"name", ""}}}}};
    auto input = parse_activity_input(j);
    ASSERT_EQ((int)input.material.size(), 2);
    ASSERT_STR_EQ(input.material[0].name, "Seil");
    ASSERT_STR_EQ(input.material[1].name, "Kocher");
    ASSERT_EQ((int)input.material[1].responsible.size(), 1);
}

TEST(parse_activity_input_tn_material_filters_non_strings)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"tn_material", {"Becher", 12, false, "Halstuch"}}};
    auto input = parse_activity_input(j);
    ASSERT_EQ((int)input.tn_material.size(), 2);
}

TEST(parse_activity_input_program_defaults_when_fields_missing)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"programs", {nlohmann::json::object()}}};
    auto input = parse_activity_input(j);
    ASSERT_EQ((int)input.programs.size(), 1);
    ASSERT_EQ(input.programs[0].duration_minutes, 0);
    ASSERT_STR_EQ(input.programs[0].title, "");
}

TEST(parse_activity_input_program_responsible_array_filters_non_strings)
{
    nlohmann::json j = {{"title", ""},
                        {"date", ""},
                        {"start_time", ""},
                        {"end_time", ""},
                        {"goal", ""},
                        {"location", ""},
                        {"programs", {{{"duration_minutes", 10}, {"title", "Start"}, {"responsible", nlohmann::json::array({"Nina", 5, nullptr})}}}}};
    auto input = parse_activity_input(j);
    ASSERT_EQ((int)input.programs.size(), 1);
    ASSERT_EQ((int)input.programs[0].responsible.size(), 1);
    ASSERT_STR_EQ(input.programs[0].responsible[0], "Nina");
}

TEST(parse_activity_input_planned_participants_zero_allowed)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"planned_participants_estimate", 0}};
    auto input = parse_activity_input(j);
    ASSERT_TRUE(input.planned_participants_estimate.has_value());
    ASSERT_EQ(*input.planned_participants_estimate, 0);
}

TEST(parse_activity_input_planned_participants_string_ignored)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"planned_participants_estimate", "12"}};
    auto input = parse_activity_input(j);
    ASSERT_FALSE(input.planned_participants_estimate.has_value());
}

TEST(parse_activity_input_non_empty_optional_set)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"siko_text", "Bitte Helm"}, {"bad_weather_info", "Turnhalle"}};
    auto input = parse_activity_input(j);
    ASSERT_TRUE(input.siko_text.has_value());
    ASSERT_TRUE(input.bad_weather_info.has_value());
    ASSERT_STR_EQ(*input.siko_text, "Bitte Helm");
    ASSERT_STR_EQ(*input.bad_weather_info, "Turnhalle");
}

TEST(can_create_dept_all_scope_true)
{
    auto u = make_user("Mitglied", "Wolfe");
    auto p = make_perm("none", "all", "none");
    ASSERT_TRUE(can_create_dept(p, u, {}, "Pfadi"));
}

TEST(can_read_dept_admin_always_true)
{
    auto u = make_user("admin", "Wolfe");
    auto p = make_perm("none", "none", "none");
    ASSERT_TRUE(can_read_dept(p, u, {}, "Pfadi"));
}

TEST(can_read_activity_admin_always_true)
{
    auto u = make_user("admin", "Wolfe");
    auto p = make_perm("none", "none", "none");
    Activity a = make_activity();
    a.department = "Pfadi";
    ASSERT_TRUE(can_read_activity(p, u, {}, a));
}

TEST(can_read_activity_all_scope_true)
{
    auto u = make_user("Mitglied", "Wolfe");
    auto p = make_perm("all", "none", "none");
    Activity a = make_activity();
    a.department = "Pfadi";
    ASSERT_TRUE(can_read_activity(p, u, {}, a));
}

TEST(can_read_activity_same_dept_true)
{
    auto u = make_user("Mitglied", "Pfadi");
    auto p = make_perm("same_dept", "none", "none");
    Activity a = make_activity();
    a.department = "Pfadi";
    ASSERT_TRUE(can_read_activity(p, u, {}, a));
}

TEST(can_read_activity_write_only_dept_access_is_not_enough)
{
    auto u = make_user("Mitglied", "Wolfe");
    auto p = make_perm("none", "none", "none");
    Activity a = make_activity();
    a.department = "Pfadi";
    std::vector<RoleDeptAccess> access = {{"Mitglied", "Pfadi", false, true}};
    ASSERT_FALSE(can_read_activity(p, u, access, a));
}

TEST(can_edit_activity_admin_always_true)
{
    auto u = make_user("admin", "Wolfe");
    auto p = make_perm("none", "none", "none");
    Activity a = make_activity();
    ASSERT_TRUE(can_edit_activity(p, u, a, "x@example.com"));
}

TEST(can_edit_activity_all_scope_true)
{
    auto u = make_user("Mitglied", "Wolfe");
    auto p = make_perm("none", "none", "all");
    Activity a = make_activity();
    ASSERT_TRUE(can_edit_activity(p, u, a, "x@example.com"));
}

TEST(can_edit_activity_same_dept_scope_via_responsible)
{
    auto u = make_user("Mitglied", "Wolfe");
    u.display_name = "Anna";
    auto p = make_perm("none", "none", "same_dept");
    Activity a = make_activity();
    a.department = "Pfadi";
    a.responsible = {"Anna"};
    ASSERT_TRUE(can_edit_activity(p, u, a, "anna@example.com"));
}

TEST(can_publish_event_admin_always_true)
{
    auto u = make_user("admin", "Wolfe");
    RolePermission p{};
    p.event_publish_scope = "none";
    Activity a = make_activity();
    ASSERT_TRUE(can_publish_event(p, u, a, "x@example.com"));
}

TEST(can_publish_event_own_scope_true_when_responsible)
{
    auto u = make_user("Mitglied", "Pfadi");
    u.display_name = "Anna";
    RolePermission p{};
    p.event_publish_scope = "own";
    Activity a = make_activity();
    a.responsible = {"Anna"};
    ASSERT_TRUE(can_publish_event(p, u, a, "anna@example.com"));
}

TEST(can_publish_event_own_dept_scope_true_by_department_match)
{
    auto u = make_user("Mitglied", "Pfadi");
    u.display_name = "Nicht Verantwortlich";
    RolePermission p{};
    p.event_publish_scope = "own_dept";
    Activity a = make_activity();
    a.department = "Pfadi";
    a.responsible = {"Jemand"};
    ASSERT_TRUE(can_publish_event(p, u, a, "x@example.com"));
}

TEST(can_publish_event_none_scope_denied)
{
    auto u = make_user("Mitglied", "Pfadi");
    RolePermission p{};
    p.event_publish_scope = "none";
    Activity a = make_activity();
    ASSERT_FALSE(can_publish_event(p, u, a, "x@example.com"));
}

TEST(parse_activity_input_responsible_non_array_ignored)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"responsible", "Anna"}};
    auto input = parse_activity_input(j);
    ASSERT_TRUE(input.responsible.empty());
}

TEST(parse_activity_input_material_non_array_ignored)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"material", "Seil"}};
    auto input = parse_activity_input(j);
    ASSERT_TRUE(input.material.empty());
}

TEST(parse_activity_input_programs_non_array_ignored)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"programs", nlohmann::json::object()}};
    auto input = parse_activity_input(j);
    ASSERT_TRUE(input.programs.empty());
}

TEST(parse_activity_input_bad_weather_non_string_ignored)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"bad_weather_info", 42}};
    auto input = parse_activity_input(j);
    ASSERT_FALSE(input.bad_weather_info.has_value());
}

TEST(parse_activity_input_siko_non_string_ignored)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"siko_text", false}};
    auto input = parse_activity_input(j);
    ASSERT_FALSE(input.siko_text.has_value());
}

TEST(parse_activity_input_department_empty_string_kept)
{
    nlohmann::json j = {{"title", ""}, {"date", ""}, {"start_time", ""}, {"end_time", ""}, {"goal", ""}, {"location", ""}, {"department", ""}};
    auto input = parse_activity_input(j);
    ASSERT_TRUE(input.department.has_value());
    ASSERT_STR_EQ(*input.department, "");
}
