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

### Nützliche Befehle

| Befehl                  | Beschreibung                               |
| ----------------------- | ------------------------------------------ |
| `make up`               | Alle Container starten                     |
| `make down`             | Alle Container stoppen                     |
| `make rebuild`          | Alles neu bauen und starten                |
| `make rebuild-backend`  | Nur Backend neu bauen                      |
| `make rebuild-frontend` | Nur Frontend neu bauen                     |
| `make logs`             | Alle Logs anzeigen                         |
| `make logs-backend`     | Nur Backend-Logs                           |
| `make db-seed`          | DB zurücksetzen und Testdaten laden        |
| `make db-shell`         | PostgreSQL Shell öffnen                    |
| `make ps`               | Container-Status anzeigen                  |
| `make clean`            | Alles entfernen (inkl. Volumes und Images) |

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

1. `MIDATA_API_KEY` in `.env` setzen.
2. Falls nötig das URL-Template via `MIDATA_API_URL_TEMPLATE` anpassen (Header ist fix `X-Token`).
3. Pro Stufe in der Admin-Ansicht eine MiData-Gruppen-ID hinterlegen.

Hinweise:

1. Ohne Gruppen-ID oder API-Key bleibt der MiData-Wert leer (`Nicht konfiguriert`).
2. Als Kinder werden Personen gezählt, die in MiData keine Einträge in `links.roles` haben.
3. Personen mit mindestens einer Rolle (`links.roles`) werden nicht mitgezählt.
