#include "infra/auth.hpp"
#include "test_harness.hpp"

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
    ASSERT_STR_EQ(extract_bearer_token("Bearer token with spaces"), "token with spaces");
}

TEST(extract_bearer_case_sensitive)
{
    ASSERT_STR_EQ(extract_bearer_token("bearer abc123"), "");
}

TEST(extract_bearer_leading_whitespace_rejected)
{
    ASSERT_STR_EQ(extract_bearer_token(" Bearer abc123"), "");
}

TEST(extract_bearer_prefix_with_no_token)
{
    ASSERT_STR_EQ(extract_bearer_token("Bearer "), "");
}

TEST(extract_bearer_tab_separator_rejected)
{
    ASSERT_STR_EQ(extract_bearer_token("Bearer\tabc123"), "");
}

TEST(extract_bearer_multiple_spaces_kept_in_token)
{
    ASSERT_STR_EQ(extract_bearer_token("Bearer  abc123"), " abc123");
}

TEST(extract_bearer_jwt_like_token)
{
    ASSERT_STR_EQ(extract_bearer_token("Bearer eyJhbGciOi...payload...sig"), "eyJhbGciOi...payload...sig");
}

TEST(extract_bearer_trailing_whitespace_preserved)
{
    ASSERT_STR_EQ(extract_bearer_token("Bearer abc123   \t"), "abc123   \t");
}
