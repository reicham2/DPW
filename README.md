# DPW

## CI / Docker Images

GitHub Actions builds Docker images for both services:

- `ghcr.io/<owner>/dpw-backend`
- `ghcr.io/<owner>/dpw-frontend`

Pipeline behavior:

- Pull requests to `main`: build both images, run smoke tests, do not push to GHCR.
- Push to `main`: build, push to GHCR, and run smoke tests.
- Push tag `v*`: build and push to GHCR.

Published tags include:

- `main` (branch builds)
- `latest` (default branch)
- `sha-<short-commit>`
- release tag name (for example `v1.2.3`)

## Schnellstart

```bash
# 1. .env erstellen
cp .env.example .env
# → Azure AD Werte eintragen (oder DEBUG=true für lokales Testen)

# 2. Alles starten
make up

# 3. (Optional) Testdaten laden
make db-seed
```

## Datenbank-Schema-Sync (init.sql)

Es gibt bewusst keine separaten Migrationsdateien. `db/init.sql` ist die einzige Quelle fuer das gesamte Schema.

Beim Start des Backends wird `init.sql` automatisch in einer Transaktion ausgefuehrt, auch gegen bereits existierende Datenbanken.
Dadurch kann ein neues Backend-Image mit einer alten DB gestartet werden, und die DB wird auf den aktuellen Stand gebracht.

`init.sql` wird mit dem Backend-Container ausgeliefert (`/etc/dpw/init.sql`) und nicht als Volume gemountet.

Eigenschaften:

1. Advisory-Lock verhindert parallele Schema-Syncs durch mehrere Backend-Instanzen.
2. Ausfuehrung ist transaktional (bei Fehlern Rollback).
3. Vor dem Sync wird ein DB-Snapshot erzeugt; bei Fehlern waehrend oder nach dem Sync wird automatisch auf den Snapshot zurueckgespielt.
4. Backend und Schema-Sync nutzen dieselbe Datei `db/init.sql`.

Wichtig fuer zukuenftige Schema-Aenderungen:

1. Alle Aenderungen ausschliesslich in `db/init.sql` pflegen.
2. Statements idempotent halten (`IF NOT EXISTS`, `ON CONFLICT`, additive `ALTER TABLE ... ADD COLUMN IF NOT EXISTS`).
3. Keine implizit destruktiven Aenderungen ohne explizite, beabsichtigte SQL-Logik.

### Nützliche Befehle

| Befehl                     | Beschreibung                               |
| -------------------------- | ------------------------------------------ |
| `make up`                  | Alle Container starten                     |
| `make down`                | Alle Container stoppen                     |
| `make rebuild`             | Alles neu bauen und starten                |
| `make rebuild-backend`     | Nur Backend neu bauen                      |
| `make rebuild-frontend`    | Nur Frontend neu bauen                     |
| `make generate-vapid-keys` | VAPID-Schluessel fuer Web-Push erzeugen    |
| `make logs`                | Alle Logs anzeigen                         |
| `make logs-backend`        | Nur Backend-Logs                           |
| `make db-seed`             | DB zurücksetzen und Testdaten laden        |
| `make db-shell`            | PostgreSQL Shell öffnen                    |
| `make ps`                  | Container-Status anzeigen                  |
| `make clean`               | Alles entfernen (inkl. Volumes und Images) |

### Web Push / PWA Benachrichtigungen

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

## Role Permissions

| Permission               | admin              | Stufenleiter                    | Leiter               | Pio                             |
| ------------------------ | ------------------ | ------------------------------- | -------------------- | ------------------------------- |
| View activities          | ✅ all             | ✅ all                          | ✅ all               | ✅ own dept only                |
| Create activity          | ✅ any dept        | ✅ own dept + Leiter            | ✅ own dept + Leiter | ✅ own dept only                |
| Edit activity            | ✅                 | ✅ own dept + if verantwortlich | ✅ if verantwortlich | ✅ if verantwortlich (own dept) |
| Delete activity          | ✅                 | ✅ own dept + if verantwortlich | ✅ if verantwortlich | ✅ if verantwortlich (own dept) |
| Send mail                | ✅                 | ✅ own dept + if verantwortlich | ✅ if verantwortlich | ✅ if verantwortlich            |
| Change own dept          | ✅                 | ❌                              | ❌                   | ❌                              |
| View mail templates      | ✅ any             | ✅ own dept only                | ❌                   | ❌                              |
| Edit mail template       | ✅ any             | ✅ own dept only                | ❌                   | ❌                              |
| View users (admin panel) | ✅ all             | ✅ own dept only                | ❌                   | ❌                              |
| Edit users (admin panel) | ✅ all, incl. role | ✅ own dept, no role change     | ❌                   | ❌                              |

_As of 10 April 2026._

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
2. Als Kinder werden Personen gezählt, die in MiData keine Einträge in `links.roles` haben.
3. Personen mit mindestens einer Rolle (`links.roles`) werden nicht mitgezählt.

### Runtime-Konfiguration (Admin)

Ein Grossteil der Integrations-Settings kann direkt in der Web-App gepflegt werden.

- Secrets werden verschlüsselt in der DB gespeichert (pgcrypto) und nie im Klartext zurückgegeben.
- Nicht-sensitive Werte (z. B. Timeouts) dürfen lesbar sein.
- Laufzeit liest aus der DB; gesetzte ENV-Werte haben Prioritaet und sperren das entsprechende Feld.
- Falls ENV gesetzt ist, werden diese Werte beim Backend-Start in die DB importiert und dort synchron gehalten.
- Standardwerte werden bereits über `db/init.sql` in `app_settings` gesetzt und können danach über Admin oder ENV-Import überschrieben werden.

Wichtige ENV-Variablen:

- Pflicht via `.env`: `DPW_CONFIG_ENCRYPTION_KEY`.
- Optional via `.env` oder Admin/Setup: `DPW_VAPID_PUBLIC_KEY`, `DPW_VAPID_PRIVATE_KEY`, `DPW_VAPID_SUBJECT`.
- Optional via `.env` (nur Initial-Import beim Start):
  `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET`,
  `MIDATA_API_KEY`, `MIDATA_API_URL_TEMPLATE`, `MIDATA_API_TIMEOUT_MS`,
  `DPW_WP_URL`, `DPW_WP_USER`, `DPW_WP_APP_PASSWORD`.

Falls Azure-Werte nicht in `.env` gesetzt sind, steht ein Initial-Setup ohne Login bereit:

- `GET /api/setup/auth-config` (Status)
- `POST /api/setup/auth-config` (tenant_id, client_id, client_secret, contact_email)

## Redis Cache (optional)

Das Backend kann API-Responses in Redis cachen, um wiederholte Lese-Anfragen zu beschleunigen.

Aktivierung:

1. In Docker Compose ist ein `redis` Service bereits hinterlegt.
2. Redis- und SQL-Verbindungswerte sind fest in den Docker-Compose-Dateien definiert (keine `.env`-Anpassung nötig).

Verhalten:

1. Ohne laufenden Redis-Service bleibt das Caching automatisch deaktiviert.
2. Schreiboperationen heben betroffene Cache-Bereiche per Versions-Invalidierung auf.
3. Die TTL begrenzt zusätzliche Stale-Zeit auf wenige Sekunden.
