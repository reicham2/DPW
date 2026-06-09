#include "utils/strings.hpp"
#include <ctime>
#include <algorithm>
#include <cctype>

std::string to_lower_ascii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim_ascii(std::string s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c)
                                    { return !std::isspace(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c)
                         { return !std::isspace(c); })
                .base(),
            s.end());
    return s;
}

// ── HTTP status text ────────────────────────────────────────────────────────

const char *status_text(int code)
{
    switch (code)
    {
    case 200:
        return "200 OK";
    case 201:
        return "201 Created";
    case 204:
        return "204 No Content";
    case 400:
        return "400 Bad Request";
    case 401:
        return "401 Unauthorized";
    case 403:
        return "403 Forbidden";
    case 404:
        return "404 Not Found";
    case 409:
        return "409 Conflict";
    case 413:
        return "413 Payload Too Large";
    case 429:
        return "429 Too Many Requests";
    case 502:
        return "502 Bad Gateway";
    case 503:
        return "503 Service Unavailable";
    case 500:
        return "500 Internal Server Error";
    default:
        return "200 OK";
    }
}

// ── URL decoding ────────────────────────────────────────────────────────────

std::string url_decode(const std::string &src)
{
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i)
    {
        if (src[i] == '%' && i + 2 < src.size())
        {
            unsigned int ch = 0;
            if (sscanf(src.c_str() + i + 1, "%2x", &ch) == 1)
            {
                out += static_cast<char>(ch);
                i += 2;
                continue;
            }
        }
        out += (src[i] == '+') ? ' ' : src[i];
    }
    return out;
}

// ── Date formatting (German) ────────────────────────────────────────────────

std::string format_date_long_de(const std::string &iso_date)
{
    if (iso_date.size() < 10)
        return iso_date;
    struct tm t{};
    t.tm_year = std::stoi(iso_date.substr(0, 4)) - 1900;
    t.tm_mon = std::stoi(iso_date.substr(5, 2)) - 1;
    t.tm_mday = std::stoi(iso_date.substr(8, 2));
    std::mktime(&t);
    static const char *wdays[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                                  "Donnerstag", "Freitag", "Samstag"};
    static const char *months[] = {"Januar", "Februar", "März", "April", "Mai",
                                   "Juni", "Juli", "August", "September",
                                   "Oktober", "November", "Dezember"};
    std::string r = wdays[t.tm_wday];
    r += ", ";
    r += std::to_string(t.tm_mday);
    r += ". ";
    r += months[t.tm_mon];
    r += " ";
    r += iso_date.substr(0, 4);
    return r;
}

std::string format_date_short_de(const std::string &iso_date)
{
    if (iso_date.size() < 10)
        return iso_date;
    return iso_date.substr(8, 2) + "." + iso_date.substr(5, 2) + "." + iso_date.substr(0, 4);
}
