// Run only against a disposable seeded Debug backend/database.
// AUDIT_BASE_URL=http://127.0.0.1:<port> AUDIT_DB_CONTAINER=<name> node scripts/test-audit.mjs
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { randomUUID } from 'node:crypto'
import net from 'node:net'
import { readFileSync } from 'node:fs'

const base = process.env.AUDIT_BASE_URL
const backendBase = process.env.AUDIT_BACKEND_URL || base
const container = process.env.AUDIT_DB_CONTAINER
assert(base && container, 'Explicit disposable AUDIT_BASE_URL and AUDIT_DB_CONTAINER are required')
const admin = 'a0000000-0000-0000-0000-000000000001'
const member = 'a0000000-0000-0000-0000-000000000003'
const otherMember = 'a0000000-0000-0000-0000-000000000004'
const sql = query => execFileSync('docker', [
  'exec', '-i', container, 'psql', '-U', process.env.AUDIT_DB_USER || 'postgres', '-d', 'activities', '-v', 'ON_ERROR_STOP=1', '-Atc', query,
], { encoding: 'utf8' }).trim()
async function request(path, method = 'GET', body, actor = admin) {
  const response = await fetch(`${base}${path}`, {
    method,
    headers: { ...(actor ? { Authorization: `Bearer debug:${actor}` } : {}), 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  })
  const text = await response.text()
  return { status: response.status, text, data: text ? JSON.parse(text) : null }
}
const locationName = `audit-test-${randomUUID()}`
const materialName = `audit-material-${randomUUID()}`
let locationId, materialId, activityId, activityShareToken, fixtureUserId
const count = () => Number(sql('SELECT count(*) FROM audit_events'))
const latest = () => JSON.parse(sql("SELECT row_to_json(a) FROM audit_events a ORDER BY id DESC LIMIT 1"))
const failTrigger = condition => sql(`
  CREATE OR REPLACE FUNCTION audit_test_fail_158() RETURNS trigger LANGUAGE plpgsql AS $$
  BEGIN IF ${condition} THEN RAISE EXCEPTION 'audit regression injected failure'; END IF; RETURN NEW; END $$;
  CREATE TRIGGER audit_test_fail_158 BEFORE INSERT OR UPDATE ON audit_events
  FOR EACH ROW EXECUTE FUNCTION audit_test_fail_158();
`)
const removeTrigger = () => sql('DROP TRIGGER IF EXISTS audit_test_fail_158 ON audit_events; DROP FUNCTION IF EXISTS audit_test_fail_158()')
const removeWriteCounter = () => sql(`
  DROP TRIGGER IF EXISTS audit_test_count_158 ON audit_events;
  DROP FUNCTION IF EXISTS audit_test_count_158();
  DROP TABLE IF EXISTS audit_test_write_counts_158;
`)

try {
  assert.equal((await request('/admin/audit-log', 'GET', undefined, null)).status, 401)
  assert.equal((await request('/admin/audit-log', 'GET', undefined, member)).status, 403)
  // Delegated system managers cannot see history belonging to other users.
  sql("UPDATE role_permissions SET user_role_scope='all' WHERE role='Leiter'")
  assert.equal((await request('/admin/audit-log', 'GET', undefined, member)).status, 403)
  sql("UPDATE role_permissions SET user_role_scope='none' WHERE role='Leiter'")
  for (const filter of ['limit=0', 'limit=101', 'limit=2x', 'before=-1', 'before=9223372036854775808', 'outcome=nope']) {
    assert.equal((await request(`/admin/audit-log?${filter}`)).status, 400, filter)
  }
  const initial = count()
  await request('/me')
  await request('/admin/audit-log')
  assert.equal(count(), initial, 'reads must not generate journal entries')

  let result = await request('/admin/locations', 'POST', { name: locationName })
  assert.equal(result.status, 201, result.text)
  locationId = result.data.id
  let entry = latest()
  assert.equal(entry.actor_id, admin)
  assert.equal(entry.actor_name, 'Admin User')
  assert.equal(entry.outcome, 'completed')
  assert.equal(entry.resource_id, locationId)
  assert.equal(entry.http_status, 201)
  assert(entry.finished_at)
  assert.equal(entry.details.resource_name, locationName)
  assert.equal(entry.details.changes[0].after, locationName, 'allowlisted name is readable')
  assert.equal((await request(`/admin/locations/${locationId}`, 'PATCH', { name: `${locationName}-renamed` })).status, 200)
  assert.deepEqual(latest().details.changes[0], {
    field: 'name', label: 'Name', redacted: false, before: locationName, after: `${locationName}-renamed`,
  })

  result = await request('/admin/materials', 'POST', { name: materialName })
  assert.equal(result.status, 201, result.text)
  materialId = result.data.id
  assert.equal((await request(`/admin/materials/${materialId}`, 'DELETE')).status, 204)
  assert.equal(latest().outcome, 'completed', 'direct 204 paths must finalize')
  materialId = null

  assert.equal((await request('/admin/locations', 'POST', {})).status, 400)
  assert.equal(latest().outcome, 'rejected')
  assert.equal((await request(`/admin/locations/${locationId}`, 'DELETE', undefined, member)).status, 403)
  assert.equal(latest().actor_id, member)
  assert.equal(latest().outcome, 'rejected')

  // Interleaved async bodies must retain their own verified actors.
  const url = new URL(backendBase)
  assert.equal(url.protocol, 'http:')
  const socket = net.connect(Number(url.port || 80), url.hostname)
  await new Promise((resolve, reject) => { socket.once('connect', resolve); socket.once('error', reject) })
  const body = JSON.stringify({ display_name: 'Leiter Eins' })
  socket.write(`PATCH ${url.pathname.replace(/\/$/, '')}/me HTTP/1.1\r\nHost: ${url.host}\r\nAuthorization: Bearer debug:${member}\r\nContent-Type: application/json\r\nContent-Length: ${Buffer.byteLength(body)}\r\nConnection: close\r\n\r\n`)
  await new Promise(resolve => setTimeout(resolve, 100))
  const slowId = latest().id
  assert.equal(latest().actor_id, member)
  assert.equal(latest().outcome, 'pending')
  await request('/auth/me', 'POST')
  let raw = ''
  socket.on('data', chunk => { raw += chunk.toString() })
  const closed = new Promise(resolve => socket.once('close', resolve))
  socket.end(body)
  await closed
  assert.match(raw, /200/)
  const slowEntry = JSON.parse(sql(`SELECT row_to_json(a) FROM audit_events a WHERE id=${slowId}`))
  assert.equal(slowEntry.actor_id, member)
  assert.equal(slowEntry.outcome, 'completed')

  const abandoned = net.connect(Number(url.port || 80), url.hostname)
  await new Promise((resolve, reject) => { abandoned.once('connect', resolve); abandoned.once('error', reject) })
  abandoned.write(`PATCH ${url.pathname.replace(/\/$/, '')}/me HTTP/1.1\r\nHost: ${url.host}\r\nAuthorization: Bearer debug:${member}\r\nContent-Length: 100\r\n\r\n`)
  await new Promise(resolve => setTimeout(resolve, 100))
  const abandonedId = latest().id
  assert.equal(latest().outcome, 'pending')
  abandoned.destroy()
  await new Promise(resolve => setTimeout(resolve, 100))
  const abandonedEntry = JSON.parse(sql(`SELECT row_to_json(a) FROM audit_events a WHERE id=${abandonedId}`))
  assert.equal(abandonedEntry.actor_id, member)
  assert.equal(abandonedEntry.outcome, 'unknown', 'aborts must not become success')

  const secret = `audit-private-${randomUUID()}`
  assert.equal((await request('/admin/app-settings/github.token', 'PUT', { value: secret })).status, 200)
  assert(!JSON.stringify(latest()).includes(secret), 'configuration values must never reach audit rows')
  assert.equal(latest().resource_id, 'github.token')
  assert.equal((await request('/admin/app-settings/github.token', 'PUT', { value: null })).status, 200)
  const retention = (await request('/admin/app-settings')).data.find(item => item.key === 'audit.retention_days')
  assert.equal(retention.value, '14')
  for (const value of ['0', '3651', '14days', '1.5']) {
    assert.equal((await request('/admin/app-settings/audit.retention_days', 'PUT', { value })).status, 400)
  }
  assert.equal((await request('/admin/cron-jobs/audit-retention', 'PUT', { enabled: false })).status, 200)
  assert.equal((await request('/admin/app-settings/audit.retention_days', 'PUT', { value: '7' })).status, 200)
  assert.equal(latest().details.changes.find(change => change.field === 'retention_days').after, 7)
  sql(`INSERT INTO audit_events(action,resource_type,resource_id,occurred_at) VALUES
    ('cron-retention-test','system','cron-retention-old',NOW() - INTERVAL '8 days'),
    ('cron-retention-test','system','cron-retention-recent',NOW() - INTERVAL '6 days')`)
  assert.equal((await request('/admin/cron-jobs/audit-retention', 'PUT', { enabled: true })).status, 200)
  for (let attempt = 0; attempt < 100; ++attempt) {
    if (sql("SELECT count(*) FROM audit_events WHERE resource_id='cron-retention-old'") === '0') break
    await new Promise(resolve => setTimeout(resolve, 100))
  }
  assert.equal(sql("SELECT count(*) FROM audit_events WHERE resource_id='cron-retention-old'"), '0', 'scheduler must apply configured retention')
  assert.equal(sql("SELECT count(*) FROM audit_events WHERE resource_id='cron-retention-recent'"), '1', 'scheduler must retain recent entries')
  sql("DELETE FROM audit_events WHERE action='cron-retention-test'")
  assert.equal((await request('/admin/app-settings/audit.retention_days', 'PUT', { value: null })).status, 200)
  sql(readFileSync(new URL('../backend/tests/test_audit_retention.sql', import.meta.url), 'utf8'))
  const jobs = (await request('/admin/cron-jobs')).data.jobs
  assert(jobs.some(job => job.id === 'audit-retention' && job.interval_minutes === 60))

  // Success for a compound activity mutation represents one whole request.
  result = await request('/activities', 'POST', {
    title: locationName, date: '2027-01-01', start_time: '14:00', end_time: '16:00',
    department: 'Pfadi', responsible: [admin, member], programs: [],
  })
  assert.equal(result.status, 201, result.text)
  activityId = result.data.id
  activityShareToken = result.data.share_token
  result = await request(`/activities/${activityId}`, 'PATCH', {
    title: `${locationName}-updated`, date: '2027-01-02', start_time: '14:00', end_time: '16:00',
    department: 'Pfadi', responsible: [admin, member], programs: [], goal: secret,
  })
  assert.equal(result.status, 200, result.text)
  assert.equal(latest().details.changes.find(change => change.field === 'title').before, locationName)
  assert.equal(latest().details.changes.find(change => change.field === 'title').after, `${locationName}-updated`)
  assert.equal(latest().details.changes.find(change => change.field === 'goal').redacted, true)
  assert(!JSON.stringify(latest()).includes(secret), 'activity free text must be protected')

  const evaluationPath = `/activities/${activityId}/evaluation`
  assert.equal((await request(evaluationPath, 'GET', undefined, null)).status, 401)
  assert.equal((await request(evaluationPath, 'PUT', {
    actual_participants: 18, positive: '', improvements: '', action_items: '', notes: '',
  }, null)).status, 401, 'evaluation writes always require authentication')
  assert.equal((await request(evaluationPath, 'GET', undefined, otherMember)).status, 403,
    'users without activity read access cannot see feedback')
  assert.equal((await request(evaluationPath, 'PUT', {
    actual_participants: 18, positive: '', improvements: '', action_items: '', notes: '',
  }, otherMember)).status, 403, 'non-responsible readers cannot write activity feedback')
  assert.equal((await request(evaluationPath, 'GET', undefined, member)).status, 200,
    'activity readers can see centrally collected feedback')
  for (const invalidCount of [-1, 1.5, 2147483648]) {
    const invalid = await request(evaluationPath, 'PUT', {
      actual_participants: invalidCount, positive: '', improvements: '', action_items: '', notes: '',
    })
    assert.equal(invalid.status, 400, `invalid actual participant count ${invalidCount}`)
  }
  const tooLong = await request(evaluationPath, 'PUT', {
    actual_participants: null, positive: '', improvements: 'é'.repeat(4001), action_items: '', notes: '',
  })
  assert.equal(tooLong.status, 400, 'feedback field limits count Unicode characters')

  const adminFeedback = {
    actual_participants: 18,
    positive: 'Gute Stimmung',
    improvements: 'Mehr Zeit einplanen',
    action_items: 'Material früher bereitstellen',
    notes: 'private feedback',
  }
  assert.equal((await request(evaluationPath, 'PUT', adminFeedback)).status, 200)
  assert.equal(latest().resource_type, 'activity')
  assert.equal(latest().details.changes.find(change => change.field === 'actual_participants').after, 18)
  assert(!JSON.stringify(latest()).includes('private feedback'), 'feedback content must not enter audit details')
  assert.equal((await request(evaluationPath, 'PUT', adminFeedback)).status, 200,
    'repeated saves update rather than duplicate one author entry')
  const memberFeedback = {
    actual_participants: 20,
    positive: '',
    improvements: 'Wegweiser besser markieren',
    action_items: 'Beschilderung vorbereiten',
    notes: '',
  }
  assert.equal((await request(evaluationPath, 'PUT', memberFeedback, member)).status, 200)
  let evaluation = (await request(evaluationPath, 'GET', undefined, member)).data
  assert.equal(evaluation.actual_participants, 20)
  assert.equal(evaluation.feedback.length, 2)
  assert.equal(evaluation.mine.improvements, memberFeedback.improvements)

  assert(activityId && activityShareToken)
  const sharedActivity = await request(`/shared/${activityShareToken}`, 'GET', undefined, null)
  assert.equal(sharedActivity.status, 200)
  assert(!JSON.stringify(sharedActivity.data).includes('Wegweiser besser markieren'),
    'public activity shares must not expose evaluation feedback')
  assert.equal((await request(`/shared/${activityShareToken}/evaluation`, 'PUT', adminFeedback, null)).status, 404,
    'public share links must not expose writable evaluation routes')

  const clearedMemberFeedback = { ...memberFeedback, improvements: '', action_items: '' }
  assert.equal((await request(evaluationPath, 'PUT', clearedMemberFeedback, member)).status, 200)
  evaluation = (await request(evaluationPath, 'GET')).data
  assert.equal(evaluation.feedback.length, 1, 'clearing a contribution removes only the current author entry')
  assert.equal(evaluation.actual_participants, 20, 'attendance remains independent from clearing feedback')

  for (const [key, value] of [['wp.url', 'http://127.0.0.1:1'], ['wp.user', 'audit-test'], ['wp.app_password', 'audit-test-only']]) {
    assert.equal((await request(`/admin/app-settings/${key}`, 'PUT', { value })).status, 200)
  }
  // WordPress failures must not turn the local publication's 2xx into audit success.
  result = await request(`/activities/${activityId}/event-publication`, 'PUT', { title: locationName, body_html: '' })
  assert.equal(result.status, 200, result.text)
  assert.equal(latest().outcome, 'unknown')
  assert.equal(latest().resource_id, activityId)
  sql(`UPDATE event_publications SET wp_event_id='158' WHERE activity_id='${activityId}'`)
  assert.equal((await request(`/activities/${activityId}/event-publication`, 'PUT', { title: locationName })).status, 200)
  assert.equal(latest().outcome, 'unknown')
  assert.equal((await request(`/activities/${activityId}/event-publication`, 'DELETE')).status, 204)
  assert.equal(latest().outcome, 'unknown')
  for (const key of ['wp.url', 'wp.user', 'wp.app_password']) {
    assert.equal((await request(`/admin/app-settings/${key}`, 'PUT', { value: null })).status, 200)
  }
  const beforeDelete = count()
  assert.equal((await request(`/activities/${activityId}`, 'DELETE')).status, 204)
  assert.equal(count(), beforeDelete + 1)
  assert.equal(latest().outcome, 'completed')
  assert.equal(latest().resource_id, activityId)

  const page = (await request('/admin/audit-log?limit=2')).data
  assert.equal(page.entries.length, 2)
  assert.equal(page.next_cursor, page.entries[1].id)
  assert.equal(typeof page.next_cursor, 'string')
  const older = (await request(`/admin/audit-log?limit=2&before=${page.next_cursor}`)).data
  assert(older.entries.every(e => BigInt(e.id) < BigInt(page.next_cursor)))
  const filtered = (await request(`/admin/audit-log?actor=${member}&outcome=rejected`)).data
  assert(filtered.entries.length > 0)
  assert(filtered.entries.every(e => e.actor_id === member && e.outcome === 'rejected'))
  const byName = (await request('/admin/audit-log?actor=Admin+User')).data
  assert(byName.entries.every(e => e.actor_id === admin))

  // IDs are strings on the wire, but sorting and cursors must remain numeric.
  sql(`INSERT INTO audit_events(id,action,resource_type,actor_id,outcome) VALUES
    (9007199254740991,'audit-order-test-158','system','audit-order-test-158','completed'),
    (9007199254740992,'audit-order-test-158','system','audit-order-test-158','completed'),
    (10000000000000000,'audit-order-test-158','system','audit-order-test-158','completed')`)
  const numericPage = (await request('/admin/audit-log?actor=audit-order-test-158&limit=2')).data
  assert.deepEqual(numericPage.entries.map(event => event.id), ['10000000000000000', '9007199254740992'])
  const numericOlder = (await request(`/admin/audit-log?actor=audit-order-test-158&before=${numericPage.next_cursor}`)).data
  assert.deepEqual(numericOlder.entries.map(event => event.id), ['9007199254740991'])
  sql("DELETE FROM audit_events WHERE action='audit-order-test-158'")

  // Nested require_auth calls must not repeat the same persisted attribution.
  sql(`
    CREATE TABLE audit_test_write_counts_158(kind text);
    CREATE FUNCTION audit_test_count_158() RETURNS trigger LANGUAGE plpgsql AS $$
    BEGIN
      INSERT INTO audit_test_write_counts_158 VALUES (
        CASE WHEN TG_OP='INSERT' THEN 'intent' WHEN NEW.outcome='pending' THEN 'attribution' ELSE 'finish' END);
      RETURN NEW;
    END $$;
    CREATE TRIGGER audit_test_count_158 AFTER INSERT OR UPDATE ON audit_events
    FOR EACH ROW EXECUTE FUNCTION audit_test_count_158();
  `)
  assert.equal((await request('/admin/app-settings/audit.retention_days', 'PUT', { value: '14' })).status, 200)
  assert.equal(sql("SELECT count(*) FROM audit_test_write_counts_158 WHERE kind='intent'"), '1')
  assert.equal(sql("SELECT count(*) FROM audit_test_write_counts_158 WHERE kind='attribution'"), '1')
  assert.equal(sql("SELECT count(*) FROM audit_test_write_counts_158 WHERE kind='finish'"), '1')
  removeWriteCounter()
  assert.equal((await request('/admin/app-settings/audit.retention_days', 'PUT', { value: null })).status, 200)

  // Persistence failure must block mutations, not silently acknowledge success.
  failTrigger("TG_OP='INSERT'")
  assert.equal((await request(`/admin/locations/${locationId}`, 'DELETE')).status, 503)
  removeTrigger()
  assert.equal(sql(`SELECT count(*) FROM predefined_locations WHERE id='${locationId}'`), '1')

  // Actor persistence failure also prevents the business mutation.
  failTrigger("NEW.actor_id IS NOT NULL AND NEW.outcome='pending'")
  assert.equal((await request(`/admin/locations/${locationId}`, 'DELETE')).status, 500)
  removeTrigger()
  assert.equal(sql(`SELECT count(*) FROM predefined_locations WHERE id='${locationId}'`), '1')

  // Finalization failure is explicit about possible effects, preserving pending.
  failTrigger("NEW.outcome<>'pending'")
  result = await request(`/admin/locations/${locationId}`, 'DELETE')
  assert.equal(result.status, 500)
  assert.match(result.text, /möglicherweise ausgeführt/)
  removeTrigger()
  assert.equal(sql(`SELECT count(*) FROM predefined_locations WHERE id='${locationId}'`), '0')
  assert.equal(latest().outcome, 'pending')
  assert.equal(latest().actor_id, admin)
  locationId = null

  // History snapshots are independent of subsequent actor rename/deletion.
  sql("UPDATE users SET display_name='Renamed' WHERE id='" + admin + "'")
  assert.equal(latest().actor_name, 'Admin User')
  sql("UPDATE users SET display_name='Admin User' WHERE id='" + admin + "'")
  assert.equal(sql("SELECT count(*) FROM information_schema.table_constraints WHERE table_name='audit_events' AND constraint_type='FOREIGN KEY'"), '0')
  fixtureUserId = randomUUID()
  sql(`INSERT INTO users(id,microsoft_oid,email,display_name,department,role)
       VALUES ('${fixtureUserId}','audit-${fixtureUserId}','audit@example.invalid','Audit fixture','Pfadi','Mitglied')`)
  assert.equal((await request(`/admin/users/${fixtureUserId}`, 'PATCH', {
    display_name: 'Audit fixture renamed', department: 'Pfadi', role: 'Leiter',
    access_token: secret, unknown_field: secret,
  })).status, 200)
  const userChange = latest()
  assert.equal(userChange.details.resource_name, 'Audit fixture renamed')
  assert.deepEqual(userChange.details.changes.find(change => change.field === 'role'), {
    field: 'role', label: 'Rolle', redacted: false, before: 'Mitglied', after: 'Leiter',
  })
  assert(!JSON.stringify(userChange).includes(secret), 'unknown request fields and tokens must not be stored')
  assert.equal((await request('/auth/me', 'POST', undefined, fixtureUserId)).status, 200)
  const fixtureEventId = latest().id
  assert.equal((await request(`/admin/users/${fixtureUserId}`, 'DELETE')).status, 200)
  fixtureUserId = null
  const historical = JSON.parse(sql(`SELECT row_to_json(a) FROM audit_events a WHERE id=${fixtureEventId}`))
  assert.equal(historical.actor_name, 'Audit fixture renamed')
  assert.equal(latest().details.resource_name, 'Audit fixture renamed')
  assert.equal(latest().details.kind, 'deleted')
  assert(historical.actor_id)
  assert.equal(historical.outcome, 'completed')
  console.log('Audit integration passed: readable before/after details, configured scheduled retention, persistence, attribution, authorization, filtering/cursors, 204/compound requests, privacy and write-failure semantics')
} finally {
  removeTrigger()
  removeWriteCounter()
  sql("DELETE FROM audit_events WHERE action='audit-order-test-158'")
  if (locationId) await request(`/admin/locations/${locationId}`, 'DELETE')
  if (materialId) await request(`/admin/materials/${materialId}`, 'DELETE')
  if (activityId) await request(`/admin/activities/${activityId}/trash`, 'DELETE')
  if (fixtureUserId) await request(`/admin/users/${fixtureUserId}`, 'DELETE')
}
