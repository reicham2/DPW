#pragma once
#include <string>
#include <optional>

// WordPress / EventON REST API client.
// Requires env vars: DPW_WP_URL, DPW_WP_USER, DPW_WP_APP_PASSWORD.
// If any are empty the functions return nullopt (WordPress sync disabled).

struct WpEventResult
{
    std::string wp_event_id; // WordPress post ID as string
};

// Returns true if WordPress integration is configured.
bool wp_configured();

// Create a new EventON event. Returns the WordPress post ID on success.
// department is used as the EventON "event_type" taxonomy term.
std::optional<WpEventResult> wp_create_event(const std::string &title,
                                              const std::string &body_html,
                                              long start_unix,
                                              long end_unix,
                                              const std::string &location,
                                              const std::string &department);

// Update an existing EventON event by WordPress post ID.
std::optional<WpEventResult> wp_update_event(const std::string &wp_id,
                                              const std::string &title,
                                              const std::string &body_html,
                                              long start_unix,
                                              long end_unix,
                                              const std::string &location,
                                              const std::string &department);

// Delete an EventON event by WordPress post ID. Returns true on success.
bool wp_delete_event(const std::string &wp_id);
