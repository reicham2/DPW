#include "infra/audit_actions.hpp"
#include "test_harness.hpp"

TEST(audit_outcomes_do_not_claim_success_for_failures)
{
    ASSERT_STR_EQ(audit_outcome(201), "completed");
    ASSERT_STR_EQ(audit_outcome(204), "completed");
    ASSERT_STR_EQ(audit_outcome(400), "rejected");
    ASSERT_STR_EQ(audit_outcome(403), "rejected");
    ASSERT_STR_EQ(audit_outcome(500), "unknown");
    ASSERT_STR_EQ(audit_outcome(503), "unknown");
    ASSERT_STR_EQ(audit_outcome(0), "unknown");
    ASSERT_STR_EQ(audit_outcome(200, true), "unknown");
}

TEST(audit_excludes_routine_and_public_sensitive_interactions)
{
    ASSERT_TRUE(!audit_action("POST", "/push/subscriptions"));
    ASSERT_TRUE(!audit_action("POST", "/push/payload"));
    ASSERT_TRUE(!audit_action("PATCH", "/notifications/:id/read"));
    ASSERT_TRUE(!audit_action("POST", "/forms/:slug/submit"));
}

TEST(audit_covers_bulk_indirect_and_sensitive_setting_actions_without_values)
{
    ASSERT_TRUE(audit_action("DELETE", "/admin/roles/:name").has_value());
    ASSERT_TRUE(audit_action("PATCH", "/activities/:id").has_value());
    ASSERT_TRUE(audit_action("POST", "/send-mail").has_value());
    auto setting = audit_action("PUT", "/admin/app-settings/:key");
    ASSERT_TRUE(setting.has_value());
    ASSERT_STR_EQ(setting->resource_type, "system");
    ASSERT_TRUE(setting->label.find(":key") == std::string::npos);
    ASSERT_TRUE(audit_action("POST", "/future-mutation").has_value());
}

TEST(audit_details_show_actual_user_changes_without_credentials)
{
    nlohmann::json before = {{"display_name", "Leiter"}, {"role", "Mitglied"}, {"department", "Pfadi"},
                             {"email", "private@example.invalid"}, {"access_token", "private-token"}};
    auto after = before;
    after["role"] = "admin";
    after["department"] = nullptr;
    auto details = audit_change_details("user", before, after);
    ASSERT_STR_EQ(details["kind"].get<std::string>(), "changed");
    ASSERT_STR_EQ(details["resource_name"].get<std::string>(), "Leiter");
    ASSERT_EQ(details["changes"].size(), static_cast<size_t>(2));
    ASSERT_TRUE(details.dump().find("private") == std::string::npos);
    ASSERT_STR_EQ(details["changes"][1]["before"].get<std::string>(), "Mitglied");
    ASSERT_STR_EQ(details["changes"][1]["after"].get<std::string>(), "admin");
    ASSERT_TRUE(details["changes"][0]["after"].is_null());
}

TEST(audit_details_redact_nested_and_free_text_content)
{
    auto details = audit_change_details("activity",
        {{"title", "Treffen"}, {"programs", nlohmann::json::array()}, {"goal", "private-old"}},
        {{"title", "Ausflug"}, {"programs", {{{"description", "private-new"}, {"access_token", "private-secret"}}}},
         {"goal", "private-new"}, {"share_token", "private-share"}});
    ASSERT_STR_EQ(details["resource_name"].get<std::string>(), "Ausflug");
    ASSERT_EQ(details["changes"].size(), static_cast<size_t>(3));
    ASSERT_TRUE(details.dump().find("private") == std::string::npos);
    ASSERT_TRUE(details["changes"][1]["redacted"].get<bool>());
    ASSERT_TRUE(!details["changes"][1].contains("after"));
    ASSERT_TRUE(audit_change_details("system", nullptr, {{"value", "private-config"}})["changes"].empty());
    ASSERT_TRUE(audit_change_details("unknown", nullptr, {{"name", "private-content"}})["changes"].empty());
}

TEST(audit_details_identify_deletions_and_bound_metadata)
{
    auto deleted = audit_change_details("user", {{"display_name", "Leiter"}, {"role", "Mitglied"}}, nullptr);
    ASSERT_STR_EQ(deleted["kind"].get<std::string>(), "deleted");
    ASSERT_STR_EQ(deleted["resource_name"].get<std::string>(), "Leiter");
    ASSERT_TRUE(deleted["changes"][0]["after"].is_null());
    auto bounded = audit_change_details("location", nullptr, {{"name", std::string(1000, 'x')}});
    ASSERT_TRUE(bounded.dump().size() < 1000);
    ASSERT_TRUE(audit_change_details("user", {{"role", "admin"}}, {{"role", "admin"}})["changes"].empty());
    ASSERT_TRUE(audit_change_details("activity",
        {{"programs", {{{"id", "old-id"}, {"title", "Spiel"}}}}},
        {{"programs", {{{"id", "new-id"}, {"title", "Spiel"}}}}})["changes"].empty());
}

TEST(audit_retention_rejects_invalid_or_unbounded_values)
{
    ASSERT_EQ(audit_retention_days("14"), 14);
    ASSERT_EQ(audit_retention_days("1"), 1);
    ASSERT_EQ(audit_retention_days("3650"), 3650);
    for (const auto &invalid : {"", "0", "-1", "3651", "14days", "1.5", "99999999999999999999"})
    {
        bool rejected = false;
        try { audit_retention_days(invalid); }
        catch (const std::invalid_argument &) { rejected = true; }
        ASSERT_TRUE(rejected);
    }
}
