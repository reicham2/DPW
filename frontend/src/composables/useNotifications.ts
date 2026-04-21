import { computed, ref } from 'vue';
import { apiFetch } from './useApi';
import { wsAddGlobalHandler } from './useWebSocket';
import type { NotificationRecord, WsEvent } from '../types';

const notifications = ref<NotificationRecord[]>([]);
const loading = ref(false);
const error = ref<string | null>(null);
let initialized = false;

function sortByNewest(items: NotificationRecord[]) {
	items.sort((a, b) =>
		new Date(b.created_at).getTime() - new Date(a.created_at).getTime(),
	);
}

wsAddGlobalHandler((event: WsEvent) => {
	if (event.event !== 'notification') return;
	const incoming = event.notification;
	const idx = notifications.value.findIndex((n) => n.id === incoming.id);
	if (idx >= 0) {
		notifications.value[idx] = incoming;
	} else {
		notifications.value.unshift(incoming);
	}
	sortByNewest(notifications.value);
});

async function fetchNotifications(limit = 50) {
	loading.value = true;
	error.value = null;
	try {
		const res = await apiFetch(`/api/notifications?limit=${limit}`);
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
	const res = await apiFetch(`/api/notifications/${id}/read`, { method: 'PATCH' });
	if (!res.ok) throw new Error(await res.text());
	const idx = notifications.value.findIndex((n) => n.id === id);
	if (idx >= 0) notifications.value[idx].is_read = true;
}

async function markAllRead() {
	const res = await apiFetch('/api/notifications/read-all', { method: 'POST' });
	if (!res.ok) throw new Error(await res.text());
	notifications.value = notifications.value.map((n) => ({ ...n, is_read: true }));
}

const unreadCount = computed(
	() => notifications.value.filter((n) => !n.is_read).length,
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
	};
}
