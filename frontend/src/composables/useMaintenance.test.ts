import { beforeEach, describe, expect, it, vi } from 'vitest';

const { apiFetchMock } = vi.hoisted(() => ({
	apiFetchMock: vi.fn(),
}));

vi.mock('./useApi', () => ({
	apiFetch: apiFetchMock,
}));

import {
	applyMaintenanceData,
	applyMaintenanceStatus,
	fetchAdminMaintenance,
	fetchMaintenanceStatus,
	maintenanceActive,
	maintenanceAnnounced,
	maintenanceMessage,
	maintenanceScheduledEnd,
	maintenanceScheduledStart,
	saveAdminMaintenance,
	startMaintenancePoll,
	stopMaintenancePoll,
} from './useMaintenance';

function response(data: unknown, ok = true, text = 'error'): Response {
	return {
		ok,
		json: vi.fn().mockResolvedValue(data),
		text: vi.fn().mockResolvedValue(text),
	} as unknown as Response;
}

describe('useMaintenance', () => {
	beforeEach(() => {
		vi.useFakeTimers();
		vi.restoreAllMocks();
		apiFetchMock.mockReset();
		maintenanceActive.value = false;
		maintenanceMessage.value = '';
		maintenanceScheduledStart.value = '';
		maintenanceScheduledEnd.value = '';
		stopMaintenancePoll();
	});

	it('applyMaintenanceData updates only provided fields', () => {
		maintenanceMessage.value = 'old';
		applyMaintenanceData({ maintenance_active: true });
		expect(maintenanceActive.value).toBe(true);
		expect(maintenanceMessage.value).toBe('old');
	});

	it('applyMaintenanceStatus sets announced computed flag correctly', () => {
		applyMaintenanceStatus({
			active: false,
			message: 'Wartung',
			scheduled_start: '2026-06-03T10:00:00Z',
			scheduled_end: '2026-06-03T12:00:00Z',
		});
		expect(maintenanceAnnounced.value).toBe(true);

		applyMaintenanceStatus({ active: true });
		expect(maintenanceAnnounced.value).toBe(false);
	});

	it('fetchMaintenanceStatus updates public maintenance state', async () => {
		vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
			response({
				active: true,
				message: 'Jetzt',
				scheduled_start: 'a',
				scheduled_end: 'b',
			}),
		);

		await fetchMaintenanceStatus();
		expect(maintenanceActive.value).toBe(true);
		expect(maintenanceMessage.value).toBe('Jetzt');
		expect(maintenanceScheduledStart.value).toBe('a');
		expect(maintenanceScheduledEnd.value).toBe('b');
	});

	it('fetchMaintenanceStatus keeps state on non-ok response', async () => {
		maintenanceMessage.value = 'keep';
		vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
			response({ active: false }, false),
		);

		await fetchMaintenanceStatus();
		expect(maintenanceMessage.value).toBe('keep');
	});

	it('fetchMaintenanceStatus swallows network errors', async () => {
		maintenanceMessage.value = 'keep';
		vi.spyOn(globalThis, 'fetch').mockRejectedValueOnce(new Error('down'));

		await expect(fetchMaintenanceStatus()).resolves.toBeUndefined();
		expect(maintenanceMessage.value).toBe('keep');
	});

	it('startMaintenancePoll triggers immediate fetch and interval polling', async () => {
		const fetchSpy = vi
			.spyOn(globalThis, 'fetch')
			.mockResolvedValue(response({ active: false, message: '' }));

		startMaintenancePoll(1000);
		await vi.advanceTimersByTimeAsync(0);
		expect(fetchSpy).toHaveBeenCalledTimes(1);

		await vi.advanceTimersByTimeAsync(1000);
		expect(fetchSpy).toHaveBeenCalledTimes(2);

		stopMaintenancePoll();
	});

	it('startMaintenancePoll replaces existing timer', async () => {
		const fetchSpy = vi
			.spyOn(globalThis, 'fetch')
			.mockResolvedValue(response({ active: false, message: '' }));

		startMaintenancePoll(1000);
		startMaintenancePoll(2000);
		// Both start calls trigger an immediate fetch; only the second timer should remain.
		expect(fetchSpy).toHaveBeenCalledTimes(2);
		await vi.advanceTimersByTimeAsync(1000);
		expect(fetchSpy).toHaveBeenCalledTimes(2);

		await vi.advanceTimersByTimeAsync(1000);
		expect(fetchSpy).toHaveBeenCalledTimes(3);
		stopMaintenancePoll();
	});

	it('fetchAdminMaintenance returns parsed config', async () => {
		apiFetchMock.mockResolvedValueOnce(
			response({
				active: false,
				message: 'm',
				enabled: true,
				scheduled_now: false,
				windows: [],
				upcoming_windows: [],
			}),
		);

		const cfg = await fetchAdminMaintenance();
		expect(cfg.enabled).toBe(true);
		expect(apiFetchMock).toHaveBeenCalledWith('/api/admin/maintenance');
	});

	it('fetchAdminMaintenance throws on non-ok response', async () => {
		apiFetchMock.mockResolvedValueOnce(response({}, false, 'forbidden'));
		await expect(fetchAdminMaintenance()).rejects.toThrow('forbidden');
	});

	it('saveAdminMaintenance updates shared public state', async () => {
		apiFetchMock.mockResolvedValueOnce(
			response({
				active: true,
				message: 'live',
				enabled: true,
				scheduled_now: true,
				scheduled_start: 's',
				scheduled_end: 'e',
				windows: [],
				upcoming_windows: [],
			}),
		);

		const result = await saveAdminMaintenance({ message: 'live' });
		expect(result.active).toBe(true);
		expect(maintenanceActive.value).toBe(true);
		expect(maintenanceMessage.value).toBe('live');
		expect(maintenanceScheduledStart.value).toBe('s');
		expect(maintenanceScheduledEnd.value).toBe('e');
	});
});
