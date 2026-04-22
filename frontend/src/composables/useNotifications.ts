import { computed, ref, watch } from 'vue';
import { apiFetch } from './useApi';
import { wsAddGlobalHandler } from './useWebSocket';
import { user } from './useAuth';
import type { NotificationRecord, WsEvent } from '../types';

const notifications = ref<NotificationRecord[]>([]);
const loading = ref(false);
const error = ref<string | null>(null);
let initialized = false;

export async function requestBrowserNotificationPermission() {
	if (typeof window === 'undefined' || !('Notification' in window))
		return 'unsupported';
	if (Notification.permission !== 'default') return Notification.permission;
	return Notification.requestPermission();
}

function base64UrlToUint8Array(base64Url: string): Uint8Array {
	const padding = '='.repeat((4 - (base64Url.length % 4)) % 4);
	const base64 = (base64Url + padding).replace(/-/g, '+').replace(/_/g, '/');
	const raw = atob(base64);
	const out = new Uint8Array(raw.length);
	for (let i = 0; i < raw.length; i += 1) out[i] = raw.charCodeAt(i);
	return out;
}

export async function syncPushSubscription(enabled: boolean) {
	if (typeof window === 'undefined') return;
	if (!('serviceWorker' in navigator) || !('PushManager' in window)) return;

	const reg =
		(await navigator.serviceWorker.getRegistration()) ??
		(await navigator.serviceWorker.ready);
	const existing = await reg.pushManager.getSubscription();

	if (!enabled) {
		if (existing) {
			await apiFetch('/api/push/subscriptions', {
				method: 'DELETE',
				body: JSON.stringify({ endpoint: existing.endpoint }),
			}).catch(() => {});
			await existing.unsubscribe().catch(() => {});
		}
		return;
	}

	const permission = await requestBrowserNotificationPermission();
	if (permission !== 'granted') return;

	const keyRes = await apiFetch('/api/push/vapid-public-key');
	if (!keyRes.ok) return;
	const keyPayload = (await keyRes.json()) as { publicKey?: string };
	if (!keyPayload.publicKey) return;

	let sub = existing;
	if (!sub) {
		sub = await reg.pushManager.subscribe({
			userVisibleOnly: true,
			applicationServerKey: base64UrlToUint8Array(keyPayload.publicKey),
		});
	}

	const json = sub.toJSON() as {
		endpoint?: string;
		keys?: { p256dh?: string; auth?: string };
	};
	if (!json.endpoint || !json.keys?.p256dh || !json.keys?.auth) return;

	await apiFetch('/api/push/subscriptions', {
		method: 'POST',
		body: JSON.stringify({
			endpoint: json.endpoint,
			keys: {
				p256dh: json.keys.p256dh,
				auth: json.keys.auth,
			},
		}),
	}).catch(() => {});
}

async function showSystemNotification(incoming: NotificationRecord) {
	const payload = (incoming.payload ?? {}) as Record<string, unknown>;
	const activityDate =
		typeof payload.activity_date_display === 'string'
			? payload.activity_date_display.trim()
			: '';
	const fallbackDate = (() => {
		const d = new Date(incoming.created_at);
		if (Number.isNaN(d.getTime())) return '';
		const dd = String(d.getDate()).padStart(2, '0');
		const mm = String(d.getMonth() + 1).padStart(2, '0');
		const yyyy = String(d.getFullYear());
		return `${dd}.${mm}.${yyyy}`;
	})();
	const compactBody = activityDate || fallbackDate ? `Datum: ${activityDate || fallbackDate}` : 'Neue Benachrichtigung';

	if (typeof window === 'undefined' || !('Notification' in window)) return;
	if (Notification.permission !== 'granted') return;

	// Prefer service-worker notifications for better support in installed PWA apps on mobile.
	if ('serviceWorker' in navigator) {
		try {
			const reg =
				(await navigator.serviceWorker.getRegistration()) ??
				(await navigator.serviceWorker.ready);
			await reg.showNotification(incoming.title, {
				body: compactBody,
				tag: `dpw-note-${incoming.id}`,
				data: { url: incoming.link ?? '/profile' },
				icon: '/logo.svg',
				badge: '/logo.svg',
			});
			return;
		} catch {
			// Fallback to window notification below.
		}
	}

	try {
		new Notification(incoming.title, { body: compactBody });
	} catch {
		// Ignore browser notification failures.
	}
}

function sortByNewest(items: NotificationRecord[]) {
	items.sort(
		(a, b) =>
			new Date(b.created_at).getTime() - new Date(a.created_at).getTime(),
	);
}

wsAddGlobalHandler((event: WsEvent) => {
	if (event.event !== 'notification') return;
	const incoming = event.notification;
	if (
		typeof window !== 'undefined' &&
		'Notification' in window &&
		Notification.permission === 'granted' &&
		(user.value?.notify_channel_websocket ?? true)
	) {
		void showSystemNotification(incoming);
	}
	const idx = notifications.value.findIndex((n) => n.id === incoming.id);
	if (idx >= 0) {
		notifications.value[idx] = incoming;
	} else {
		notifications.value.unshift(incoming);
	}
	sortByNewest(notifications.value);
});

async function fetchNotifications(limit?: number) {
	loading.value = true;
	error.value = null;
	try {
		const path =
			typeof limit === 'number' && limit > 0
				? `/api/notifications?limit=${limit}`
				: '/api/notifications';
		const res = await apiFetch(path);
		if (!res.ok) throw new Error(await res.text());
		notifications.value = (await res.json()) as NotificationRecord[];
		sortByNewest(notifications.value);
		initialized = true;
	} catch (e) {
		error.value = String(e);
		throw e;
	} finally {
		loading.value = false;
	}
}

async function ensureNotifications() {
	if (initialized) return;
	await fetchNotifications();
}

async function markRead(id: string) {
	const res = await apiFetch(`/api/notifications/${id}/read`, {
		method: 'PATCH',
	});
	if (!res.ok) throw new Error(await res.text());
	const idx = notifications.value.findIndex((n) => n.id === id);
	if (idx >= 0) notifications.value[idx].is_read = true;
}

async function markAllRead() {
	const res = await apiFetch('/api/notifications/read-all', { method: 'POST' });
	if (!res.ok) throw new Error(await res.text());
	notifications.value = notifications.value.map((n) => ({
		...n,
		is_read: true,
	}));
}

const unreadCount = computed(
	() =>
		notifications.value.filter((n) => {
			if (n.is_read) return false;
			if (!user.value) return false;
			if (n.category === 'material_assigned')
				return user.value.notify_material_assigned;
			if (n.category === 'mail_own_activity')
				return user.value.notify_mail_own_activity;
			if (n.category === 'mail_department')
				return user.value.notify_mail_department;
			return true;
		}).length,
);

watch(
	() => user.value?.id ?? null,
	(newUserId, oldUserId) => {
		if (newUserId === oldUserId) return;
		notifications.value = [];
		initialized = false;
		if (newUserId) {
			fetchNotifications().catch(() => {});
			void syncPushSubscription(user.value?.notify_channel_websocket ?? true);
		}
	},
	{ immediate: true },
);

export function useNotifications() {
	return {
		notifications,
		loading,
		error,
		unreadCount,
		fetchNotifications,
		ensureNotifications,
		markRead,
		markAllRead,
		syncPushSubscription,
	};
}
