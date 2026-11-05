-- Run with psql -v ON_ERROR_STOP=1 against an initialized test database.
BEGIN;
DO $$
DECLARE
    test_user UUID := gen_random_uuid();
    expired_activity UUID := gen_random_uuid();
    current_activity UUID := gen_random_uuid();
    expired_notification UUID := gen_random_uuid();
    current_notification UUID := gen_random_uuid();
    global_notification UUID := gen_random_uuid();
BEGIN
    INSERT INTO users (id, microsoft_oid, email, display_name, role)
    VALUES (test_user, test_user::text, 'cron-test@example.invalid', 'Cron Test', 'Mitglied');
    INSERT INTO activities (id, title, date, start_time, end_time, goal, location)
    VALUES (expired_activity, 'Cron expired', CURRENT_DATE - 1, '10:00', '11:00', '', ''),
           (current_activity, 'Cron current', CURRENT_DATE, '10:00', '11:00', '', '');
    INSERT INTO notifications (id, user_id, category, title, message, payload)
    VALUES (expired_notification, test_user, 'activity_assigned', 'Expired', '',
            jsonb_build_object('activity_id', expired_activity::text)),
           (current_notification, test_user, 'activity_assigned', 'Current', '',
            jsonb_build_object('activity_id', current_activity::text)),
           (global_notification, test_user, 'activity_assigned', 'Global', '', '{}');

    ASSERT (SELECT COUNT(*) FROM notifications WHERE user_id = test_user) = 3,
        'Physical cleanup must not be required to hide expired notifications';
    ASSERT (SELECT COUNT(*) FROM active_notifications WHERE user_id = test_user) = 2,
        'Only expired activity notifications must be filtered';
    ASSERT NOT EXISTS (SELECT 1 FROM active_notifications WHERE id = expired_notification),
        'Expired notification visible before cron cleanup';

    UPDATE active_notifications SET is_read = true WHERE id = expired_notification;
    ASSERT NOT (SELECT is_read FROM notifications WHERE id = expired_notification),
        'Mark-one must not update expired notifications';
    UPDATE active_notifications SET is_read = true WHERE user_id = test_user;
    ASSERT (SELECT COUNT(*) FROM notifications WHERE user_id = test_user AND is_read) = 2,
        'Mark-all must only update active notifications';

    INSERT INTO push_subscriptions (user_id, endpoint, p256dh, auth)
    VALUES (test_user, 'https://example.invalid/' || test_user::text, 'test-key', 'test-auth');
    UPDATE notifications SET is_read = false WHERE user_id = test_user;
    ASSERT (SELECT COUNT(*) FROM push_subscriptions ps
            JOIN active_notifications n ON n.user_id = ps.user_id
            WHERE ps.user_id = test_user AND NOT n.is_read) = 2,
        'Push lookup must not return expired notifications';

    UPDATE activities SET date = CURRENT_DATE - 1 WHERE id = current_activity;
    ASSERT (SELECT COUNT(*) FROM active_notifications WHERE user_id = test_user) = 1,
        'Expiry must follow activity changes immediately without a cron run';
END $$;
ROLLBACK;
