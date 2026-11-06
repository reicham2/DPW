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
    department_name TEXT := 'Mail template regression ' || gen_random_uuid()::text;
    legacy_template UUID;
    alternate_template UUID;
BEGIN
    INSERT INTO departments(name) VALUES (department_name);

    INSERT INTO mail_templates (name, department, subject, body, recipients, cc, is_default)
    VALUES ('Standard', department_name, 'Gespeicherter Betreff', '<p>Bestehender Inhalt</p>',
            ARRAY['eltern@example.test'], ARRAY[]::TEXT[], TRUE)
    RETURNING id INTO legacy_template;
    INSERT INTO mail_templates (name, department, subject, body, recipients, cc, is_default)
    VALUES ('Alternative', department_name, 'Alternativer Betreff', '<p>Alternative</p>',
            ARRAY[]::TEXT[], ARRAY['leitung@example.test'], FALSE)
    RETURNING id INTO alternate_template;

    PERFORM pg_temp.assert_true(
        (SELECT count(*) = 2 FROM mail_templates WHERE department = department_name),
        'A department must support multiple named mail templates');
    PERFORM pg_temp.assert_true(
        (SELECT is_default AND subject = 'Gespeicherter Betreff'
         FROM mail_templates WHERE id = legacy_template),
        'The original department template content remains intact and default');

    UPDATE mail_templates SET is_default = FALSE WHERE id = legacy_template;
    UPDATE mail_templates SET is_default = TRUE WHERE id = alternate_template;
    PERFORM pg_temp.assert_true(
        (SELECT count(*) = 1 AND min(name) = 'Alternative'
         FROM mail_templates WHERE department = department_name AND is_default),
        'Selecting a new default must leave exactly one default in the department');

    DELETE FROM mail_templates WHERE id = alternate_template;
    PERFORM pg_temp.assert_true(
        (SELECT count(*) = 1 FROM mail_templates WHERE department = department_name)
        AND (SELECT count(*) = 0 FROM mail_templates WHERE department = department_name AND is_default),
        'Deleting the default must not silently promote another template');
END;
$$;

ROLLBACK;
