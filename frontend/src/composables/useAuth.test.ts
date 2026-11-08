import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

const mocks = vi.hoisted(() => {
	class InteractionRequiredAuthError extends Error {}
	const msal = {
		initialize: vi.fn(),
		handleRedirectPromise: vi.fn(),
		getActiveAccount: vi.fn(),
		getAllAccounts: vi.fn(),
		setActiveAccount: vi.fn(),
		loginPopup: vi.fn(),
		acquireTokenSilent: vi.fn(),
		acquireTokenPopup: vi.fn(),
	};
	return {
		InteractionRequiredAuthError,
		msal,
		createMsal: vi.fn(function () { return msal; }),
		wsDisconnect: vi.fn(),
		config: { AZURE_CLIENT_ID: 'client', AZURE_TENANT_ID: 'tenant' },
	};
});

vi.mock('@azure/msal-browser', () => ({
	InteractionRequiredAuthError: mocks.InteractionRequiredAuthError,
	PublicClientApplication: mocks.createMsal,
}));
vi.mock('./useWebSocket', () => ({ wsDisconnect: mocks.wsDisconnect }));
vi.mock('../config', () => ({ config: mocks.config }));
vi.mock('./useMaintenance', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		maintenanceActive: ref(false),
		maintenanceMessage: ref(''),
		maintenanceScheduledEnd: ref(''),
	};
});

const account = { homeAccountId: 'account-a' };
const otherAccount = { homeAccountId: 'account-b' };
const userRecord = {
	id: 'user-1',
	microsoft_oid: 'oid-1',
	email: 'user@example.com',
	display_name: 'User',
	department: null,
	role: 'Mitglied',
	time_display_mode: 'minutes',
	proofreading_enabled: true,
	notify_material_assigned: true,
	notify_activity_assigned: true,
	notify_program_assigned: true,
	notify_mail_own_activity: true,
	notify_mail_department: true,
	notify_channel_websocket: true,
	notify_channel_email: false,
	avatar_color: '#123456',
	created_at: '2026-01-01',
	updated_at: '2026-01-01',
};

function authResult(idToken = 'id-token', currentAccount = account, expiresIn = 3600) {
	return {
		account: currentAccount,
		idToken,
		idTokenClaims: { exp: Math.floor(Date.now() / 1000) + expiresIn },
		accessToken: 'graph-token',
		expiresOn: new Date(Date.now() + 3600_000),
	};
}

function jsonResponse(data: unknown, status = 200): Response {
	return new Response(JSON.stringify(data), {
		status,
		headers: { 'Content-Type': 'application/json' },
	});
}

function deferred<T>() {
	let resolve!: (value: T) => void;
	let reject!: (error: Error) => void;
	const promise = new Promise<T>((res, rej) => {
		resolve = res;
		reject = rej;
	});
	return { promise, resolve, reject };
}

describe('useAuth', () => {
	beforeEach(() => {
		vi.resetModules();
		vi.clearAllMocks();
		vi.stubEnv('VITE_ENABLE_DEBUG_AUTH', '0');
		const storage = new Map<string, string>();
		vi.stubGlobal('localStorage', {
			getItem: (key: string) => storage.get(key) ?? null,
			setItem: (key: string, value: string) => storage.set(key, String(value)),
			removeItem: (key: string) => storage.delete(key),
			clear: () => storage.clear(),
		});
		mocks.config.AZURE_CLIENT_ID = 'client';
		mocks.config.AZURE_TENANT_ID = 'tenant';
		mocks.msal.initialize.mockReset().mockResolvedValue(undefined);
		mocks.msal.handleRedirectPromise.mockReset().mockResolvedValue(null);
		mocks.msal.getActiveAccount.mockReset().mockReturnValue(null);
		mocks.msal.getAllAccounts.mockReset().mockReturnValue([]);
		mocks.msal.setActiveAccount.mockReset().mockImplementation((value) => {
			mocks.msal.getActiveAccount.mockReturnValue(value);
		});
		mocks.msal.loginPopup.mockReset().mockResolvedValue(authResult());
		mocks.msal.acquireTokenSilent.mockReset().mockResolvedValue(authResult());
		mocks.msal.acquireTokenPopup.mockReset().mockResolvedValue(authResult());
		vi.stubGlobal('fetch', vi.fn().mockImplementation(async () => jsonResponse(userRecord)));
	});

	afterEach(() => {
		vi.unstubAllGlobals();
		vi.unstubAllEnvs();
	});

	it('shares and waits for MSAL initialization across concurrent token requests', async () => {
		const initialization = deferred<void>();
		mocks.msal.initialize.mockReturnValueOnce(initialization.promise);
		mocks.msal.getActiveAccount.mockReturnValue(account);
		const auth = await import('./useAuth');

		const idToken = auth.getIdToken();
		const graphToken = auth.getGraphAccessToken();
		await vi.waitFor(() => expect(mocks.msal.initialize).toHaveBeenCalledOnce());
		expect(mocks.createMsal).toHaveBeenCalledOnce();
		expect(mocks.msal.acquireTokenSilent).not.toHaveBeenCalled();

		initialization.resolve();
		expect(await idToken).toBe('id-token');
		expect(await graphToken).toBe('graph-token');
		expect(mocks.msal.handleRedirectPromise).toHaveBeenCalledOnce();
	});

	it('surfaces initialization errors and retries initialization on the next login', async () => {
		mocks.msal.initialize.mockRejectedValueOnce(new Error('initialization failed'));
		const auth = await import('./useAuth');

		await expect(auth.login()).rejects.toThrow('initialization failed');
		expect(auth.loginError.value).toBe('initialization failed');
		await auth.login();
		expect(mocks.createMsal).toHaveBeenCalledTimes(2);
		expect(auth.user.value).toEqual(userRecord);
		expect(auth.loginError.value).toBeNull();
	});

	it('shows a useful error when Microsoft returns an empty error message', async () => {
		mocks.msal.loginPopup.mockRejectedValueOnce(new Error(''));
		const auth = await import('./useAuth');

		await expect(auth.login()).rejects.toThrow();
		expect(auth.loginError.value).toBe('Anmeldung fehlgeschlagen.');
	});

	it('restores the active account instead of an unrelated cached account', async () => {
		mocks.msal.getActiveAccount.mockReturnValue(otherAccount);
		mocks.msal.getAllAccounts.mockReturnValue([account, otherAccount]);
		const auth = await import('./useAuth');

		await auth.initAuth();
		expect(mocks.msal.setActiveAccount).toHaveBeenCalledWith(otherAccount);
		expect(mocks.msal.acquireTokenSilent).toHaveBeenCalledWith(
			expect.objectContaining({ account: otherAccount, forceRefresh: false }),
		);
		expect(auth.user.value).toEqual(userRecord);
		expect(auth.authLoading.value).toBe(false);
	});

	it('reports expired sessions without opening a popup during initialization', async () => {
		mocks.msal.getActiveAccount.mockReturnValue(account);
		mocks.msal.acquireTokenSilent.mockRejectedValueOnce(
			new mocks.InteractionRequiredAuthError('interaction required'),
		);
		const auth = await import('./useAuth');

		await auth.initAuth();
		expect(mocks.msal.acquireTokenPopup).not.toHaveBeenCalled();
		expect(auth.user.value).toBeNull();
		expect(auth.loginError.value).toContain('Anmeldung ist abgelaufen');
		expect(auth.authLoading.value).toBe(false);
	});

	it('does not open a popup for a network failure but does for interaction-required errors', async () => {
		mocks.msal.getActiveAccount.mockReturnValue(account);
		mocks.msal.acquireTokenSilent
			.mockRejectedValueOnce(new Error('network down'))
			.mockRejectedValueOnce(new mocks.InteractionRequiredAuthError('interaction required'));
		const auth = await import('./useAuth');

		await expect(auth.getIdToken()).rejects.toThrow('network down');
		expect(mocks.msal.acquireTokenPopup).not.toHaveBeenCalled();
		expect(await auth.getIdToken()).toBe('id-token');
		expect(mocks.msal.acquireTokenPopup).toHaveBeenCalledOnce();
	});

	it('only opens a Graph popup when Microsoft requires interaction', async () => {
		mocks.msal.getActiveAccount.mockReturnValue(account);
		mocks.msal.acquireTokenSilent
			.mockRejectedValueOnce(new Error('network down'))
			.mockRejectedValueOnce(new mocks.InteractionRequiredAuthError('consent required'));
		const auth = await import('./useAuth');

		await expect(auth.getGraphAccessToken()).rejects.toThrow('network down');
		expect(mocks.msal.acquireTokenPopup).not.toHaveBeenCalled();
		expect(await auth.getGraphAccessToken()).toBe('graph-token');
		expect(mocks.msal.acquireTokenPopup).toHaveBeenCalledOnce();
	});

	it('uses the ID-token expiry rather than the access-token expiry for caching', async () => {
		mocks.msal.getActiveAccount.mockReturnValue(account);
		mocks.msal.acquireTokenSilent
			.mockResolvedValueOnce(authResult('almost-expired', account, 30))
			.mockResolvedValueOnce(authResult('fresh-token'));
		const auth = await import('./useAuth');

		expect(await auth.getIdToken()).toBe('almost-expired');
		expect(await auth.getIdToken()).toBe('fresh-token');
		expect(await auth.getIdToken()).toBe('fresh-token');
		expect(mocks.msal.acquireTokenSilent).toHaveBeenCalledTimes(2);
	});

	it('surfaces backend errors and rejects malformed user responses', async () => {
		const fetchMock = vi.mocked(fetch);
		fetchMock
			.mockResolvedValueOnce(jsonResponse({ error: 'Zugriff verweigert' }, 403))
			.mockResolvedValueOnce(jsonResponse({ id: 'incomplete' }));
		const auth = await import('./useAuth');

		await expect(auth.login()).rejects.toThrow('Zugriff verweigert');
		expect(auth.loginError.value).toBe('Zugriff verweigert');
		expect(auth.user.value).toBeNull();
		await expect(auth.login()).rejects.toThrow('ungültige Benutzerdaten');
		expect(auth.user.value).toBeNull();
	});

	it('reports a backend error during session restoration', async () => {
		mocks.msal.getActiveAccount.mockReturnValue(account);
		vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({ error: 'Server nicht verfügbar' }, 503));
		const auth = await import('./useAuth');

		await auth.initAuth();
		expect(auth.user.value).toBeNull();
		expect(auth.loginError.value).toBe('Server nicht verfügbar');
		expect(auth.authLoading.value).toBe(false);
	});

	it('redirects to maintenance when restoring a blocked session', async () => {
		mocks.msal.getActiveAccount.mockReturnValue(account);
		vi.mocked(fetch).mockResolvedValueOnce(jsonResponse({
			error: 'maintenance',
			message: 'Wartungsarbeiten',
			scheduled_end: '2026-11-05T00:00:00Z',
		}, 503));
		const replace = vi.spyOn(window.location, 'replace').mockImplementation(() => {});
		const auth = await import('./useAuth');
		const maintenance = await import('./useMaintenance');

		await auth.initAuth();
		expect(replace).toHaveBeenCalledWith('/maintenance');
		expect(maintenance.maintenanceActive.value).toBe(true);
		expect(maintenance.maintenanceMessage.value).toBe('Wartungsarbeiten');
		expect(auth.user.value).toBeNull();
		replace.mockRestore();
	});

	it('keeps an explicit logout across reloads until a successful login', async () => {
		mocks.msal.getAllAccounts.mockReturnValue([account]);
		const auth = await import('./useAuth');
		await auth.login();
		await auth.logout();
		expect(auth.user.value).toBeNull();
		expect(localStorage.getItem('dpw.signedOut')).toBe('1');

		vi.resetModules();
		const reloaded = await import('./useAuth');
		await reloaded.initAuth();
		expect(reloaded.user.value).toBeNull();
		expect(mocks.msal.getAllAccounts).not.toHaveBeenCalled();
		await reloaded.login();
		expect(localStorage.getItem('dpw.signedOut')).toBeNull();
		expect(reloaded.user.value).toEqual(userRecord);
	});

	it('does not return or cache a token acquired after logout', async () => {
		const token = deferred<ReturnType<typeof authResult>>();
		mocks.msal.getActiveAccount.mockReturnValue(account);
		mocks.msal.acquireTokenSilent.mockReturnValueOnce(token.promise);
		const auth = await import('./useAuth');
		const pending = auth.getIdToken();
		const rejected = expect(pending).rejects.toThrow('zwischenzeitlich geändert');
		await vi.waitFor(() => expect(mocks.msal.acquireTokenSilent).toHaveBeenCalledOnce());

		await auth.logout();
		token.resolve(authResult('old-token'));
		await rejected;
		expect(auth.user.value).toBeNull();
	});

	it('does not restore a user when a login response arrives after logout', async () => {
		const response = deferred<Response>();
		vi.mocked(fetch).mockReturnValueOnce(response.promise);
		const auth = await import('./useAuth');
		const pending = auth.login();
		const rejected = expect(pending).rejects.toThrow('zwischenzeitlich geändert');
		await vi.waitFor(() => expect(fetch).toHaveBeenCalledOnce());

		await auth.logout();
		response.resolve(jsonResponse(userRecord));
		await rejected;
		expect(auth.user.value).toBeNull();
		expect(localStorage.getItem('dpw.signedOut')).toBe('1');
	});

	it('does not let an old acquisition overwrite a freshly renewed token', async () => {
		const oldToken = deferred<ReturnType<typeof authResult>>();
		mocks.msal.getActiveAccount.mockReturnValue(account);
		mocks.msal.acquireTokenSilent
			.mockReturnValueOnce(oldToken.promise)
			.mockResolvedValueOnce(authResult('fresh-token'));
		const auth = await import('./useAuth');
		const pending = auth.getIdToken();
		await vi.waitFor(() => expect(mocks.msal.acquireTokenSilent).toHaveBeenCalledOnce());

		auth.clearTokenCache();
		expect(await auth.getIdToken({ forceRefresh: true })).toBe('fresh-token');
		expect(mocks.msal.acquireTokenSilent).toHaveBeenLastCalledWith(
			expect.objectContaining({ forceRefresh: true }),
		);
		oldToken.resolve(authResult('old-token'));
		expect(await pending).toBe('old-token');
		expect(await auth.getIdToken()).toBe('fresh-token');
	});

	it('invalidates an in-flight login when the Microsoft configuration changes', async () => {
		const response = deferred<Response>();
		vi.mocked(fetch).mockReturnValueOnce(response.promise);
		const auth = await import('./useAuth');
		const pending = auth.login();
		const rejected = expect(pending).rejects.toThrow('zwischenzeitlich geändert');
		await vi.waitFor(() => expect(fetch).toHaveBeenCalledOnce());

		auth.setRuntimeAuthConfig('new-tenant', 'new-client');
		response.resolve(jsonResponse(userRecord));
		await rejected;
		expect(auth.user.value).toBeNull();
		await auth.login();
		expect(mocks.createMsal).toHaveBeenCalledTimes(2);
		expect(mocks.createMsal).toHaveBeenLastCalledWith(expect.objectContaining({
			auth: expect.objectContaining({ clientId: 'new-client' }),
		}));
	});

	it('preserves explicit logout for debug sessions too', async () => {
		vi.stubEnv('VITE_ENABLE_DEBUG_AUTH', '1');
		const auth = await import('./useAuth');
		await auth.debugLogin('debug-user');
		expect(await auth.getIdToken()).toBe('debug:debug-user');
		await auth.logout();
		expect(localStorage.getItem('dpw.debugUserId')).toBeNull();
		expect(localStorage.getItem('dpw.signedOut')).toBe('1');
		await auth.initAuth();
		expect(auth.user.value).toBeNull();
	});

	it('does not reset an initialized client when runtime settings are unchanged', async () => {
		mocks.msal.getActiveAccount.mockReturnValue(account);
		const auth = await import('./useAuth');
		await auth.getIdToken();
		auth.setRuntimeAuthConfig('tenant', 'client');
		await auth.getGraphAccessToken();
		expect(mocks.createMsal).toHaveBeenCalledOnce();
	});
});
