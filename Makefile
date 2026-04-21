# ── Pinned dependency versions ───────────────────────────────────────────────
UWEBSOCKETS_VERSION := v20.62.0
USOCKETS_VERSION    := v0.8.8

# Dev-Compose-Datei (docker-compose.yml = Produktion mit GHCR-Images)
DC := docker compose -f docker-compose-dev.yml

.PHONY: up down build rebuild logs logs-backend logs-frontend logs-db \
       restart restart-backend restart-frontend restart-db \
	db-reset db-seed ps clean full-rebuild update-deps \
	build-prod rebuild-prod rebuild-backend-prod rebuild-frontend-prod \
	new-branch fresh test test-frontend test-backend test-watch

# ── Shortcut-Targets ─────────────────────────────────────────────────────────
new-branch: clean rebuild db-seed

fresh:
	$(DC) down -v --remove-orphans
	BACKEND_BUILD_TYPE=Debug ENABLE_DEBUG_AUTH=1 FRONTEND_ENABLE_DEBUG_AUTH=1 $(DC) up -d --build --force-recreate --wait
	$(DC) exec -T db psql -U activities_user -d activities < db/seed.sql
	@echo "✅ Frische Umgebung aufgebaut und Testdaten geladen"

# ── Tests (laufen im Container) ──────────────────────────────────────────────
test: test-frontend test-backend

test-frontend:
	@echo "── Frontend Unit Tests ──────────────────────────────────────────"
	docker run --rm -w /app -v "$(CURDIR)/frontend:/app" node:22-alpine sh -c "npm ci --silent 2>/dev/null && npx vitest run --reporter=verbose"

test-backend:
	@echo "── Backend Unit Tests ───────────────────────────────────────────"
	docker build -f backend/Dockerfile.test backend/ -t dpw-test-backend -q && \
	docker run --rm dpw-test-backend

# ── Alles starten / stoppen ──────────────────────────────────────────────────
up:
	$(DC) up -d

down:
	$(DC) down

# ── Bauen (immer Debug + Debug-Auth) ─────────────────────────────────────────
build:
	BACKEND_BUILD_TYPE=Debug ENABLE_DEBUG_AUTH=1 FRONTEND_ENABLE_DEBUG_AUTH=1 $(DC) build

rebuild:
	BACKEND_BUILD_TYPE=Debug ENABLE_DEBUG_AUTH=1 FRONTEND_ENABLE_DEBUG_AUTH=1 $(DC) up -d --no-deps --build --force-recreate

rebuild-backend:
	BACKEND_BUILD_TYPE=Debug ENABLE_DEBUG_AUTH=1 $(DC) up -d --no-deps --build --force-recreate backend

rebuild-frontend:
	FRONTEND_ENABLE_DEBUG_AUTH=1 $(DC) up -d --no-deps --build --force-recreate frontend

# ── Bauen (Production: Release, kein Debug-Auth) ─────────────────────────────
build-prod:
	BACKEND_BUILD_TYPE=Release ENABLE_DEBUG_AUTH=0 FRONTEND_ENABLE_DEBUG_AUTH=0 $(DC) build

rebuild-prod:
	BACKEND_BUILD_TYPE=Release ENABLE_DEBUG_AUTH=0 FRONTEND_ENABLE_DEBUG_AUTH=0 $(DC) up -d --no-deps --build --force-recreate

rebuild-backend-prod:
	BACKEND_BUILD_TYPE=Release ENABLE_DEBUG_AUTH=0 $(DC) up -d --no-deps --build --force-recreate backend

rebuild-frontend-prod:
	FRONTEND_ENABLE_DEBUG_AUTH=0 $(DC) up -d --no-deps --build --force-recreate frontend

# ── Logs ─────────────────────────────────────────────────────────────────────
logs:
	$(DC) logs -f

logs-backend:
	$(DC) logs -f backend

logs-frontend:
	$(DC) logs -f frontend

logs-db:
	$(DC) logs -f db

# ── Neustart (ohne neu zu bauen) ─────────────────────────────────────────────
restart:
	$(DC) restart

restart-backend:
	$(DC) restart backend

restart-frontend:
	$(DC) restart frontend

restart-db:
	$(DC) restart db

# ── Datenbank ────────────────────────────────────────────────────────────────
db-reset:
	$(DC) down -v
	$(DC) up -d --wait
	@echo "✅ DB bereit (Schema aus init.sql geladen)"

db-seed: db-reset
	$(DC) exec -T db psql -U activities_user -d activities < db/seed.sql
	@echo "✅ Testdaten geladen"

db-shell:
	$(DC) exec db psql -U activities_user -d activities

# ── Status / Aufräumen ───────────────────────────────────────────────────────
ps:
	$(DC) ps

clean:
	$(DC) down -v --rmi local --remove-orphans

# ── Backend-Deps aktualisieren ───────────────────────────────────────────────
update-deps:
	rm -rf backend/deps/uWebSockets backend/deps/uSockets
	git -c advice.detachedHead=false clone --depth 1 --branch $(UWEBSOCKETS_VERSION) https://github.com/uNetworking/uWebSockets.git backend/deps/uWebSockets
	git -c advice.detachedHead=false clone --depth 1 --branch $(USOCKETS_VERSION) https://github.com/uNetworking/uSockets.git backend/deps/uSockets
	rm -rf backend/deps/uWebSockets/.git backend/deps/uSockets/.git
	@echo "✅ Deps aktualisiert (uWebSockets $(UWEBSOCKETS_VERSION), uSockets $(USOCKETS_VERSION))"

# ── Komplett-Rebuild (Cache löschen) ─────────────────────────────────────────
full-rebuild: update-deps
	docker builder prune --all -f
	$(DC) build --no-cache
	$(DC) up -d --force-recreate
	@echo "✅ Full rebuild abgeschlossen"
