import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { NotificationRecord, WsEvent } from '../types';

const { apiFetchMock, wsHandlers, userRef } = vi.hoisted(() => ({
	apiFetchMock: vi.fn(),
	wsHandlers: [] as Array<(event: WsEvent) => void>,
	userRef: { value: null as any },
}));

vi.mock('./useApi', () => ({
	apiFetch: apiFetchMock,
}));

vi.mock('./useWebSocket', () => ({
	wsAddGlobalHandler: (handler: (event: WsEvent) => void) =>
		wsHandlers.push(handler),
}));

vi.mock('./useAuth', () => ({
	user: userRef,
}));

import {
	requestBrowserNotificationPermission,
	syncPushSubscription,
	useNotifications,
} from './useNotifications';

function response(data: unknown, ok = true, text = 'error'): Response {
	return {
		ok,
		json: vi.fn().mockResolvedValue(data),
		text: vi.fn().mockResolvedValue(text),
	} as unknown as Response;
}

function note(overrides: Partial<NotificationRecord> = {}): NotificationRecord {
	return {
		id: 'n1',
		user_id: 'u1',
		category: 'activity_assigned',
		title: 'Titel',
		message: 'Text',
		link: '/x',
		payload: {},
		is_read: false,
		created_at: '2026-01-01T00:00:00Z',
		...overrides,
	};
}

describe('useNotifications', () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		apiFetchMock.mockReset();
		userRef.value = {
			id: 'u1',
			notify_material_assigned: true,
			notify_activity_assigned: true,
			notify_program_assigned: true,
			notify_mail_own_activity: true,
			notify_mail_department: false,
			notify_channel_websocket: false,
		};
	});

	it('fetchNotifications loads and sorts newest first', async () => {
		const n = useNotifications();
		apiFetchMock.mockResolvedValueOnce(
			response([
				note({ id: 'old', created_at: '2026-01-01T00:00:00Z' }),
				note({ id: 'new', created_at: '2026-01-02T00:00:00Z' }),
			]),
		);

		await n.fetchNotifications();
		expect(n.notifications.value.map((x) => x.id)).toEqual(['new', 'old']);
	});

	it('ensureNotifications fetches only once when initialized', async () => {
		const n = useNotifications();
		apiFetchMock.mockResolvedValueOnce(response([]));
		// Prime module singleton initialized state explicitly.
		await n.fetchNotifications();
		apiFetchMock.mockReset();

		await n.ensureNotifications();
		await n.ensureNotifications();
		expect(apiFetchMock).toHaveBeenCalledTimes(0);
	});

	it('markRead updates local state', async () => {
		const n = useNotifications();
		n.notifications.value = [note({ id: 'n1', is_read: false })];
		apiFetchMock.mockResolvedValueOnce(response({}));

		await n.markRead('n1');
		expect(n.notifications.value[0].is_read).toBe(true);
	});

	it('markAllRead marks every notification as read', async () => {
		const n = useNotifications();
		n.notifications.value = [note({ id: 'a' }), note({ id: 'b' })];
		apiFetchMock.mockResolvedValueOnce(response({}));

		await n.markAllRead();
		expect(n.notifications.value.every((x) => x.is_read)).toBe(true);
	});

	it('unreadCount respects user category preferences', () => {
		const n = useNotifications();
		n.notifications.value = [
			note({ id: '1', category: 'activity_assigned', is_read: false }),
			note({ id: '2', category: 'mail_department', is_read: false }),
			note({ id: '3', category: 'program_assigned', is_read: true }),
		];
		expect(n.unreadCount.value).toBe(1);
	});

	it('websocket notification updates existing item instead of duplicating', () => {
		const n = useNotifications();
		n.notifications.value = [note({ id: 'same', title: 'Old' })];

		wsHandlers[0]({
			event: 'notification',
			notification: note({ id: 'same', title: 'New' }),
		});

		expect(n.notifications.value).toHaveLength(1);
		expect(n.notifications.value[0].title).toBe('New');
	});

	it('websocket notification prepends new item and keeps sorting', () => {
		const n = useNotifications();
		n.notifications.value = [
			note({ id: 'old', created_at: '2026-01-01T00:00:00Z' }),
		];

		wsHandlers[0]({
			event: 'notification',
			notification: note({ id: 'new', created_at: '2026-01-02T00:00:00Z' }),
		});

		expect(n.notifications.value.map((x) => x.id)).toEqual(['new', 'old']);
	});
});

describe('notification permission + push sync', () => {
	beforeEach(() => {
		apiFetchMock.mockReset();
	});

	it('requestBrowserNotificationPermission returns unsupported without Notification API', async () => {
		const originalWindow = (globalThis as any).window;
		Object.defineProperty(globalThis, 'window', {
			value: {},
			configurable: true,
		});

		const result = await requestBrowserNotificationPermission();
		expect(result).toBe('unsupported');

		Object.defineProperty(globalThis, 'window', {
			value: originalWindow,
			configurable: true,
		});
	});

	it('syncPushSubscription exits quietly when browser features are missing', async () => {
		Object.defineProperty(globalThis, 'window', {
			value: globalThis,
			configurable: true,
		});
		Object.defineProperty(globalThis, 'navigator', {
			value: {},
			configurable: true,
		});

		await expect(syncPushSubscription(true)).resolves.toBeUndefined();
		expect(apiFetchMock).not.toHaveBeenCalled();
	});
});
