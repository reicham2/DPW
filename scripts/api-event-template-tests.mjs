import assert from 'node:assert/strict';
import { after, before, test } from 'node:test';

const baseUrl = (process.env.DPW_API_BASE_URL || 'http://127.0.0.1:8000/api').replace(/\/+$/, '');
const admin = 'debug:a0000000-0000-0000-0000-000000000001';
const manager = 'debug:a0000000-0000-0000-0000-000000000002';
const publisher = 'debug:a0000000-0000-0000-0000-000000000003';
const outsider = 'debug:a0000000-0000-0000-0000-000000000005';
const missingPermission = 'debug:f0000000-0000-0000-0000-000000000001';
const createdTemplates = new Set();
const createdActivities = [];
let original;
let alpha;
let zulu;
let foreign;
let activityId;
let unownedActivityId;

async function request(method, path, token = admin, body) {
	const response = await fetch(`${baseUrl}${path}`, {
		method,
		headers: {
			...(token ? { Authorization: `Bearer ${token}` } : {}),
			...(body !== undefined ? { 'Content-Type': 'application/json' } : {}),
		},
		body: body === undefined ? undefined : JSON.stringify(body),
		signal: AbortSignal.timeout(10000),
	});
	const text = await response.text();
	return { status: response.status, json: text ? JSON.parse(text) : null, text };
}

function status(response, expected) {
	assert.equal(response.status, expected, response.text);
	return response.json;
}

async function createTemplate(name, department = 'Pfadi') {
	const template = status(await request('POST', '/event-templates', admin, {
		name, department, title: '{{titel}}', body: '<p>{{formular_link|Anmelden}} / {{roter_faden}}</p>',
	}), 201);
	createdTemplates.add(template.id);
	return template;
}

async function createActivity(responsible) {
	const activity = status(await request('POST', '/activities', admin, {
		title: 'Event template regression', date: '2099-07-01', start_time: '10:00', end_time: '11:00',
		department: 'Pfadi', responsible, programs: [], material: [],
	}), 201);
	createdActivities.push(activity.id);
	return activity.id;
}

before(async () => {
	original = status(await request('GET', '/event-templates/Pfadi'), 200);
	alpha = await createTemplate('AAA Event regression');
	zulu = await createTemplate('ZZZ Event regression');
	foreign = await createTemplate('Event regression andere Stufe', 'Wölfe');
	activityId = await createActivity(['a0000000-0000-0000-0000-000000000003']);
	unownedActivityId = await createActivity([]);
});

after(async () => {
	for (const id of createdTemplates) {
		const result = await request('DELETE', `/event-templates/${id}`);
		assert.ok([204, 404].includes(result.status), result.text);
	}
	if (original) status(await request('PUT', `/event-templates/${original.id}`, admin, {
		name: original.name, title: original.title, body: original.body, is_default: original.is_default,
	}), 200);
	for (const id of createdActivities) {
		const result = await request('DELETE', `/activities/${id}`);
		assert.ok([200, 204, 404].includes(result.status), result.text);
	}
});

test('new Event template routes require authentication', async () => {
	for (const [method, path, body] of [
		['POST', '/event-templates', { name: 'Unauthorized', department: 'Pfadi' }],
		['PUT', `/event-templates/${alpha.id}`, { title: 'Unauthorized' }],
		['DELETE', `/event-templates/${alpha.id}`],
		['GET', `/activities/${activityId}/event-template-context`],
	]) {
		status(await request(method, path, null, body), 401);
	}
});

test('multiple named templates have scoped CRUD and valid metadata', async () => {
	const templates = status(await request('GET', '/event-templates', manager), 200);
	assert.ok(templates.some(template => template.id === alpha.id));
	assert.ok(templates.some(template => template.id === zulu.id));
	assert.ok(templates.every(template => template.department === 'Pfadi'));
	assert.ok(templates.every(template => typeof template.name === 'string' && typeof template.is_default === 'boolean'));
	assert.ok(templates.findIndex(template => template.id === alpha.id) < templates.findIndex(template => template.id === zulu.id));
	for (const token of [publisher, outsider, missingPermission]) {
		status(await request('GET', '/event-templates', token), 403);
		status(await request('POST', '/event-templates', token, { name: 'Not allowed', department: 'Pfadi' }), 403);
		status(await request('PUT', `/event-templates/${alpha.id}`, token, { title: 'Not allowed' }), 403);
		status(await request('DELETE', `/event-templates/${alpha.id}`, token), 403);
	}
	status(await request('POST', '/event-templates', manager, { name: 'Foreign', department: 'Wölfe' }), 403);
	status(await request('PUT', `/event-templates/${foreign.id}`, manager, { title: 'Foreign' }), 403);
	status(await request('DELETE', `/event-templates/${foreign.id}`, manager), 403);
	const updated = status(await request('PUT', `/event-templates/${alpha.id}`, manager, { name: 'AAA Umbenannt' }), 200);
	assert.equal(updated.body, alpha.body);
	assert.equal(updated.title, alpha.title);
	status(await request('PUT', `/event-templates/${alpha.id}`, admin, { department: 'Wölfe' }), 400);
	status(await request('PUT', '/event-templates/00000000-0000-0000-0000-000000000000', admin, { title: 'Missing' }), 404);
});

test('Event template input errors are rejected without writes', async () => {
	for (const body of [
		{}, [], { name: ' ', department: 'Pfadi' }, { name: 'Test', department: 123 },
		{ name: 'Test', department: 'Missing department' }, { name: 'Test', department: 'Pfadi', title: [] },
		{ name: 'Test', department: 'Pfadi', is_default: 'true' },
	]) {
		status(await request('POST', '/event-templates', admin, body), 400);
	}
	for (const body of [{ name: '' }, { body: null }, { is_default: 1 }, []]) {
		status(await request('PUT', `/event-templates/${alpha.id}`, admin, body), 400);
	}
});

test('publisher-only users can select templates for their activity, not manage them or read other activities', async () => {
	const context = status(await request('GET', `/activities/${activityId}/event-template-context`, publisher), 200);
	assert.ok(context.templates.some(template => template.id === alpha.id));
	assert.ok(context.templates.every(template => template.department === 'Pfadi'));
	status(await request('GET', `/activities/${unownedActivityId}/event-template-context`, publisher), 403);
	status(await request('GET', `/activities/${activityId}/event-template-context`, manager), 200);
	status(await request('GET', `/activities/${activityId}/event-template-context`, outsider), 403);
	status(await request('GET', `/activities/${activityId}/event-template-context`, missingPermission), 403);
	status(await request('GET', '/activities/00000000-0000-0000-0000-000000000000/event-template-context'), 404);
});

test('concurrent content updates preserve other fields and concurrent default changes leave exactly one default', async () => {
	const updates = await Promise.all([
		request('PUT', `/event-templates/${alpha.id}`, manager, { title: 'Concurrent title' }),
		request('PUT', `/event-templates/${alpha.id}`, manager, { body: '<p>Concurrent body</p>' }),
	]);
	for (const result of updates) status(result, 200);
	let templates = status(await request('GET', '/event-templates'), 200);
	let updated = templates.find(template => template.id === alpha.id);
	assert.equal(updated.title, 'Concurrent title');
	assert.equal(updated.body, '<p>Concurrent body</p>');
	const defaults = await Promise.all([
		request('PUT', `/event-templates/${alpha.id}`, manager, { is_default: true }),
		request('PUT', `/event-templates/${zulu.id}`, manager, { is_default: true }),
	]);
	for (const result of defaults) status(result, 200);
	templates = status(await request('GET', '/event-templates'), 200);
	assert.equal(templates.filter(template => template.department === 'Pfadi' && template.is_default).length, 1);
	updated = templates.find(template => template.id === alpha.id);
	assert.equal(updated.body, '<p>Concurrent body</p>');
});

test('legacy department PUT updates the selected default without modifying alternative templates', async () => {
	status(await request('PUT', `/event-templates/${zulu.id}`, admin, { is_default: true }), 200);
	const updated = status(await request('PUT', '/event-templates/Pfadi', admin, { title: 'Legacy title', body: '<p>Legacy body</p>' }), 200);
	assert.equal(updated.id, zulu.id);
	assert.equal(updated.name, zulu.name);
	const templates = status(await request('GET', '/event-templates'), 200);
	assert.equal(templates.find(template => template.id === alpha.id).body, '<p>Concurrent body</p>');
});

test('deleting or unsetting a default leaves an explicit template choice and preserves published Event content', async () => {
	status(await request('PUT', `/event-templates/${zulu.id}`, admin, { is_default: false }), 200);
	let context = status(await request('GET', `/activities/${activityId}/event-template-context`, publisher), 200);
	assert.equal(context.templates.filter(template => template.is_default).length, 0);
	assert.equal(status(await request('GET', '/event-templates/Pfadi'), 200).id, alpha.id);
	const published = status(await request('PUT', `/activities/${activityId}/event-publication`, admin, {
		title: 'Already edited Event', body_html: '<p>Keep my Event content</p>',
	}), 200);
	status(await request('PUT', `/event-templates/${alpha.id}`, admin, { is_default: true }), 200);
	status(await request('DELETE', `/event-templates/${alpha.id}`, manager), 204);
	createdTemplates.delete(alpha.id);
	context = status(await request('GET', `/activities/${activityId}/event-template-context`, publisher), 200);
	assert.ok(context.templates.some(template => template.id === zulu.id));
	assert.equal(context.templates.filter(template => template.is_default).length, 0);
	const preserved = status(await request('GET', `/activities/${activityId}/event-publication`), 200);
	assert.equal(preserved.id, published.id);
	assert.equal(preserved.title, 'Already edited Event');
	assert.equal(preserved.body_html, '<p>Keep my Event content</p>');
	status(await request('DELETE', `/event-templates/${alpha.id}`), 404);
});
