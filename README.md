# DPW

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
