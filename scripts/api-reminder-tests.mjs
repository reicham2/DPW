import assert from 'node:assert/strict';
import { test } from 'node:test';

const baseUrl = (process.env.DPW_API_BASE_URL || 'http://127.0.0.1:8000/api').replace(/\/+$/, '');
const admin = 'a0000000-0000-0000-0000-000000000001';
const ids = [2, 3, 4, 5].map(n => `a0000000-0000-0000-0000-00000000000${n}`);
const missingPermissions = 'f0000000-0000-0000-0000-000000000001';
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

async function request(method, path, user = admin, body, expected = 200) {
	const response = await fetch(`${baseUrl}${path}`, {
		method,
		headers: { Authorization: `Bearer debug:${user}`, 'Content-Type': 'application/json' },
		body: body === undefined ? undefined : JSON.stringify(body),
		signal: AbortSignal.timeout(10000),
	});
	const text = await response.text();
	assert.equal(response.status, expected, `${method} ${path}: ${text}`);
	return text ? JSON.parse(text) : null;
}

async function waitFor(probe, message) {
	const deadline = Date.now() + 15000;
	while (Date.now() < deadline) {
		if (await probe()) return;
		await delay(50);
	}
	assert.fail(message);
}

function schedule(hours) {
	const date = new Date(Date.now() + hours * 3600000);
	const parts = Object.fromEntries(new Intl.DateTimeFormat('sv-SE', {
		timeZone: 'Europe/Zurich', year: 'numeric', month: '2-digit', day: '2-digit',
		hour: '2-digit', minute: '2-digit', hourCycle: 'h23',
	}).formatToParts(date).map(part => [part.type, part.value]));
	return { date: `${parts.year}-${parts.month}-${parts.day}`, start_time: `${parts.hour}:${parts.minute}` };
}

async function connect(user) {
	const url = new URL(baseUrl);
	url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
	url.pathname = `${url.pathname.replace(/\/api$/, '').replace(/\/+$/, '')}/ws`;
	url.searchParams.set('token', `debug:${user}`);
	const socket = new WebSocket(url);
	const messages = [];
	socket.addEventListener('message', event => messages.push(JSON.parse(String(event.data))));
	await new Promise((resolve, reject) => {
		const timer = setTimeout(() => reject(new Error('WebSocket connection timeout')), 5000);
		socket.addEventListener('open', () => { clearTimeout(timer); resolve(); }, { once: true });
		socket.addEventListener('error', () => { clearTimeout(timer); reject(new Error('WebSocket connection failed')); }, { once: true });
	});
	return { socket, messages };
}

test('shared scheduler delivers preference-aware reminders without duplicates and retracts obsolete schedules',
	{ timeout: 90000 }, async () => {
		const originals = new Map();
		const clients = new Map();
		let activity;
		let originalEnabled;
		const job = async () => (await request('GET', '/admin/cron-jobs')).jobs.find(j => j.id === 'activity-reminders');
		const setEnabled = enabled => request('PUT', '/admin/cron-jobs/activity-reminders', admin, { enabled });
		// An explicit zero is clamped to one by the endpoint; fetch all before filtering by activity.
		const notes = async user => (await request('GET', '/notifications', user))
			.filter(n => n.category === 'activity_reminder' && n.payload.activity_id === activity);
		const messages = user => clients.get(user).messages.filter(event =>
			event.event === 'notification' && event.notification.category === 'activity_reminder'
			&& event.notification.payload.activity_id === activity);
		const run = async () => {
			await setEnabled(false);
			const before = await job();
			await setEnabled(true);
			await waitFor(async () => {
				const current = await job();
				assert.equal(current.failures, before.failures, 'Reminder job must not fail');
				return current.runs > before.runs && !current.running;
			}, 'Reminder job must run immediately on resume, not wait a full minute');
			await setEnabled(false);
		};
		try {
			const registered = await job();
			assert.ok(registered, 'Reminder job is registered in the shared scheduler');
			assert.equal(registered.interval_minutes, 1);
			originalEnabled = registered.enabled;
			await setEnabled(false);
			await waitFor(async () => !(await job()).running, 'Previous run must finish');

			for (const id of [...ids, missingPermissions]) {
				originals.set(id, await request('GET', '/me', id));
				clients.set(id, await connect(id));
			}
			const moved = originals.get(ids[2]);
			await request('PATCH', `/admin/users/${ids[2]}`, admin, {
				display_name: moved.display_name, role: moved.role, department: 'Pfadi',
			});
			for (const [index, hours] of [2, 24, 48].entries()) {
				await request('PATCH', '/me', ids[index], {
					notify_activity_reminder: true, activity_reminder_hours: hours,
					notify_channel_websocket: index !== 1,
				});
			}
			await request('PATCH', '/me', ids[3], { notify_activity_reminder: true, activity_reminder_hours: 48 });
			const body = {
				title: 'Scheduled reminder regression', ...schedule(30),
				end_time: '23:59', department: 'Pfadi', responsible: [],
				goal: '', location: 'Wald', material: [], tn_material: [], programs: [],
			};
			activity = (await request('POST', '/activities', admin, body, 201)).id;
			await delay(200);
			assert.equal((await notes(ids[2])).length, 0, 'Paused job must not create reminders');
			await run();
			assert.equal((await notes(ids[0])).length, 0, '2h recipient is not yet due');
			assert.equal((await notes(ids[1])).length, 0, '24h recipient is not yet due');
			assert.equal((await notes(ids[2])).length, 1, '48h recipient is due');
			assert.equal((await notes(ids[3])).length, 0, 'Other Stufe receives nothing');
			assert.equal((await notes(missingPermissions)).length, 0, 'Missing role permissions receive nothing');
			await waitFor(() => messages(ids[2]).length === 1, 'Worker must safely defer delivery to the HTTP loop');

			await request('PATCH', `/activities/${activity}`, admin, { ...body, ...schedule(23) });
			await run();
			assert.equal((await notes(ids[0])).length, 0);
			assert.equal((await notes(ids[1])).length, 1, 'Browser-disabled recipient still has an in-app reminder');
			assert.equal((await notes(ids[2])).length, 1, 'Rescheduling replaces the prior reminder');
			assert.equal(messages(ids[1]).length, 0, 'Disabled Browser/App channel must not receive WebSocket notifications');
			await waitFor(() => messages(ids[2]).length === 2, 'Rescheduled reminder reaches eligible browser');

			await request('PATCH', `/activities/${activity}`, admin, { ...body, ...schedule(1.5) });
			await run();
			for (const id of ids.slice(0, 3)) assert.equal((await notes(id)).length, 1, 'Each configured lead is now due');
			await waitFor(() => messages(ids[0]).length === 1 && messages(ids[2]).length === 3, 'Due browsers receive current reminders');
			const currentId = (await notes(ids[0]))[0].id;
			await request('PATCH', '/me', ids[0], { activity_reminder_hours: 48 });
			await run();
			assert.equal((await notes(ids[0]))[0].id, currentId, 'Changing lead after issuance does not resend');
			assert.equal(messages(ids[0]).length, 1, 'Repeated runs cannot duplicate delivery');

			await request('DELETE', `/activities/${activity}`, admin, undefined, 204);
			await run();
			for (const id of ids.slice(0, 3)) assert.equal((await notes(id)).length, 0, 'Cancellation removes stale reminders');
			await request('PATCH', '/me', ids[0], { notify_activity_reminder: false });
			await request('POST', `/admin/activities/${activity}/restore`);
			await run();
			assert.equal((await notes(ids[0])).length, 0, 'Opt-out respected after restoration');
			assert.equal((await notes(ids[1])).length, 1);
			assert.equal((await notes(ids[2])).length, 1);
			assert.equal(messages(ids[0]).length, 1, 'Opted-out browser receives no restored reminder');
		} finally {
			for (const client of clients.values()) client.socket.close();
			await setEnabled(false);
			await waitFor(async () => !(await job()).running, 'Cleanup waits for the scheduler');
			if (activity) {
				const existing = await fetch(`${baseUrl}/activities/${activity}`, {
					headers: { Authorization: `Bearer debug:${admin}` },
				});
				if (existing.status === 200) await request('DELETE', `/activities/${activity}`, admin, undefined, 204);
				else assert.equal(existing.status, 404);
				await request('DELETE', `/admin/activities/${activity}/trash`);
			}
			for (const [id, user] of originals) {
				await request('PATCH', '/me', id, {
					activity_reminder_hours: user.activity_reminder_hours,
					notify_activity_reminder: user.notify_activity_reminder,
					notify_channel_websocket: user.notify_channel_websocket,
				});
			}
			if (originals.has(ids[2])) {
				const user = originals.get(ids[2]);
				await request('PATCH', `/admin/users/${user.id}`, admin, {
					display_name: user.display_name, role: user.role, department: user.department,
				});
			}
			if (originalEnabled !== undefined) await setEnabled(originalEnabled);
		}
	});
