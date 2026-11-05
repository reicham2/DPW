import assert from 'node:assert/strict';
import { after, before, test } from 'node:test';

const baseUrl = (process.env.DPW_API_BASE_URL || 'http://127.0.0.1:8000/api').replace(/\/+$/, '');
const adminToken = 'debug:a0000000-0000-0000-0000-000000000001';
const leaderToken = 'debug:a0000000-0000-0000-0000-000000000002';
const outsiderToken = 'debug:a0000000-0000-0000-0000-000000000005';
const missingPermissionsToken = 'debug:f0000000-0000-0000-0000-000000000001';
const missingUserToken = 'debug:00000000-0000-0000-0000-000000000000';
const sockets = [];

let activityId;
let originalMailTemplate;
let originalEventTemplate;

async function request(method, path, token, body) {
	const headers = {};
	if (token) headers.Authorization = `Bearer ${token}`;
	if (body !== undefined) headers['Content-Type'] = 'application/json';
	const response = await fetch(`${baseUrl}${path}`, {
		method,
		headers,
		body: body === undefined ? undefined : JSON.stringify(body),
		signal: AbortSignal.timeout(10000),
	});
	const text = await response.text();
	const json = text && response.headers.get('content-type')?.includes('application/json')
		? JSON.parse(text)
		: null;
	return { status: response.status, text, json };
}

function expectStatus(response, expected, label) {
	assert.equal(response.status, expected, `${label}: ${response.text}`);
}

function activityBody(title, department = 'Pfadi') {
	const date = new Date();
	date.setUTCDate(date.getUTCDate() + 30);
	return {
		title,
		date: date.toISOString().slice(0, 10),
		start_time: '10:00',
		end_time: '11:00',
		department,
		responsible: [],
		material: [],
		programs: [],
	};
}

function websocketUrl(token) {
	const url = new URL(baseUrl);
	url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
	url.pathname = `${url.pathname.replace(/\/api$/, '').replace(/\/+$/, '')}/ws`;
	url.search = new URLSearchParams({ token }).toString();
	return url.toString();
}

async function connectSocket(token) {
	const socket = new WebSocket(websocketUrl(token));
	const client = { socket, messages: [] };
	socket.addEventListener('message', (event) => {
		client.messages.push(JSON.parse(String(event.data)));
	});
	await new Promise((resolve, reject) => {
		const timeout = setTimeout(() => reject(new Error('WebSocket connection timed out')), 5000);
		socket.addEventListener('open', () => {
			clearTimeout(timeout);
			resolve();
		}, { once: true });
		socket.addEventListener('error', () => {
			clearTimeout(timeout);
			reject(new Error('WebSocket connection failed'));
		}, { once: true });
	});
	sockets.push(client);
	return client;
}

async function waitForMessage(client, predicate, from = 0) {
	const deadline = Date.now() + 5000;
	while (Date.now() < deadline) {
		const message = client.messages.slice(from).find(predicate);
		if (message) return message;
		await new Promise((resolve) => setTimeout(resolve, 20));
	}
	throw new Error(`Expected WebSocket message not received: ${JSON.stringify(client.messages.slice(from))}`);
}

function sendSocket(client, message) {
	client.socket.send(JSON.stringify(message));
}

async function settleMessages() {
	await new Promise((resolve) => setTimeout(resolve, 300));
}

before(async () => {
	const users = await request('GET', '/debug/users');
	expectStatus(users, 200, 'debug users');
	for (const id of [
		'a0000000-0000-0000-0000-000000000001',
		'a0000000-0000-0000-0000-000000000002',
		'a0000000-0000-0000-0000-000000000005',
		'f0000000-0000-0000-0000-000000000001',
	]) {
		assert.ok(users.json.some((user) => user.id === id), `Missing test fixture ${id}`);
	}

	const mail = await request('GET', '/mail-templates/Pfadi', adminToken);
	const event = await request('GET', '/event-templates/Pfadi', adminToken);
	expectStatus(mail, 200, 'original mail template');
	expectStatus(event, 200, 'original event template');
	originalMailTemplate = mail.json;
	originalEventTemplate = event.json;

	const created = await request('POST', '/activities', adminToken, activityBody('WebSocket authorization test'));
	expectStatus(created, 201, 'create WebSocket test activity');
	activityId = created.json.id;
	assert.ok(activityId, 'Test activity has an ID');
});

after(async () => {
	for (const { socket } of sockets) {
		if (socket.readyState === WebSocket.OPEN) socket.close();
	}
	if (originalMailTemplate) {
		const restored = await request('PUT', '/mail-templates/Pfadi', adminToken, originalMailTemplate);
		expectStatus(restored, 200, 'restore mail template');
	}
	if (originalEventTemplate) {
		const restored = await request('PUT', '/event-templates/Pfadi', adminToken, originalEventTemplate);
		expectStatus(restored, 200, 'restore event template');
	}
	if (activityId) {
		const deleted = await request('DELETE', `/activities/${activityId}`, adminToken);
		assert.ok([200, 204, 404].includes(deleted.status), `delete test activity: ${deleted.text}`);
	}
});

test('a token without a local user cannot open a WebSocket', async () => {
	await assert.rejects(connectSocket(missingUserToken), /WebSocket connection failed/);
});

test('live activity and template updates follow the same permissions', async () => {
	const [leader, outsider, missing, admin] = await Promise.all([
		connectSocket(leaderToken),
		connectSocket(outsiderToken),
		connectSocket(missingPermissionsToken),
		connectSocket(adminToken),
	]);
	sendSocket(admin, { type: 'join', activity_id: activityId });
	await waitForMessage(admin, (message) => message.event === 'locks_state' && message.activity_id === activityId);
	sendSocket(leader, { type: 'join', activity_id: activityId });
	await waitForMessage(leader, (message) => message.event === 'locks_state' && message.activity_id === activityId);
	let adminStart = admin.messages.length;
	sendSocket(leader, { type: 'lock', activity_id: activityId, section: 'move-test' });
	await waitForMessage(admin, (message) => message.event === 'lock' && message.section === 'move-test', adminStart);

	let leaderStart = leader.messages.length;
	let outsiderStart = outsider.messages.length;
	let missingStart = missing.messages.length;
	const firstTitle = 'Live authorization: Pfadi';
	expectStatus(await request('PATCH', `/activities/${activityId}`, adminToken, activityBody(firstTitle)), 200, 'update activity');
	await waitForMessage(leader, (message) => message.event === 'updated' && message.activity.title === firstTitle, leaderStart);
	await settleMessages();
	assert.ok(!outsider.messages.slice(outsiderStart).some((message) => message.activity?.title === firstTitle));
	assert.ok(!missing.messages.slice(missingStart).some((message) => message.activity?.title === firstTitle));

	leaderStart = leader.messages.length;
	outsiderStart = outsider.messages.length;
	missingStart = missing.messages.length;
	adminStart = admin.messages.length;
	const movedTitle = 'Live authorization: Pio';
	expectStatus(await request('PATCH', `/activities/${activityId}`, adminToken, activityBody(movedTitle, 'Pio')), 200, 'move activity');
	await waitForMessage(leader, (message) => message.event === 'deleted' && message.id === activityId, leaderStart);
	await waitForMessage(outsider, (message) => message.event === 'created' && message.activity.title === movedTitle, outsiderStart);
	await waitForMessage(admin, (message) => message.event === 'unlock' && message.section === 'move-test', adminStart);
	assert.ok(!missing.messages.slice(missingStart).some((message) => message.activity?.id === activityId || message.id === activityId));

	leaderStart = leader.messages.length;
	outsiderStart = outsider.messages.length;
	expectStatus(await request('PATCH', `/activities/${activityId}`, adminToken, activityBody('Live authorization: restored')), 200, 'restore department');
	await waitForMessage(leader, (message) => message.event === 'created' && message.activity.id === activityId, leaderStart);
	await waitForMessage(outsider, (message) => message.event === 'deleted' && message.id === activityId, outsiderStart);

	sendSocket(admin, { type: 'join', activity_id: activityId });
	await waitForMessage(admin, (message) => message.event === 'locks_state' && message.activity_id === activityId);
	sendSocket(leader, { type: 'join', activity_id: activityId });
	await waitForMessage(leader, (message) => message.event === 'locks_state' && message.activity_id === activityId);
	outsiderStart = outsider.messages.length;
	adminStart = admin.messages.length;
	sendSocket(outsider, { type: 'join', activity_id: activityId });
	sendSocket(outsider, { type: 'lock', activity_id: activityId, section: 'unauthorized' });
	await settleMessages();
	assert.ok(!outsider.messages.slice(outsiderStart).some((message) => message.activity_id === activityId));
	assert.ok(!admin.messages.slice(adminStart).some((message) => message.event === 'lock' && message.section === 'unauthorized'));
	sendSocket(leader, { type: 'lock', activity_id: activityId, section: 'title' });
	await waitForMessage(admin, (message) => message.event === 'lock' && message.section === 'title', adminStart);
	sendSocket(leader, { type: 'unlock', activity_id: activityId, section: 'title' });

	for (const room of ['tpl_Pfadi', 'evt_tpl_Pfadi']) {
		const adminRoomStart = admin.messages.length;
		const leaderRoomStart = leader.messages.length;
		outsiderStart = outsider.messages.length;
		sendSocket(admin, { type: 'join', activity_id: room });
		sendSocket(leader, { type: 'join', activity_id: room });
		await waitForMessage(admin, (message) => message.event === 'locks_state' && message.activity_id === room, adminRoomStart);
		await waitForMessage(leader, (message) => message.event === 'locks_state' && message.activity_id === room, leaderRoomStart);
		sendSocket(outsider, { type: 'join', activity_id: room });
		sendSocket(outsider, { type: 'lock', activity_id: room, section: 'unauthorized' });
		await settleMessages();
		assert.ok(!outsider.messages.slice(outsiderStart).some((message) => message.activity_id === room));
		assert.ok(!admin.messages.slice(adminRoomStart).some((message) => message.event === 'lock' && message.section === 'unauthorized'));
		sendSocket(leader, { type: 'lock', activity_id: room, section: 'subject' });
		await waitForMessage(admin, (message) => message.event === 'lock' && message.section === 'subject', adminRoomStart);
		sendSocket(leader, { type: 'unlock', activity_id: room, section: 'subject' });
	}

	leaderStart = leader.messages.length;
	outsiderStart = outsider.messages.length;
	missingStart = missing.messages.length;
	expectStatus(await request('PUT', '/mail-templates/Pfadi', adminToken, { subject: 'Live mail test', body: '' }), 200, 'update mail template');
	expectStatus(await request('PUT', '/event-templates/Pfadi', adminToken, { title: 'Live event test', body: '' }), 200, 'update event template');
	await waitForMessage(leader, (message) => message.event === 'template_updated' && message.template.subject === 'Live mail test', leaderStart);
	await waitForMessage(leader, (message) => message.event === 'event_template_updated' && message.template.title === 'Live event test', leaderStart);
	await settleMessages();
	for (const client of [outsider, missing]) {
		const start = client === outsider ? outsiderStart : missingStart;
		assert.ok(!client.messages.slice(start).some((message) =>
			message.template?.subject === 'Live mail test' || message.template?.title === 'Live event test'));
	}
});
