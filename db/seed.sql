-- ============================================================================
-- Seed-Daten für Entwicklung & Testing
-- Ausführen mit: make db-seed
-- ============================================================================

-- ── Stufen & Rollen ─────────────────────────────────────────────────────────
INSERT INTO departments (name, color) VALUES
    ('Leiter', '#065f46'),
    ('Pio',    '#6b7280'),
    ('Pfadi',  '#1e40af'),
    ('Wölfe',  '#92400e'),
    ('Biber',  '#7c3aed')
ON CONFLICT (name) DO NOTHING;

UPDATE roles SET sort_order = 4 WHERE name = 'Mitglied';

INSERT INTO roles (name, color, sort_order) VALUES
    ('Stufenleiter',  '#1e40af', 1),
    ('Leiter',        '#065f46', 2),
    ('Pio',           '#6b7280', 3)
ON CONFLICT (name) DO NOTHING;

INSERT INTO role_permissions (role, can_read_own_dept, can_write_own_dept, can_read_all_depts, can_write_all_depts, mail_send_scope, mail_templates_scope, user_dept_scope, user_role_scope) VALUES
    ('Stufenleiter',  true, true, false, false, 'same_dept', 'own_dept', 'own_dept', 'own_dept'),
    ('Leiter',        true, true, false, false, 'own',       'none',     'none',     'none'),
    ('Pio',           true, false, false, false, 'none',     'none',     'none',     'none')
ON CONFLICT (role) DO NOTHING;

INSERT INTO mail_templates (department, subject, body, recipients) VALUES
    ('Leiter', '', '', '{}'),
    ('Pio',    '', '', '{}'),
    ('Pfadi',  '', '', '{}'),
    ('Wölfe',  '', '', '{}'),
    ('Biber',  '', '', '{}')
ON CONFLICT (department) DO NOTHING;

-- ── Test-Benutzer ───────────────────────────────────────────────────────────
INSERT INTO users (id, microsoft_oid, email, display_name, department, role) VALUES
    ('a0000000-0000-0000-0000-000000000001', 'oid-admin',        'admin@pfadihue.ch',     'Admin User',       'Leiter', 'admin'),
    ('a0000000-0000-0000-0000-000000000002', 'oid-stufenleiter', 'stufenleiter@pfadihue.ch', 'Stufen Leiter', 'Pfadi',  'Stufenleiter'),
    ('a0000000-0000-0000-0000-000000000003', 'oid-leiter1',      'leiter1@pfadihue.ch',   'Leiter Eins',      'Pfadi',  'Leiter'),
    ('a0000000-0000-0000-0000-000000000004', 'oid-leiter2',      'leiter2@pfadihue.ch',   'Leiter Zwei',      'Wölfe',  'Leiter'),
    ('a0000000-0000-0000-0000-000000000005', 'oid-pio1',         'pio1@pfadihue.ch',      'Pio Eins',         'Pio',    'Pio'),
    ('a0000000-0000-0000-0000-000000000006', 'oid-leiter3',      'leiter3@pfadihue.ch',   'Leiter Drei',      'Biber',  'Leiter')
ON CONFLICT (microsoft_oid) DO NOTHING;

-- ── Test-Aktivitäten ────────────────────────────────────────────────────────
INSERT INTO activities (id, title, date, start_time, end_time, goal, location, responsible, department, material, siko_text, bad_weather_info) VALUES

    -- Pfadi-Aktivität (vergangen)
    ('b0000000-0000-0000-0000-000000000001',
     'Geländespiel im Wald',
     CURRENT_DATE - INTERVAL '7 days',
     '14:00', '17:00',
     'Orientierung und Teamwork üben',
     'Waldlichtung Hüttenberg',
     ARRAY['Leiter Eins', 'Stufen Leiter'],
     'Pfadi',
     '[{"name": "Karte 1:25000", "responsible": ["Leiter Eins"]}, {"name": "Kompass (5x)", "responsible": []}, {"name": "Posten-Blätter", "responsible": ["Stufen Leiter"]}]'::jsonb,
     NULL,
     'Aktivität findet im Pfadiheim statt mit Indoor-Posten'),

    -- Pfadi-Aktivität (heute)
    ('b0000000-0000-0000-0000-000000000002',
     'Pioniertechnik: Seilbrücke',
     CURRENT_DATE,
     '13:30', '17:00',
     'Seilbrücke bauen und Knoten repetieren',
     'Pfadiheim Hüetli',
     ARRAY['Leiter Eins'],
     'Pfadi',
     '[{"name": "Seil 20m (3x)", "responsible": ["Leiter Eins"]}, {"name": "Blachen (4x)", "responsible": []}, {"name": "Zeltmaterial", "responsible": ["Stufen Leiter"]}]'::jsonb,
     'Nächstes Spital: Kantonsspital, 10 Min. Kontakt: 044 123 45 67. Notfallnummern bei Leitung.',
     NULL),

    -- Wölfe-Aktivität (nächste Woche)
    ('b0000000-0000-0000-0000-000000000003',
     'Schatzsuche',
     CURRENT_DATE + INTERVAL '5 days',
     '14:00', '16:30',
     'Spielerisch die Umgebung erkunden',
     'Schulhaus Dorf',
     ARRAY['Leiter Zwei'],
     'Wölfe',
     '[{"name": "Schatzkiste", "responsible": ["Leiter Zwei"]}, {"name": "Hinweis-Zettel (20x)", "responsible": []}]'::jsonb,
     NULL,
     'Schatzsuche wird im Schulhaus durchgeführt'),

    -- Pio-Aktivität (nächste Woche)
    ('b0000000-0000-0000-0000-000000000004',
     'Biwak-Vorbereitung',
     CURRENT_DATE + INTERVAL '10 days',
     '10:00', '16:00',
     'Material für Biwak zusammenstellen und Menüplanung',
     'Pfadiheim Hüetli',
     ARRAY['Pio Eins'],
     'Pio',
     '[{"name": "Kocher (2x)", "responsible": ["Pio Eins"]}, {"name": "Pfannen-Set", "responsible": []}, {"name": "Lebensmittel-Liste", "responsible": ["Pio Eins"]}]'::jsonb,
     NULL,
     NULL),

    -- Biber-Aktivität (in 2 Wochen)
    ('b0000000-0000-0000-0000-000000000005',
     'Bastelnachmittag',
     CURRENT_DATE + INTERVAL '14 days',
     '14:00', '16:00',
     'Kreativität fördern und Spass haben',
     'Gemeindesaal',
     ARRAY['Leiter Drei'],
     'Biber',
     '[{"name": "Bastelpapier", "responsible": []}, {"name": "Schere (10x)", "responsible": ["Leiter Drei"]}, {"name": "Leim", "responsible": []}]'::jsonb,
     NULL,
     NULL),

    -- Leiter-Aktivität (ohne Material)
    ('b0000000-0000-0000-0000-000000000006',
     'Leiterbesprechung Q3',
     CURRENT_DATE + INTERVAL '3 days',
     '19:00', '21:00',
     'Quartalsplanung und Rückblick',
     'Pfadiheim Hüetli',
     ARRAY['Admin User', 'Stufen Leiter'],
     'Leiter',
     '[]'::jsonb,
     NULL,
     NULL);

-- ── Programm-Einträge ───────────────────────────────────────────────────────

-- Programme für "Geländespiel im Wald"
INSERT INTO programs (activity_id, time, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000001', '14:00', 'Einstieg',          'Begrüssung und Erklärung der Regeln',         ARRAY['Leiter Eins']),
    ('b0000000-0000-0000-0000-000000000001', '14:15', 'Geländespiel',      'Posten im Wald ablaufen mit Karte & Kompass',  ARRAY['Stufen Leiter']),
    ('b0000000-0000-0000-0000-000000000001', '16:00', 'Auswertung',        'Punkte zählen und Sieger küren',               ARRAY['Leiter Eins']),
    ('b0000000-0000-0000-0000-000000000001', '16:30', 'Abschluss',         'Zvieri und Aufräumen',                         ARRAY['Stufen Leiter']);

-- Programme für "Pioniertechnik: Seilbrücke"
INSERT INTO programs (activity_id, time, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000002', '13:30', 'Knoten-Repetition', 'Mastwurf, Kreuzknoten, Achterknoten üben',     ARRAY['Leiter Eins']),
    ('b0000000-0000-0000-0000-000000000002', '14:15', 'Brücke planen',     'Standort wählen und Skizze zeichnen',          ARRAY['Leiter Eins']),
    ('b0000000-0000-0000-0000-000000000002', '14:45', 'Brücke bauen',      'Seilbrücke aufbauen in Gruppen',               ARRAY['Leiter Eins']),
    ('b0000000-0000-0000-0000-000000000002', '16:30', 'Überquerung & Abbau', 'Jede Gruppe überquert, danach Abbau',        ARRAY['Leiter Eins']);

-- Programme für "Schatzsuche"
INSERT INTO programs (activity_id, time, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000003', '14:00', 'Einführung',        'Geschichte erzählen und Gruppen bilden',        ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000003', '14:20', 'Schatzsuche',       'Hinweise suchen und Rätsel lösen',             ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000003', '15:45', 'Schatz öffnen',     'Gemeinsam den Schatz öffnen',                  ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000003', '16:00', 'Zvieri',            'Zvieri und Verabschiedung',                    ARRAY['Leiter Zwei']);

-- Mail-Templates mit Beispielinhalt
UPDATE mail_templates SET
    subject = 'Einladung zur Pfadi-Aktivität: {{titel}}',
    body = '<p>Liebe Eltern</p><p>Wir laden herzlich zur nächsten Aktivität ein:</p><p>📅 Datum: {{datum}}<br>🕐 Zeit: {{startzeit}} – {{endzeit}}<br>📍 Ort: {{ort}}</p><p>Ziel: {{ziel}}</p><p>Bei Schlechtwetter: {{schlechtwetter}}</p><p>Freundliche Grüsse<br>{{absender_name}}<br>{{absender_email}}</p>',
    recipients = ARRAY['eltern-pfadi@pfadihue.ch']
WHERE department = 'Pfadi';

UPDATE mail_templates SET
    subject = 'Wölfe-Aktivität: {{titel}}',
    body = '<p>Liebe Eltern</p><p>Die nächste Wölfe-Aktivität steht an:</p><p>📅 {{datum_kurz}}<br>🕐 {{startzeit}} – {{endzeit}}<br>📍 {{ort}}</p><p>Mitbringen: wetterfeste Kleidung, Zvieri</p><p>Bis bald!<br>Das Wölfe-Team<br>{{absender_name}}</p>',
    recipients = ARRAY['eltern-woelfe@pfadihue.ch']
WHERE department = 'Wölfe';
