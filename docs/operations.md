# Betrieb und Verhalten

Zurueck zur [README](../README.md). Diese Referenz ergaenzt die Einrichtung und
Befehlsuebersicht dort; alle Shell-Befehle werden im Repository-Root mit Bash
ausgefuehrt. Nominale Job-Intervalle, Audit-Frist und Erinnerungsoptionen stehen
in der automatisch geprueften [Repository-Referenz](../README.md#repository-reference).
Die zugrunde liegenden Quellen sind [App-Settings](../backend/src/infra/app_config.cpp),
[Startup-Jobs](../backend/src/main.cpp) und [Schema](../db/init.sql).

## CI / Docker Images

GitHub Actions builds Docker images for both services:

- `ghcr.io/<owner>/dpw-backend`
- `ghcr.io/<owner>/dpw-frontend`

Pipeline behavior:

- Pull requests to `main`: Dokumentations-/Unit-/Smoke-/API-Pruefungen und
  separat Produktionstest-Images fuer den Upgrade-Test; kein GHCR-Push.
- Push to `main` und tag `v*`: Pruefungen vor Veroeffentlichung beider
  Produktionsimages (Release, Debug-Auth deaktiviert).
- Der genehmigungspflichtige Debug-Deploy ist unabhaengig davon und setzt
  seine Testdaten bei jedem Deploy zurueck; er testet kein persistentes Upgrade.

Published tags include:

- `main` (branch builds)
- `latest` (default branch)
- `sha-<short-commit>`
- release tag name (for example `v1.2.3`)

### Persisted environment upgrade check

Every pull request to `main` also runs **Upgrade Compatibility / Persisted
Environment Upgrade**, independently of the fresh-install smoke/API suite.
The baseline is the most recent successful `CI` push run on `main`, whose
backend and frontend images have both been published. This follows production's
`latest` convention without racing two independently updated `latest` tags:
the check pulls matching `sha-<short-commit>` images, verifies both full revision
labels, and pins their digests for the entire run. A missing baseline or image
fails the check; it never silently substitutes a fresh install or another version.
The baseline revision, image digests and candidate merge revision are recorded
in the Actions job summary. This covers the previous published main version,
not every historical release or PostgreSQL major-version upgrade.

The check starts all four services using the baseline revision's production Compose
file (including its PostgreSQL/Redis versions) with isolated configuration,
no Docker socket access, and a random loopback HTTP port. The old backend initializes
the database before linked users, permissions, activities, programs, binary
attachments, forms, responses and encrypted settings are inserted. It then
recreates the containers using the PR Compose file and production PR images (Release, debug authentication
disabled), retaining the same PostgreSQL volume and cluster identifier.

It verifies container health and restart counts, frontend and proxied backend
HTTP health, pre-upgrade values and relationships, and secret decryption with the
unchanged configuration key. The backend image's bundled SQL must match the PR's
`db/init.sql`, preventing a stale packaged schema from becoming its own oracle.
A separate fresh PR environment supplies a canonical
schema reference (tables, columns/defaults, constraints, indexes, views, triggers,
functions, enums and sequences). The upgraded database must match it without
manually replaying SQL. A backend restart checks schema-sync idempotence.
Negative controls must detect changed data, schema drift and a stopped container.
Logs, schema comparisons and expected negative-control failures are retained as
the `upgrade-diagnostics` artifact, including on failure; test volumes are removed.

To reproduce with Docker Compose 2.24.4+ and Bash, first build production candidate
images using `backend/Dockerfile` (repository-root context) and
`frontend/Dockerfile` (frontend context), then run:

```bash
BASELINE_BACKEND_IMAGE=ghcr.io/reicham2/dpw-backend@sha256:<digest> \
BASELINE_FRONTEND_IMAGE=ghcr.io/reicham2/dpw-frontend@sha256:<digest> \
BASELINE_COMPOSE_FILE=/path/to/baseline/docker-compose.yml \
CANDIDATE_BACKEND_IMAGE=dpw-upgrade-backend:candidate \
CANDIDATE_FRONTEND_IMAGE=dpw-upgrade-frontend:candidate \
bash scripts/environment-upgrade-tests.sh
```

Only disposable test data is used; no deployment secrets are required. The script
generates a temporary encryption key shared across old, upgraded and reference
environments. Set `UPGRADE_RESULTS_DIR` to retain diagnostics in a chosen directory.
When adding intentional data transformations, update the fixtures/assertions to
express that migration explicitly rather than weakening the preservation check.

Quellen: [Upgrade-Workflow](../.github/workflows/upgrade.yml),
[Testskript](../scripts/environment-upgrade-tests.sh),
[isolierte Compose-Uebersteuerung](../scripts/upgrade-compose.yml).

## Audit Log

Unter **System > Audit Log** sehen Benutzer mit der Rolle `admin` ein persistentes,
serverseitiges Journal wichtiger Aktionen. Der API-Endpunkt `GET /admin/audit-log`
prueft diese Berechtigung unabhaengig von der UI. Filter: `actor` (Name/ID),
`outcome` (`completed`, `rejected`, `unknown`, `pending`), `limit` (1–100, Standard 50)
und `before` (ID-Cursor). IDs werden als Strings geliefert; Seiten sind absteigend
nach Journal-ID geordnet und enthalten `next_cursor`.

Erfasst werden Anmeldung/Profil, Aktivitaeten inklusive Programmen und Material
(auch indirekte/bulk Aenderungen innerhalb einer Anfrage), Anhaenge, Freigabelinks,
Wetterort, Benutzer/Rollen/Abteilungen/Berechtigungen, Orte/Materialverantwortliche,
Systemeinstellungen/Wartung/Ersteinrichtung, Mailversand und Entwuerfe,
Eventveroeffentlichungen, Vorlagen, Formularverwaltung/Antwortloeschung,
Ideenkiste und Fehlermeldungen. Eine Anfrage ergibt einen Journal-Eintrag, nicht
einen Eintrag pro betroffenem Datensatz. Automatische Hintergrundjobs, Lesezugriffe,
Polling, Push-Abonnements, Benachrichtigungs-Lesestatus und oeffentliche
Formularantworten werden nicht erfasst.

Das Journal speichert verifizierte Benutzer-ID und Anzeigenamen als Momentaufnahme,
Zeitpunkt, lesbare Aktion, Ressourcentyp, UUIDs, bekannte Einstellungsschluessel
und HTTP-Ergebnis. Die aufklappbaren **Details** zeigen erlaubte Metadaten wie Anzeigename,
Rolle, Stufe, Titel, Termin und Ort. Benutzer-/Profil-, Aktivitaets-, Rollen-,
Abteilungs-, Berechtigungs- und Orts-/Materialaenderungen zeigen vorher/nachher;
bei anderen Antworten werden nur erlaubte gespeicherte Metadaten angezeigt.
Verschachtelte Programm-/Materialaenderungen und sensible Freitexte werden nur als
geschuetzte Aenderung markiert. Es werden keine kompletten Request-/Response-Inhalte,
IP-Adressen, E-Mail-Adressen, Tokens, Freigabe-Slugs, Konfigurationswerte
(ausser der Audit-Aufbewahrungsdauer), Mailinhalte oder Formularantworten gespeichert.
Mailversand zeigt Empfaengeranzahl und optionale Aktivitaets-UUID, nicht Empfaengeradressen
oder Betreff. Metadaten sind auf 300 Bytes plus Kuerzungszeichen begrenzt.
Alte Eintraege ohne Details bleiben lesbar; fehlende historische Details werden nicht erfunden.
Ohne verifizierte Anmeldung bleibt der Akteur leer.
Erstregistrierung verwendet bis zur Anlage des Benutzers die verifizierte Microsoft-ID.
Historie bleibt nach Benutzer-/Ressourcenloeschung innerhalb der Aufbewahrungsfrist erhalten.
Unter **System > Einstellungen > Allgemein > Audit Log Aufbewahrung (Tage)** ist
die Frist konfigurierbar; Standard und Bereich stehen in der README-Referenz. Optional setzt
`DPW_AUDIT_RETENTION_DAYS` die Frist per ENV (sperrt den UI-Wert).
Der Cron-Job `audit-retention` loescht beim Start und danach im konfigurierten Intervall alle
Eintraege, die aelter als die Frist sind, einschliesslich alter offener/unterbrochener
Eintraege. Eine geaenderte Frist gilt beim naechsten Lauf; bei normal laufendem Job koennen
Eintraege bis zum naechsten Intervall vorhanden sein. **System > Cron-Jobs** zeigt
Zustand und Fehler; Pausieren setzt auch die automatische Loeschung aus.
Es gibt keine Archiv-/Exportfunktion; abgelaufene Eintraege werden endgueltig geloescht.

Die Tabelle zeigt zunaechst nur Aktion, Ziel, Ausgang und eine kurze Felduebersicht.
Ein Klick auf eine Zeile oder den per Tastatur erreichbaren Aktionsknopf oeffnet
die vollstaendigen Details (inklusive IDs und HTTP-Status); ein weiterer Klick
schliesst sie. Nur ein Detailbereich wird gleichzeitig gerendert.
Die kompakten, einzeiligen Zeilen liegen in einem auf 60 % der Fensterhoehe
(maximal 36 rem) begrenzten Scrollbereich mit feststehender Tabellenkopfzeile.
Lange Texte werden in der Uebersicht gekuerzt; vollstaendige Namen stehen im
Detailbereich und als Tooltip bereit. Der Scrollbereich ist per Tastatur erreichbar.
Aufklappen nutzt die bereits geladene, auf 50 Eintraege begrenzte Seite und erzeugt keine
weitere API-/DB-Anfrage. Es gibt kein automatisches Audit-Polling.

Der Schreibpfad behaelt die drei fuer die Haltbarkeit notwendigen Journal-Schritte
(Intent, verifizierter Akteur, Abschluss) bei; wiederholte Auth-Pruefungen derselben
Anfrage schreiben den Akteur nicht erneut. Die Anmeldung aktualisiert ihren
Akteurssnapshot nach Benutzeranlage weiterhin explizit. Vorher-Zustaende von
Orten/Materialverantwortlichen werden per Primaerschluessel statt per Vollistenabfrage
gelesen; Benutzer-/Abteilungspruefungen verwenden vorhandene Snapshots erneut.
Bereits vorbereitete Details werden beim Abschluss verschoben statt kopiert und
Antwort-JSON wird nur geparst, wenn Ressourcen-ID oder Details noch fehlen.
Pagination sortiert numerisch ueber den ID-Index statt OFFSET oder Textsortierung;
der Retention-Job hat einen Zeitindex und laeuft im vorhandenen Worker, nicht im
HTTP-Thread. Die optionale Teilnamensuche kann bei fehlenden Treffern weiterhin
den Verlauf durchsuchen; sie wird nur auf explizite Filteranfrage ausgefuehrt.

Vor Ausfuehrung wird ein `pending`-Eintrag dauerhaft geschrieben; ist dies nicht
moeglich, wird die Aktion mit HTTP 503 blockiert. Erst nach der Handler-Antwort
wird `completed` (2xx), `rejected` (4xx) oder `unknown` (andere Ergebnisse/Abbruch)
gespeichert. Dies beschreibt den **Anfrageausgang**, nicht einen atomaren Rollback
aller DB-/externen Effekte: bei Fehlern koennen Teilaktionen bereits erfolgt sein.
Ein Crash kann `pending` hinterlassen. Scheitert das abschliessende Schreiben,
wird explizit HTTP 500 mit Hinweis auf moeglicherweise ausgefuehrte Aktionen
zurueckgegeben und ein Betriebsfehler protokolliert; vor einem erneuten Versuch
den Zustand pruefen. Audit-Schreiboperationen werfen bei DB-Fehlern, statt Erfolg
vorzutaeuschen. `audit_events` wird additiv durch den bestehenden Schema-Sync angelegt.
Bekannte Fehler der WordPress-Synchronisierung markieren auch bei unveraendertem
HTTP-2xx-Verhalten den Audit-Ausgang als `unknown`.

Audit-Regression: Frontend `npm test -- src/components/AuditLogPanel.test.ts`,
Backend ueber `backend/Dockerfile.test`; zusaetzlich
`AUDIT_BASE_URL=http://127.0.0.1:<frontend-port>/api AUDIT_BACKEND_URL=http://127.0.0.1:<backend-port> AUDIT_DB_CONTAINER=<name> node scripts/test-audit.mjs`
gegen einen **wegwerfbaren, mit `db/seed.sql` vorbereiteten Debug-Backend/DB-Stack**.
Die Ports liefert `docker compose -f docker-compose-dev.yml port frontend 80`
und `docker compose -f docker-compose-dev.yml port backend 8080`.
Bei abweichendem DB-Benutzer `AUDIT_DB_USER` setzen. Der Integrationstest injiziert
gezielt DB-Schreibfehler und veraendert Testdaten; niemals gegen Produktion ausfuehren.
CI fuehrt ihn vor der bestehenden API-Suite im isolierten Teststack aus.

Quellen: [Audit-Handler/Journal](../backend/src/infra/audit.cpp),
[Audit-Aktionen](../backend/src/infra/audit_actions.cpp),
[DB-Retention](../backend/src/db/db_audit.cpp),
[Regressionstest](../scripts/test-audit.mjs).

## Relative Datumsvariablen in Vorlagen

Formular-, Mail- und Event-Vorlagen unterstützen `{{datum|-7}}` (langes deutsches
Datum) und `{{datum_kurz|-7}}` (`DD.MM.YYYY`), z.B. `Anmeldung bis {{datum_kurz|-7}}`.
Bei einer Aktivität am 15.07.2026 ergibt das `Anmeldung bis 08.07.2026`.
Die Zahl nach `|` ist frei wählbar: negative ganze Zahlen ziehen Kalendertage ab,
positive ganze Zahlen addieren sie, `0` lässt das Datum unverändert. Ein führendes
`+`, Leerzeichen oder Dezimalzahlen sind nicht zulässig. Ohne Versatz bleiben
`{{datum}}` und `{{datum_kurz}}` unverändert unterstützt.

Monats-/Jahreswechsel, Schaltjahre und Sommerzeit werden als Kalendertage behandelt,
nicht als lokale Zeitdauer. Ungültige Versätze, ungültige Aktivitätsdaten oder
Ergebnisse außerhalb von `0001-01-01` bis `9999-12-31` lassen die Variable sichtbar
unverändert. Formularvariablen werden beim öffentlichen Abruf anhand der verknüpften Aktivität
ersetzt (Titel, Fragen, Untertitel und Auswahlbeschriftungen); Mail-/Event-Vorlagen
werden beim Anwenden expandiert. Dies berechnet einen Datumstext und schließt ein
Formular nicht automatisch. Beispiele stehen im Menü „Verfügbare Variablen“.

## Datenbank-Schema-Sync (init.sql)

Es gibt bewusst keine separaten Migrationsdateien.
[db/init.sql](../db/init.sql) ist die einzige Quelle fuer das gesamte Schema.

Beim Start des Backends wird `init.sql` automatisch in einer Transaktion ausgefuehrt, auch gegen bereits existierende Datenbanken.
Dadurch kann ein neues Backend-Image mit einer alten DB gestartet werden, und die DB wird auf den aktuellen Stand gebracht.

`init.sql` wird mit dem Backend-Container ausgeliefert (`/etc/dpw/init.sql`) und nicht als Volume gemountet.

Eigenschaften:

1. Advisory-Lock verhindert parallele Schema-Syncs durch mehrere Backend-Instanzen.
2. Ausfuehrung ist transaktional (bei Fehlern Rollback).
3. Vor dem Sync wird ein temporaerer DB-Snapshot erzeugt; bei Sync-/Verifikationsfehlern
   wird dessen Wiederherstellung versucht. Scheitert sie, wird der Fehler explizit
   gemeldet und der Backend-Start abgebrochen. Das ist kein externes Backup.
4. Backend und Schema-Sync nutzen dieselbe Datei `db/init.sql`.

Wichtig fuer zukuenftige Schema-Aenderungen:

1. Alle Aenderungen ausschliesslich in `db/init.sql` pflegen.
2. Statements idempotent halten (`IF NOT EXISTS`, `ON CONFLICT`, additive `ALTER TABLE ... ADD COLUMN IF NOT EXISTS`).
3. Keine implizit destruktiven Aenderungen ohne explizite, beabsichtigte SQL-Logik.

Der Snapshot wird nach dem Startversuch entfernt. Vor einem Produktiv-Upgrade
ein externes Backup von PostgreSQL erstellen und dessen Wiederherstellbarkeit
pruefen; den unveraenderten `DPW_CONFIG_ENCRYPTION_KEY` separat sicher bewahren.
Ein altes Image gegen eine bereits veraenderte Datenbank ist kein garantierter
Rollback. PostgreSQL-Major-Upgrades brauchen eine eigene Migrationsstrategie.
Implementierung: [Schema-Sync](../backend/src/db/db_connection.cpp).

Das optionale Aktivitätsfeld «Roter Faden» wird beim Schema-Sync automatisch
hinzugefügt. Bestehende Aktivitäten bleiben unverändert und haben zunächst
keinen roten Faden. Ältere Clients können weiterhin speichern, ohne dieses
Feld zu überschreiben. Bei getrennten Rollouts sollte das Backend vor dem
Frontend aktualisiert werden. Ist noch ein älteres Backend aktiv, wird das
Eingabefeld beim Erstellen und Bearbeiten bis zum Backend-Update deaktiviert;
bereits vorhandene lokale Entwürfe bleiben erhalten.

Der rote Faden kann in der Aktivität erfasst und bearbeitet werden; er erscheint
auch in der geteilten Ansicht und im Word-Export. In Formular-, Mail- und
Eventvorlagen steht er als `{{roter_faden}}` zur Verfügung. Beim Wechsel zur
Ansicht werden ausstehende Änderungen sofort zum Speichern angestossen. Bestätigt
das Backend den roten Faden nicht, wird ein Fehler angezeigt und der lokale
Entwurf bleibt erhalten, sofern der Browserspeicher verfügbar ist.

Die aktuelle Befehls-/Testuebersicht mit Warnungen fuer destruktive Targets steht
in der [README](../README.md#tests). Mit lokalen Make-Tests wird nicht automatisch
die komplette CI-Matrix ausgefuehrt.

Die API-HTTP- und Berechtigungsregressionstests laufen gemeinsam über die
Postman-Collection. Echtzeitberechtigungen über WebSockets werden ergänzend mit
einem separaten Node-Test geprüft.

## Web Push / PWA Benachrichtigungen

```bash
make generate-vapid-keys
```

Der Befehl ist weiterhin nutzbar, aber nicht mehr zwingend noetig. Wenn keine VAPID-Werte
per ENV gesetzt sind, erzeugt der Backend-Start ein Schluesselpaar automatisch und speichert
es verschluesselt in der DB.

Optional per `.env` setzbar:

- `DPW_VAPID_PUBLIC_KEY`
- `DPW_VAPID_PRIVATE_KEY`
- `DPW_VAPID_SUBJECT`

Zusätzlich fuer oeffentliche Links (z. B. in Benachrichtigungen/Mails):

- `DPW_PUBLIC_URL` (z. B. `https://dpw.example.org`)

## Vorlagenvariablen

Die vorhandenen Variablen (z. B. `{{titel}}`, `{{datum_kurz}}`, `{{ort}}`
und `{{abteilung}}`) funktionieren auch im Mail-Betreff, im Formular-Titel
und im Event-Titel.

- Mail-Betreff und Event-Titel werden beim Laden der Vorlage als Klartext
  aufgeloest; `{{formular_link}}` und `{{formular_link|Anmelden}}` ergeben
  dort die Formular-URL statt HTML. Die Vorschau kann vor dem Versand bzw.
  Publizieren bearbeitet werden.
- Der Name einer Formularvorlage wird beim Erstellen als Formular-Titel
  uebernommen. Ein selbst eingegebener Titel bleibt beim Vorlagenwechsel
  erhalten. In Vorlagen und Entwuerfen bleiben die Variablen stehen.
  Beim Erstellen wird der Titel mit den aktuellen Aktivitaetsdaten
  aufgeloest und gespeichert, sodass auch Verwaltung und Editor sofort
  den fertigen Titel anzeigen. Variablen in den Fragen werden weiterhin
  erst im oeffentlichen Formular ersetzt.
- Unbekannte Variablen bleiben unveraendert. Rich-Text-Inhalte behalten
  ihre bisherige Formatierung und Linkdarstellung.

## Wartungsmodus

Unter **Admin > System > Wartungsfenster** koennen Admins den Wartungsmodus
manuell aktivieren oder Wartungsfenster planen. Solange er aktiv ist, koennen
sich nur Benutzer mit der Rolle `admin` anmelden. Angemeldete Admins sehen
einen nicht ausblendbaren Hinweis mit der konfigurierten Wartungsnachricht.
Der Hinweis folgt dem aktuellen Serverstatus (WebSocket und Status-Polling),
auch bei geplanten Wartungsfenstern, und verschwindet nach deren Ende.
Danach wird ein Hinweis zum Neuladen angezeigt.

Bereits angemeldete Nicht-Admins werden nach dem Speichern ausstehender
Aenderungen auf die Wartungsseite umgeleitet (maximal 1,5 Sekunden Wartezeit).
Bestehende API- und WebSocket-Sitzungen bleiben bewusst zugaenglich, damit
dieses Speichern moeglich ist; der Wartungsmodus ist keine serverseitige
Zugriffssperre fuer bestehende Tokens. Oeffentliche Formulare, Freigabelinks
und der Wartungsstatus bleiben erreichbar. Die Wartungsseite prueft regelmaessig,
ob die Anwendung wieder verfuegbar ist.

Die Banner gelten auch fuer geplante/wiederkehrende Fenster. Eine zuvor
geschlossene Planungswarnung blendet den aktiven Admin-Hinweis nicht aus.
Wartungsmodus stoppt keine Hintergrundjobs und ersetzt keine konsistente
Datensicherung oder das Stoppen von Schreibern vor einem Upgrade.
Quellen: [Wartungs-Handler](../backend/src/handlers/setup_maintenance.cpp),
[Frontend-Banner](../frontend/src/components/MaintenanceBanner.vue).

Der Wartungs-Integrationstest laeuft gegen einen wegwerfbaren Debug-Stack mit
[Auth-Fixtures](../scripts/api-auth-fixtures.sql). `<frontend-port>` ersetzen:

```bash
DPW_API_BASE_URL=http://127.0.0.1:<frontend-port>/api node --test scripts/api-maintenance-tests.mjs
```

## Zentrale Cron-Jobs

Das Backend registriert wiederkehrende Serveraufgaben in `CronScheduler` und startet
den Scheduler erst nach erfolgreichem HTTP-Listen. Unter **System > Cron-Jobs** koennen
Systemadministratoren (`admin` oder `user_role_scope = all`) Jobs einsehen, pausieren
und fortsetzen. Die API ist `GET /admin/cron-jobs` und
`PUT /admin/cron-jobs/:id` mit `{"enabled":false}` bzw. `{"enabled":true}`.

Die aktuell registrierten Jobs und ihre Standardintervalle stehen in der
geprueften README-Referenz, einschliesslich `activity-reminders`. Dazu gehoeren
Audit-Aufbewahrung, abgelaufene Aktivitaetsbenachrichtigungen und permanentes
Loeschen von seit mehr als 90 Tagen im Papierkorb liegenden Aktivitaeten.
Die DB-View `active_notifications` filtert abgelaufene Eintraege bei
Lesen, Gelesen-Markierung und Push-Abfragen sofort aus; physisches Loeschen erfolgt
nur noch im zentralen Job. Browser-Autosave, Wartungsstatus-Polling und bedarfsabhaengige
MiData-/Wetterabfragen bleiben unveraendert; sie sind keine serverweiten Cron-Jobs.

Neue Jobs werden zentral in `backend/src/main.cpp` mit einem eindeutigen Namen,
Callback und einem Intervall von **1, 5, 15, 30, 60 oder 1440 Minuten** registriert.
Es gibt keine leeren Platzhalterjobs fuer ungenutzte Intervalle. Jeder Job laeuft
einmal beim Start und danach mit festem monotonem Intervall (taeglich = 24 Stunden,
nicht lokale Mitternacht). Bei Verspaetungen wird nur einmal nachgeholt; ausgelassene
Intervalle werden gezaehlt. Jobs laufen seriell in einem separaten Worker, nicht
auf dem HTTP-/WebSocket-Thread. Fehler eines Jobs stoppen andere Jobs nicht.
Eigene DB-Verbindungen ohne Schema-Sync nutzen 5 Sekunden Verbindungs-/Lock-Timeout
und 30 Sekunden Statement-Timeout.

Das Panel aktualisiert alle 10 Sekunden und zeigt naechsten Lauf, letzten Start,
Ende, Erfolg, Dauer, Fehler und verpasste Intervalle. Nach 5 Sekunden Verspaetung
wird ein Job als ueberfaellig markiert, auch wenn ein vorheriger Lauf noch laeuft.
Fehlerdetails werden im Backend als `[cron]` geloggt, nicht als rohe DB-Fehler an
den Browser geliefert. Pausieren bricht einen laufenden Job nicht ab; Fortsetzen
plant einen sofortigen Lauf ohne Wiederholung der pausierten Intervalle.

Scheduler, Pausen und Statistiken sind **pro Backend-Prozess** und werden beim
Neustart zurueckgesetzt; die Startzeit wird im Panel angezeigt. Es gibt keine
persistente Run-Historie oder verteilte Leader-Wahl. Die aktuellen Loeschjobs sind
idempotent, aber neue nicht-idempotente Jobs benoetigen bei mehreren Backend-Replikas
zusaetzliche DB-seitige Koordination. Beim Ende des HTTP-Loops wird der Worker
gestoppt und ein laufender Callback vor dem Zerstoeren seiner Abhaengigkeiten beendet.

Nach einem Neustart sind alle Jobs wieder aktiviert; Pausieren setzt insbesondere
die automatische Aufbewahrungs-/Loeschfrist aus. Quelle:
[Scheduler](../backend/src/infra/cron_scheduler.cpp).

Regressionstests fuer die sofortige Benachrichtigungsfilterung:
`psql -v ON_ERROR_STOP=1 -f backend/tests/test_cron_notifications.sql`
gegen eine mit `db/init.sql` initialisierte Testdatenbank ausfuehren.
Die Testdaten werden innerhalb einer Transaktion vollstaendig zurueckgerollt.
## Aktivitaetserinnerungen

Die Kategorie `activity_reminder` erinnert registrierte Benutzer an kommende Aktivitaeten
ihrer aktuellen Stufe, sofern sie Lesezugriff und das Abonnement im Profil aktiviert haben.
Vorlaufoptionen und Standard fuer neue/bisherige Benutzer stehen in der
README-Referenz. Datum/Startzeit werden in `Europe/Zurich` interpretiert; der Vorlauf
ist eine verstrichene Stundenzahl, auch ueber Sommerzeitwechsel.

Verspaetete Durchlaeufe holen faellige Erinnerungen bis zum Beginn nach, einschliesslich
spaet angelegter Aktivitaeten und neu zugeordneter Benutzer. Eine geaenderte Vorlaufzeit gilt
fuer noch nicht ausgestellte Erinnerungen, nicht als erneuter Versand einer bereits
ausgestellten Erinnerung. Der transaktionale DB-Ledger verhindert doppelte In-App-Meldungen
bei parallelen Prozessen, Neustarts und geloeschten/gelesenen Meldungen. Datum, Startzeit,
Stufe und Loeschen/Wiederherstellen erzeugen eine neue Terminrevision und entfernen alte
In-App-Erinnerungen; reine Titel-/Programmaenderungen loesen keinen erneuten Versand aus.

In-App-Meldungen werden bis zur bestehenden Aktivitaetsbenachrichtigungs-Ablaufgrenze
(Folgetag um Mitternacht) gespeichert. Browser/App/Web-Push respektieren den
Kanal-Schalter; fehlgeschlagene Push-Zustellungen werden mit persistenter Fuenf-Minuten-Lease
bis zum Beginn erneut versucht (maximal 20 Empfaenger pro Durchlauf). Externe Zustellung
ist mindestens einmal: ein Absturz nach dem Senden kann eine Wiederholung verursachen,
wobei die Browser-Notification-ID denselben Hinweis ersetzt. Bereits zugestellte externe
Meldungen koennen bei spaeterer Absage nicht zurueckgezogen werden. Automatische E-Mails
werden nicht versendet: der bestehende Mail-Pfad benoetigt ein delegiertes Benutzer-Token.
`DPW_PUBLIC_URL` muss fuer Aktivitaetslinks konfiguriert sein.
Ein fehlender Wert fuehrt zu einem expliziten Cron-Fehler, nicht zu erfolgreichen
Erinnerungen mit ungueltigen Links. Browser-Abonnement/Berechtigung und HTTPS
bleiben fuer Push erforderlich.

Der zentrale CronScheduler fuehrt `activity-reminders` beim Serverstart und im
registrierten Intervall aus;
er ist im System-Bereich sichtbar und pausierbar. Der Job verwendet eine eigene DB-Verbindung
mit begrenzten Verbindungs-, Statement- und Lock-Timeouts. Push laeuft im Worker; WebSocket-
Zustellung wird auf den HTTP-Loop verschoben und prueft dort erneut Terminrevision, aktuellen
Stufenzugriff und Einstellungen. Es gibt keinen zweiten Scheduler.

SQL-Regressionen (in einer Testdatenbank; alle Fixtures werden zurueckgerollt):

```bash
psql -v ON_ERROR_STOP=1 -U activities_user -d activities -f backend/tests/test_activity_reminders.sql
```

Quellen: [Erinnerungsjob](../backend/src/infra/activity_reminders.cpp),
[DB-Zustellung](../backend/src/db/db_notifications.cpp),
[SQL-Regressionen](../backend/tests/test_activity_reminders.sql).

## Role Permissions

Jede Rolle hat folgende Berechtigungen, jeweils mit einem Scope-Level (`none`, `own`, `own_dept`, `same_dept`, `all`):

| Permission | Beschreibung | Scopes |
|---|---|---|
| `activity_read_scope` | Aktivitaeten lesen/anzeigen | none / same_dept / all |
| `activity_create_scope` | Neue Aktivitaeten erstellen | none / own_dept / all |
| `activity_edit_scope` | Aktivitaeten bearbeiten und loeschen | none / own / same_dept / all |
| `mail_send_scope` | Mails versenden | none / own / same_dept / all |
| `mail_templates_scope` | Mail-Vorlagen verwalten | none / own_dept / all |
| `form_scope` | Formulare erstellen und verwalten | none / own / same_dept / all |
| `form_templates_scope` | Formular-Vorlagen verwalten | none / own_dept / all |
| `event_templates_scope` | Event-Vorlagen verwalten | none / own_dept / all |
| `event_publish_scope` | Events auf WordPress publizieren | none / own / own_dept / all |
| `user_dept_scope` | Benutzer-Abteilung aendern | none / own / own_dept / all |
| `user_role_scope` | Benutzer-Rolle aendern | none / own / own_dept / all |
| `locations_manage_scope` | Vordefinierte Standorte verwalten | none / all |
| `materials_manage_scope` | Vordefinierte Material-Verantwortliche verwalten | none / all |
| `ideenkiste_scope` | Ideenkiste lesen | none / own_dept / all |
| `ideenkiste_add_scope` | Ideen hinzufuegen | none / own_dept / all |
| `ideenkiste_delete_scope` | Ideen loeschen | none / own_dept / all |

Zusaetzlich gibt es Cross-Department-Zugriff (`role_dept_access`) um Rollen Lese-/Schreibzugriff auf andere Abteilungen zu geben.
Aktivitäten ohne Stufe können nur mit dem Erstellungs-Scope `all` angelegt
werden; für `own_dept` oder einen stufenübergreifenden Schreibzugriff muss die
Zielstufe angegeben werden.

## MiData Integration

Für die aktuelle Kinderanzahl pro Aktivität kann DPW MiData abfragen.

Voraussetzungen:

1. Im Backend muss `DPW_CONFIG_ENCRYPTION_KEY` gesetzt sein (für verschlüsselte Secret-Speicherung).
2. In der Admin-Ansicht unter `Administration -> System` die Settings setzen:
   - `midata.api_key` (Secret)
   - `midata.api_url_template` (optional)
   - `midata.api_timeout_ms` (optional)
3. Pro Stufe in der Admin-Ansicht eine MiData-Gruppen-ID hinterlegen.

Hinweise:

1. Ohne Gruppen-ID oder API-Key bleibt der MiData-Wert leer (`Nicht konfiguriert`).
2. Teilnehmerrollen werden anhand der verknüpften MiData-Rollen erkannt: Biber, Wolf,
   Leitwolf, Pfadi, Leitpfadi, Pio, Rover und Teilnehmer. Leitungs- und Verwaltungsrollen
   allein werden nicht mitgezählt; Rollen anderer Gruppen werden ignoriert.
3. Jede Person wird höchstens einmal gezählt. Explizit leere Rollenlisten werden wie
   bisher mitgezählt. Fehlende Rollendetails werden als Fehler angezeigt, nicht als
   scheinbar erfolgreiche Kinderanzahl von null.

### Wettervorhersage

Für Termine innerhalb der nächsten acht Tage lädt DPW den neuesten verfügbaren
MeteoSwiss-Vorhersagelauf mit stündlichen Temperatur- und Niederschlagsdaten.
Noch leere Tagesdateien werden übersprungen; Downloads werden zehn Minuten
zwischengespeichert. Die Detailansicht wartet bis zu 30 Sekunden auf die erste Abfrage.
Bei hinterlegtem Wetterort werden Quellen- oder Zuordnungsfehler angezeigt,
statt einen saisonalen Durchschnitt als vermeintliche Vorhersage auszugeben.
Ohne gespeicherten Wetterort wird kein Wetterfehler angezeigt; vorhandene
Wetter-Snapshots vergangener Aktivitäten bleiben sichtbar. Saisonale
Durchschnittswerte werden nur für Termine ausserhalb des Vorhersagehorizonts verwendet.

### Runtime-Konfiguration (Admin)

Ein Grossteil der Integrations-Settings kann direkt in der Web-App gepflegt werden.

- Secrets werden verschlüsselt in der DB gespeichert (pgcrypto); die
  Admin-Settings-API gibt keine gespeicherten Secret-Werte im Klartext zurueck.
- Nicht-sensitive Werte (z. B. Timeouts) dürfen lesbar sein.
- Laufzeit liest aus der DB; gesetzte ENV-Werte haben Prioritaet und sperren das entsprechende Feld.
- Falls ENV gesetzt ist, werden diese Werte beim Backend-Start in die DB importiert und dort synchron gehalten.
- Standardwerte werden bereits über `db/init.sql` in `app_settings` gesetzt und können danach über Admin oder ENV-Import überschrieben werden.

Wichtige ENV-Variablen:

- Pflicht via `.env`: `DPW_CONFIG_ENCRYPTION_KEY`.
- `DPW_VAPID_PUBLIC_KEY` und `DPW_VAPID_PRIVATE_KEY` werden automatisch erzeugt
  oder per `.env` vorgegeben; sie sind im Admin-Settings-Editor nicht editierbar.
  `DPW_VAPID_SUBJECT` ist via `.env` oder Admin/Setup pflegbar.
- Optional via `.env` (Prioritaet zur Laufzeit und Import beim Start):
  `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET`,
  `MIDATA_API_KEY`, `MIDATA_API_URL_TEMPLATE`, `MIDATA_API_TIMEOUT_MS`,
  `DPW_WP_URL`, `DPW_WP_USER`, `DPW_WP_APP_PASSWORD`.

Falls Azure-Werte nicht in `.env` gesetzt sind, steht ein Initial-Setup ohne Login bereit:

- `GET /api/setup/auth-config` (Status)
- `POST /api/setup/auth-config` (tenant_id, client_id, client_secret, contact_email)

Das Setup bleibt erforderlich, bis auch eine echte Kontaktadresse gesetzt ist.
Nur fehlende Felder koennen ohne Anmeldung gesetzt werden; eine fertige
Konfiguration laesst sich dort nicht ueberschreiben. Entfernen einer ENV
loescht ihren zuvor importierten DB-Wert nicht. Die vollstaendige Variablenliste
steht in [.env.example](../.env.example), die Aufloesung in
[app_config.cpp](../backend/src/infra/app_config.cpp).

### Anmeldung und Wiederherstellung

- Eine bestehende Microsoft-Anmeldung wird beim Start ohne Popup wiederhergestellt.
  Falls eine erneute Interaktion erforderlich ist, erscheint ein Hinweis zur Anmeldung.
- Die Abmeldung beendet die lokale DPW-Sitzung, nicht die Microsoft-Sitzung im Browser.
  Nach dem Abmelden ist eine ausdrückliche erneute Anmeldung erforderlich.
- Ist der Setup-Status vorübergehend nicht erreichbar, kann die Prüfung auf der
  Setup-Seite erneut gestartet werden, ohne eine neue Konfiguration zu speichern.
- Das Backend prüft unter anderem die Gültigkeitsdauer und die Signatur der
  Microsoft-ID-Tokens. Signaturschlüssel werden pro Tenant zwischengespeichert;
  fehlgeschlagene oder ungültige Antworten werden nicht übernommen.
- Interne Aktivitätszugriffe und Echtzeitmeldungen berücksichtigen den lokalen
  Benutzer und seine Rollen- und Stufenberechtigungen.

### Aktivitäten extern teilen

Ein Freigabelink `/shared/<token>` öffnet genau die geteilte Aktivität ohne
Anmeldung in einer öffentlichen Leseansicht. Der Link sollte nur an die
vorgesehenen Empfänger weitergegeben werden. Interne Freigabelinks können nur
für lesbare Aktivitäten abgerufen, erstellt oder widerrufen werden; nach dem
Widerruf ist der bisherige öffentliche Link nicht mehr gültig. Öffentliche
Anmeldeformulare bleiben unabhängig davon ohne Anmeldung erreichbar.

## Impressum und Datenschutzhinweise

`/impressum` und `/datenschutz` sind öffentliche Routen und von den
Anmelde-, Freigabe- und Formularseiten erreichbar; sie werden nicht in der
angemeldeten App angezeigt. Die Hinweise beschreiben
DPWeb-Datenflüsse aus dem Quellcode, darunter die öffentliche Anzeige von
Freigaben, Formularantworten einschliesslich gespeicherter IP-/User-Agent-Daten,
Mailinhalte, das Audit-Log, die automatische endgültige Löschung von Aktivitäten
aus dem Papierkorb nach 90 Tagen und konfigurierte externe Dienste. Die
Standardfrist des Audit-Logs beträgt 14 Tage und ist administrativ einstellbar.
Die Datenschutzhinweise enthalten zudem die von der Betreiberin bestätigten
Angaben zu Schweizer Servern/Datenbanken, möglichen Drittlandübermittlungen und
den genannten Übermittlungsgarantien sowie die aufgeführten Betroffenenrechte.

Die rechtlichen Angaben dieser Instanz beruhen auf den von der Betreiberin
bestätigten Kontaktdaten und dem bestätigten Hosting in der Schweiz. Bei
anderen Installationen müssen Betreibername, Kontakt und Hosting-Angaben sowie
die tatsächlich aktivierten Integrationen vor einer öffentlichen Nutzung
angepasst und durch die verantwortliche Stelle geprüft werden. Der Quellcode
legt die Aufbewahrung von Hosting-Logs und Sicherungskopien nicht fest.

## Redis Cache (optional)

Das Backend kann API-Responses in Redis cachen, um wiederholte Lese-Anfragen zu beschleunigen.

Aktivierung:

1. In Docker Compose ist ein `redis` Service bereits hinterlegt.
2. Redis- und SQL-Verbindungswerte sind fest in den Docker-Compose-Dateien definiert (keine `.env`-Anpassung nötig).

Verhalten:

1. Ohne `REDIS_HOST` ist das Backend-Caching deaktiviert; nicht erfolgreiche
   Cache-Abfragen fallen auf Datenbankabfragen zurueck. Die mitgelieferten
   Compose-Dateien warten beim Start jedoch auf einen gesunden Redis-Service.
2. Schreiboperationen heben betroffene Cache-Bereiche per Versions-Invalidierung auf.
3. Die Response-TTL wird per `DPW_CACHE_TTL_SECONDS` gesetzt (Wert in Compose).
   Lokale Versionsabfragen verwenden `DPW_CACHE_VERSION_TTL_MS`; gesetzte Werte
   bestimmen die zusaetzliche Cache-Verzoegerung, nicht ein festes
   "wenige Sekunden"-Versprechen. Quelle: [Cache](../backend/src/infra/cache.cpp).
