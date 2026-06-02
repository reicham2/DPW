#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${DPW_API_BASE_URL:-http://localhost:8000/api}"
RESP_CODE=""
RESP_BODY=""

request() {
  local method="$1"
  local path="$2"
  local body="${3:-}"
  local token="${4:-}"

  local tmp
  tmp="$(mktemp)"

  local -a args
  args=( -sS -o "$tmp" -w "%{http_code}" -X "$method" "${BASE_URL}${path}" )

  if [[ -n "$token" ]]; then
    args+=( -H "Authorization: Bearer ${token}" )
  fi
  if [[ -n "$body" ]]; then
    args+=( -H "Content-Type: application/json" --data "$body" )
  fi

  RESP_CODE="$(curl "${args[@]}")"
  RESP_BODY="$(cat "$tmp")"
  rm -f "$tmp"
}

assert_status() {
  local expected="$1"
  local label="$2"
  if [[ "$RESP_CODE" != "$expected" ]]; then
    echo "FAIL: ${label} (expected ${expected}, got ${RESP_CODE})"
    echo "Response body: ${RESP_BODY}"
    exit 1
  fi
  echo "OK: ${label}"
}

assert_status_in() {
  local expected_csv="$1"
  local label="$2"
  local ok="0"
  IFS=',' read -r -a expected <<< "$expected_csv"
  for code in "${expected[@]}"; do
    if [[ "$RESP_CODE" == "$code" ]]; then
      ok="1"
      break
    fi
  done
  if [[ "$ok" != "1" ]]; then
    echo "FAIL: ${label} (expected one of ${expected_csv}, got ${RESP_CODE})"
    echo "Response body: ${RESP_BODY}"
    exit 1
  fi
  echo "OK: ${label}"
}

echo "Running API tests against ${BASE_URL}"

# 1) Health
request "GET" "/health"
assert_status "200" "health endpoint"

# 2) Unauthenticated access should fail
request "GET" "/activities"
assert_status "401" "activities require auth"

# 3) Invalid token should fail
request "GET" "/activities" "" "invalid-token"
assert_status "401" "invalid token is rejected"

# 4) Login endpoint should require auth header
request "POST" "/auth/me"
assert_status "401" "auth/me requires bearer token"

# 5) Resolve a debug user (requires debug-auth build)
request "GET" "/debug/users"
assert_status "200" "debug users endpoint"

USER_INFO="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const users = JSON.parse(fs.readFileSync(0, "utf8"));
if (!Array.isArray(users) || users.length === 0) process.exit(2);
const u = users.find((x) => x.role === "admin") || users[0];
if (!u || !u.id) process.exit(3);
const dept = u.department == null ? "" : String(u.department);
process.stdout.write(`${u.id}|${String(u.role || "")}|${dept}`);
')"

USER_ID="${USER_INFO%%|*}"
REST="${USER_INFO#*|}"
USER_ROLE="${REST%%|*}"
USER_DEPT="${REST#*|}"
TOKEN="debug:${USER_ID}"

echo "Using debug user ${USER_ID} (role=${USER_ROLE}, dept=${USER_DEPT:-none})"

# 6) Auth flow
request "POST" "/auth/me" "" "$TOKEN"
assert_status "200" "auth/me with debug token"

request "GET" "/me" "" "$TOKEN"
assert_status "200" "me endpoint"

# 7) Activities list
request "GET" "/activities" "" "$TOKEN"
assert_status "200" "list activities"

# 8) Negative create case (missing required fields)
request "POST" "/activities" '{}' "$TOKEN"
assert_status "400" "create activity validation"

# 9) Create activity
SUFFIX="$(date +%s)"
CREATE_PAYLOAD="$(cat <<JSON
{
  "title": "API Test ${SUFFIX}",
  "date": "2026-12-31",
  "start_time": "10:00",
  "end_time": "11:00",
  "goal": "API test goal",
  "location": "API test location",
  "responsible": ["Debug"],
  "material": [],
  "tn_material": [],
  "programs": []
}
JSON
)"
request "POST" "/activities" "$CREATE_PAYLOAD" "$TOKEN"
assert_status "201" "create activity"

ACTIVITY_ID="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
process.stdout.write(String(obj.id || ""));
')"

if [[ -z "$ACTIVITY_ID" ]]; then
  echo "FAIL: create activity returned no id"
  echo "Response body: ${RESP_BODY}"
  exit 1
fi

echo "Created activity ${ACTIVITY_ID}"

# 10) Read created activity
request "GET" "/activities/${ACTIVITY_ID}" "" "$TOKEN"
assert_status "200" "get created activity"

# 11) Update activity
PATCH_PAYLOAD="$(cat <<JSON
{
  "title": "API Test Updated ${SUFFIX}",
  "date": "2026-12-31",
  "start_time": "10:00",
  "end_time": "11:30",
  "goal": "Updated goal",
  "location": "Updated location",
  "responsible": ["Debug"],
  "material": [],
  "tn_material": ["Cup"],
  "programs": []
}
JSON
)"
request "PATCH" "/activities/${ACTIVITY_ID}" "$PATCH_PAYLOAD" "$TOKEN"
assert_status "200" "update activity"

UPDATED_TITLE="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
process.stdout.write(String(obj.title || ""));
')"
if [[ "$UPDATED_TITLE" != "API Test Updated ${SUFFIX}" ]]; then
  echo "FAIL: updated title mismatch"
  echo "Response body: ${RESP_BODY}"
  exit 1
fi

echo "OK: update reflected in response"

# 12) Delete activity
request "DELETE" "/activities/${ACTIVITY_ID}" "" "$TOKEN"
assert_status_in "200,204" "delete activity"

# 13) Ensure deleted activity is gone
request "GET" "/activities/${ACTIVITY_ID}" "" "$TOKEN"
assert_status "404" "deleted activity not found"

echo "API test suite passed"
