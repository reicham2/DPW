import { ref } from 'vue';
import type { PublicClientApplication, AccountInfo } from '@azure/msal-browser';
import type { User } from '../types';
import { config } from '../config';

const msalConfig = {
	auth: {
		clientId: config.MSAL_CLIENT_ID,
		authority: `https://login.microsoftonline.com/${config.MSAL_TENANT_ID}`,
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
let debugUserId: string | null = null;

export const user = ref<User | null>(null);
export const authLoading = ref(true);
export const loginError = ref<string | null>(null);

async function getMsal(): Promise<PublicClientApplication> {
	if (!msalInstance) {
		const { PublicClientApplication } = await import('@azure/msal-browser');
		msalInstance = new PublicClientApplication(msalConfig);
		await msalInstance.initialize();
		await msalInstance.handleRedirectPromise();
	}
	return msalInstance;
}

export async function getIdToken(): Promise<string> {
	if (debugUserId) return `debug:${debugUserId}`;

	const msal = await getMsal();
	const account = msal.getActiveAccount();
	if (!account) throw new Error('Nicht angemeldet');

	try {
		const result = await msal.acquireTokenSilent({ ...loginRequest, account });
		return result.idToken;
	} catch {
		// InteractionRequiredAuthError or similar — fall back to popup
		const result = await msal.acquireTokenPopup({ ...loginRequest, account });
		return result.idToken;
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
	if (!debugUserId) {
		const msal = await getMsal();
		msal.setActiveAccount(null);
	}
	debugUserId = null;
	user.value = null;
}

export const isDebug = config.DEBUG;

export async function debugLogin(userId: string): Promise<void> {
	loginError.value = null;
	try {
		const res = await fetch('/api/auth/debug-login', {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify({ user_id: userId }),
		});
		if (res.ok) {
			user.value = (await res.json()) as User;
			debugUserId = userId;
		} else {
			const text = await res.text();
			throw new Error(text || `Server-Fehler: ${res.status}`);
		}
	} catch (err) {
		loginError.value =
			err instanceof Error ? err.message : 'Debug-Anmeldung fehlgeschlagen.';
		throw err;
	}
}

export async function initAuth(): Promise<void> {
	try {
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
