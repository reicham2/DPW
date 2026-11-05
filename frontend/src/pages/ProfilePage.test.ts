import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import type { User } from '../types';

const { userRef, apiFetchMock } = vi.hoisted(() => ({
	userRef: { value: null as Partial<User> | null },
	apiFetchMock: vi.fn(),
}));

vi.mock('../composables/useAuth', () => ({ user: userRef }));
vi.mock('../composables/useApi', () => ({ apiFetch: apiFetchMock }));
vi.mock('../composables/usePermissions', () => ({
	usePermissions: () => ({
		myPermissions: { value: null },
		departments: { value: [] },
		fetchMyPermissions: vi.fn().mockResolvedValue(undefined),
		fetchDepartments: vi.fn().mockResolvedValue(undefined),
	}),
}));
vi.mock('../composables/useNotifications', () => ({
	requestBrowserNotificationPermission: vi.fn().mockResolvedValue('denied'),
	syncPushSubscription: vi.fn().mockResolvedValue(undefined),
}));
vi.mock('../composables/useUserPrefs', () => ({
	statsDisplayMode: { value: 'auto' },
	themeMode: { value: 'light' },
}));

import ProfilePage from './ProfilePage.vue';

describe('activity reminder profile settings', () => {
	beforeEach(() => {
		vi.useFakeTimers();
		userRef.value = { id: 'user', display_name: 'Test', department: 'Pfadi' };
		apiFetchMock.mockReset();
		apiFetchMock.mockImplementation(async (_path, options) => ({
			ok: true,
			json: async () => ({ ...userRef.value, ...JSON.parse(options.body) }),
		}));
	});
	afterEach(() => vi.useRealTimers());

	it('defaults missing settings to 24h and offers only 2, 24 and 48 hours', async () => {
		const wrapper = mount(ProfilePage);
		await flushPromises();
		expect(wrapper.find('#activity-reminder-hours').element).toHaveProperty('value', '24');
		expect(wrapper.findAll('#activity-reminder-hours option').map(option => option.attributes('value')))
			.toEqual(['2', '24', '48']);
		expect(apiFetchMock).not.toHaveBeenCalled();
		wrapper.unmount();
	});

	it.each([2, 24, 48] as const)('loads and persists the %ih choice through own profile autosave', async (hours) => {
		userRef.value = { ...userRef.value, notify_activity_reminder: true, activity_reminder_hours: hours };
		const wrapper = mount(ProfilePage);
		await flushPromises();
		const select = wrapper.find('#activity-reminder-hours');
		expect(select.element).toHaveProperty('value', String(hours));
		await select.setValue(hours === 2 ? '48' : '2');
		await vi.advanceTimersByTimeAsync(500);
		expect(apiFetchMock).toHaveBeenCalledWith('/api/me', expect.objectContaining({
			method: 'PATCH',
			body: expect.any(String),
		}));
		const body = JSON.parse(apiFetchMock.mock.calls[0][1].body);
		expect(body.activity_reminder_hours).toBe(hours === 2 ? 48 : 2);
		expect(body.notify_activity_reminder).toBe(true);
		expect(userRef.value?.activity_reminder_hours).toBe(body.activity_reminder_hours);
		wrapper.unmount();
	});

	it('disables lead selection when reminders are opted out', async () => {
		userRef.value = { ...userRef.value, notify_activity_reminder: false, activity_reminder_hours: 48 };
		const wrapper = mount(ProfilePage);
		await flushPromises();
		expect(wrapper.find('#activity-reminder-hours').attributes()).toHaveProperty('disabled');
		expect(wrapper.find('#activity-reminder-hours').element).toHaveProperty('value', '48');
		wrapper.unmount();
	});
});
