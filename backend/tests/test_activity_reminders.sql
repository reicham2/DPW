\set ON_ERROR_STOP on
BEGIN;

CREATE FUNCTION pg_temp.assert_true(condition BOOLEAN, message TEXT)
RETURNS VOID LANGUAGE plpgsql AS $$
BEGIN
    IF condition IS DISTINCT FROM true THEN RAISE EXCEPTION '%', message; END IF;
END;
$$;

DO $$
DECLARE
    activity UUID;
    user24 UUID;
    user2 UUID;
    user48 UUID;
    opted_out UUID;
    other_dept UUID;
    no_access UUID;
    starts_at TIMESTAMPTZ := '2027-01-16 13:00:00+00';
    first_note UUID;
    claimed INTEGER;
BEGIN
    INSERT INTO departments(name) VALUES ('Reminder test'), ('Reminder other');
    INSERT INTO roles(name, sort_order)
        SELECT 'Reminder reader', max(sort_order) + 1 FROM roles;
    INSERT INTO roles(name, sort_order)
        SELECT 'Reminder blocked', max(sort_order) + 1 FROM roles;
    INSERT INTO role_permissions(role, activity_read_scope)
        VALUES ('Reminder reader', 'same_dept'), ('Reminder blocked', 'none');
    INSERT INTO users(microsoft_oid, email, display_name, department, role)
        VALUES ('reminder-default', 'default@example.test', 'Default', 'Reminder test', 'Reminder reader')
        RETURNING id INTO user24;
    PERFORM pg_temp.assert_true((SELECT activity_reminder_hours = 24 AND notify_activity_reminder
        FROM users WHERE id = user24), 'Existing/new user defaults must be subscribed and 24 hours');
    INSERT INTO users(microsoft_oid, email, display_name, department, role, activity_reminder_hours)
        VALUES ('reminder-2', '2@example.test', 'Two', 'Reminder test', 'Reminder reader', 2)
        RETURNING id INTO user2;
    INSERT INTO users(microsoft_oid, email, display_name, department, role, activity_reminder_hours)
        VALUES ('reminder-48', '48@example.test', 'Forty eight', 'Reminder test', 'Reminder reader', 48)
        RETURNING id INTO user48;
    INSERT INTO users(microsoft_oid, email, display_name, department, role, notify_activity_reminder)
        VALUES ('reminder-off', 'off@example.test', 'Off', 'Reminder test', 'Reminder reader', false)
        RETURNING id INTO opted_out;
    INSERT INTO users(microsoft_oid, email, display_name, department, role)
        VALUES ('reminder-other', 'other@example.test', 'Other', 'Reminder other', 'Reminder reader')
        RETURNING id INTO other_dept;
    INSERT INTO users(microsoft_oid, email, display_name, department, role)
        VALUES ('reminder-blocked', 'blocked@example.test', 'Blocked', 'Reminder test', 'Reminder blocked')
        RETURNING id INTO no_access;
    INSERT INTO activities(title, date, start_time, end_time, goal, location, department)
        VALUES ('Reminder activity', '2027-01-16', '14:00', '16:00', '', 'Wald', 'Reminder test')
        RETURNING id INTO activity;

    PERFORM pg_temp.assert_true(activity_reminder_start('2027-01-16', '14:00') = starts_at,
        'Winter activity time must be Europe/Zurich, independent of DB timezone');
    PERFORM pg_temp.assert_true(activity_reminder_start('2027-07-16', '14:00') = '2027-07-16 12:00+00',
        'Summer activity must use Zurich DST');
    PERFORM pg_temp.assert_true(activity_reminder_start('2027-03-28', '14:00') - INTERVAL '24 hours'
        = '2027-03-27 12:00+00', 'Spring DST lead is elapsed 24 hours, not a calendar day');
    PERFORM pg_temp.assert_true(activity_reminder_start('2027-10-31', '14:00') - INTERVAL '24 hours'
        = '2027-10-30 13:00+00', 'Autumn DST lead is elapsed 24 hours');
    PERFORM pg_temp.assert_true(activity_reminder_start('2027-01-16', 'not a time') IS NULL,
        'Invalid stored times must not abort other reminders');
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '48 hours 1 second') = 0,
        'Nothing before earliest lead');
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '48 hours', 'https://dpw.example.test/') = 1,
        '48h recipient at exact boundary');
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '24 hours 1 second') = 0,
        '24h user must not receive early');
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '24 hours', 'https://dpw.example.test') = 1,
        '24h recipient at exact boundary');
    SELECT id INTO first_note FROM notifications WHERE user_id = user24 AND category = 'activity_reminder';
    PERFORM pg_temp.assert_true((SELECT link = 'https://dpw.example.test/activities/' || activity
        AND payload->>'reminder_hours' = '24' AND payload->>'activity_date_display' = '16.01.2027'
        AND payload->>'notification_recipient_name' = 'Default'
        FROM notifications WHERE id = first_note), 'Category payload and activity link');
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '2 hours 1 second') = 0,
        '2h user must not receive early');
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '2 hours') = 1,
        '2h recipient at exact boundary');
    PERFORM pg_temp.assert_true(NOT EXISTS (SELECT 1 FROM notifications
        WHERE user_id IN (opted_out, other_dept, no_access)), 'Preference, department and read-access eligibility');
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '1 hour') = 0,
        'Repeated job/restart must not duplicate');
    UPDATE users SET activity_reminder_hours = 48 WHERE id = user24;
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '1 hour') = 0,
        'Lead changes must not resend already-issued reminders');
    DELETE FROM notifications WHERE id = first_note;
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '1 hour') = 0,
        'Deleted notification must not erase duplicate-prevention ledger');

    UPDATE users SET notify_channel_websocket = false WHERE id = user48;
    SELECT count(*) INTO claimed FROM claim_activity_reminder_deliveries(starts_at - INTERVAL '1 hour');
    PERFORM pg_temp.assert_true(claimed = 1, 'Browser delivery must honor channel toggle');
    PERFORM pg_temp.assert_true((SELECT count(*) = 0 FROM claim_activity_reminder_deliveries(starts_at - INTERVAL '59 minutes')),
        'An active lease must prevent retry/overlap');
    PERFORM pg_temp.assert_true((SELECT count(*) = 1 FROM claim_activity_reminder_deliveries(starts_at - INTERVAL '55 minutes')),
        'Crash/failure lease must retry after five minutes');
    UPDATE activity_reminders SET browser_delivered = true WHERE user_id = user2;
    PERFORM pg_temp.assert_true((SELECT count(*) = 0 FROM claim_activity_reminder_deliveries(starts_at - INTERVAL '45 minutes')),
        'Successful delivery must not retry');

    UPDATE activities SET title = 'Edited title', end_time = '17:00' WHERE id = activity;
    PERFORM pg_temp.assert_true((SELECT reminder_revision = 0 FROM activities WHERE id = activity),
        'Unrelated edits must not change schedule identity');
    UPDATE activities SET start_time = '15:00' WHERE id = activity;
    PERFORM pg_temp.assert_true(NOT EXISTS (SELECT 1 FROM notifications WHERE payload->>'activity_id' = activity::text),
        'Rescheduling must retract stale in-app reminders');
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '1 hour') = 3,
        'Changed schedule permits a fresh reminder');
    UPDATE activities SET deleted_at = starts_at - INTERVAL '30 minutes' WHERE id = activity;
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at) = 0,
        'Deleted/cancelled activity must never issue reminders');
    PERFORM pg_temp.assert_true((SELECT count(*) = 0 FROM claim_activity_reminder_deliveries(starts_at)),
        'Deleted/cancelled activity must not retry deliveries');
    UPDATE activities SET deleted_at = NULL WHERE id = activity;
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at) = 3, 'Restored future schedule is eligible');
    DELETE FROM activities WHERE id = activity;
    PERFORM pg_temp.assert_true(NOT EXISTS (SELECT 1 FROM activity_reminders WHERE activity_id = activity),
        'Hard deletion must remove all ledger/outbox rows');
    PERFORM pg_temp.assert_true(NOT EXISTS (SELECT 1 FROM notifications WHERE payload->>'activity_id' = activity::text),
        'Hard deletion must retract in-app reminders');

    INSERT INTO activities(title, date, start_time, end_time, goal, location, department)
        VALUES ('Late creation', '2027-01-16', '14:00', '16:00', '', '', 'Reminder test')
        RETURNING id INTO activity;
    UPDATE users SET activity_reminder_hours = 2 WHERE id = user24;
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '3 hours') = 1,
        'Late job catches up only users whose lead has elapsed');
    UPDATE users SET activity_reminder_hours = 24 WHERE id = user24;
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '3 hours') = 1,
        'Longer preference brings forward an unissued reminder');
    UPDATE users SET department = 'Reminder other' WHERE id = user24;
    UPDATE users SET notify_activity_reminder = false WHERE id = user48;
    UPDATE users SET department = 'Reminder test' WHERE id = other_dept;
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at - INTERVAL '1 hour') = 2,
        'Membership is resolved at send time, including new members');
    PERFORM pg_temp.assert_true((SELECT count(*) = 2 FROM claim_activity_reminder_deliveries(starts_at - INTERVAL '59 minutes')),
        'Outbox rechecks current department and opt-out');
    PERFORM pg_temp.assert_true(create_due_activity_reminders(starts_at) = 0, 'Never notify at/past start');
    PERFORM pg_temp.assert_true((SELECT count(*) = 0 FROM claim_activity_reminder_deliveries(starts_at)),
        'Never retry at/past start');
    BEGIN
        UPDATE users SET activity_reminder_hours = 3 WHERE id = user24;
        RAISE EXCEPTION 'Unsupported lead was accepted';
    EXCEPTION WHEN check_violation THEN NULL;
    END;
    RAISE NOTICE 'Activity reminder timing, defaults, recipients, preferences, lifecycle and retry regressions passed';
END;
$$;
ROLLBACK;
