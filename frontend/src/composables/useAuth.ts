import { ref } from 'vue';
import type { AuthenticationResult, PublicClientApplication } from '@azure/msal-browser';
import type { User } from '../types';
import { config } from '../config';
import { wsDisconnect } from './useWebSocket';
import { maintenanceActive, maintenanceMessage, maintenanceScheduledEnd } from './useMaintenance';

const loginRequest = {
	scopes: ['openid', 'profile', 'email'],
};

const graphMailScopes = {
	scopes: ['Mail.Send', 'People.Read'],
};

const DEBUG_USER_STORAGE_KEY = 'dpw.debugUserId';
const SIGNED_OUT_STORAGE_KEY = 'dpw.signedOut';
const DEBUG_AUTH_ENABLED = import.meta.env.VITE_ENABLE_DEBUG_AUTH === '1';

export const user = ref<User | null>(null);
export const authLoading = ref(true);
export const loginError = ref<string | null>(null);

let runtimeClientId: string | null = null;
let runtimeTenantId: string | null = null;
let msalPromise: Promise<PublicClientApplication> | null = null;
let interactionRequiredErrorClass: typeof import('@azure/msal-browser').InteractionRequiredAuthError | null = null;
let debugUserId: string | null = null;
let sessionVersion = 0;
let tokenCacheVersion = 0;
let cachedIdToken: string | null = null;
let cachedIdTokenExpiry = 0;

interface TokenOptions {
	forceRefresh?: boolean;
	allowInteraction?: boolean;
}

let pendingIdToken: {
	promise: Promise<string>;
	sessionVersion: number;
	cacheVersion: number;
	forceRefresh: boolean;
	allowInteraction: boolean;
} | null = null;

let pendingInitAuth: {
	promise: Promise<void>;
	sessionVersion: number;
} | null = null;

class MaintenanceError extends Error {
	constructor() {
		super('maintenance');
	}
}

function effectiveClientId(): string {
	return runtimeClientId || config.AZURE_CLIENT_ID || '';
}

function effectiveTenantId(): string {
	return runtimeTenantId || config.AZURE_TENANT_ID || '';
}

function beginSessionTransition(): number {
	sessionVersion++;
	clearTokenCache();
	if (pendingInitAuth) {
		pendingInitAuth = null;
		authLoading.value = false;
	}
	return sessionVersion;
}

function requireCurrentSession(version: number): void {
	if (version !== sessionVersion) {
		throw new Error('Die Anmeldung wurde zwischenzeitlich geändert.');
	}
}

function isInteractionRequired(error: unknown): boolean {
	return interactionRequiredErrorClass !== null && error instanceof interactionRequiredErrorClass;
}

export function setRuntimeAuthConfig(tenantId: string, clientId: string): void {
	const previousTenantId = effectiveTenantId();
	const previousClientId = effectiveClientId();
	runtimeTenantId = tenantId;
	runtimeClientId = clientId;

	if (previousTenantId === effectiveTenantId() && previousClientId === effectiveClientId()) {
		return;
	}

	beginSessionTransition();
	msalPromise = null;
	if (!debugUserId && user.value) {
		wsDisconnect();
		user.value = null;
		loginError.value = 'Die Microsoft-Konfiguration wurde geändert. Bitte melde dich erneut an.';
	}
}

function getMsal(): Promise<PublicClientApplication> {
	const clientId = effectiveClientId();
	const tenantId = effectiveTenantId();
	if (!clientId || !tenantId) {
		return Promise.reject(new Error('Microsoft-Konfiguration fehlt'));
	}

	if (!msalPromise) {
		const promise = (async () => {
			const { PublicClientApplication, InteractionRequiredAuthError } = await import('@azure/msal-browser');
			interactionRequiredErrorClass = InteractionRequiredAuthError;
			const instance = new PublicClientApplication({
				auth: {
					clientId,
					authority: `https://login.microsoftonline.com/${tenantId}`,
					redirectUri: window.location.origin,
				},
				cache: {
					cacheLocation: 'localStorage',
					storeAuthStateInCookie: false,
				},
			});
			await instance.initialize();
			await instance.handleRedirectPromise();
			return instance;
		})();
		msalPromise = promise;
		void promise.catch(() => {
			if (msalPromise === promise) msalPromise = null;
		});
	}
	return msalPromise;
}

function cacheIdToken(result: AuthenticationResult, cacheVersion: number): string {
	if (!result.idToken) throw new Error('Microsoft hat kein ID-Token zurückgegeben.');
	const expiry = result.idTokenClaims && 'exp' in result.idTokenClaims
		? result.idTokenClaims.exp
		: undefined;
	if (cacheVersion === tokenCacheVersion && typeof expiry === 'number' && Number.isFinite(expiry)) {
		cachedIdToken = result.idToken;
		cachedIdTokenExpiry = expiry * 1000;
	}
	return result.idToken;
}

export async function getIdToken(options: TokenOptions = {}): Promise<string> {
	if (DEBUG_AUTH_ENABLED) {
		if (!debugUserId) {
			debugUserId = localStorage.getItem(DEBUG_USER_STORAGE_KEY);
		}
		if (debugUserId) return `debug:${debugUserId}`;
	}

	const forceRefresh = options.forceRefresh === true;
	const allowInteraction = options.allowInteraction !== false;
	if (!forceRefresh && cachedIdToken && Date.now() < cachedIdTokenExpiry - 60_000) {
		return cachedIdToken;
	}

	if (
		pendingIdToken &&
		pendingIdToken.sessionVersion === sessionVersion &&
		pendingIdToken.cacheVersion === tokenCacheVersion &&
		(!forceRefresh || pendingIdToken.forceRefresh) &&
		pendingIdToken.allowInteraction === allowInteraction
	) {
		return pendingIdToken.promise;
	}

	const version = sessionVersion;
	const cacheVersion = tokenCacheVersion;
	const promise = (async () => {
		const msal = await getMsal();
		requireCurrentSession(version);
		const account = msal.getActiveAccount();
		if (!account) throw new Error('Nicht angemeldet');

		let result: AuthenticationResult;
		try {
			result = await msal.acquireTokenSilent({
				...loginRequest,
				account,
				forceRefresh,
			});
		} catch (error) {
			requireCurrentSession(version);
			if (!allowInteraction || !isInteractionRequired(error)) {
				throw error;
			}
			result = await msal.acquireTokenPopup({ ...loginRequest, account });
		}
		requireCurrentSession(version);
		return cacheIdToken(result, cacheVersion);
	})();
	pendingIdToken = { promise, sessionVersion: version, cacheVersion, forceRefresh, allowInteraction };

	try {
		return await promise;
	} finally {
		if (pendingIdToken?.promise === promise) pendingIdToken = null;
	}
}

export async function getGraphAccessToken(): Promise<string> {
	const version = sessionVersion;
	const msal = await getMsal();
	requireCurrentSession(version);
	const account = msal.getActiveAccount();
	if (!account) throw new Error('Nicht angemeldet');

	let result: AuthenticationResult;
	try {
		result = await msal.acquireTokenSilent({
			...graphMailScopes,
			account,
		});
	} catch (error) {
		requireCurrentSession(version);
		if (!isInteractionRequired(error)) throw error;
		result = await msal.acquireTokenPopup({ ...graphMailScopes, account });
	}
	requireCurrentSession(version);
	if (!result.accessToken) throw new Error('Microsoft hat kein Zugriffstoken zurückgegeben.');
	return result.accessToken;
}

function isUser(value: unknown): value is User {
	if (!value || typeof value !== 'object') return false;
	const data = value as Record<string, unknown>;
	const stringFields = [
		'id', 'microsoft_oid', 'email', 'display_name', 'role',
		'avatar_color', 'created_at', 'updated_at',
	];
	const booleanFields = [
		'proofreading_enabled',
		'notify_material_assigned', 'notify_activity_assigned', 'notify_program_assigned',
		'notify_mail_own_activity', 'notify_mail_department',
		'notify_channel_websocket', 'notify_channel_email',
	];
	return stringFields.every((field) => typeof data[field] === 'string') &&
		booleanFields.every((field) => typeof data[field] === 'boolean') &&
		(data.department === null || typeof data.department === 'string') &&
		(data.time_display_mode === 'minutes' || data.time_display_mode === 'clock');
}

function parseErrorBody(body: string): Record<string, unknown> | null {
	try {
		const value: unknown = JSON.parse(body);
		return value && typeof value === 'object' && !Array.isArray(value)
			? value as Record<string, unknown>
			: null;
	} catch {
		return null;
	}
}

async function syncUser(idToken: string, version: number): Promise<void> {
	const res = await fetch('/api/auth/me', {
		method: 'POST',
		headers: { Authorization: `Bearer ${idToken}` },
	});
	requireCurrentSession(version);
	if (res.ok) {
		const data: unknown = await res.json();
		requireCurrentSession(version);
		if (!isUser(data)) throw new Error('Der Server hat ungültige Benutzerdaten zurückgegeben.');
		user.value = data;
		return;
	}

	const body = await res.text();
	requireCurrentSession(version);
	const data = parseErrorBody(body);
	if (res.status === 503 && data?.error === 'maintenance') {
		maintenanceActive.value = true;
		if (typeof data.message === 'string') maintenanceMessage.value = data.message;
		if (typeof data.scheduled_end === 'string') maintenanceScheduledEnd.value = data.scheduled_end;
		throw new MaintenanceError();
	}
	const message = typeof data?.error === 'string' ? data.error : body;
	throw new Error(message || `Server-Fehler: ${res.status}`);
}

function handleAuthError(error: unknown, fallback: string): boolean {
	if (error instanceof MaintenanceError) {
		window.location.replace('/maintenance');
		return true;
	}
	loginError.value = error instanceof Error && error.message.trim() ? error.message : fallback;
	return false;
}

export async function login(): Promise<void> {
	const version = beginSessionTransition();
	wsDisconnect();
	user.value = null;
	loginError.value = null;
	let msal: PublicClientApplication | null = null;
	try {
		debugUserId = null;
		localStorage.removeItem(DEBUG_USER_STORAGE_KEY);
		msal = await getMsal();
		requireCurrentSession(version);
		const result = await msal.loginPopup(loginRequest);
		requireCurrentSession(version);
		if (!result.account || !result.idToken) {
			throw new Error('Microsoft hat keine gültigen Anmeldedaten zurückgegeben.');
		}
		msal.setActiveAccount(result.account);
		await syncUser(result.idToken, version);
		requireCurrentSession(version);
		localStorage.removeItem(SIGNED_OUT_STORAGE_KEY);
	} catch (error) {
		if (version === sessionVersion) {
			user.value = null;
			clearTokenCache();
			msal?.setActiveAccount(null);
			if (handleAuthError(error, 'Anmeldung fehlgeschlagen.')) return;
		}
		throw error;
	}
}

export function clearTokenCache(): void {
	tokenCacheVersion++;
	cachedIdToken = null;
	cachedIdTokenExpiry = 0;
	pendingIdToken = null;
}

export async function logout(): Promise<void> {
	const previousMsal = msalPromise;
	const wasDebug = !!debugUserId;
	const version = beginSessionTransition();
	wsDisconnect();
	user.value = null;
	debugUserId = null;
	loginError.value = null;
	try {
		localStorage.removeItem(DEBUG_USER_STORAGE_KEY);
		localStorage.setItem(SIGNED_OUT_STORAGE_KEY, '1');
		if (!wasDebug && previousMsal) {
			const msal = await previousMsal;
			if (version === sessionVersion) msal.setActiveAccount(null);
		}
	} catch (error) {
		if (version === sessionVersion) {
			loginError.value = error instanceof Error && error.message.trim()
				? error.message
				: 'Abmeldung fehlgeschlagen.';
		}
		throw error;
	}
}

export const isDebug = DEBUG_AUTH_ENABLED;

export async function debugLogin(userId: string): Promise<void> {
	if (!DEBUG_AUTH_ENABLED) {
		throw new Error('Debug-Authentifizierung ist in diesem Build deaktiviert.');
	}
	const version = beginSessionTransition();
	wsDisconnect();
	user.value = null;
	loginError.value = null;
	try {
		debugUserId = userId;
		localStorage.setItem(DEBUG_USER_STORAGE_KEY, userId);
		await syncUser(`debug:${userId}`, version);
		requireCurrentSession(version);
		localStorage.removeItem(SIGNED_OUT_STORAGE_KEY);
	} catch (error) {
		if (version === sessionVersion) {
			user.value = null;
			clearTokenCache();
			debugUserId = null;
			localStorage.removeItem(DEBUG_USER_STORAGE_KEY);
			if (handleAuthError(error, 'Debug-Anmeldung fehlgeschlagen.')) return;
		}
		throw error;
	}
}

export async function initAuth(): Promise<void> {
	if (pendingInitAuth?.sessionVersion === sessionVersion) return pendingInitAuth.promise;

	const version = sessionVersion;
	authLoading.value = true;
	loginError.value = null;
	const promise = (async () => {
		try {
			if (!DEBUG_AUTH_ENABLED) {
				localStorage.removeItem(DEBUG_USER_STORAGE_KEY);
			}
			if (localStorage.getItem(SIGNED_OUT_STORAGE_KEY) === '1') return;

			const storedDebugUserId = DEBUG_AUTH_ENABLED
				? localStorage.getItem(DEBUG_USER_STORAGE_KEY)
				: null;
			if (storedDebugUserId) {
				debugUserId = storedDebugUserId;
				await syncUser(`debug:${storedDebugUserId}`, version);
				return;
			}

			const msal = await getMsal();
			requireCurrentSession(version);
			const account = msal.getActiveAccount() ?? msal.getAllAccounts()[0];
			if (account) {
				msal.setActiveAccount(account);
				const token = await getIdToken({ allowInteraction: false });
				await syncUser(token, version);
			}
		} catch (error) {
			if (version !== sessionVersion) return;
			user.value = null;
			if (isInteractionRequired(error)) {
				loginError.value = 'Deine Anmeldung ist abgelaufen. Bitte melde dich erneut an.';
			} else {
				handleAuthError(error, 'Anmeldung konnte nicht wiederhergestellt werden.');
			}
		}
	})();
	pendingInitAuth = { promise, sessionVersion: version };
	try {
		await promise;
	} finally {
		if (pendingInitAuth?.promise === promise) {
			pendingInitAuth = null;
			authLoading.value = false;
		}
	}
}
