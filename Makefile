.PHONY: up down build rebuild logs logs-backend logs-frontend logs-db \
       restart restart-backend restart-frontend restart-db \
       db-reset db-seed ps clean

# ── Alles starten / stoppen ──────────────────────────────────────────────────
up:
	docker compose up -d

down:
	docker compose down

# ── Bauen ────────────────────────────────────────────────────────────────────
build:
	docker compose build

rebuild:
	docker compose up -d --no-deps --build --force-recreate

rebuild-backend:
	docker compose up -d --no-deps --build --force-recreate backend

rebuild-frontend:
	docker compose up -d --no-deps --build --force-recreate frontend

# ── Logs ─────────────────────────────────────────────────────────────────────
logs:
	docker compose logs -f

logs-backend:
	docker compose logs -f backend

logs-frontend:
	docker compose logs -f frontend

logs-db:
	docker compose logs -f db

# ── Neustart (ohne neu zu bauen) ─────────────────────────────────────────────
restart:
	docker compose restart

restart-backend:
	docker compose restart backend

restart-frontend:
	docker compose restart frontend

restart-db:
	docker compose restart db

# ── Datenbank ────────────────────────────────────────────────────────────────
db-reset:
	docker compose down -v
	docker compose up -d db
	@echo "⏳ Warte auf DB (healthcheck) …"
	@until [ "$$(docker inspect --format='{{.State.Health.Status}}' dpw-db-1 2>/dev/null)" = "healthy" ]; do sleep 1; done
	@echo "✅ DB bereit (Schema aus init.sql geladen)"

db-seed: db-reset
	docker compose exec -T db psql -U activities_user -d activities < db/seed.sql
	@echo "✅ Testdaten geladen"
	docker compose up -d

db-shell:
	docker compose exec db psql -U activities_user -d activities

# ── Status / Aufräumen ───────────────────────────────────────────────────────
ps:
	docker compose ps

clean:
	docker compose down -v --rmi local --remove-orphans
