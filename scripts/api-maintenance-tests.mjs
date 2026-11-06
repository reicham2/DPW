import assert from 'node:assert/strict';
import { test } from 'node:test';

const baseUrl = (process.env.DPW_API_BASE_URL || 'http://127.0.0.1:8000/api').replace(/\/+$/, '');
const adminToken = 'debug:a0000000-0000-0000-0000-000000000001';
const memberToken = 'debug:a0000000-0000-0000-0000-000000000002';

async function request(method, path, token, body) {
	const headers = {};
	if (token) headers.Authorization = `Bearer ${token}`;
	if (body !== undefined) headers['Content-Type'] = 'application/json';
	const response = await fetch(`${baseUrl}${path}`, {
		method, headers,
		body: body === undefined ? undefined : JSON.stringify(body),
		signal: AbortSignal.timeout(10000),
	});
	const text = await response.text();
	return { status: response.status, text, json: JSON.parse(text) };
}

function expectStatus(response, expected) {
	assert.equal(response.status, expected, response.text);
	return response.json;
}

function localTimestamp(timestamp) {
	// Match the datetime-local inputs and the backend container's configured timezone.
	return new Intl.DateTimeFormat('sv-SE', {
		timeZone: 'Europe/Zurich',
		year: 'numeric', month: '2-digit', day: '2-digit',
		hour: '2-digit', minute: '2-digit', hourCycle: 'h23',
	}).format(new Date(timestamp)).replace(' ', 'T');
}

test('maintenance restricts login, preserves existing API sessions, and broadcasts mode transitions', async () => {
	const original = expectStatus(await request('GET', '/admin/maintenance', adminToken), 200);
	const socketUrl = new URL(baseUrl);
	socketUrl.protocol = socketUrl.protocol === 'https:' ? 'wss:' : 'ws:';
	socketUrl.pathname = `${socketUrl.pathname.replace(/\/api$/, '').replace(/\/+$/, '')}/ws`;
	socketUrl.search = new URLSearchParams({ token: memberToken }).toString();
	let socket;
	const messages = [];

	async function save(patch) {
		return expectStatus(await request('PUT', '/admin/maintenance', adminToken, patch), 200);
	}

	async function waitForStatus(active) {
		const deadline = Date.now() + 5000;
		while (Date.now() < deadline) {
			const index = messages.findIndex((event) => event.event === 'maintenance_status' && event.active === active);
			if (index !== -1) return messages.splice(0, index + 1).at(-1);
			await new Promise((resolve) => setTimeout(resolve, 20));
		}
		assert.fail(`No maintenance_status with active=${active}`);
	}

	async function assertActive() {
		assert.equal(expectStatus(await request('GET', '/maintenance'), 200).active, true);
		expectStatus(await request('POST', '/auth/me', adminToken), 200);
		const blocked = expectStatus(await request('POST', '/auth/me', memberToken), 503);
		assert.equal(blocked.error, 'maintenance');
		assert.equal(blocked.message, 'Maintenance regression test');
		// Existing sessions keep API access so pending edits can finish saving.
		expectStatus(await request('GET', '/me', memberToken), 200);
		expectStatus(await request('GET', '/activities', memberToken), 200);
	}

	try {
		await save({ enabled: false, windows: [], scheduled_start: '', scheduled_end: '' });
		expectStatus(await request('POST', '/auth/me', memberToken), 200);
		expectStatus(await request('GET', '/admin/maintenance', memberToken), 403);
		expectStatus(await request('PUT', '/admin/maintenance', memberToken, { enabled: true }), 403);
		assert.equal(expectStatus(await request('GET', '/maintenance'), 200).active, false);

		socket = new WebSocket(socketUrl);
		socket.addEventListener('message', (event) => messages.push(JSON.parse(String(event.data))));
		await new Promise((resolve, reject) => {
			const timeout = setTimeout(() => reject(new Error('WebSocket connection timed out')), 5000);
			socket.addEventListener('open', () => { clearTimeout(timeout); resolve(); }, { once: true });
			socket.addEventListener('error', () => { clearTimeout(timeout); reject(new Error('WebSocket connection failed')); }, { once: true });
		});

		const manual = await save({ enabled: true, message: 'Maintenance regression test' });
		assert.equal(manual.active, true);
		assert.equal(manual.scheduled_now, false);
		assert.equal((await waitForStatus(true)).message, manual.message);
		await assertActive();

		assert.equal((await save({ enabled: false })).active, false);
		await waitForStatus(false);
		expectStatus(await request('POST', '/auth/me', memberToken), 200);

		const scheduled = await save({
			windows: [{
				id: 'maintenance-regression',
				start: localTimestamp(Date.now() - 60_000),
				end: localTimestamp(Date.now() + 300_000),
				message: 'Maintenance regression test',
				recurrence: 'none', interval: 1,
			}],
		});
		assert.equal(scheduled.active, true);
		assert.equal(scheduled.enabled, false);
		assert.equal(scheduled.scheduled_now, true);
		await waitForStatus(true);
		await assertActive();

		assert.equal((await save({ end_window_id: 'maintenance-regression' })).active, false);
		await waitForStatus(false);
		expectStatus(await request('POST', '/auth/me', memberToken), 200);
		assert.equal(socket.readyState, WebSocket.OPEN);
	} finally {
		socket?.close();
		await save({ enabled: original.enabled, message: original.message, windows: original.windows });
	}
});
