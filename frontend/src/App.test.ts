import { flushPromises, shallowMount } from '@vue/test-utils';
import { nextTick } from 'vue';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { User } from './types';

const mocks = vi.hoisted(() => ({
	wsRegister: vi.fn(),
	wsDisconnect: vi.fn(),
	route: { path: '/', meta: {} },
}));

vi.mock('vue-router', () => ({
	useRoute: () => mocks.route,
}));

vi.mock('./components/UserAvatar.vue', () => ({ default: { template: '<span />' } }));
vi.mock('./components/BugReportButton.vue', () => ({ default: { template: '<span />' } }));
vi.mock('./components/MaintenanceBanner.vue', () => ({ default: { template: '<span />' } }));

vi.mock('./composables/useAuth', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		user: ref<User | null>(null),
		authLoading: ref(false),
		loginError: ref(null),
		initAuth: vi.fn(),
		login: vi.fn(),
		debugLogin: vi.fn(),
		isDebug: false,
	};
});

vi.mock('./composables/useWebSocket', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		useWebSocket: () => ({ connected: ref(false) }),
		wsRegister: mocks.wsRegister,
		wsDisconnect: mocks.wsDisconnect,
	};
});

vi.mock('./composables/usePermissions', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		usePermissions: () => ({
			myPermissions: ref(null),
			fetchMyPermissions: vi.fn().mockResolvedValue(null),
			resetMyPermissions: vi.fn(),
		}),
	};
});

vi.mock('./composables/useMidataCounts', () => ({
	useMidataCounts: () => ({
		startMidataAutoRefresh: vi.fn(),
		stopMidataAutoRefresh: vi.fn(),
		resetMidataCounts: vi.fn(),
	}),
}));

vi.mock('./composables/useUserPrefs', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { statsDisplayMode: ref('auto') };
});

vi.mock('./composables/useApi', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { useApiConnectionStatus: () => ({ apiOnline: ref(true) }) };
});

vi.mock('./composables/useSetupAuthConfig', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { ensureSetupStatus: vi.fn().mockResolvedValue(false), setupLoading: ref(false) };
});

vi.mock('./composables/useMaintenance', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		maintenanceActive: ref(false),
		applyMaintenanceStatus: vi.fn(),
		fetchMaintenanceStatus: vi.fn(),
		startMaintenancePoll: vi.fn(),
		stopMaintenancePoll: vi.fn(),
	};
});

vi.mock('./config', async () => {
	const { reactive } = await vi.importActual<typeof import('vue')>('vue');
	return {
		config: reactive({
			MIDATA_ENABLED: false,
			MIDATA_WEATHER_REFRESH_INTERVAL: 900000,
			GITHUB_BUG_REPORT_ENABLED: false,
		}),
	};
});

import App from './App.vue';
import { user } from './composables/useAuth';
import { config } from './config';
import { maintenanceActive } from './composables/useMaintenance';

const testUser: User = {
	id: 'user-1',
	microsoft_oid: 'oid-1',
	email: 'user@example.com',
	display_name: 'User',
	department: null,
	role: 'admin',
	time_display_mode: 'minutes',
	proofreading_enabled: true,
	notify_material_assigned: true,
	notify_activity_assigned: true,
	notify_activity_reminder: true,
	activity_reminder_hours: 24,
	notify_program_assigned: true,
	notify_mail_own_activity: true,
	notify_mail_department: true,
	notify_channel_websocket: true,
	notify_channel_email: false,
	avatar_color: '#123456',
	created_at: '2026-01-01',
	updated_at: '2026-01-01',
};

describe('App authentication lifecycle', () => {
	let wrapper: ReturnType<typeof shallowMount> | undefined;

	beforeEach(() => {
		user.value = null;
		maintenanceActive.value = false;
		config.MIDATA_ENABLED = false;
		mocks.route.path = '/';
		vi.clearAllMocks();
	});

	it('keeps activity content unmounted during login and shows it after authentication', async () => {
		mocks.route.path = '/activities/activity-1';
		wrapper = shallowMount(App, { global: { stubs: ['router-link', 'router-view'] } });
		await flushPromises();
		expect(wrapper.find('.auth-box').exists()).toBe(true);
		expect(wrapper.find('router-view-stub').exists()).toBe(false);

		user.value = testUser;
		await nextTick();
		expect(wrapper.find('.auth-box').exists()).toBe(false);
		expect(wrapper.find('router-view-stub').exists()).toBe(true);
		expect(mocks.route.path).toBe('/activities/activity-1');
	});

	it('keeps an existing admin session in the app when maintenance starts', async () => {
		const replace = vi.spyOn(window.location, 'replace').mockImplementation(() => {});
		user.value = testUser;
		wrapper = shallowMount(App, { global: { stubs: ['router-link', 'router-view'] } });
		maintenanceActive.value = true;
		await flushPromises();
		expect(replace).not.toHaveBeenCalled();
		replace.mockRestore();
	});

	it('flushes pending changes before redirecting an existing non-admin session', async () => {
		const replace = vi.spyOn(window.location, 'replace').mockImplementation(() => {});
		user.value = { ...testUser, role: 'Mitglied' };
		let finishSave!: () => void;
		const pendingSave = new Promise<void>((resolve) => { finishSave = resolve; });
		const flush = (event: Event) => {
			(event as CustomEvent<{ waitUntil: (promise: Promise<void>) => void }>).detail.waitUntil(pendingSave);
		};
		window.addEventListener('dpw:before-maintenance-redirect', flush);
		try {
			wrapper = shallowMount(App, { global: { stubs: ['router-link', 'router-view'] } });
			maintenanceActive.value = true;
			await flushPromises();
			expect(replace).not.toHaveBeenCalled();
			finishSave();
			await flushPromises();
			expect(replace).toHaveBeenCalledExactlyOnceWith('/maintenance');
		} finally {
			window.removeEventListener('dpw:before-maintenance-redirect', flush);
			replace.mockRestore();
		}
	});

	it('cancels the redirect if maintenance ends while autosave is pending', async () => {
		const replace = vi.spyOn(window.location, 'replace').mockImplementation(() => {});
		user.value = { ...testUser, role: 'Mitglied' };
		let finishSave!: () => void;
		const pendingSave = new Promise<void>((resolve) => { finishSave = resolve; });
		const flush = (event: Event) => {
			(event as CustomEvent<{ waitUntil: (promise: Promise<void>) => void }>).detail.waitUntil(pendingSave);
		};
		window.addEventListener('dpw:before-maintenance-redirect', flush);
		try {
			wrapper = shallowMount(App, { global: { stubs: ['router-link', 'router-view'] } });
			maintenanceActive.value = true;
			await flushPromises();
			maintenanceActive.value = false;
			finishSave();
			await flushPromises();
			expect(replace).not.toHaveBeenCalled();
		} finally {
			window.removeEventListener('dpw:before-maintenance-redirect', flush);
			replace.mockRestore();
		}
	});

	afterEach(() => {
		wrapper?.unmount();
	});

	it.each([false, true])('keeps realtime connected on login with MiData enabled: %s', async (midataEnabled) => {
		config.MIDATA_ENABLED = midataEnabled;
		wrapper = shallowMount(App, { global: { stubs: ['router-link', 'router-view'] } });
		mocks.wsDisconnect.mockClear();

		user.value = testUser;
		await nextTick();
		expect(mocks.wsRegister).toHaveBeenCalledWith(testUser.display_name, testUser.microsoft_oid);
		expect(mocks.wsDisconnect).not.toHaveBeenCalled();

		config.MIDATA_ENABLED = !midataEnabled;
		await nextTick();
		expect(mocks.wsDisconnect).not.toHaveBeenCalled();

		user.value = null;
		await nextTick();
		expect(mocks.wsDisconnect).toHaveBeenCalledOnce();
	});
});
