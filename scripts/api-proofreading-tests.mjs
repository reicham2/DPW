import assert from 'node:assert/strict';
import { test } from 'node:test';

const baseUrl = (process.env.DPW_API_BASE_URL || 'http://127.0.0.1:8000/api').replace(/\/+$/, '');
const debugToken = 'debug:a0000000-0000-0000-0000-000000000001';
const expectUnavailable = process.env.EXPECT_PROOFREADING_UNAVAILABLE === '1';

async function post(body, { token = debugToken } = {}) {
	const headers = { 'Content-Type': 'application/json' };
	if (token) headers.Authorization = `Bearer ${token}`;
	const response = await fetch(`${baseUrl}/proofreading/check`, {
		method: 'POST',
		headers,
		body,
		signal: AbortSignal.timeout(40000),
	});
	const text = await response.text();
	let payload;
	try {
		payload = text ? JSON.parse(text) : null;
	} catch {
		assert.fail(`Expected JSON response, got: ${text}`);
	}
	return { response, payload };
}

async function dictionary(word, { token = debugToken, method = 'POST' } = {}) {
	const headers = { 'Content-Type': 'application/json' };
	if (token) headers.Authorization = `Bearer ${token}`;
	const response = await fetch(`${baseUrl}/proofreading/dictionary`, {
		method, headers, body: JSON.stringify({ word }), signal: AbortSignal.timeout(10000),
	});
	return { response, payload: await response.json() };
}

async function profileRequest(method, path = '/me', { token = debugToken, body } = {}) {
	const headers = { 'Content-Type': 'application/json' };
	if (token) headers.Authorization = `Bearer ${token}`;
	const response = await fetch(`${baseUrl}${path}`, {
		method, headers,
		body: body === undefined ? undefined : JSON.stringify(body),
		signal: AbortSignal.timeout(10000),
	});
	return { response, payload: await response.json() };
}

test('proofreading preference defaults, persists, stays isolated and validates boolean updates', async () => {
	const otherToken = 'debug:a0000000-0000-0000-0000-000000000003';
	const original = await profileRequest('GET');
	const otherOriginal = await profileRequest('GET', '/me', { token: otherToken });
	assert.equal(original.response.status, 200);
	assert.equal(otherOriginal.response.status, 200);
	assert.equal(original.payload.proofreading_enabled, true, 'existing seeded users default to enabled');
	assert.equal(otherOriginal.payload.proofreading_enabled, true);
	const changedMode = otherOriginal.payload.time_display_mode === 'minutes' ? 'clock' : 'minutes';
	try {
		assert.equal((await profileRequest('PATCH', '/me', {
			token: null, body: { proofreading_enabled: false },
		})).response.status, 401);

		const disabled = await profileRequest('PATCH', '/me', {
			token: otherToken,
			body: { proofreading_enabled: false, id: original.payload.id },
		});
		assert.equal(disabled.response.status, 200, 'ordinary users can change their own preference');
		assert.equal(disabled.payload.id, otherOriginal.payload.id, 'a body ID cannot target another profile');
		assert.equal(disabled.payload.proofreading_enabled, false);
		assert.equal((await profileRequest('GET', '/me', { token: otherToken })).payload.proofreading_enabled, false);
		assert.equal((await profileRequest('POST', '/auth/me', { token: otherToken })).payload.proofreading_enabled, false,
			'login must preserve an opted-out preference');
		assert.equal((await profileRequest('GET')).payload.proofreading_enabled, true,
			'another user must remain enabled');

		const users = await profileRequest('GET', '/users');
		assert.equal(users.response.status, 200);
		assert.equal(users.payload.find(user => user.id === otherOriginal.payload.id).proofreading_enabled, false);
		assert.equal(users.payload.find(user => user.id === original.payload.id).proofreading_enabled, true);

		const omitted = await profileRequest('PATCH', '/me', {
			token: otherToken, body: { time_display_mode: changedMode },
		});
		assert.equal(omitted.response.status, 200);
		assert.equal(omitted.payload.time_display_mode, changedMode);
		assert.equal(omitted.payload.proofreading_enabled, false, 'older profile payloads must preserve opt-out');
		assert.equal((await profileRequest('PATCH', '/me', {
			token: otherToken, body: {},
		})).payload.proofreading_enabled, false);

		const adminUpdate = await profileRequest('PATCH', `/admin/users/${otherOriginal.payload.id}`, {
			body: {
				display_name: otherOriginal.payload.display_name,
				department: otherOriginal.payload.department,
				role: otherOriginal.payload.role,
			},
		});
		assert.equal(adminUpdate.response.status, 200);
		assert.equal(adminUpdate.payload.proofreading_enabled, false, 'admin edits also preserve and expose opt-out');

		for (const invalid of ['false', 'true', null, 0, 1, [], {}]) {
			const rejected = await profileRequest('PATCH', '/me', {
				token: otherToken,
				body: { proofreading_enabled: invalid, time_display_mode: otherOriginal.payload.time_display_mode },
			});
			assert.equal(rejected.response.status, 400, `reject non-boolean ${JSON.stringify(invalid)}`);
			assert.match(rejected.payload.error, /proofreading_enabled.*Boolean/);
			const unchanged = await profileRequest('GET', '/me', { token: otherToken });
			assert.equal(unchanged.payload.proofreading_enabled, false);
			assert.equal(unchanged.payload.time_display_mode, changedMode, 'invalid updates must not partially persist');
		}

		const enabled = await profileRequest('PATCH', '/me', {
			token: otherToken, body: { proofreading_enabled: true },
		});
		assert.equal(enabled.response.status, 200);
		assert.equal(enabled.payload.proofreading_enabled, true);
		assert.equal((await profileRequest('GET', '/me', { token: otherToken })).payload.proofreading_enabled, true);
	} finally {
		const restored = await profileRequest('PATCH', '/me', {
			token: otherToken,
			body: {
				proofreading_enabled: otherOriginal.payload.proofreading_enabled,
				time_display_mode: otherOriginal.payload.time_display_mode,
			},
		});
		assert.equal(restored.response.status, 200, 'restore the test profile');
	}
});

if (expectUnavailable) {
	test('returns a clear error when the local proofreading service is unavailable', async () => {
		const { response, payload } = await post(JSON.stringify({ text: 'Ich habe ein Fehler gemacht.' }));
		assert.equal(response.status, 503);
		assert.match(payload.error, /lokale Korrekturdienst/i);
	});
} else {
	test('requires authentication and validates JSON and text size', async () => {
		const unauthorized = await post(JSON.stringify({ text: 'Text' }), { token: null });
		assert.equal(unauthorized.response.status, 401);

		assert.equal((await post('not json')).response.status, 400);
		assert.equal((await post(JSON.stringify({ text: '' }))).response.status, 400);
		assert.equal((await post(JSON.stringify({ text: 'x'.repeat(20 * 1024 + 1) }))).response.status, 413);
	});

	test('uses the real local LanguageTool service and returns de-CH suggestions', async () => {
		const text = 'Ich habe ein Fehler gemacht.';
		const { response, payload } = await post(JSON.stringify({ text }));
		assert.equal(response.status, 200);
		assert.equal(payload.language, 'de-CH');
		assert.ok(Array.isArray(payload.matches));
		assert.ok(payload.matches.length > 0, 'LanguageTool should report the known grammar error');
		assert.ok(payload.matches.some(match =>
			typeof match.offset === 'number'
			&& typeof match.length === 'number'
			&& typeof match.message === 'string'
			&& match.replacements.some(replacement => replacement.value.toLowerCase().includes('einen')),
		), 'The response should include the expected accusative correction');
	});

	test('personal words persist, filter only spelling matches, and remain isolated between users', async () => {
		const word = 'einfahcer';
		const otherUser = 'debug:a0000000-0000-0000-0000-000000000003';
		const text = `Ich habe ein Fehler gemacht. 🐾 Das ist ein ${word} Tset.`;
		const spelling = (payload) => payload.matches.filter(match =>
			match.rule.issueType === 'misspelling' && text.slice(match.offset, match.offset + match.length) === word);
		assert.equal((await dictionary(word, { token: null })).response.status, 401);
		for (const invalid of ['', 'zwei Wörter', 'wort\n', 'x'.repeat(257), 'test\u0000suffix'])
			assert.equal((await dictionary(invalid)).response.status, 400);
		try {
			await dictionary(word, { method: 'DELETE' });
			await dictionary(word, { token: otherUser, method: 'DELETE' });
			const before = await post(JSON.stringify({ text }));
			assert.equal(before.response.status, 200);
			assert.ok(spelling(before.payload).length, 'real LanguageTool must flag the test word');
			assert.equal((await dictionary(word)).response.status, 200);
			assert.equal((await dictionary(word)).response.status, 200, 'adding an existing word is idempotent');
			const after = await post(JSON.stringify({ text }));
			assert.equal(spelling(after.payload).length, 0);
			assert.ok(after.payload.matches.some(match => match.rule.issueType !== 'misspelling'),
				'grammar matches must remain');
			const other = await post(JSON.stringify({ text }), { token: otherUser });
			assert.ok(spelling(other.payload).length, 'another user must not inherit the word');
			assert.equal((await dictionary(word, { method: 'DELETE' })).payload.removed, true);
			assert.ok(spelling((await post(JSON.stringify({ text }))).payload).length, 'deleted word must be flagged again');
			assert.equal((await dictionary('Pfadia\u0308')).response.status, 200);
			assert.equal((await dictionary('Pfadiä', { method: 'DELETE' })).payload.removed, true,
				'canonically equivalent spellings must identify the same word');
		} finally {
			await dictionary(word, { method: 'DELETE' });
			await dictionary(word, { token: otherUser, method: 'DELETE' });
		}
	});

	test('limits dictionary mutation requests per user', async () => {
		const token = 'debug:a0000000-0000-0000-0000-000000000004';
		for (let i = 0; i < 30; i++)
			assert.equal((await dictionary('', { token })).response.status, 400);
		assert.equal((await dictionary('', { token })).response.status, 429);
	});
}
