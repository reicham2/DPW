#include "infra/activity_reminders.hpp"
#include "infra/web_push.hpp"
#include "db/database.hpp"
#include <stdexcept>

void run_activity_reminders(Database &db,
                            const std::function<void(const NotificationRecord &)> &notify_browser)
{
    db.create_due_activity_reminders();
    bool failed = false;
    for (const auto &note : db.claim_activity_reminder_deliveries()) {
        const auto recipient = db.get_user_by_id(note.user_id);
        if (!recipient || !recipient->notify_activity_reminder || !recipient->notify_channel_websocket ||
            !recipient->department || *recipient->department != note.payload.at("department").get<std::string>())
            continue;
        notify_browser(note);
        const bool delivered = deliver_web_push_for_user(db, *recipient, note);
        db.finish_activity_reminder_delivery(note.id, delivered);
        failed = failed || !delivered;
    }
    if (failed)
        throw std::runtime_error("activity-reminders: browser push failed; pending deliveries will retry");
}
