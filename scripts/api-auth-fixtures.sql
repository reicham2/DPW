INSERT INTO roles (name, color, sort_order)
SELECT '__test_missing_permissions__', '#6b7280', COALESCE(MAX(sort_order), -1) + 1
FROM roles
HAVING NOT EXISTS (SELECT 1 FROM roles WHERE name = '__test_missing_permissions__');

INSERT INTO users (id, microsoft_oid, email, display_name, department, role)
VALUES (
    'f0000000-0000-0000-0000-000000000001',
    'oid-test-missing-permissions',
    'missing-permissions@example.invalid',
    'Test Missing Permissions',
    'Pfadi',
    '__test_missing_permissions__'
)
ON CONFLICT (id) DO NOTHING;

UPDATE app_settings
SET value_text = 'http://127.0.0.1:8000'
WHERE key = 'frontend.public_base_url' AND COALESCE(value_text, '') = '';
