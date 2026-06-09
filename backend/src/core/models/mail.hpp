#pragma once
#include <string>
#include <vector>

struct MailTemplate
{
    std::string id;
    std::string department;
    std::string subject;
    std::string body;
    std::vector<std::string> recipients;
    std::vector<std::string> cc;
    std::string created_at;
    std::string updated_at;
};

struct EventTemplate
{
    std::string id;
    std::string department;
    std::string title;
    std::string body;
    std::string created_at;
    std::string updated_at;
};

struct EventPublication
{
    std::string id;
    std::string activity_id;
    std::string published_by;
    std::string title;
    std::string body_html;
    std::string wp_event_id;
    std::string published_at;
};

struct SentMail
{
    std::string id;
    std::string activity_id;
    std::string sender_id;
    std::string sender_email;
    std::vector<std::string> to_emails;
    std::vector<std::string> cc_emails;
    std::string subject;
    std::string body_html;
    std::string sent_at;
};

struct MailDraft
{
    std::string id;
    std::string activity_id;
    std::vector<std::string> recipients;
    std::vector<std::string> cc;
    std::string subject;
    std::string body_html;
    std::string updated_by;
    std::string updated_at;
};

struct FormDraft
{
    std::string id;
    std::string activity_id;
    std::string form_type;
    std::string title;
    std::string questions_json;
    std::string updated_by;
    std::string updated_at;
};
