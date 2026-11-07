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
		signal: AbortSignal.timeout(20000),
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
}
