import { describe, it, expect, vi, beforeEach } from 'vitest';

const { getIdTokenMock, clearTokenCacheMock } = vi.hoisted(() => ({
	getIdTokenMock: vi.fn(),
	clearTokenCacheMock: vi.fn(),
}));

// Mock useAuth to avoid MSAL side effects
vi.mock('./useAuth', () => ({
	getIdToken: getIdTokenMock,
	clearTokenCache: clearTokenCacheMock,
}));

import { apiFetch, formatApiError, useApiConnectionStatus } from './useApi';

describe('apiFetch', () => {
	const fetchSpy = vi.fn();

	beforeEach(() => {
		getIdTokenMock.mockReset();
		clearTokenCacheMock.mockReset();
		getIdTokenMock.mockResolvedValue('test-token');

		Object.defineProperty(navigator, 'onLine', {
			value: true,
			configurable: true,
		});
		window.dispatchEvent(new Event('online'));

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

	it('overrides custom Authorization header with bearer token', async () => {
		await apiFetch('/api/test', {
			headers: { Authorization: 'Bearer custom-token' },
		});

		const [, options] = fetchSpy.mock.calls[0];
		expect(options.headers.Authorization).toBe('Bearer test-token');
	});

	it('retries once and clears token cache on 401', async () => {
		fetchSpy
			.mockResolvedValueOnce(new Response('unauthorized', { status: 401 }))
			.mockResolvedValueOnce(new Response('ok', { status: 200 }));

		const response = await apiFetch('/api/protected');

		expect(clearTokenCacheMock).toHaveBeenCalledTimes(1);
		expect(fetchSpy).toHaveBeenCalledTimes(2);
		expect(response.status).toBe(200);
	});

	it('does not retry when already in retry mode', async () => {
		fetchSpy.mockResolvedValueOnce(
			new Response('unauthorized', { status: 401 }),
		);

		const response = await apiFetch('/api/protected', {}, true);

		expect(clearTokenCacheMock).not.toHaveBeenCalled();
		expect(fetchSpy).toHaveBeenCalledTimes(1);
		expect(response.status).toBe(401);
	});

	it('marks apiReachable false on likely network error and true again on success', async () => {
		const { apiReachable } = useApiConnectionStatus();
		fetchSpy.mockRejectedValueOnce(new Error('Failed to fetch'));

		await expect(apiFetch('/api/network-fail')).rejects.toThrow(
			'Failed to fetch',
		);
		expect(apiReachable.value).toBe(false);

		fetchSpy.mockResolvedValueOnce(new Response('ok', { status: 200 }));
		await apiFetch('/api/recover');
		expect(apiReachable.value).toBe(true);
	});

	it('keeps apiReachable true for non-network errors', async () => {
		const { apiReachable } = useApiConnectionStatus();
		fetchSpy.mockRejectedValueOnce(new Error('Boom'));

		await expect(apiFetch('/api/boom')).rejects.toThrow('Boom');
		expect(apiReachable.value).toBe(true);
	});

	it('reacts to browser offline and online events', () => {
		const { browserOnline, apiReachable, apiOnline } = useApiConnectionStatus();

		window.dispatchEvent(new Event('offline'));
		expect(browserOnline.value).toBe(false);
		expect(apiReachable.value).toBe(false);
		expect(apiOnline.value).toBe(false);

		window.dispatchEvent(new Event('online'));
		expect(browserOnline.value).toBe(true);
		expect(apiReachable.value).toBe(true);
		expect(apiOnline.value).toBe(true);
	});
});

describe('formatApiError', () => {
	beforeEach(() => {
		Object.defineProperty(navigator, 'onLine', {
			value: true,
			configurable: true,
		});
	});

	it('returns offline message when browser is offline', () => {
		Object.defineProperty(navigator, 'onLine', {
			value: false,
			configurable: true,
		});

		const msg = formatApiError(new Error('Failed to fetch'));
		expect(msg).toContain('Du bist offline');
	});

	it('returns network interruption message for likely network errors', () => {
		const msg = formatApiError(new Error('Network request failed'));
		expect(msg).toContain('Verbindung zum Server ist unterbrochen');
	});

	it('strips Error prefix for generic errors', () => {
		expect(formatApiError(new Error('Error: Something bad'))).toBe(
			'Something bad',
		);
	});

	it('returns unknown error fallback for empty message', () => {
		expect(formatApiError('')).toBe(
			'Unbekannter Fehler bei der Serververbindung.',
		);
	});

	it('handles non-error values', () => {
		expect(formatApiError(404)).toBe('404');
	});
});
