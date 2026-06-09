#pragma once
#include <string>
#include "core/models/user.hpp"
#include "core/models/notification.hpp"

class Database;

void deliver_web_push_for_user(Database &db, const UserRecord &user, const NotificationRecord &notification);
