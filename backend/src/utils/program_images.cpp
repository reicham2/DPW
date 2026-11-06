#include "utils/program_images.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

namespace
{
    const std::regex image_reference(
        R"(/api/program-images/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})(?![0-9a-f-]))",
        std::regex::icase);

    std::string lower_id(std::string id)
    {
        std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return std::tolower(c); });
        return id;
    }
}

bool valid_program_image(const std::string &content_type, const std::vector<uint8_t> &data)
{
    if (data.empty() || data.size() > kMaxProgramImageBytes)
        return false;
    if (content_type == "image/png")
    {
        const std::vector<uint8_t> signature{0x89, 'P', 'N', 'G', 13, 10, 26, 10};
        return data.size() >= 24 && std::equal(signature.begin(), signature.end(), data.begin())
            && std::equal(data.begin() + 12, data.begin() + 16, "IHDR");
    }
    if (content_type == "image/jpeg")
        return data.size() >= 4 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff;
    if (content_type == "image/gif")
        return data.size() >= 13 && (std::equal(data.begin(), data.begin() + 6, "GIF87a")
            || std::equal(data.begin(), data.begin() + 6, "GIF89a"));
    if (content_type == "image/webp")
        return data.size() >= 16 && std::equal(data.begin(), data.begin() + 4, "RIFF")
            && std::equal(data.begin() + 8, data.begin() + 12, "WEBP");
    return false;
}

bool decode_program_image(const std::string &base64, const std::string &content_type,
                          std::vector<uint8_t> &data)
{
    data.clear();
    if (base64.empty() || base64.size() % 4 != 0
        || base64.size() > 4 * ((kMaxProgramImageBytes + 2) / 3))
        return false;
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    data.reserve(base64.size() / 4 * 3);
    for (size_t i = 0; i < base64.size(); i += 4)
    {
        const bool last = i + 4 == base64.size();
        const bool pad2 = base64[i + 2] == '=';
        const bool pad3 = base64[i + 3] == '=';
        if ((pad2 && !pad3) || ((pad2 || pad3) && !last))
            return false;
        const size_t a = alphabet.find(base64[i]);
        const size_t b = alphabet.find(base64[i + 1]);
        const size_t c = pad2 ? 0 : alphabet.find(base64[i + 2]);
        const size_t d = pad3 ? 0 : alphabet.find(base64[i + 3]);
        if (a == std::string::npos || b == std::string::npos
            || c == std::string::npos || d == std::string::npos
            || (pad2 && (b & 15)) || (pad3 && !pad2 && (c & 3)))
            return false;
        data.push_back(static_cast<uint8_t>((a << 2) | (b >> 4)));
        if (!pad2) data.push_back(static_cast<uint8_t>((b << 4) | (c >> 2)));
        if (!pad3) data.push_back(static_cast<uint8_t>((c << 6) | d));
    }
    return valid_program_image(content_type, data);
}

std::vector<std::string> program_image_ids(const std::string &html)
{
    std::set<std::string> ids;
    for (std::sregex_iterator it(html.begin(), html.end(), image_reference), end; it != end; ++it)
        ids.insert(lower_id((*it)[1].str()));
    return {ids.begin(), ids.end()};
}

std::string replace_program_image_ids(const std::string &html,
                                     const std::map<std::string, std::string> &replacements)
{
    std::string out;
    size_t offset = 0;
    for (std::sregex_iterator it(html.begin(), html.end(), image_reference), end; it != end; ++it)
    {
        const auto &match = *it;
        out.append(html, offset, static_cast<size_t>(match.position()) - offset);
        const auto replacement = replacements.find(lower_id(match[1].str()));
        out += replacement == replacements.end() ? match.str() : "/api/program-images/" + replacement->second;
        offset = static_cast<size_t>(match.position() + match.length());
    }
    out.append(html, offset, std::string::npos);
    return out;
}
