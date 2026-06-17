#pragma once
#include <string>
#include <vector>
#include <optional>
#include "json.hpp"
#include "core/models/activity.hpp" // Program, ProgramInput reused for camp Programmpunkte

// ── Camp Planning domain models ──────────────────────────

struct Camp
{
    std::string id;
    std::string title;
    std::string short_title;
    std::string motto;
    std::string kind;
    std::string organizer;
    std::string address_name;
    std::string address_street;
    std::string address_zipcode;
    std::string address_city;
    std::string coach_name;
    std::string course_number;
    std::string color;
    std::optional<std::string> department;
    std::optional<std::string> created_by;
    bool is_prototype{false};
    std::string created_at;
    std::string updated_at;
};

struct CampInput
{
    std::string title;
    std::string short_title;
    std::string motto;
    std::string kind;
    std::string organizer;
    std::string address_name;
    std::string address_street;
    std::string address_zipcode;
    std::string address_city;
    std::string coach_name;
    std::string course_number;
    std::string color{"#0080ff"};
    std::optional<std::string> department;
};

struct CampCollaboration
{
    std::string id;
    std::string camp_id;
    std::optional<std::string> user_id;
    std::string display_name;
    std::string role;        // member | manager | guest
    std::string camp_role;   // RF function label
    std::string abbreviation;
    std::string color;
    std::string status;      // invited | established | inactive
    std::string created_at;
    std::string updated_at;
};

struct CampCollaborationInput
{
    std::optional<std::string> user_id;
    std::string display_name;
    std::string role{"member"};
    std::string camp_role;
    std::string abbreviation;
    std::string color{"#6b7280"};
    std::string status{"established"};
};

// Who is responsible for a specific calendar day of a period (Tagesverantwortliche).
struct CampDayResponsible
{
    std::string id;
    std::string period_id;
    int day_offset{0}; // 0 = period start_date
    std::string collaboration_id;
};

struct CampCategory
{
    std::string id;
    std::string camp_id;
    std::string short_name;
    std::string name;
    std::string color;
    std::string numbering_style;
    int position{0};
    std::string created_at;
    std::string updated_at;
};

struct CampCategoryInput
{
    std::string short_name;
    std::string name;
    std::string color{"#0080ff"};
    std::string numbering_style{"1"};
    int position{0};
};

struct CampPeriod
{
    std::string id;
    std::string camp_id;
    std::string description;
    std::string start_date;
    std::string end_date;
    int position{0};
    std::string created_at;
    std::string updated_at;
};

struct CampPeriodInput
{
    std::string description;
    std::string start_date;
    std::string end_date;
    int position{0};
};

// One scheduled placement of an activity on the timeline.
struct ScheduleEntry
{
    std::string id;
    std::string activity_id;
    std::string period_id;
    int period_offset{0}; // minutes from period start
    int length{60};       // minutes
    double left_fraction{0.0};
    double width_fraction{1.0};
    std::string created_at;
    std::string updated_at;
};

struct ScheduleEntryInput
{
    std::string period_id;
    int period_offset{0};
    int length{60};
    double left_fraction{0.0};
    double width_fraction{1.0};
};

// Recursive content node (layout + content widgets).
struct ContentNode
{
    std::string id;
    std::string activity_id;
    std::optional<std::string> parent_id;
    std::string slot;
    int position{0};
    std::string content_type; // ColumnLayout | Storyboard | MaterialNode | SingleText | MultiSelect | Checklist
    std::string instance_name;
    bool is_root{false};
    nlohmann::json data = nlohmann::json::object();
    std::string created_at;
    std::string updated_at;
};

struct ContentNodeInput
{
    std::optional<std::string> id; // present when updating existing node
    std::optional<std::string> parent_id;
    std::string slot;
    int position{0};
    std::string content_type;
    std::string instance_name;
    bool is_root{false};
    nlohmann::json data = nlohmann::json::object();
};

struct CampMaterialItem
{
    std::string id;
    std::string material_list_id;
    std::optional<std::string> content_node_id;
    std::optional<std::string> period_id;
    std::string article_name;
    std::optional<double> quantity;
    std::string unit;
    std::string created_at;
    std::string updated_at;
};

struct CampMaterialItemInput
{
    std::optional<std::string> content_node_id;
    std::optional<std::string> period_id;
    std::string article_name;
    std::optional<double> quantity;
    std::string unit;
};

struct CampMaterialList
{
    std::string id;
    std::string camp_id;
    std::optional<std::string> collaboration_id;
    std::string name;
    std::string created_at;
    std::string updated_at;
    std::vector<CampMaterialItem> items;
};

// A camp activity with its schedule entries, responsibles and content tree.
struct CampActivity
{
    std::string id;
    std::string camp_id;
    std::optional<std::string> category_id;
    std::string title;
    std::string location;
    std::vector<std::string> responsible; // user IDs or free-text, like Activity
    std::string created_at;
    std::string updated_at;
    std::vector<ScheduleEntry> schedule_entries;
    std::vector<std::string> responsible_collaboration_ids;
    std::vector<ContentNode> content_nodes;
    std::vector<Program> programs; // Programmpunkte, same as activity programs
};

struct CampActivityInput
{
    std::optional<std::string> category_id;
    std::string title;
    std::string location;
    std::vector<std::string> responsible; // user IDs or free-text, like Activity
    std::vector<ScheduleEntryInput> schedule_entries;
    std::vector<std::string> responsible_collaboration_ids;
    std::vector<ContentNodeInput> content_nodes;
    std::vector<ProgramInput> programs; // Programmpunkte
};
