#pragma once
#include <string>

struct TokenClaims {
    std::string oid;          // stable Microsoft user object ID
    std::string email;        // preferred_username / upn
    std::string display_name; // "name" claim
    std::string tid;          // tenant ID
};

// Validates a Microsoft ID token (RS256 JWT) against the JWKS endpoint.
// Throws std::runtime_error on any validation failure.
TokenClaims validate_microsoft_token(const std::string& jwt_token);

// Extracts the raw token from an "Authorization: Bearer <token>" header value.
// Returns empty string if the header is missing or not a Bearer token.
std::string extract_bearer_token(std::string_view auth_header);
