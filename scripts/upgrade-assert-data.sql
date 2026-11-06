DO $$
DECLARE
    fixture record;
    old_row jsonb;
    preserved boolean;
BEGIN
    IF (SELECT count(*) FROM upgrade_test.expected) <> 12 THEN
        RAISE EXCEPTION 'Baseline data oracle is missing or incomplete';
    END IF;
    FOR fixture IN SELECT * FROM upgrade_test.expected LOOP
        FOR old_row IN SELECT value FROM jsonb_array_elements(fixture.rows) LOOP
            -- New columns are allowed, but every existing value must survive.
            EXECUTE format('SELECT EXISTS (
                SELECT 1 FROM public.%I t WHERE (to_jsonb(t) - ''updated_at'') @> $1
            )', fixture.table_name) INTO preserved USING old_row;
            IF NOT preserved THEN
                RAISE EXCEPTION 'Upgrade lost or changed data in %: %', fixture.table_name, old_row;
            END IF;
        END LOOP;
    END LOOP;
END $$;
