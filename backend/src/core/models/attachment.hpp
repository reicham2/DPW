#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

struct Attachment
{
    std::string id;
    std::string activity_id;
    std::string filename;
    std::string content_type;
    std::string created_at;
};

struct AttachmentData
{
    std::string activity_id;
    std::string filename;
    std::string content_type;
    std::vector<uint8_t> data;
};

struct ProgramImageData
{
    std::optional<std::string> activity_id;
    std::optional<std::string> ideenkiste_id;
    std::string filename;
    std::string content_type;
    std::vector<uint8_t> data;
};
