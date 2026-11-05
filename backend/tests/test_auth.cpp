#include "infra/auth.hpp"
#include "infra/jwks_cache.hpp"
#include "test_harness.hpp"
#include "json.hpp"

#include <cstdlib>
#include <ctime>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

#include <openssl/evp.h>

namespace {

const std::string key_a = R"({"keys":[{"kty":"RSA","kid":"key-a","n":"AQ","e":"AQAB"}]})";
const std::string key_b = R"({"keys":[{"kty":"RSA","kid":"key-b","n":"AQ","e":"AQAB"}]})";

class ScopedAuthEnvironment {
public:
    ScopedAuthEnvironment()
        : tenant_(read("AZURE_TENANT_ID")), client_(read("AZURE_CLIENT_ID")) {
        setenv("AZURE_TENANT_ID", "test-tenant", 1);
        setenv("AZURE_CLIENT_ID", "test-client", 1);
    }

    ~ScopedAuthEnvironment() {
        restore("AZURE_TENANT_ID", tenant_);
        restore("AZURE_CLIENT_ID", client_);
    }

private:
    static std::optional<std::string> read(const char* name) {
        const char* value = std::getenv(name);
        return value ? std::optional<std::string>(value) : std::nullopt;
    }

    static void restore(const char* name, const std::optional<std::string>& value) {
        if (value) setenv(name, value->c_str(), 1);
        else unsetenv(name);
    }

    std::optional<std::string> tenant_;
    std::optional<std::string> client_;
};

std::string base64url_encode(const std::string& input) {
    std::string output(4 * ((input.size() + 2) / 3), '\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(output.data()),
                    reinterpret_cast<const unsigned char*>(input.data()),
                    static_cast<int>(input.size()));
    for (char& c : output) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!output.empty() && output.back() == '=') output.pop_back();
    return output;
}

nlohmann::json valid_claims() {
    return {
        {"iss", "https://login.microsoftonline.com/test-tenant/v2.0"},
        {"aud", "test-client"},
        {"exp", static_cast<long long>(time(nullptr)) + 3600},
    };
}

void expect_claim_error(const nlohmann::json& claims, const std::string& expected) {
    ScopedAuthEnvironment env;
    const nlohmann::json header = {{"alg", "RS256"}, {"kid", "test-key"}};
    const std::string token = base64url_encode(header.dump()) + "." +
                              base64url_encode(claims.dump()) + ".AA";
    try {
        validate_microsoft_token(token);
    } catch (const std::exception& error) {
        ASSERT_STR_EQ(error.what(), expected);
        return;
    }
    throw std::runtime_error("Expected token validation to fail");
}

} // namespace

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

TEST(jwt_rejects_missing_expiration)
{
    auto claims = valid_claims();
    claims.erase("exp");
    expect_claim_error(claims, "JWT missing exp claim");
}

TEST(jwt_rejects_non_numeric_expiration)
{
    auto claims = valid_claims();
    claims["exp"] = "tomorrow";
    expect_claim_error(claims, "JWT invalid exp claim");
}

TEST(jwt_rejects_overflowing_expiration)
{
    auto claims = valid_claims();
    claims["exp"] = std::numeric_limits<unsigned long long>::max();
    expect_claim_error(claims, "JWT invalid exp claim");
}

TEST(jwt_rejects_expired_token)
{
    auto claims = valid_claims();
    claims["exp"] = 0;
    expect_claim_error(claims, "JWT expired");
}

TEST(jwt_rejects_expiration_at_current_time)
{
    auto claims = valid_claims();
    claims["exp"] = static_cast<long long>(time(nullptr));
    expect_claim_error(claims, "JWT expired");
}

TEST(jwt_rejects_future_not_before)
{
    auto claims = valid_claims();
    claims["nbf"] = static_cast<long long>(time(nullptr)) + 600;
    expect_claim_error(claims, "JWT not yet valid");
}

TEST(jwt_rejects_invalid_not_before)
{
    auto claims = valid_claims();
    claims["nbf"] = nullptr;
    expect_claim_error(claims, "JWT invalid nbf claim");
}

TEST(jwt_allows_small_clock_skew_for_not_before)
{
    auto claims = valid_claims();
    claims["nbf"] = static_cast<long long>(time(nullptr)) + 120;
    claims["iss"] = "incorrect-issuer";
    expect_claim_error(claims, "JWT issuer mismatch");
}

TEST(jwks_cache_is_scoped_to_the_tenant)
{
    JwksCache cache;
    int fetches = 0;
    auto fetch_a = [&] { ++fetches; return key_a; };
    auto fetch_b = [&] { ++fetches; return key_b; };

    ASSERT_STR_EQ(cache.get("tenant-a", fetch_a), key_a);
    ASSERT_STR_EQ(cache.get("tenant-a", fetch_a), key_a);
    ASSERT_EQ(fetches, 1);
    ASSERT_STR_EQ(cache.get("tenant-b", fetch_b), key_b);
    ASSERT_EQ(fetches, 2);
}

TEST(jwks_cache_retries_after_invalid_responses)
{
    JwksCache cache;
    int fetches = 0;
    auto fetch = [&]() -> std::string {
        ++fetches;
        if (fetches == 1) return R"({"error":"temporarily unavailable"})";
        return key_a;
    };

    try {
        cache.get("tenant-a", fetch);
        throw std::runtime_error("Expected invalid JWKS to fail");
    } catch (const std::exception& error) {
        ASSERT_STR_EQ(error.what(), "JWKS response contains no signing keys");
    }
    ASSERT_STR_EQ(cache.get("tenant-a", fetch), key_a);
    ASSERT_EQ(fetches, 2);
}

TEST(jwks_cache_retries_after_fetch_failure)
{
    JwksCache cache;
    int fetches = 0;
    auto fetch = [&]() -> std::string {
        if (++fetches == 1) throw std::runtime_error("network unavailable");
        return key_a;
    };

    try {
        cache.get("tenant-a", fetch);
        throw std::runtime_error("Expected fetch failure");
    } catch (const std::exception& error) {
        ASSERT_STR_EQ(error.what(), "network unavailable");
    }
    ASSERT_STR_EQ(cache.get("tenant-a", fetch), key_a);
    ASSERT_EQ(fetches, 2);
}

TEST(jwks_cache_rejects_malformed_signing_keys)
{
    JwksCache cache;
    int fetches = 0;
    auto fetch = [&]() -> std::string {
        if (++fetches == 1) return R"({"keys":[{"kty":"RSA","kid":"key-a"}]})";
        return key_a;
    };

    try {
        cache.get("tenant-a", fetch);
        throw std::runtime_error("Expected malformed signing key to fail");
    } catch (const std::exception& error) {
        ASSERT_STR_EQ(error.what(), "JWKS response contains no signing keys");
    }
    ASSERT_STR_EQ(cache.get("tenant-a", fetch), key_a);
    ASSERT_EQ(fetches, 2);
}

TEST(jwks_cache_does_not_invalidate_a_newer_document)
{
    JwksCache cache;
    const std::string& first = key_a;
    const std::string& second = key_b;
    int fetches = 0;
    auto fetch = [&] { return ++fetches == 1 ? first : second; };

    ASSERT_STR_EQ(cache.get("tenant-a", fetch), first);
    cache.invalidate("tenant-a", first);
    ASSERT_STR_EQ(cache.get("tenant-a", fetch), second);
    cache.invalidate("tenant-a", first);
    ASSERT_STR_EQ(cache.get("tenant-a", fetch), second);
    ASSERT_EQ(fetches, 2);
}
