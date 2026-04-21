import { computed, ref } from 'vue';
import { getIdToken } from './useAuth';

const browserOnline = ref(
	typeof navigator === 'undefined' ? true : navigator.onLine,
);
const apiReachable = ref(true);
let networkListenersBound = false;

function bindNetworkListeners() {
	if (networkListenersBound) return;
	if (typeof window === 'undefined') return;
	networkListenersBound = true;
	window.addEventListener('online', () => {
		browserOnline.value = true;
		// Optimistically reset API reachability; next request confirms state.
		apiReachable.value = true;
	});
	window.addEventListener('offline', () => {
		browserOnline.value = false;
		apiReachable.value = false;
	});
}

bindNetworkListeners();

function isLikelyNetworkError(message: string): boolean {
	return (
		message.includes('failed to fetch') ||
		message.includes('networkerror') ||
		message.includes('load failed') ||
		message.includes('network request failed')
	);
}

function isOffline(): boolean {
	return typeof navigator !== 'undefined' && navigator.onLine === false;
}

export function useApiConnectionStatus() {
	return {
		browserOnline,
		apiReachable,
		apiOnline: computed(() => browserOnline.value && apiReachable.value),
	};
}

export function formatApiError(error: unknown): string {
	const raw = error instanceof Error ? error.message : String(error ?? '');
	const msg = raw.trim();
	const normalized = msg.toLowerCase();

	if (isOffline()) {
		return 'Du bist offline. Änderungen wurden lokal zwischengespeichert und werden gespeichert, sobald die Verbindung wieder da ist.';
	}

	if (isLikelyNetworkError(normalized)) {
		return 'Die Verbindung zum Server ist unterbrochen. Änderungen bleiben lokal zwischengespeichert.';
	}

	if (!msg) return 'Unbekannter Fehler bei der Serververbindung.';
	if (msg.startsWith('Error: ')) return msg.slice(7);
	return msg;
}

export async function apiFetch(
	url: string,
	options: RequestInit = {},
): Promise<Response> {
	const token = await getIdToken();
	try {
		const response = await fetch(url, {
			...options,
			headers: {
				...((options.headers as Record<string, string>) ?? {}),
				Authorization: `Bearer ${token}`,
				...(options.body && !(options.body instanceof FormData)
					? { 'Content-Type': 'application/json' }
					: {}),
			},
		});
		apiReachable.value = true;
		return response;
	} catch (error) {
		const raw = error instanceof Error ? error.message : String(error ?? '');
		const normalized = raw.trim().toLowerCase();
		if (isOffline() || isLikelyNetworkError(normalized)) {
			apiReachable.value = false;
		}
		throw error;
	}
}
