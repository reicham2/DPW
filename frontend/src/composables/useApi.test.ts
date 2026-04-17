import { describe, it, expect, vi, beforeEach } from 'vitest';

// Mock useAuth to avoid MSAL side effects
vi.mock('./useAuth', () => ({
	getIdToken: vi.fn().mockResolvedValue('test-token'),
}));

import { apiFetch } from './useApi';

describe('apiFetch', () => {
	const fetchSpy = vi.fn();

	beforeEach(() => {
		vi.stubGlobal('fetch', fetchSpy);
		fetchSpy.mockReset();
		fetchSpy.mockResolvedValue(new Response('ok'));
	});

	it('sets Authorization header with bearer token', async () => {
		await apiFetch('/api/test');

		expect(fetchSpy).toHaveBeenCalledOnce();
		const [url, options] = fetchSpy.mock.calls[0];
		expect(url).toBe('/api/test');
		expect(options.headers.Authorization).toBe('Bearer test-token');
	});

	it('sets Content-Type for JSON body', async () => {
		await apiFetch('/api/test', {
			method: 'POST',
			body: JSON.stringify({ title: 'Test' }),
		});

		const [, options] = fetchSpy.mock.calls[0];
		expect(options.headers['Content-Type']).toBe('application/json');
	});

	it('does not set Content-Type for FormData', async () => {
		const formData = new FormData();
		await apiFetch('/api/upload', {
			method: 'POST',
			body: formData,
		});

		const [, options] = fetchSpy.mock.calls[0];
		expect(options.headers['Content-Type']).toBeUndefined();
	});

	it('does not set Content-Type when no body', async () => {
		await apiFetch('/api/data');

		const [, options] = fetchSpy.mock.calls[0];
		expect(options.headers['Content-Type']).toBeUndefined();
	});

	it('preserves custom headers', async () => {
		await apiFetch('/api/test', {
			headers: { 'X-Custom': 'value' },
		});

		const [, options] = fetchSpy.mock.calls[0];
		expect(options.headers['X-Custom']).toBe('value');
		expect(options.headers.Authorization).toBe('Bearer test-token');
	});

	it('passes method and body through', async () => {
		const body = JSON.stringify({ name: 'Test' });
		await apiFetch('/api/items', { method: 'PUT', body });

		const [url, options] = fetchSpy.mock.calls[0];
		expect(url).toBe('/api/items');
		expect(options.method).toBe('PUT');
		expect(options.body).toBe(body);
	});
});
