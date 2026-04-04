#include "auth.hpp"
#include "json.hpp"

#include <stdexcept>
#include <string>
#include <vector>
#include <mutex>
#include <ctime>
#include <cstdlib>
#include <string_view>

#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <openssl/bn.h>
#include <openssl/bio.h>

#include <curl/curl.h>

// ---- Base64url decode -------------------------------------------------------

static std::vector<uint8_t> base64url_decode(const std::string& input) {
    std::string s = input;
    for (char& c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (s.size() % 4 != 0) s += '=';

    BIO* b64  = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new_mem_buf(s.data(), static_cast<int>(s.size()));
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    std::vector<uint8_t> buf(s.size());
    int len = BIO_read(b64, buf.data(), static_cast<int>(buf.size()));
    BIO_free_all(b64);

    if (len < 0) throw std::runtime_error("base64url_decode: BIO_read failed");
    buf.resize(static_cast<size_t>(len));
    return buf;
}

// ---- libcurl JWKS fetch ------------------------------------------------------

static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string fetch_url(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("JWKS fetch failed: ") + curl_easy_strerror(rc));
    return response;
}

// ---- JWKS in-memory cache (1 hour TTL) --------------------------------------

static std::mutex  g_jwks_mutex;
static std::string g_jwks_cache;
static time_t      g_jwks_fetched_at = 0;
static const int   JWKS_TTL          = 3600;

static std::string get_jwks(const std::string& tenant_id) {
    std::lock_guard<std::mutex> lock(g_jwks_mutex);
    time_t now = time(nullptr);
    if (g_jwks_cache.empty() || (now - g_jwks_fetched_at) > JWKS_TTL) {
        std::string url = "https://login.microsoftonline.com/" + tenant_id
                        + "/discovery/v2.0/keys";
        g_jwks_cache      = fetch_url(url);
        g_jwks_fetched_at = now;
    }
    return g_jwks_cache;
}

// ---- RSA public key from JWK ------------------------------------------------

static EVP_PKEY* jwk_to_evp_pkey(const nlohmann::json& jwk) {
    auto n_bytes = base64url_decode(jwk.at("n").get<std::string>());
    auto e_bytes = base64url_decode(jwk.at("e").get<std::string>());

    BIGNUM* n_bn = BN_bin2bn(n_bytes.data(), static_cast<int>(n_bytes.size()), nullptr);
    BIGNUM* e_bn = BN_bin2bn(e_bytes.data(), static_cast<int>(e_bytes.size()), nullptr);
    if (!n_bn || !e_bn) {
        BN_free(n_bn); BN_free(e_bn);
        throw std::runtime_error("BN_bin2bn failed");
    }

    OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
    OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, n_bn);
    OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, e_bn);
    OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
    OSSL_PARAM_BLD_free(bld);
    BN_free(n_bn); BN_free(e_bn);

    EVP_PKEY_CTX* ctx  = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
    EVP_PKEY*     pkey = nullptr;
    if (!ctx || EVP_PKEY_fromdata_init(ctx) <= 0 ||
        EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        OSSL_PARAM_free(params);
        throw std::runtime_error("Failed to construct RSA public key from JWK");
    }
    EVP_PKEY_CTX_free(ctx);
    OSSL_PARAM_free(params);
    return pkey;
}

// ---- RSA-SHA256 signature verification --------------------------------------

static bool verify_rs256(EVP_PKEY* pkey,
                          const std::string& message,
                          const std::vector<uint8_t>& signature) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return false;

    bool ok = (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
               EVP_DigestVerifyUpdate(ctx, message.data(), message.size()) == 1 &&
               EVP_DigestVerifyFinal(ctx,
                   signature.data(), signature.size()) == 1);
    EVP_MD_CTX_free(ctx);
    return ok;
}

// ---- Public API -------------------------------------------------------------

std::string extract_bearer_token(std::string_view auth_header) {
    const std::string_view prefix = "Bearer ";
    if (auth_header.size() <= prefix.size()) return {};
    if (auth_header.substr(0, prefix.size()) != prefix) return {};
    return std::string(auth_header.substr(prefix.size()));
}

TokenClaims validate_microsoft_token(const std::string& jwt_token) {
    // 1. Split JWT into header.payload.signature
    auto dot1 = jwt_token.find('.');
    auto dot2 = (dot1 != std::string::npos) ? jwt_token.find('.', dot1 + 1) : std::string::npos;
    if (dot1 == std::string::npos || dot2 == std::string::npos)
        throw std::runtime_error("Invalid JWT format");

    std::string header_b64  = jwt_token.substr(0, dot1);
    std::string payload_b64 = jwt_token.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string sig_b64     = jwt_token.substr(dot2 + 1);
    std::string signed_part = header_b64 + "." + payload_b64;

    // 2. Decode and parse header
    auto header_bytes = base64url_decode(header_b64);
    auto header_json  = nlohmann::json::parse(header_bytes.begin(), header_bytes.end());

    if (!header_json.contains("kid"))
        throw std::runtime_error("JWT header missing kid");
    if (header_json.value("alg", "") != "RS256")
        throw std::runtime_error("JWT algorithm must be RS256");

    std::string kid = header_json["kid"].get<std::string>();

    // 3. Decode and parse payload
    auto payload_bytes = base64url_decode(payload_b64);
    auto payload       = nlohmann::json::parse(payload_bytes.begin(), payload_bytes.end());

    // 4. Read Azure config from environment
    const char* tenant_id_env = std::getenv("AZURE_TENANT_ID");
    const char* client_id_env = std::getenv("AZURE_CLIENT_ID");
    if (!tenant_id_env || std::string(tenant_id_env).empty())
        throw std::runtime_error("AZURE_TENANT_ID not configured");
    if (!client_id_env || std::string(client_id_env).empty())
        throw std::runtime_error("AZURE_CLIENT_ID not configured");

    std::string tenant_id = tenant_id_env;
    std::string client_id = client_id_env;

    // 5. Validate standard claims
    time_t now = time(nullptr);
    if (payload.contains("exp") && payload["exp"].get<long long>() < static_cast<long long>(now))
        throw std::runtime_error("JWT expired");

    std::string expected_iss = "https://login.microsoftonline.com/" + tenant_id + "/v2.0";
    if (payload.value("iss", "") != expected_iss)
        throw std::runtime_error("JWT issuer mismatch");

    bool aud_ok = false;
    if (payload.contains("aud")) {
        auto& aud = payload["aud"];
        if (aud.is_string())
            aud_ok = (aud.get<std::string>() == client_id);
        else if (aud.is_array()) {
            for (auto& a : aud)
                if (a.is_string() && a.get<std::string>() == client_id) { aud_ok = true; break; }
        }
    }
    if (!aud_ok) throw std::runtime_error("JWT audience mismatch");

    if (payload.contains("tid") && payload["tid"].get<std::string>() != tenant_id)
        throw std::runtime_error("JWT tenant mismatch");

    // 6. Fetch JWKS and find matching key by kid
    std::string jwks_str = get_jwks(tenant_id);
    auto jwks = nlohmann::json::parse(jwks_str);

    nlohmann::json matching_key;
    if (jwks.contains("keys")) {
        for (auto& key : jwks["keys"]) {
            if (key.value("kid", "") == kid) { matching_key = key; break; }
        }
    }
    if (matching_key.is_null()) {
        // Kid not found — invalidate cache and retry once
        {
            std::lock_guard<std::mutex> lock(g_jwks_mutex);
            g_jwks_fetched_at = 0;
        }
        jwks_str = get_jwks(tenant_id);
        jwks = nlohmann::json::parse(jwks_str);
        if (jwks.contains("keys")) {
            for (auto& key : jwks["keys"]) {
                if (key.value("kid", "") == kid) { matching_key = key; break; }
            }
        }
        if (matching_key.is_null())
            throw std::runtime_error("JWT kid not found in JWKS: " + kid);
    }

    // 7. Verify signature
    EVP_PKEY* pkey      = jwk_to_evp_pkey(matching_key);
    auto      sig_bytes = base64url_decode(sig_b64);
    bool      valid     = verify_rs256(pkey, signed_part, sig_bytes);
    EVP_PKEY_free(pkey);

    if (!valid) throw std::runtime_error("JWT signature verification failed");

    // 8. Extract claims
    TokenClaims claims;
    claims.oid = payload.value("oid", "");
    if (claims.oid.empty())
        throw std::runtime_error("JWT missing oid claim");

    claims.email = payload.contains("preferred_username")
                   ? payload["preferred_username"].get<std::string>()
                   : payload.value("email", "");
    claims.display_name = payload.value("name", claims.email);
    claims.tid          = payload.value("tid", "");

    return claims;
}
