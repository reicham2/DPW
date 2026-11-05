import { flushPromises, mount } from '@vue/test-utils';
import { beforeEach, describe, expect, it, vi } from 'vitest';

const mocks = vi.hoisted(() => ({
	replace: vi.fn(),
	login: vi.fn(),
	initAuth: vi.fn(),
	ensureSetupStatus: vi.fn(),
	submitSetupConfig: vi.fn(),
}));

vi.mock('vue-router', () => ({ useRouter: () => ({ replace: mocks.replace }) }));
vi.mock('../composables/useAuth', () => ({
	login: mocks.login,
	initAuth: mocks.initAuth,
}));
vi.mock('../composables/useSetupAuthConfig', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		ensureSetupStatus: mocks.ensureSetupStatus,
		submitSetupConfig: mocks.submitSetupConfig,
		setupError: ref<string | null>(null),
		setupFieldConfigured: ref({
			tenant_id: true,
			client_id: true,
			client_secret: true,
			contact_email: true,
		}),
		setupForm: ref({
			tenant_id: 'tenant',
			client_id: 'client',
			client_secret: '',
			contact_email: 'user@example.com',
		}),
		setupLoading: ref(false),
		setupRequired: ref<boolean | null>(null),
		setupSubmitting: ref(false),
	};
});

import SetupPage from './SetupPage.vue';
import { setupError, setupLoading, setupRequired } from '../composables/useSetupAuthConfig';

describe('SetupPage', () => {
	beforeEach(() => {
		vi.clearAllMocks();
		setupError.value = null;
		setupLoading.value = false;
		setupRequired.value = null;
		mocks.ensureSetupStatus.mockReset().mockResolvedValue(true);
		mocks.submitSetupConfig.mockReset().mockResolvedValue(undefined);
		mocks.login.mockReset().mockResolvedValue(undefined);
		mocks.initAuth.mockReset().mockResolvedValue(undefined);
		mocks.replace.mockReset().mockResolvedValue(undefined);
	});

	it('offers a retry when setup status is unavailable and restores auth after recovery', async () => {
		setupError.value = 'Server nicht erreichbar';
		const wrapper = mount(SetupPage);
		await flushPromises();
		expect(wrapper.text()).toContain('Server nicht erreichbar');
		expect(wrapper.find('form').exists()).toBe(false);

		mocks.ensureSetupStatus.mockImplementationOnce(async () => {
			setupRequired.value = false;
			return false;
		});
		await wrapper.get('button').trigger('click');
		await flushPromises();
		expect(mocks.initAuth).toHaveBeenCalledOnce();
		expect(mocks.replace).toHaveBeenCalledWith('/');
	});

	it('returns to the login screen if setup was saved but the popup fails', async () => {
		setupRequired.value = true;
		mocks.login.mockRejectedValueOnce(new Error('popup blocked'));
		const wrapper = mount(SetupPage);
		await flushPromises();

		await wrapper.get('form').trigger('submit');
		await flushPromises();
		expect(mocks.submitSetupConfig).toHaveBeenCalledOnce();
		expect(mocks.login).toHaveBeenCalledOnce();
		expect(mocks.replace).toHaveBeenCalledWith('/');
	});

	it('keeps the setup form available when saving fails', async () => {
		setupRequired.value = true;
		mocks.submitSetupConfig.mockImplementationOnce(async () => {
			setupError.value = 'Konfiguration konnte nicht gespeichert werden';
			throw new Error('save failed');
		});
		const wrapper = mount(SetupPage);
		await flushPromises();

		await wrapper.get('form').trigger('submit');
		await flushPromises();
		expect(wrapper.text()).toContain('Konfiguration konnte nicht gespeichert werden');
		expect(mocks.login).not.toHaveBeenCalled();
		expect(mocks.replace).not.toHaveBeenCalled();
	});
});
