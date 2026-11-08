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
