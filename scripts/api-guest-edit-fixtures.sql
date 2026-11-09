INSERT INTO activities (
    id, title, date, start_time, end_time, goal, location, responsible, department
)
VALUES (
    'e0000000-0000-0000-0000-000000000001',
    'Expired guest edit fixture',
    CURRENT_DATE,
    '14:00',
    '15:00',
    '',
    'Test',
    ARRAY[]::TEXT[],
    'Pfadi'
)
ON CONFLICT (id) DO NOTHING;

INSERT INTO activity_guest_edit_links (activity_id, token_hash, expires_at)
VALUES (
    'e0000000-0000-0000-0000-000000000001',
    encode(digest(repeat('e', 64), 'sha256'), 'hex'),
    NOW()
)
ON CONFLICT (activity_id) DO UPDATE
SET token_hash = EXCLUDED.token_hash, expires_at = EXCLUDED.expires_at;
