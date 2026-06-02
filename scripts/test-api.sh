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

require_non_empty() {
  local value="$1"
  local label="$2"
  if [[ -z "$value" ]]; then
    echo "FAIL: ${label} is empty"
    echo "Response body: ${RESP_BODY}"
    exit 1
  fi
}

echo "Running API tests against ${BASE_URL}"

# 1) Health
request "GET" "/health"
assert_status "200" "health endpoint"

# 1b) Status
request "GET" "/status"
assert_status "200" "status endpoint"

# 2) Unauthenticated access should fail
request "GET" "/activities"
assert_status "401" "activities require auth"

# 3) Invalid token should fail
request "GET" "/activities" "" "invalid-token"
assert_status "401" "invalid token is rejected"

# 4) Login endpoint should require auth header
request "POST" "/auth/me"
assert_status "401" "auth/me requires bearer token"

# 4b) Additional protected routes must reject missing auth
request "GET" "/departments"
assert_status "401" "departments require auth"

request "GET" "/users"
assert_status "401" "users require auth"

request "GET" "/my-permissions"
assert_status "401" "my-permissions require auth"

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

ME_ID="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
process.stdout.write(String(obj.id || ""));
')"
require_non_empty "$ME_ID" "me.id"

# 6b) User profile patch validation + update
request "PATCH" "/me" '{"time_display_mode":"invalid-mode"}' "$TOKEN"
assert_status "400" "patch me rejects invalid time_display_mode"

request "PATCH" "/me" '{"time_display_mode":"clock","notify_channel_email":false}' "$TOKEN"
assert_status "200" "patch me updates preferences"

PATCHED_MODE="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
process.stdout.write(String(obj.time_display_mode || ""));
')"
if [[ "$PATCHED_MODE" != "clock" ]]; then
  echo "FAIL: patched time_display_mode mismatch"
  echo "Response body: ${RESP_BODY}"
  exit 1
fi
echo "OK: patch me reflected in response"

# 6c) Authenticated core endpoints
request "GET" "/users" "" "$TOKEN"
assert_status "200" "list users"

FOUND_SELF="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const users = JSON.parse(fs.readFileSync(0, "utf8"));
const meId = process.argv[1];
const ok = Array.isArray(users) && users.some((u) => String(u.id || "") === meId);
process.stdout.write(ok ? "1" : "0");
' "$ME_ID")"
if [[ "$FOUND_SELF" != "1" ]]; then
  echo "FAIL: current user not found in /users"
  echo "Response body: ${RESP_BODY}"
  exit 1
fi
echo "OK: users list contains current user"

request "GET" "/departments" "" "$TOKEN"
assert_status "200" "list departments"

request "GET" "/my-permissions" "" "$TOKEN"
assert_status "200" "my-permissions endpoint"

request "GET" "/admin/roles" "" "$TOKEN"
assert_status "200" "admin roles endpoint"

request "GET" "/admin/departments" "" "$TOKEN"
assert_status "200" "admin departments endpoint"

request "GET" "/admin/role-permissions" "" "$TOKEN"
assert_status "200" "admin role-permissions endpoint"

request "GET" "/admin/activities/trash" "" "$TOKEN"
assert_status "200" "admin trash endpoint"

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

# 10b) Optional share-link/public-link checks (depends on public_base_url)
request "GET" "/activities/${ACTIVITY_ID}/share" "" "$TOKEN"
if [[ "$RESP_CODE" == "409" ]]; then
  echo "SKIP: share-link checks (public_base_url not configured)"
else
  assert_status "200" "get share link"

  SHARE_TOKEN="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
const t = obj.share_token;
process.stdout.write(t == null ? "" : String(t));
')"

  if [[ -z "$SHARE_TOKEN" ]]; then
    request "POST" "/activities/${ACTIVITY_ID}/share" "" "$TOKEN"
    assert_status "201" "create share link"
    SHARE_TOKEN="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
process.stdout.write(String(obj.share_token || ""));
')"
  fi

  require_non_empty "$SHARE_TOKEN" "share token"

  request "GET" "/shared/${SHARE_TOKEN}"
  assert_status "200" "public shared activity"

  request "DELETE" "/activities/${ACTIVITY_ID}/share" "" "$TOKEN"
  assert_status "200" "delete share link"

  request "GET" "/activities/${ACTIVITY_ID}/share" "" "$TOKEN"
  assert_status "200" "get share link after delete"
  SHARE_TOKEN_AFTER_DELETE="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
const t = obj.share_token;
process.stdout.write(t == null ? "" : String(t));
')"
  if [[ -n "$SHARE_TOKEN_AFTER_DELETE" ]]; then
    echo "FAIL: share token should be null/empty after delete"
    echo "Response body: ${RESP_BODY}"
    exit 1
  fi
  echo "OK: share token cleared after delete"
fi

# 10c) Form lifecycle: create, public fetch, submit, admin responses, cleanup
request "GET" "/activities/${ACTIVITY_ID}/form" "" "$TOKEN"
assert_status "200" "get activity form before create"
FORM_EXISTS_BEFORE="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
process.stdout.write(obj.exists ? "1" : "0");
')"
if [[ "$FORM_EXISTS_BEFORE" != "0" ]]; then
  echo "FAIL: form should not exist before create"
  echo "Response body: ${RESP_BODY}"
  exit 1
fi
echo "OK: form absent before create"

request "POST" "/activities/${ACTIVITY_ID}/form" '{}' "$TOKEN"
assert_status "400" "create form validation"

FORM_CREATE_PAYLOAD='{"form_type":"registration","title":"API Form Test","questions":[{"question_text":"Name","question_type":"text_input","is_required":true}]}'
request "POST" "/activities/${ACTIVITY_ID}/form" "$FORM_CREATE_PAYLOAD" "$TOKEN"
assert_status "201" "create activity form"

FORM_INFO="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
const q = Array.isArray(obj.questions) && obj.questions.length > 0 ? obj.questions[0] : null;
process.stdout.write(`${String(obj.id || "")}|${String(obj.public_slug || "")}|${String(q && q.id ? q.id : "")}`);
')"
FORM_ID="${FORM_INFO%%|*}"
REST_FORM_INFO="${FORM_INFO#*|}"
FORM_PUBLIC_SLUG="${REST_FORM_INFO%%|*}"
FORM_QUESTION_ID="${REST_FORM_INFO#*|}"
require_non_empty "$FORM_ID" "form id"
require_non_empty "$FORM_PUBLIC_SLUG" "form public_slug"
require_non_empty "$FORM_QUESTION_ID" "form question id"

request "GET" "/activities/${ACTIVITY_ID}/form" "" "$TOKEN"
assert_status "200" "get activity form after create"
FORM_EXISTS_AFTER="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
process.stdout.write(obj.exists ? "1" : "0");
')"
if [[ "$FORM_EXISTS_AFTER" != "1" ]]; then
  echo "FAIL: form should exist after create"
  echo "Response body: ${RESP_BODY}"
  exit 1
fi
echo "OK: form exists after create"

request "GET" "/activities/${ACTIVITY_ID}/form/stats" "" "$TOKEN"
assert_status "200" "get form stats"

request "GET" "/forms/${FORM_PUBLIC_SLUG}"
assert_status "200" "get public form"

request "POST" "/forms/${FORM_PUBLIC_SLUG}/submit" '{"answers":[]}'
assert_status "400" "public form submit missing required answer"

PUBLIC_SUBMIT_PAYLOAD="$(cat <<JSON
{
  "answers": [
    {
      "question_id": "${FORM_QUESTION_ID}",
      "answer_value": "Tester"
    }
  ]
}
JSON
)"
request "POST" "/forms/${FORM_PUBLIC_SLUG}/submit" "$PUBLIC_SUBMIT_PAYLOAD"
assert_status "201" "public form submit"

FORM_RESPONSE_ID="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
process.stdout.write(String(obj.id || ""));
')"
require_non_empty "$FORM_RESPONSE_ID" "form response id"

request "GET" "/activities/${ACTIVITY_ID}/form/responses" "" "$TOKEN"
assert_status "200" "list form responses"

request "GET" "/activities/${ACTIVITY_ID}/form/responses/${FORM_RESPONSE_ID}" "" "$TOKEN"
assert_status "200" "get single form response"

request "DELETE" "/activities/${ACTIVITY_ID}/form/responses/${FORM_RESPONSE_ID}" "" "$TOKEN"
assert_status "200" "delete form response"

request "GET" "/activities/${ACTIVITY_ID}/form/responses/${FORM_RESPONSE_ID}" "" "$TOKEN"
assert_status "404" "deleted form response not found"

request "DELETE" "/activities/${ACTIVITY_ID}/form" "" "$TOKEN"
assert_status "200" "delete activity form"

request "GET" "/activities/${ACTIVITY_ID}/form" "" "$TOKEN"
assert_status "200" "get activity form after delete"
FORM_EXISTS_FINAL="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const obj = JSON.parse(fs.readFileSync(0, "utf8"));
process.stdout.write(obj.exists ? "1" : "0");
')"
if [[ "$FORM_EXISTS_FINAL" != "0" ]]; then
  echo "FAIL: form should not exist after delete"
  echo "Response body: ${RESP_BODY}"
  exit 1
fi
echo "OK: form removed"

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

# 14) Admin trash and restore
request "GET" "/admin/activities/trash" "" "$TOKEN"
assert_status "200" "list trash after delete"

IN_TRASH="$(printf '%s' "$RESP_BODY" | node -e '
const fs = require("fs");
const arr = JSON.parse(fs.readFileSync(0, "utf8"));
const id = process.argv[1];
const ok = Array.isArray(arr) && arr.some((a) => {
  const top = String(a && a.id ? a.id : "");
  const nested = String(a && a.activity && a.activity.id ? a.activity.id : "");
  return top === id || nested === id;
});
process.stdout.write(ok ? "1" : "0");
' "$ACTIVITY_ID")"
if [[ "$IN_TRASH" != "1" ]]; then
  echo "FAIL: deleted activity not present in trash"
  echo "Response body: ${RESP_BODY}"
  exit 1
fi
echo "OK: deleted activity appears in trash"

request "POST" "/admin/activities/${ACTIVITY_ID}/restore" "" "$TOKEN"
assert_status "200" "restore deleted activity"

request "GET" "/activities/${ACTIVITY_ID}" "" "$TOKEN"
assert_status "200" "restored activity is readable"

# 15) Final cleanup delete
request "DELETE" "/activities/${ACTIVITY_ID}" "" "$TOKEN"
assert_status_in "200,204" "final delete activity"

request "GET" "/activities/${ACTIVITY_ID}" "" "$TOKEN"
assert_status "404" "final deleted activity not found"

echo "API test suite passed"
