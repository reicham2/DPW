#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

: "${BASELINE_BACKEND_IMAGE:?Set the immutable old backend image}"
: "${BASELINE_FRONTEND_IMAGE:?Set the immutable old frontend image}"
: "${BASELINE_COMPOSE_FILE:?Set the production Compose file from the baseline revision}"
: "${CANDIDATE_BACKEND_IMAGE:?Set the PR backend image}"
: "${CANDIDATE_FRONTEND_IMAGE:?Set the PR frontend image}"
export UPGRADE_ENCRYPTION_KEY="${UPGRADE_ENCRYPTION_KEY:-$(openssl rand -hex 32)}"
project="dpw-upgrade-${GITHUB_RUN_ID:-local}-$$"
upgraded_project="$project"
reference="${project}-reference"
compose_file="$BASELINE_COMPOSE_FILE"
output="${UPGRADE_RESULTS_DIR:-$(mktemp -d)}"
mkdir -p "$output"
output="$(cd "$output" && pwd)"

compose() {
    docker compose --project-directory "$PWD" -p "$project" \
        -f "$compose_file" -f scripts/upgrade-compose.yml "$@"
}
sql() {
    compose exec -T db psql -X -qAt -v ON_ERROR_STOP=1 \
        -U activities_user -d activities "$@"
}
use_images() {
    export UPGRADE_BACKEND_IMAGE="$1" UPGRADE_FRONTEND_IMAGE="$2"
}
cleanup() {
    local result=$?
    trap - EXIT
    set +e
    for project in "$upgraded_project" "$reference"; do
        compose ps -a > "$output/${project}-status.txt" 2>&1
        compose logs --no-color > "$output/${project}-logs.txt" 2>&1
        if ! compose down -v --remove-orphans; then result=1; fi
    done
    echo "Upgrade diagnostics: $output"
    exit "$result"
}
use_images "$BASELINE_BACKEND_IMAGE" "$BASELINE_FRONTEND_IMAGE"
trap cleanup EXIT

docker run --rm --entrypoint cat "$CANDIDATE_BACKEND_IMAGE" /etc/dpw/init.sql \
    > "$output/candidate-init.sql"
if ! diff -u --strip-trailing-cr db/init.sql "$output/candidate-init.sql" \
    > "$output/candidate-schema-source.diff"; then
    echo "Candidate image schema does not match the PR's db/init.sql" >&2
    exit 1
fi

assert_services() {
    local service id state
    local services="db redis backend frontend"
    if compose config --services | grep -Fxq languagetool; then
        services="$services languagetool"
    fi
    for service in $services; do
        id="$(compose ps -a -q "$service")"
        [ -n "$id" ] || { echo "Missing service: $service" >&2; return 1; }
        state="$(docker inspect --format \
            '{{.State.Status}} {{if .State.Health}}{{.State.Health.Status}}{{else}}missing-healthcheck{{end}} {{.RestartCount}}' "$id")"
        [ "$state" = "running healthy 0" ] || {
            echo "$service not stable: $state" >&2; return 1;
        }
    done
    local address
    address="$(compose port frontend 80)"
    curl --fail --silent --show-error "http://$address/" > /dev/null
    curl --fail --silent --show-error "http://$address/api/health" > /dev/null
}
assert_schema() {
    sql < scripts/upgrade-schema.sql > "$output/upgraded-schema.txt"
    diff -u "$output/reference-schema.txt" "$output/upgraded-schema.txt"
}
assert_data() {
    sql < scripts/upgrade-assert-data.sql
    [ "$(sql -v encryption_key="$UPGRADE_ENCRYPTION_KEY" <<'SQL'
SELECT pgp_sym_decrypt(value_secret, :'encryption_key')
FROM app_settings WHERE key = 'wp.app_password';
SQL
    )" = 'upgrade-test-secret' ]
}
volume() {
    docker inspect --format '{{range .Mounts}}{{if eq .Destination "/var/lib/postgresql/data"}}{{.Name}}{{end}}{{end}}' \
        "$(compose ps -q db)"
}

echo "Starting old production environment"
compose up -d --wait --wait-timeout 180
assert_services
sql -v encryption_key="$UPGRADE_ENCRYPTION_KEY" < scripts/upgrade-fixtures.sql
assert_data
old_volume="$(volume)"
[ -n "$old_volume" ]
old_cluster="$(sql -c 'SELECT system_identifier FROM pg_control_system()')"
sql < scripts/upgrade-schema.sql > "$output/baseline-schema.txt"

# Stop containers without removing the volume. Recreate ALL services on that volume.
compose stop
compose_file=docker-compose.yml
use_images "$CANDIDATE_BACKEND_IMAGE" "$CANDIDATE_FRONTEND_IMAGE"
echo "Upgrading persisted environment to the PR images"
compose up -d --force-recreate --wait --wait-timeout 180
[ "$(volume)" = "$old_volume" ]
[ "$(sql -c 'SELECT system_identifier FROM pg_control_system()')" = "$old_cluster" ]
assert_services
assert_data
sql <<'SQL'
INSERT INTO users (id, microsoft_oid, email, display_name)
VALUES ('15500000-0000-0000-0000-000000000009', 'upgrade-proofreading-user',
        'proofreading@example.invalid', 'Proofreading upgrade user');
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM users WHERE id='15500000-0000-0000-0000-000000000001' AND proofreading_enabled
    ) OR NOT EXISTS (
        SELECT 1 FROM users WHERE id='15500000-0000-0000-0000-000000000009' AND proofreading_enabled
    ) THEN
        RAISE EXCEPTION 'Existing and new users must default to enabled proofreading after upgrade';
    END IF;
END $$;
UPDATE users SET proofreading_enabled=false WHERE id='15500000-0000-0000-0000-000000000009';
SQL
sql -c "INSERT INTO proofreading_dictionary(user_id, word) VALUES ('15500000-0000-0000-0000-000000000001', 'Pfadihüttli')"

# Independent fresh environment is an oracle only, never the upgrade under test.
project="$reference"
compose up -d --wait --wait-timeout 180
assert_services
sql < scripts/upgrade-schema.sql > "$output/reference-schema.txt"
project="$upgraded_project"
assert_schema

echo "Restarting the PR backend to verify idempotence"
compose restart backend
compose up -d --wait --wait-timeout 180
assert_services
assert_data
assert_schema
[ "$(sql -c "SELECT count(*) FROM proofreading_dictionary WHERE user_id='15500000-0000-0000-0000-000000000001' AND word='Pfadihüttli'")" = 1 ]
sql <<'SQL'
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM users WHERE id='15500000-0000-0000-0000-000000000009' AND NOT proofreading_enabled
    ) OR NOT EXISTS (
        SELECT 1 FROM users WHERE id='15500000-0000-0000-0000-000000000001' AND proofreading_enabled
    ) THEN
        RAISE EXCEPTION 'Backend restart changed or leaked persisted proofreading preferences';
    END IF;
END $$;
BEGIN;
DELETE FROM users WHERE id='15500000-0000-0000-0000-000000000001';
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM proofreading_dictionary WHERE user_id='15500000-0000-0000-0000-000000000001') THEN
        RAISE EXCEPTION 'Dictionary entries survived user deletion';
    END IF;
END;
$$;
ROLLBACK;
SQL

echo "Checking that the oracles detect data loss, schema drift and a stopped service"
if {
    printf "BEGIN; UPDATE activities SET title = 'CORRUPTED' WHERE id = '15500000-0000-0000-0000-000000000002';\n"
    cat scripts/upgrade-assert-data.sql
} | sql > "$output/expected-data-failure.txt" 2>&1; then
    echo "Data loss was not detected" >&2
    exit 1
fi
# The rejected transaction above is rolled back when psql exits.
assert_data
{
    printf 'BEGIN; ALTER TABLE activities ADD COLUMN upgrade_ci_unexpected text;\n'
    cat scripts/upgrade-schema.sql
    printf 'ROLLBACK;\n'
} | sql > "$output/invalid-schema.txt"
if diff -u "$output/reference-schema.txt" "$output/invalid-schema.txt" > "$output/expected-schema-failure.txt"; then
    echo "Schema drift was not detected" >&2
    exit 1
fi
compose stop frontend
if assert_services > "$output/expected-service-failure.txt" 2>&1; then
    echo "Stopped container was not detected" >&2
    exit 1
fi
compose up -d --wait --wait-timeout 180
assert_services
assert_data
assert_schema
echo "Persisted environment upgrade, repeated startup and failure detection passed."
