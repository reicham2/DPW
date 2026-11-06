#pragma once
#include <functional>
#include "core/models/notification.hpp"

class Database;

void run_activity_reminders(Database &db,
                            const std::function<void(const NotificationRecord &)> &notify_browser);
