import { ref } from 'vue';
import type { PublicClientApplication, AccountInfo } from '@azure/msal-browser';
import type { User } from '../types';
import { config } from '../config';
import { wsDisconnect } from './useWebSocket';

const msalConfig = {
	auth: {
		clientId: config.AZURE_CLIENT_ID,
		authority: `https://login.microsoftonline.com/${config.AZURE_TENANT_ID}`,
		redirectUri: window.location.origin,
	},
	cache: {
		cacheLocation: 'localStorage' as const,
		storeAuthStateInCookie: false,
	},
};

const loginRequest = {
	scopes: ['openid', 'profile', 'email'],
};

const graphMailScopes = {
	scopes: ['Mail.Send', 'People.Read'],
};

let msalInstance: PublicClientApplication | null = null;
let runtimeClientId: string | null = null;
let runtimeTenantId: string | null = null;
let pendingIdTokenPromise: Promise<string> | null = null;
let debugUserId: string | null = null;
const DEBUG_USER_STORAGE_KEY = 'dpw.debugUserId';
const DEBUG_AUTH_ENABLED = import.meta.env.VITE_ENABLE_DEBUG_AUTH === '1';

export const user = ref<User | null>(null);
export const authLoading = ref(true);
export const loginError = ref<string | null>(null);

function effectiveClientId(): string {
	return runtimeClientId || config.AZURE_CLIENT_ID || '';
}

function effectiveTenantId(): string {
	return runtimeTenantId || config.AZURE_TENANT_ID || '';
}

export function setRuntimeAuthConfig(tenantId: string, clientId: string): void {
	runtimeTenantId = tenantId;
	runtimeClientId = clientId;
	msalInstance = null;
	cachedIdToken = null;
	cachedIdTokenExpiry = 0;
	pendingIdTokenPromise = null;
}

async function getMsal(): Promise<PublicClientApplication> {
	const clientId = effectiveClientId();
	const tenantId = effectiveTenantId();
	if (!clientId || !tenantId) throw new Error('Microsoft-Konfiguration fehlt');

	if (!msalInstance) {
		const { PublicClientApplication } = await import('@azure/msal-browser');
		msalInstance = new PublicClientApplication({
			auth: {
				clientId,
				authority: `https://login.microsoftonline.com/${tenantId}`,
				redirectUri: window.location.origin,
			},
			cache: msalConfig.cache,
		});
		await msalInstance.initialize();
		await msalInstance.handleRedirectPromise();
	}
	return msalInstance;
}

let cachedIdToken: string | null = null;
let cachedIdTokenExpiry = 0;

export async function getIdToken(): Promise<string> {
	if (DEBUG_AUTH_ENABLED) {
		if (!debugUserId) {
			const storedDebugUserId = localStorage.getItem(DEBUG_USER_STORAGE_KEY);
			if (storedDebugUserId) {
				debugUserId = storedDebugUserId;
			}
		}
		if (debugUserId) return `debug:${debugUserId}`;
	}

	// Return cached token if still valid (with 60s buffer before expiry)
	if (cachedIdToken && Date.now() < cachedIdTokenExpiry - 60_000) {
		return cachedIdToken;
	}

	if (pendingIdTokenPromise) {
		return pendingIdTokenPromise;
	}

	pendingIdTokenPromise = (async () => {
		const msal = await getMsal();
		const account = msal.getActiveAccount();
		if (!account) throw new Error('Nicht angemeldet');

		try {
			const result = await msal.acquireTokenSilent({
				...loginRequest,
				account,
			});
			cachedIdToken = result.idToken;
			cachedIdTokenExpiry = result.expiresOn
				? result.expiresOn.getTime()
				: Date.now() + 3600_000;
			return result.idToken;
		} catch {
			// InteractionRequiredAuthError or similar — fall back to popup
			const result = await msal.acquireTokenPopup({ ...loginRequest, account });
			cachedIdToken = result.idToken;
			cachedIdTokenExpiry = result.expiresOn
				? result.expiresOn.getTime()
				: Date.now() + 3600_000;
			return result.idToken;
		}
	})();

	try {
		return await pendingIdTokenPromise;
	} finally {
		pendingIdTokenPromise = null;
	}
}

export async function getGraphAccessToken(): Promise<string> {
	const msal = await getMsal();
	const account = msal.getActiveAccount();
	if (!account) throw new Error('Nicht angemeldet');

	try {
		const result = await msal.acquireTokenSilent({
			...graphMailScopes,
			account,
		});
		return result.accessToken;
	} catch {
		const result = await msal.acquireTokenPopup({
			...graphMailScopes,
			account,
		});
		return result.accessToken;
	}
}

async function syncUser(idToken: string): Promise<void> {
	const res = await fetch('/api/auth/me', {
		method: 'POST',
		headers: { Authorization: `Bearer ${idToken}` },
	});
	if (res.ok) {
		user.value = (await res.json()) as User;
	} else {
		const text = await res.text();
		throw new Error(text || `Server-Fehler: ${res.status}`);
	}
}

export async function login(): Promise<void> {
	loginError.value = null;
	debugUserId = null;
	localStorage.removeItem(DEBUG_USER_STORAGE_KEY);
	const msal = await getMsal();
	try {
		const result = await msal.loginPopup(loginRequest);
		msal.setActiveAccount(result.account);
		await syncUser(result.idToken);
	} catch (err) {
		loginError.value =
			err instanceof Error ? err.message : 'Anmeldung fehlgeschlagen.';
		throw err;
	}
}

export async function logout(): Promise<void> {
	wsDisconnect();
	cachedIdToken = null;
	cachedIdTokenExpiry = 0;
	if (!debugUserId) {
		const msal = await getMsal();
		msal.setActiveAccount(null);
	}
	debugUserId = null;
	localStorage.removeItem(DEBUG_USER_STORAGE_KEY);
	user.value = null;
}

export const isDebug = DEBUG_AUTH_ENABLED;

export async function debugLogin(userId: string): Promise<void> {
	if (!DEBUG_AUTH_ENABLED) {
		throw new Error('Debug-Authentifizierung ist in diesem Build deaktiviert.');
	}
	wsDisconnect();
	loginError.value = null;
	try {
		debugUserId = userId;
		localStorage.setItem(DEBUG_USER_STORAGE_KEY, userId);
		await syncUser(`debug:${userId}`);
	} catch (err) {
		debugUserId = null;
		localStorage.removeItem(DEBUG_USER_STORAGE_KEY);
		loginError.value =
			err instanceof Error ? err.message : 'Debug-Anmeldung fehlgeschlagen.';
		throw err;
	}
}

export async function initAuth(): Promise<void> {
	try {
		if (!DEBUG_AUTH_ENABLED) {
			localStorage.removeItem(DEBUG_USER_STORAGE_KEY);
		}

		const storedDebugUserId = DEBUG_AUTH_ENABLED
			? localStorage.getItem(DEBUG_USER_STORAGE_KEY)
			: null;
		if (storedDebugUserId) {
			debugUserId = storedDebugUserId;
			await syncUser(`debug:${storedDebugUserId}`);
			return;
		}

		const msal = await getMsal();
		const accounts = msal.getAllAccounts();
		if (accounts.length > 0) {
			msal.setActiveAccount(accounts[0]);
			const token = await getIdToken();
			await syncUser(token);
		}
	} catch {
		user.value = null;
	} finally {
		authLoading.value = false;
	}
}
