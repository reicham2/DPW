#pragma once
#include <string>
#include <string_view>

// ── URL helpers ─────────────────────────────────────────────────────────────

std::string url_decode(const std::string &src);

// ── String normalization helpers ───────────────────────────────────────────

std::string to_lower_ascii(std::string s);
std::string trim_ascii(std::string s);

// ── HTTP status text ────────────────────────────────────────────────────────

const char *status_text(int code);

// ── Date formatting (German) ────────────────────────────────────────────────

std::string format_date_long_de(const std::string &iso_date);
std::string format_date_short_de(const std::string &iso_date);
