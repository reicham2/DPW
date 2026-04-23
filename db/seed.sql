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

UPDATE departments
SET midata_group_id = CASE name
    WHEN 'Biber' THEN '6512'
    WHEN 'Pfadi' THEN '6513'
    WHEN 'Pio' THEN '6534'
    WHEN 'Wölfe' THEN '6529'
    ELSE midata_group_id
END
WHERE name IN ('Biber', 'Pfadi', 'Pio', 'Wölfe');

UPDATE roles SET sort_order = 4 WHERE name = 'Mitglied';

INSERT INTO roles (name, color, sort_order) VALUES
    ('Stufenleiter',  '#1e40af', 1),
    ('Leiter',        '#065f46', 2),
    ('Pio',           '#6b7280', 3)
ON CONFLICT (name) DO NOTHING;

INSERT INTO role_permissions (role, can_read_own_dept, can_write_own_dept, can_read_all_depts, can_write_all_depts, activity_read_scope, activity_create_scope, activity_edit_scope, mail_send_scope, mail_templates_scope, event_templates_scope, user_dept_scope, user_role_scope, locations_manage_scope) VALUES
    ('Stufenleiter',  true, true, false, false, 'same_dept', 'own_dept', 'same_dept', 'same_dept', 'own_dept', 'own_dept', 'own_dept', 'own_dept', 'none'),
    ('Leiter',        true, true, false, false, 'same_dept', 'own_dept', 'own',       'own',       'none',     'none',     'none',     'none',     'none'),
    ('Pio',           true, false, false, false, 'same_dept', 'none',     'none',      'none',      'none',     'none',     'none',     'none',     'none')
ON CONFLICT (role) DO NOTHING;

INSERT INTO event_templates (department, title, body) VALUES
    ('Leiter', '', ''),
    ('Pio',    '', ''),
    ('Pfadi',  '', ''),
    ('Wölfe',  '', ''),
    ('Biber',  '', '')
ON CONFLICT (department) DO NOTHING;

INSERT INTO mail_templates (department, subject, body, recipients, cc) VALUES
    ('Leiter', '', '', '{}', '{}'),
    ('Pio',    '', '', '{}', '{}'),
    ('Pfadi',  '', '', '{}', '{}'),
    ('Wölfe',  '', '', '{}', '{}'),
    ('Biber',  '', '', '{}', '{}')
ON CONFLICT (department) DO NOTHING;

-- ── Test-Benutzer ───────────────────────────────────────────────────────────
INSERT INTO users (id, microsoft_oid, email, display_name, department, role) VALUES
    ('a0000000-0000-0000-0000-000000000001', 'oid-admin',        'development@pfadihue.ch',     'Admin User',       'Leiter', 'admin'),
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
     NULL),

    -- Pfadi: Wanderung (in 4 Tagen)
    ('b0000000-0000-0000-0000-000000000007',
     'Wanderung Säntis',
     CURRENT_DATE + INTERVAL '4 days',
     '08:00', '16:00',
     'Alpine Tour und Naturkunde',
     'Appenzell, Säntis',
     ARRAY['Stufen Leiter'],
     'Pfadi',
     '[{"name": "Topomaps", "responsible": ["Stufen Leiter"]}, {"name": "Erste-Hilfe-Rucksack", "responsible": []}, {"name": "Proviant für 8h", "responsible": ["Leiter Eins"]}]'::jsonb,
     'Sanitäter: Stufen Leiter (Handy stets erreichbar). Notfall Alpine Rettung: 1414',
     'Alternativ: Schloss Appenzell besichtiging'),

    -- Wölfe: Kochkurs (in 6 Tagen)
    ('b0000000-0000-0000-0000-000000000008',
     'Pizza selber backen',
     CURRENT_DATE + INTERVAL '6 days',
     '15:00', '18:30',
     'Teig kneten und Pizza belegen',
     'Schulhaus Dorf',
     ARRAY['Leiter Zwei'],
     'Wölfe',
     '[{"name": "Mehl (5kg)", "responsible": ["Leiter Zwei"]}, {"name": "Belag (Käse, Tomaten, Salami)", "responsible": ["Leiter Zwei"]}, {"name": "Pizzaschaufel (3x)", "responsible": []}]'::jsonb,
     NULL,
     'Backofen im Schulhaus Küche'),

    -- Biber: Badetag (in 8 Tagen)
    ('b0000000-0000-0000-0000-000000000009',
     'Badetag im Freibad',
     CURRENT_DATE + INTERVAL '8 days',
     '14:00', '16:30',
     'Schwimmen und Plantschen lernen',
     'Freibad Grünwald',
     ARRAY['Leiter Drei'],
     'Biber',
     '[{"name": "Schwimmleuchten (10x)", "responsible": []}, {"name": "Schwimmflügel-Set", "responsible": ["Leiter Drei"]}, {"name": "Mikrofon Badeverbot-Info", "responsible": []}]'::jsonb,
     'Bademeister vor Ort, Erste Hilfe durch Leiter Drei',
     'Nur bei Schönwetter'),

    -- Pio: Felsklettern (in 9 Tagen)
    ('b0000000-0000-0000-0000-000000000010',
     'Felsklettern: Seile und Sicherung',
     CURRENT_DATE + INTERVAL '9 days',
     '09:00', '17:00',
     'Klettertechniken und Sicherheit trainieren',
     'Alpstein',
     ARRAY['Pio Eins'],
     'Pio',
     '[{"name": "Klettergurt (4x)", "responsible": ["Pio Eins"]}, {"name": "Kletterseile 50m", "responsible": ["Pio Eins"]}, {"name": "Karabiner-Set", "responsible": []}, {"name": "Sturzpuffer", "responsible": []}]'::jsonb,
     'Seilschaft max 2 Personen. Notfall: Bergwacht 140. Leiter Pio Eins begleitet.',
     'Nur bei stabiler Wetterlage'),

    -- Pfadi: Übernachtung im Tipi (in 11 Tagen)
    ('b0000000-0000-0000-0000-000000000011',
     'Tipi-Nacht',
     CURRENT_DATE + INTERVAL '11 days',
     '17:00', '09:00',
     'Draußen übernachten und Lagerfeuer genießen',
     'Naturlehrgebiet Eggenberg',
     ARRAY['Leiter Eins', 'Stufen Leiter'],
     'Pfadi',
     '[{"name": "Tipis (2x)", "responsible": ["Leiter Eins"]}, {"name": "Schlafsäcke (8x)", "responsible": []}, {"name": "Isomatten (10x)", "responsible": ["Stufen Leiter"]}, {"name": "Brennholz", "responsible": []}, {"name": "Grill & Töpfe", "responsible": ["Leiter Eins"]}]'::jsonb,
     'Nachtbereitschaft: Leiter Eins hat GPS-Tracker. Notfallnummer vor Ort ausgehängt.',
     'Bei Gewitter: Übernachtung im Pfadiheim alternate'),

    -- Wölfe & Pfadi gemeinsam: Waldspiele (in 12 Tagen, LOCATION OVERLAP TEST 1)
    ('b0000000-0000-0000-0000-000000000012',
     'Waldspiele kombiniert (Pfadi & Wölfe)',
     CURRENT_DATE + INTERVAL '12 days',
     '10:00', '12:30',
     'Gemeinsame Waldspiele und Verbandsgeist',
     'Waldlichtung Hüttenberg',
     ARRAY['Stufen Leiter', 'Leiter Zwei'],
     'Pfadi',
     '[{"name": "Fahnen (4x)", "responsible": ["Stufen Leiter"]}, {"name": "Pfeifen (3x)", "responsible": ["Leiter Zwei"]}, {"name": "Urkunden", "responsible": []}]'::jsonb,
     NULL,
     NULL),

    -- Wölfe: Nachmittagstraining (in 12 Tagen, LOCATION OVERLAP TEST 2 - gleiche Zeit + Ort, andere Abteilung)
    ('b0000000-0000-0000-0000-000000000013',
     'Wölfe-Nachmittagstraining',
     CURRENT_DATE + INTERVAL '12 days',
     '10:00', '12:30',
     'Gerätetraining auf dem Sportplatz',
     'Waldlichtung Hüttenberg',
     ARRAY['Leiter Zwei'],
     'Wölfe',
     '[{"name": "Springseile (5x)", "responsible": []}, {"name": "Hütchen (20x)", "responsible": ["Leiter Zwei"]}, {"name": "Fußbälle (3x)", "responsible": []}]'::jsonb,
     NULL,
    'Zeitgleich mit Pfadi-Waldspiel – koordinierte Aktivitäten'),

    -- Vergangene Aktivität ohne Wetter-Snapshot (Freeze-Fehlerfall testbar)
    ('b0000000-0000-0000-0000-000000000014',
    'Rückblickabend Biber',
    CURRENT_DATE - INTERVAL '20 days',
    '18:30', '20:00',
    'Abzeichen reflektieren und Fotos schauen',
    'Pfadiheim Hüetli',
    ARRAY['Leiter Drei'],
    'Biber',
    '[]'::jsonb,
    NULL,
    'Bei Bedarf wird auf Bastelraum ausgewichen');

-- ── Programm-Einträge ───────────────────────────────────────────────────────

-- Programme für "Geländespiel im Wald" (14:00–17:00)
INSERT INTO programs (activity_id, duration_minutes, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000001',  15, 'Einstieg',          'Begrüssung und Erklärung der Regeln',         ARRAY['Leiter Eins']),
    ('b0000000-0000-0000-0000-000000000001', 105, 'Geländespiel',      'Posten im Wald ablaufen mit Karte & Kompass',  ARRAY['Stufen Leiter']),
    ('b0000000-0000-0000-0000-000000000001',  30, 'Auswertung',        'Punkte zählen und Sieger küren',               ARRAY['Leiter Eins']),
    ('b0000000-0000-0000-0000-000000000001',  30, 'Abschluss',         'Zvieri und Aufräumen',                         ARRAY['Stufen Leiter']);

-- Programme für "Pioniertechnik: Seilbrücke" (13:30–17:00)
INSERT INTO programs (activity_id, duration_minutes, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000002',  45, 'Knoten-Repetition', 'Mastwurf, Kreuzknoten, Achterknoten üben',     ARRAY['Leiter Eins']),
    ('b0000000-0000-0000-0000-000000000002',  30, 'Brücke planen',     'Standort wählen und Skizze zeichnen',          ARRAY['Leiter Eins']),
    ('b0000000-0000-0000-0000-000000000002', 105, 'Brücke bauen',      'Seilbrücke aufbauen in Gruppen',               ARRAY['Leiter Eins']),
    ('b0000000-0000-0000-0000-000000000002',  30, 'Überquerung & Abbau', 'Jede Gruppe überquert, danach Abbau',        ARRAY['Leiter Eins']);

-- Programme für "Schatzsuche" (14:00–16:30)
INSERT INTO programs (activity_id, duration_minutes, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000003',  20, 'Einführung',        'Geschichte erzählen und Gruppen bilden',        ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000003',  85, 'Schatzsuche',       'Hinweise suchen und Rätsel lösen',             ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000003',  15, 'Schatz öffnen',     'Gemeinsam den Schatz öffnen',                  ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000003',  30, 'Zvieri',            'Zvieri und Verabschiedung',                    ARRAY['Leiter Zwei']);

-- Programme für "Wanderung Säntis" (08:00–16:00)
INSERT INTO programs (activity_id, duration_minutes, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000007',  60, 'Treffpunkt & Start', 'Ausrüstungskontrolle und Kurzbriefing',        ARRAY['Stufen Leiter']),
    ('b0000000-0000-0000-0000-000000000007', 180, 'Start Wanderung',   'Asphalt bis Berghütte',                        ARRAY['Stufen Leiter']),
    ('b0000000-0000-0000-0000-000000000007', 120, 'Gipfel & Picknick',  'Mittagspause mit Aussicht',                    ARRAY['Stufen Leiter']),
    ('b0000000-0000-0000-0000-000000000007', 120, 'Abstieg',           'Zurück zum Startpunkt',                        ARRAY['Stufen Leiter']),
    ('b0000000-0000-0000-0000-000000000007',   0, 'Debrief & Ende',    'Erlebnisse teilen, Abfahrt',                   ARRAY['Stufen Leiter']);

-- Programme für "Pizza selber backen" (15:00–18:30)
INSERT INTO programs (activity_id, duration_minutes, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000008',  15, 'Einstieg',          'Pizzageschichte und Teig-Zutaten erklären',    ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000008',  45, 'Teig kneten',       'Jedes Kind knetet seinen Teig',                ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000008',  30, 'Formen & Belegen',  'Pizza-Formen verzieren und belegen',           ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000008',  90, 'Backen & Essen',    'Im Ofen backen und gemeinsam essen',           ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000008',  30, 'Aufräumen & Tschüss', 'Küche putzen und bis bald',                   ARRAY['Leiter Zwei']);

-- Programme für "Badetag im Freibad" (14:00–16:30)
INSERT INTO programs (activity_id, duration_minutes, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000009',  15, 'Umzug & Belegung',  'Umzug ins Freibad, Platz suchen',              ARRAY['Leiter Drei']),
    ('b0000000-0000-0000-0000-000000000009',  30, 'Sicherheitsbriefing', 'Regeln erklären und Wassergewöhnung',        ARRAY['Leiter Drei']),
    ('b0000000-0000-0000-0000-000000000009',  60, 'Freischwimmen',    'Freies Spielen im Wasser und Becken',         ARRAY['Leiter Drei']),
    ('b0000000-0000-0000-0000-000000000009',  30, 'Flaschenbad',       'Erfrischungen und Snack am Beckenrand',        ARRAY['Leiter Drei']),
    ('b0000000-0000-0000-0000-000000000009',  15, 'Umzug & Ende',      'Zurück zur Sammelstelle',                      ARRAY['Leiter Drei']);

-- Programme für "Felsklettern: Seile und Sicherung" (09:00–17:00)
INSERT INTO programs (activity_id, duration_minutes, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000010',  60, 'Anmarsch & Setup',  'Zu Kletterplatz gehen, Ausrüstung aufbauen',   ARRAY['Pio Eins']),
    ('b0000000-0000-0000-0000-000000000010',  60, 'Theorie Knoten',    'Kletterknoten und Sicherung üben',             ARRAY['Pio Eins']),
    ('b0000000-0000-0000-0000-000000000010',  90, 'Erste Kletter',     'Beginners an easy Via mit Sicherung',          ARRAY['Pio Eins']),
    ('b0000000-0000-0000-0000-000000000010',  60, 'Mittagspause',      'Proviant und Aussicht genießen',               ARRAY['Pio Eins']),
    ('b0000000-0000-0000-0000-000000000010', 150, 'Fortgeschrittene',  'Schwierigere Routen für Erfahrene',            ARRAY['Pio Eins']),
    ('b0000000-0000-0000-0000-000000000010',  60, 'Abbau & Rückweg',   'Material packen und Debrief',                  ARRAY['Pio Eins']);

-- Programme für "Tipi-Nacht" (17:00–09:00 Folgetag)
INSERT INTO programs (activity_id, duration_minutes, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000011',  60, 'Anreise & Aufbau', 'Tipis aufbauen und Schlafplätze herrichten',  ARRAY['Leiter Eins', 'Stufen Leiter']),
    ('b0000000-0000-0000-0000-000000000011',  60, 'Feuer & Essen',    'Lagerfeuer entzünden und Abendessen kochen',   ARRAY['Leiter Eins']),
    ('b0000000-0000-0000-0000-000000000011', 120, 'Lagerfeuer-Programm', 'Geschichten, Lieder, Spiele ums Feuer',     ARRAY['Stufen Leiter']),
    ('b0000000-0000-0000-0000-000000000011', 720, 'Zubettmachen',     'Runterfahren und Schlafen gehen',              ARRAY['Leiter Eins', 'Stufen Leiter']),
    ('b0000000-0000-0000-0000-000000000011',   0, 'Frühstück & Abbau', 'Frühstück zubereiten und Tipis abbauen',      ARRAY['Stufen Leiter']);

-- Programme für "Waldspiele kombiniert (Pfadi & Wölfe)" (10:00–12:30)
INSERT INTO programs (activity_id, duration_minutes, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000012',  15, 'Begrüssung',       'Verbands- und Gruppengeist aktivieren',        ARRAY['Stufen Leiter']),
    ('b0000000-0000-0000-0000-000000000012',  75, 'Gemischte Spiele', 'Alters-Mix: Staffeln und Teamspiele',          ARRAY['Stufen Leiter', 'Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000012',  30, 'Prämierung',       'Sieger küren und Urkunden verteilen',          ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000012',  30, 'Zvieri & Ende',    'Gemeinsamer Zvieri',                           ARRAY['Stufen Leiter']);

-- Programme für "Wölfe-Nachmittagstraining" (10:00–12:30)
INSERT INTO programs (activity_id, duration_minutes, title, description, responsible) VALUES
    ('b0000000-0000-0000-0000-000000000013',  15, 'Aufwärmspiel',     'Fangis und Ballgewöhnung',                     ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000013',  45, 'Koordinations-Parcours', 'Hütchen-Slalom und Sprünge',             ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000013',  45, 'Freispiel & Tore', 'Mannschaftsspiele auf Kleinfeldern',           ARRAY['Leiter Zwei']),
    ('b0000000-0000-0000-0000-000000000013',  45, 'Cool-down',        'Dehnübungen und Abklatschen',                  ARRAY['Leiter Zwei']);

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

-- ── Formular-Vorlagen ───────────────────────────────────────────────────────

-- Pfadi: Standard-Anmeldung (is_default = true)
INSERT INTO form_templates (id, name, department, form_type, is_default, template_config, created_by) VALUES
    ('c0000000-0000-0000-0000-000000000001',
     'Standard-Anmeldung Pfadi',
     'Pfadi',
     'registration',
     true,
     '[
        {"question_text": "Angaben zum Kind", "question_type": "section", "position": 0, "is_required": false, "metadata": {"subtitle": "Bitte vollständig ausfüllen"}},
        {"question_text": "Vorname & Name", "question_type": "text_input", "position": 1, "is_required": true, "metadata": {}},
        {"question_text": "Pfadiname", "question_type": "text_input", "position": 2, "is_required": false, "metadata": {}},
        {"question_text": "T-Shirt Grösse", "question_type": "dropdown", "position": 3, "is_required": true, "metadata": {"choices": [{"id": "xs", "label": "XS"}, {"id": "s", "label": "S"}, {"id": "m", "label": "M"}, {"id": "l", "label": "L"}, {"id": "xl", "label": "XL"}]}},
        {"question_text": "Kontakt", "question_type": "section", "position": 4, "is_required": false, "metadata": {"subtitle": "Erreichbare Person während der Aktivität"}},
        {"question_text": "Telefon Eltern", "question_type": "text_input", "position": 5, "is_required": true, "metadata": {}},
        {"question_text": "E-Mail", "question_type": "text_input", "position": 6, "is_required": true, "metadata": {}},
        {"question_text": "Verpflegung", "question_type": "single_choice", "position": 7, "is_required": true, "metadata": {"choices": [{"id": "normal", "label": "Normal"}, {"id": "vegetarisch", "label": "Vegetarisch"}, {"id": "vegan", "label": "Vegan"}, {"id": "laktosefrei", "label": "Laktosefrei"}]}},
        {"question_text": "Allergien / Unverträglichkeiten", "question_type": "text_input", "position": 8, "is_required": false, "metadata": {"multiline": true}},
        {"question_text": "Welche Aktivitäten bevorzugt dein Kind?", "question_type": "multiple_choice", "position": 9, "is_required": false, "metadata": {"choices": [{"id": "wandern", "label": "Wandern"}, {"id": "pioniertechnik", "label": "Pioniertechnik"}, {"id": "kochen", "label": "Kochen"}, {"id": "spiele", "label": "Spiele"}, {"id": "basteln", "label": "Basteln"}]}},
        {"question_text": "Bemerkungen", "question_type": "text_input", "position": 10, "is_required": false, "metadata": {"multiline": true}}
     ]'::jsonb,
     'a0000000-0000-0000-0000-000000000001')
ON CONFLICT (department, form_type, name) DO NOTHING;

-- Pfadi: Abmeldung
INSERT INTO form_templates (id, name, department, form_type, is_default, template_config, created_by) VALUES
    ('c0000000-0000-0000-0000-000000000002',
     'Standard-Abmeldung Pfadi',
     'Pfadi',
     'deregistration',
     false,
     '[
        {"question_text": "Abmeldung", "question_type": "section", "position": 0, "is_required": false, "metadata": {"subtitle": "Bitte den Grund angeben"}},
        {"question_text": "Vorname & Name", "question_type": "text_input", "position": 1, "is_required": true, "metadata": {}},
        {"question_text": "Grund der Abmeldung", "question_type": "dropdown", "position": 2, "is_required": true, "metadata": {"choices": [{"id": "krank", "label": "Krankheit"}, {"id": "ferien", "label": "Ferien"}, {"id": "andere", "label": "Andere Verpflichtung"}, {"id": "sonstiges", "label": "Sonstiges"}]}},
        {"question_text": "Details", "question_type": "text_input", "position": 3, "is_required": false, "metadata": {"multiline": true}}
     ]'::jsonb,
     'a0000000-0000-0000-0000-000000000001')
ON CONFLICT (department, form_type, name) DO NOTHING;

-- Wölfe: Standard-Anmeldung (is_default = true)
INSERT INTO form_templates (id, name, department, form_type, is_default, template_config, created_by) VALUES
    ('c0000000-0000-0000-0000-000000000003',
     'Standard-Anmeldung Wölfe',
     'Wölfe',
     'registration',
     true,
     '[
        {"question_text": "Angaben zum Wolf", "question_type": "section", "position": 0, "is_required": false, "metadata": {"subtitle": ""}},
        {"question_text": "Vorname & Name", "question_type": "text_input", "position": 1, "is_required": true, "metadata": {}},
        {"question_text": "Geburtsdatum", "question_type": "text_input", "position": 2, "is_required": true, "metadata": {}},
        {"question_text": "Liebste Farbe", "question_type": "dropdown", "position": 3, "is_required": false, "metadata": {"choices": [{"id": "rot", "label": "Rot"}, {"id": "blau", "label": "Blau"}, {"id": "gruen", "label": "Grün"}, {"id": "gelb", "label": "Gelb"}]}},
        {"question_text": "Kontakt Eltern", "question_type": "section", "position": 4, "is_required": false, "metadata": {}},
        {"question_text": "Telefon", "question_type": "text_input", "position": 5, "is_required": true, "metadata": {}},
        {"question_text": "Essgewohnheiten", "question_type": "single_choice", "position": 6, "is_required": true, "metadata": {"choices": [{"id": "alles", "label": "Isst alles"}, {"id": "vegetarisch", "label": "Vegetarisch"}, {"id": "other", "label": "Anderes (bitte angeben)"}]}},
        {"question_text": "Besonderes", "question_type": "text_input", "position": 7, "is_required": false, "metadata": {"multiline": true}}
     ]'::jsonb,
     'a0000000-0000-0000-0000-000000000001')
ON CONFLICT (department, form_type, name) DO NOTHING;

-- Pio: Standard-Anmeldung (is_default = true)
INSERT INTO form_templates (id, name, department, form_type, is_default, template_config, created_by) VALUES
    ('c0000000-0000-0000-0000-000000000004',
     'Standard-Anmeldung Pio',
     'Pio',
     'registration',
     true,
     '[
        {"question_text": "Persönliche Angaben", "question_type": "section", "position": 0, "is_required": false, "metadata": {"subtitle": "Für den Biwak"}},
        {"question_text": "Vorname & Name", "question_type": "text_input", "position": 1, "is_required": true, "metadata": {}},
        {"question_text": "Alter", "question_type": "text_input", "position": 2, "is_required": true, "metadata": {}},
        {"question_text": "Erfahrung Outdoor", "question_type": "dropdown", "position": 3, "is_required": true, "metadata": {"choices": [{"id": "keine", "label": "Keine"}, {"id": "wenig", "label": "Wenig"}, {"id": "mittel", "label": "Mittel"}, {"id": "viel", "label": "Viel"}]}},
        {"question_text": "Transportmittel zum Treffpunkt", "question_type": "single_choice", "position": 4, "is_required": true, "metadata": {"choices": [{"id": "selbst", "label": "Komme selbst"}, {"id": "eltern", "label": "Werde gebracht"}, {"id": "oev", "label": "ÖV"}]}},
        {"question_text": "Was möchtest du lernen?", "question_type": "multiple_choice", "position": 5, "is_required": false, "metadata": {"choices": [{"id": "feuer", "label": "Feuermachen"}, {"id": "orientierung", "label": "Orientierung"}, {"id": "seiltech", "label": "Seiltechnik"}, {"id": "kochen", "label": "Outdoor-Kochen"}, {"id": "erste_hilfe", "label": "Erste Hilfe"}]}},
        {"question_text": "Medizinische Hinweise", "question_type": "text_input", "position": 6, "is_required": false, "metadata": {"multiline": true}},
        {"question_text": "Notfallkontakt Telefon", "question_type": "text_input", "position": 7, "is_required": true, "metadata": {}}
     ]'::jsonb,
     'a0000000-0000-0000-0000-000000000001')
ON CONFLICT (department, form_type, name) DO NOTHING;

-- Biber: Standard-Anmeldung (is_default = true)
INSERT INTO form_templates (id, name, department, form_type, is_default, template_config, created_by) VALUES
    ('c0000000-0000-0000-0000-000000000005',
     'Standard-Anmeldung Biber',
     'Biber',
     'registration',
     true,
     '[
        {"question_text": "Angaben zum Biber", "question_type": "section", "position": 0, "is_required": false, "metadata": {"subtitle": "Wird von den Eltern ausgefüllt"}},
        {"question_text": "Vorname & Name des Kindes", "question_type": "text_input", "position": 1, "is_required": true, "metadata": {}},
        {"question_text": "Geburtsdatum", "question_type": "text_input", "position": 2, "is_required": true, "metadata": {}},
        {"question_text": "Name Elternteil", "question_type": "text_input", "position": 3, "is_required": true, "metadata": {}},
        {"question_text": "Telefon", "question_type": "text_input", "position": 4, "is_required": true, "metadata": {}},
        {"question_text": "Darf fotografiert werden?", "question_type": "single_choice", "position": 5, "is_required": true, "metadata": {"choices": [{"id": "ja", "label": "Ja"}, {"id": "nein", "label": "Nein"}]}},
        {"question_text": "Bemerkungen", "question_type": "text_input", "position": 6, "is_required": false, "metadata": {"multiline": true}}
     ]'::jsonb,
     'a0000000-0000-0000-0000-000000000001')
ON CONFLICT (department, form_type, name) DO NOTHING;

-- Leiter: Lager-Anmeldung (kein Standard)
INSERT INTO form_templates (id, name, department, form_type, is_default, template_config, created_by) VALUES
    ('c0000000-0000-0000-0000-000000000006',
     'Lager-Anmeldung Leiter',
     'Leiter',
     'registration',
     false,
     '[
        {"question_text": "Lager-Anmeldung", "question_type": "section", "position": 0, "is_required": false, "metadata": {"subtitle": "Sommerlager 2026"}},
        {"question_text": "Vorname & Name", "question_type": "text_input", "position": 1, "is_required": true, "metadata": {}},
        {"question_text": "Verfügbarkeit", "question_type": "dropdown", "position": 2, "is_required": true, "metadata": {"choices": [{"id": "ganz", "label": "Ganzes Lager"}, {"id": "erste_haelfte", "label": "Erste Hälfte"}, {"id": "zweite_haelfte", "label": "Zweite Hälfte"}, {"id": "tageweise", "label": "Nur tageweise"}]}},
        {"question_text": "Funktion im Lager", "question_type": "single_choice", "position": 3, "is_required": true, "metadata": {"choices": [{"id": "lagerleitung", "label": "Lagerleitung"}, {"id": "kuechenchef", "label": "Küchenchef"}, {"id": "leiter", "label": "Leiter/in"}, {"id": "helfer", "label": "Helfer/in"}]}},
        {"question_text": "Kursbestätigungen", "question_type": "multiple_choice", "position": 4, "is_required": false, "metadata": {"choices": [{"id": "glk", "label": "GLK"}, {"id": "slk", "label": "SLK"}, {"id": "pano", "label": "Panorama"}, {"id": "lpk", "label": "LPK"}, {"id": "coach", "label": "Coach-Kurs"}]}},
        {"question_text": "Bemerkungen", "question_type": "text_input", "position": 5, "is_required": false, "metadata": {"multiline": true}}
     ]'::jsonb,
     'a0000000-0000-0000-0000-000000000001')
ON CONFLICT (department, form_type, name) DO NOTHING;

-- ── Test-Formulare für bestehende Aktivitäten ───────────────────────────────

-- Planwerte für Teilnehmende (neues Feld planned_participants_estimate)
UPDATE activities
SET planned_participants_estimate = CASE id
    WHEN 'b0000000-0000-0000-0000-000000000001' THEN 18
    WHEN 'b0000000-0000-0000-0000-000000000002' THEN 24
    WHEN 'b0000000-0000-0000-0000-000000000003' THEN 20
    WHEN 'b0000000-0000-0000-0000-000000000004' THEN 12
    WHEN 'b0000000-0000-0000-0000-000000000005' THEN 16
    WHEN 'b0000000-0000-0000-0000-000000000006' THEN 9
    WHEN 'b0000000-0000-0000-0000-000000000007' THEN 22
    WHEN 'b0000000-0000-0000-0000-000000000008' THEN 15
    WHEN 'b0000000-0000-0000-0000-000000000009' THEN 14
    WHEN 'b0000000-0000-0000-0000-000000000010' THEN 10
    WHEN 'b0000000-0000-0000-0000-000000000011' THEN 19
    WHEN 'b0000000-0000-0000-0000-000000000012' THEN 26
    WHEN 'b0000000-0000-0000-0000-000000000013' THEN 21
    WHEN 'b0000000-0000-0000-0000-000000000014' THEN 11
    ELSE planned_participants_estimate
END
WHERE id IN (
    'b0000000-0000-0000-0000-000000000001',
    'b0000000-0000-0000-0000-000000000002',
    'b0000000-0000-0000-0000-000000000003',
    'b0000000-0000-0000-0000-000000000004',
    'b0000000-0000-0000-0000-000000000005',
    'b0000000-0000-0000-0000-000000000006',
    'b0000000-0000-0000-0000-000000000007',
    'b0000000-0000-0000-0000-000000000008',
    'b0000000-0000-0000-0000-000000000009',
    'b0000000-0000-0000-0000-000000000010',
    'b0000000-0000-0000-0000-000000000011',
    'b0000000-0000-0000-0000-000000000012',
    'b0000000-0000-0000-0000-000000000013',
    'b0000000-0000-0000-0000-000000000014'
);

-- Persistierte Midata-/Wetter-Snapshots für Freeze-Tests
-- Aktivität 001: vollständiger Snapshot vorhanden (soll nach Aktivitätstag fix angezeigt werden)
UPDATE activities
SET
    midata_children_value = 27,
    midata_children_recorded_at = NOW() - INTERVAL '2 days',
    weather_snapshot = '{
        "available": true,
        "mode": "forecast",
        "temperature_c": 14.8,
        "season": null,
        "point_name": "Zug, Neustadt",
        "postal_code": "6300",
        "source": "meteoswiss",
        "note": "Seed snapshot for frozen-weather test"
    }'::jsonb,
    weather_recorded_at = NOW() - INTERVAL '2 days'
WHERE id = 'b0000000-0000-0000-0000-000000000001';

-- Aktivität 014: nur Midata-Snapshot vorhanden (Wetter fehlt absichtlich)
UPDATE activities
SET
    midata_children_value = 12,
    midata_children_recorded_at = NOW() - INTERVAL '15 days',
    weather_location = '6300 Zug',
    weather_snapshot = NULL,
    weather_recorded_at = NULL
WHERE id = 'b0000000-0000-0000-0000-000000000014';

UPDATE activities
SET weather_location = '9050 Appenzell'
WHERE id = 'b0000000-0000-0000-0000-000000000007';

-- Testformular + Antworten für Statistik (Anmeldungen / Abmeldungen)
-- Ziel: Activity-Form-Statistik liefert realistische Werte inkl. expected_current.
WITH upsert_form AS (
    INSERT INTO signup_forms (id, activity_id, public_slug, form_type, title, created_by)
    VALUES (
        'd0000000-0000-0000-0000-000000000001',
        'b0000000-0000-0000-0000-000000000002',
        'pfadi-seilbruecke-test',
        'registration',
        'An-/Abmeldung Seilbruecke (Seed)',
        'a0000000-0000-0000-0000-000000000001'
    )
    ON CONFLICT (activity_id) DO UPDATE
    SET form_type = EXCLUDED.form_type,
        title = EXCLUDED.title,
        created_by = EXCLUDED.created_by,
        updated_at = NOW()
    RETURNING id
),
target_form AS (
    SELECT id FROM upsert_form
    UNION ALL
    SELECT id FROM signup_forms WHERE activity_id = 'b0000000-0000-0000-0000-000000000002'
    LIMIT 1
)
INSERT INTO form_questions (id, form_id, question_text, question_type, position, is_required, metadata)
SELECT *
FROM (
    SELECT
        'd0000000-0000-0000-0000-000000000011'::uuid,
        (SELECT id FROM target_form),
        'Name Kind',
        'text_input',
        1,
        true,
        '{}'::jsonb
    UNION ALL
    SELECT
        'd0000000-0000-0000-0000-000000000012'::uuid,
        (SELECT id FROM target_form),
        'Verpflegung',
        'single_choice',
        2,
        true,
        '{"choices":[{"id":"normal","label":"Normal"},{"id":"vegetarisch","label":"Vegetarisch"},{"id":"vegan","label":"Vegan"}]}'::jsonb
    UNION ALL
    SELECT
        'd0000000-0000-0000-0000-000000000013'::uuid,
        (SELECT id FROM target_form),
        'Bemerkungen',
        'text_input',
        3,
        false,
        '{"multiline":true}'::jsonb
) q(id, form_id, question_text, question_type, position, is_required, metadata)
ON CONFLICT (id) DO UPDATE
SET question_text = EXCLUDED.question_text,
    question_type = EXCLUDED.question_type,
    position = EXCLUDED.position,
    is_required = EXCLUDED.is_required,
    metadata = EXCLUDED.metadata;

INSERT INTO form_responses (id, form_id, submission_mode, submitted_at, user_agent, ip_address)
SELECT
    r.id,
    sf.id,
    r.submission_mode,
    NOW() - r.delta,
    'seed/test-suite',
    '127.0.0.1'
FROM signup_forms sf
JOIN (
    VALUES
        ('d1000000-0000-0000-0000-000000000001'::uuid, 'registration'::text, INTERVAL '9 days'),
        ('d1000000-0000-0000-0000-000000000002'::uuid, 'registration'::text, INTERVAL '8 days'),
        ('d1000000-0000-0000-0000-000000000003'::uuid, 'registration'::text, INTERVAL '7 days'),
        ('d1000000-0000-0000-0000-000000000004'::uuid, 'registration'::text, INTERVAL '6 days'),
        ('d1000000-0000-0000-0000-000000000005'::uuid, 'registration'::text, INTERVAL '5 days'),
        ('d1000000-0000-0000-0000-000000000006'::uuid, 'registration'::text, INTERVAL '4 days'),
        ('d1000000-0000-0000-0000-000000000007'::uuid, 'registration'::text, INTERVAL '3 days'),
        ('d1000000-0000-0000-0000-000000000008'::uuid, 'registration'::text, INTERVAL '2 days'),
        ('d1000000-0000-0000-0000-000000000009'::uuid, 'registration'::text, INTERVAL '36 hours'),
        ('d1000000-0000-0000-0000-000000000010'::uuid, 'registration'::text, INTERVAL '12 hours')
    ) AS r(id, submission_mode, delta)
    ON TRUE
WHERE sf.activity_id = 'b0000000-0000-0000-0000-000000000002'
ON CONFLICT (id) DO NOTHING;

INSERT INTO response_answers (id, response_id, question_id, answer_value)
VALUES
    ('d2000000-0000-0000-0000-000000000001', 'd1000000-0000-0000-0000-000000000001', 'd0000000-0000-0000-0000-000000000011', 'Mila'),
    ('d2000000-0000-0000-0000-000000000002', 'd1000000-0000-0000-0000-000000000001', 'd0000000-0000-0000-0000-000000000012', 'normal'),
    ('d2000000-0000-0000-0000-000000000003', 'd1000000-0000-0000-0000-000000000002', 'd0000000-0000-0000-0000-000000000011', 'Noah'),
    ('d2000000-0000-0000-0000-000000000004', 'd1000000-0000-0000-0000-000000000002', 'd0000000-0000-0000-0000-000000000012', 'vegetarisch'),
    ('d2000000-0000-0000-0000-000000000005', 'd1000000-0000-0000-0000-000000000003', 'd0000000-0000-0000-0000-000000000011', 'Lina'),
    ('d2000000-0000-0000-0000-000000000006', 'd1000000-0000-0000-0000-000000000003', 'd0000000-0000-0000-0000-000000000012', 'normal'),
    ('d2000000-0000-0000-0000-000000000007', 'd1000000-0000-0000-0000-000000000004', 'd0000000-0000-0000-0000-000000000011', 'Jonas'),
    ('d2000000-0000-0000-0000-000000000008', 'd1000000-0000-0000-0000-000000000004', 'd0000000-0000-0000-0000-000000000012', 'vegan'),
    ('d2000000-0000-0000-0000-000000000009', 'd1000000-0000-0000-0000-000000000005', 'd0000000-0000-0000-0000-000000000011', 'Lea'),
    ('d2000000-0000-0000-0000-000000000010', 'd1000000-0000-0000-0000-000000000005', 'd0000000-0000-0000-0000-000000000012', 'normal'),
    ('d2000000-0000-0000-0000-000000000011', 'd1000000-0000-0000-0000-000000000006', 'd0000000-0000-0000-0000-000000000011', 'Nico'),
    ('d2000000-0000-0000-0000-000000000012', 'd1000000-0000-0000-0000-000000000006', 'd0000000-0000-0000-0000-000000000012', 'vegetarisch'),
    ('d2000000-0000-0000-0000-000000000013', 'd1000000-0000-0000-0000-000000000007', 'd0000000-0000-0000-0000-000000000011', 'Timo'),
    ('d2000000-0000-0000-0000-000000000014', 'd1000000-0000-0000-0000-000000000007', 'd0000000-0000-0000-0000-000000000012', 'normal'),
    ('d2000000-0000-0000-0000-000000000015', 'd1000000-0000-0000-0000-000000000008', 'd0000000-0000-0000-0000-000000000011', 'Sina'),
    ('d2000000-0000-0000-0000-000000000016', 'd1000000-0000-0000-0000-000000000008', 'd0000000-0000-0000-0000-000000000012', 'normal'),
    ('d2000000-0000-0000-0000-000000000017', 'd1000000-0000-0000-0000-000000000009', 'd0000000-0000-0000-0000-000000000011', 'Noah'),
    ('d2000000-0000-0000-0000-000000000018', 'd1000000-0000-0000-0000-000000000009', 'd0000000-0000-0000-0000-000000000013', 'Ferien'),
    ('d2000000-0000-0000-0000-000000000019', 'd1000000-0000-0000-0000-000000000010', 'd0000000-0000-0000-0000-000000000011', 'Jonas'),
    ('d2000000-0000-0000-0000-000000000020', 'd1000000-0000-0000-0000-000000000010', 'd0000000-0000-0000-0000-000000000013', 'Krankheit')
ON CONFLICT (id) DO NOTHING;

-- Zusätzliche historische Aktivitäten mit Antworten für Planwert-Schätzung
-- Die neue Schätzung nutzt /form/stats ähnlicher vergangener Aktivitäten.
INSERT INTO activities (id, title, date, start_time, end_time, goal, location, responsible, department, material, siko_text, bad_weather_info)
VALUES
    ('b0000000-0000-0000-0000-000000000015',
     'Pfadi-Postenlauf Frühling',
     CURRENT_DATE - INTERVAL '28 days',
     '13:30', '16:30',
     'Postenarbeit und Teamkoordination vertiefen',
     'Waldlichtung Hüttenberg',
     ARRAY['Leiter Eins', 'Stufen Leiter'],
     'Pfadi',
     '[{"name":"Postenkarten","responsible":["Leiter Eins"]},{"name":"Kompassset","responsible":[]}]'::jsonb,
     NULL,
     NULL),
    ('b0000000-0000-0000-0000-000000000016',
     'Wölfe-Spielnachmittag',
     CURRENT_DATE - INTERVAL '21 days',
     '14:00', '16:00',
     'Bewegungsspiele und Gruppenstärkung',
     'Schulhaus Dorf',
     ARRAY['Leiter Zwei'],
     'Wölfe',
     '[{"name":"Ballset","responsible":["Leiter Zwei"]},{"name":"Markierungshütchen","responsible":[]}]'::jsonb,
     NULL,
     NULL),
    ('b0000000-0000-0000-0000-000000000017',
     'Pio-Outdoortraining',
     CURRENT_DATE - INTERVAL '35 days',
     '10:00', '15:00',
     'Outdoor-Kompetenzen in Teams trainieren',
     'Pfadiheim Hüetli',
     ARRAY['Pio Eins'],
     'Pio',
     '[{"name":"Kocher","responsible":["Pio Eins"]},{"name":"Seiltechnik-Set","responsible":[]}]'::jsonb,
     NULL,
     NULL),
    ('b0000000-0000-0000-0000-000000000018',
     'Biber-Werkstatt',
     CURRENT_DATE - INTERVAL '27 days',
     '14:00', '16:00',
     'Kreative Bastelposten und gemeinsames Spielen',
     'Gemeindesaal',
     ARRAY['Leiter Drei'],
     'Biber',
     '[{"name":"Bastelmaterial","responsible":["Leiter Drei"]},{"name":"Farbstifte","responsible":[]}]'::jsonb,
     NULL,
     NULL)
ON CONFLICT (id) DO NOTHING;

INSERT INTO signup_forms (id, activity_id, public_slug, form_type, title, created_by)
VALUES
    ('e0000000-0000-0000-0000-000000000001', 'b0000000-0000-0000-0000-000000000015', 'pfadi-postenlauf-fruehling', 'registration', 'An-/Abmeldung Postenlauf', 'a0000000-0000-0000-0000-000000000001'),
    ('e0000000-0000-0000-0000-000000000002', 'b0000000-0000-0000-0000-000000000016', 'woelfe-spielnachmittag', 'registration', 'An-/Abmeldung Spielnachmittag', 'a0000000-0000-0000-0000-000000000001'),
    ('e0000000-0000-0000-0000-000000000003', 'b0000000-0000-0000-0000-000000000017', 'pio-outdoortraining', 'registration', 'An-/Abmeldung Outdoortraining', 'a0000000-0000-0000-0000-000000000001'),
    ('e0000000-0000-0000-0000-000000000004', 'b0000000-0000-0000-0000-000000000018', 'biber-werkstatt', 'registration', 'An-/Abmeldung Biber-Werkstatt', 'a0000000-0000-0000-0000-000000000001')
ON CONFLICT (activity_id) DO UPDATE
SET form_type = EXCLUDED.form_type,
    title = EXCLUDED.title,
    created_by = EXCLUDED.created_by,
    updated_at = NOW();

INSERT INTO form_responses (id, form_id, submission_mode, submitted_at, user_agent, ip_address)
SELECT
    r.id,
    sf.id,
    r.submission_mode,
    NOW() - r.delta,
    'seed/estimate-tests',
    '127.0.0.1'
FROM signup_forms sf
JOIN (
    VALUES
        ('b0000000-0000-0000-0000-000000000015'::uuid, 'e1000000-0000-0000-0000-000000000001'::uuid, 'registration'::text, INTERVAL '26 days'),
        ('b0000000-0000-0000-0000-000000000015'::uuid, 'e1000000-0000-0000-0000-000000000002'::uuid, 'registration'::text, INTERVAL '25 days'),
        ('b0000000-0000-0000-0000-000000000015'::uuid, 'e1000000-0000-0000-0000-000000000003'::uuid, 'registration'::text, INTERVAL '24 days'),
        ('b0000000-0000-0000-0000-000000000015'::uuid, 'e1000000-0000-0000-0000-000000000004'::uuid, 'registration'::text, INTERVAL '23 days'),

        ('b0000000-0000-0000-0000-000000000016'::uuid, 'e1000000-0000-0000-0000-000000000005'::uuid, 'registration'::text, INTERVAL '20 days'),
        ('b0000000-0000-0000-0000-000000000016'::uuid, 'e1000000-0000-0000-0000-000000000006'::uuid, 'registration'::text, INTERVAL '19 days'),
        ('b0000000-0000-0000-0000-000000000016'::uuid, 'e1000000-0000-0000-0000-000000000007'::uuid, 'registration'::text, INTERVAL '18 days'),
        ('b0000000-0000-0000-0000-000000000016'::uuid, 'e1000000-0000-0000-0000-000000000008'::uuid, 'registration'::text, INTERVAL '17 days'),

        ('b0000000-0000-0000-0000-000000000017'::uuid, 'e1000000-0000-0000-0000-000000000009'::uuid, 'registration'::text, INTERVAL '33 days'),
        ('b0000000-0000-0000-0000-000000000017'::uuid, 'e1000000-0000-0000-0000-000000000010'::uuid, 'registration'::text, INTERVAL '32 days'),
        ('b0000000-0000-0000-0000-000000000017'::uuid, 'e1000000-0000-0000-0000-000000000011'::uuid, 'registration'::text, INTERVAL '31 days'),

        ('b0000000-0000-0000-0000-000000000018'::uuid, 'e1000000-0000-0000-0000-000000000012'::uuid, 'registration'::text, INTERVAL '25 days'),
        ('b0000000-0000-0000-0000-000000000018'::uuid, 'e1000000-0000-0000-0000-000000000013'::uuid, 'registration'::text, INTERVAL '24 days'),
        ('b0000000-0000-0000-0000-000000000018'::uuid, 'e1000000-0000-0000-0000-000000000014'::uuid, 'registration'::text, INTERVAL '23 days'),
        ('b0000000-0000-0000-0000-000000000018'::uuid, 'e1000000-0000-0000-0000-000000000015'::uuid, 'registration'::text, INTERVAL '22 days'),
        ('b0000000-0000-0000-0000-000000000018'::uuid, 'e1000000-0000-0000-0000-000000000016'::uuid, 'registration'::text, INTERVAL '21 days')
) AS r(activity_id, id, submission_mode, delta)
    ON sf.activity_id = r.activity_id
ON CONFLICT (id) DO NOTHING;

-- Für alle vergangenen Aktivitäten: Formular sicherstellen und konsistente Testdaten
-- Ziel: jede vergangene Aktivität hat ein Formular mit exakt 24 Responses,
--       wobei submission_mode immer form_type entspricht.
INSERT INTO signup_forms (id, activity_id, public_slug, form_type, title, created_by)
SELECT
    (
        substr(md5('seed-form:' || a.id::text), 1, 8) || '-' ||
        substr(md5('seed-form:' || a.id::text), 9, 4) || '-' ||
        substr(md5('seed-form:' || a.id::text), 13, 4) || '-' ||
        substr(md5('seed-form:' || a.id::text), 17, 4) || '-' ||
        substr(md5('seed-form:' || a.id::text), 21, 12)
    )::uuid,
    a.id,
    'seed-' || replace(a.id::text, '-', ''),
    'registration',
    'Seed-Formular ' || a.title,
    'a0000000-0000-0000-0000-000000000001'
FROM activities a
WHERE a.date < CURRENT_DATE
ON CONFLICT (activity_id) DO UPDATE
SET form_type = EXCLUDED.form_type,
    title = EXCLUDED.title,
    created_by = EXCLUDED.created_by,
    updated_at = NOW();

DELETE FROM response_answers
WHERE response_id IN (
    SELECT fr.id
    FROM form_responses fr
    JOIN signup_forms sf ON sf.id = fr.form_id
    JOIN activities a ON a.id = sf.activity_id
    WHERE a.date < CURRENT_DATE
);

DELETE FROM form_responses
WHERE form_id IN (
    SELECT sf.id
    FROM signup_forms sf
    JOIN activities a ON a.id = sf.activity_id
    WHERE a.date < CURRENT_DATE
);

INSERT INTO form_responses (id, form_id, submission_mode, submitted_at, user_agent, ip_address)
SELECT
    (
        substr(md5('seed-response:' || sf.id::text || ':' || gs.n::text), 1, 8) || '-' ||
        substr(md5('seed-response:' || sf.id::text || ':' || gs.n::text), 9, 4) || '-' ||
        substr(md5('seed-response:' || sf.id::text || ':' || gs.n::text), 13, 4) || '-' ||
        substr(md5('seed-response:' || sf.id::text || ':' || gs.n::text), 17, 4) || '-' ||
        substr(md5('seed-response:' || sf.id::text || ':' || gs.n::text), 21, 12)
    )::uuid,
    sf.id,
    sf.form_type,
    (a.date::timestamp + a.start_time::time) - ((25 - gs.n) * INTERVAL '1 hour'),
    'seed/estimate-window',
    '127.0.0.1'
FROM signup_forms sf
JOIN activities a ON a.id = sf.activity_id
CROSS JOIN generate_series(1, 24) AS gs(n)
WHERE a.date < CURRENT_DATE
ON CONFLICT (id) DO NOTHING;
