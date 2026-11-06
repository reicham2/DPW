#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

inline constexpr size_t kMaxProgramImageBytes = 10 * 1024 * 1024;
inline constexpr size_t kMaxProgramImageBodyBytes = 4 * ((kMaxProgramImageBytes + 2) / 3) + 4096;

bool valid_program_image(const std::string &content_type, const std::vector<uint8_t> &data);
bool decode_program_image(const std::string &base64, const std::string &content_type,
                          std::vector<uint8_t> &data);
std::vector<std::string> program_image_ids(const std::string &html);
std::string replace_program_image_ids(const std::string &html,
                                     const std::map<std::string, std::string> &replacements);
