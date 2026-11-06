BEGIN;

DO $$
DECLARE deleted_count INTEGER;
BEGIN
    INSERT INTO audit_events(action,resource_type,resource_id,occurred_at,outcome) VALUES
        ('retention-test','system','retention-old',NOW() - INTERVAL '15 days','completed'),
        ('retention-test','system','retention-boundary',NOW() - INTERVAL '14 days','completed'),
        ('retention-test','system','retention-recent',NOW() - INTERVAL '2 days','completed'),
        ('retention-test','system','retention-pending',NOW() - INTERVAL '15 days','pending'),
        ('retention-test','system','retention-rejected',NOW() - INTERVAL '15 days','rejected'),
        ('retention-test','system','retention-unknown',NOW() - INTERVAL '15 days','unknown');
    deleted_count := purge_audit_events(14);
    IF EXISTS (SELECT 1 FROM audit_events WHERE resource_id IN ('retention-old','retention-pending','retention-rejected','retention-unknown')) THEN
        RAISE EXCEPTION 'Expired audit events were retained';
    END IF;
    IF (SELECT count(*) FROM audit_events WHERE resource_id IN ('retention-boundary','retention-recent')) <> 2 THEN
        RAISE EXCEPTION 'Retention removed events inside the retention window';
    END IF;
    PERFORM purge_audit_events(1);
    IF EXISTS (SELECT 1 FROM audit_events WHERE resource_id IN ('retention-boundary','retention-recent')) THEN
        RAISE EXCEPTION 'Custom retention window was not applied';
    END IF;
    BEGIN
        PERFORM purge_audit_events(0);
        RAISE EXCEPTION 'Invalid retention was accepted';
    EXCEPTION WHEN raise_exception THEN
        IF SQLERRM <> 'Audit retention must be between 1 and 3650 days' THEN RAISE; END IF;
    END;
END;
$$;

ROLLBACK;
