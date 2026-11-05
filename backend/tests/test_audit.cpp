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
