#pragma once

#include <array>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <openssl/evp.h>

namespace dpw::crypto
{

inline std::string sha256_hex(const std::string &value)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (EVP_Digest(value.data(), value.size(), digest.data(), &digest_size, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA-256 hashing failed");
    static constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(digest_size * 2);
    for (unsigned int i = 0; i < digest_size; ++i)
    {
        output.push_back(hex[digest[i] >> 4]);
        output.push_back(hex[digest[i] & 0x0f]);
    }
    return output;
}

inline bool is_guest_edit_token(const std::string &token)
{
    return token.size() == 64 && std::all_of(token.begin(), token.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

} // namespace dpw::crypto
