import { beforeEach, describe, expect, it, vi } from 'vitest';

const {
	setRuntimeAuthConfigMock,
	applyRuntimeConfigMock,
	applyMaintenanceDataMock,
} = vi.hoisted(() => ({
	setRuntimeAuthConfigMock: vi.fn(),
	applyRuntimeConfigMock: vi.fn(),
	applyMaintenanceDataMock: vi.fn(),
}));

vi.mock('./useAuth', () => ({
	setRuntimeAuthConfig: setRuntimeAuthConfigMock,
}));

vi.mock('../config', () => ({
	applyRuntimeConfig: applyRuntimeConfigMock,
}));

vi.mock('./useMaintenance', () => ({
	applyMaintenanceData: applyMaintenanceDataMock,
}));

import {
	ensureSetupStatus,
	setupError,
	setupFieldConfigured,
	setupForm,
	setupLoading,
	setupRequired,
	setupSubmitting,
	submitSetupConfig,
} from './useSetupAuthConfig';

function jsonResponse(data: unknown, ok = true): Response {
	return {
		ok,
		json: vi.fn().mockResolvedValue(data),
		text: vi.fn().mockResolvedValue('request failed'),
	} as unknown as Response;
}

describe('useSetupAuthConfig', () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		setRuntimeAuthConfigMock.mockReset();
		applyRuntimeConfigMock.mockReset();
		applyMaintenanceDataMock.mockReset();

		setupLoading.value = false;
		setupSubmitting.value = false;
		setupRequired.value = null;
		setupError.value = null;
		setupFieldConfigured.value = {
			tenant_id: false,
			client_id: false,
			client_secret: false,
			contact_email: false,
		};
		setupForm.value = {
			tenant_id: '',
			client_id: '',
			client_secret: '',
			contact_email: '',
		};
	});

	it('returns cached setupRequired when already determined', async () => {
		setupRequired.value = false;
		const fetchSpy = vi.spyOn(globalThis, 'fetch');

		const result = await ensureSetupStatus();

		expect(result).toBe(false);
		expect(fetchSpy).not.toHaveBeenCalled();
	});

	it('loads setup status, applies runtime config and marks setup required', async () => {
		vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
			jsonResponse({
				configured: false,
				tenant_id: 'tenant-1',
				client_id: 'client-1',
				contact_email: 'admin@example.com',
				tenant_id_configured: true,
				client_id_configured: true,
				client_secret_configured: false,
				contact_email_configured: true,
				autosave_interval_ms: 1200,
				autosave_debounce: true,
				maintenance_active: true,
				maintenance_message: 'Wartung',
			}),
		);

		const result = await ensureSetupStatus(true);

		expect(result).toBe(true);
		expect(setupRequired.value).toBe(true);
		expect(setupForm.value.tenant_id).toBe('tenant-1');
		expect(setupForm.value.client_id).toBe('client-1');
		expect(setupForm.value.contact_email).toBe('admin@example.com');
		expect(setupFieldConfigured.value).toEqual({
			tenant_id: true,
			client_id: true,
			client_secret: false,
			contact_email: true,
		});
		expect(applyRuntimeConfigMock).toHaveBeenCalledTimes(1);
		expect(applyMaintenanceDataMock).toHaveBeenCalledTimes(1);
		expect(setRuntimeAuthConfigMock).not.toHaveBeenCalled();
		expect(setupLoading.value).toBe(false);
	});

	it('sets runtime auth config when setup is already configured', async () => {
		vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
			jsonResponse({
				configured: true,
				tenant_id: 'tenant-x',
				client_id: 'client-x',
			}),
		);

		const result = await ensureSetupStatus(true);

		expect(result).toBe(false);
		expect(setupRequired.value).toBe(false);
		expect(setRuntimeAuthConfigMock).toHaveBeenCalledWith(
			'tenant-x',
			'client-x',
		);
	});

	it('uses safe defaults when loading setup status fails', async () => {
		vi.spyOn(globalThis, 'fetch').mockRejectedValueOnce(
			new Error('network down'),
		);

		const result = await ensureSetupStatus(true);

		expect(result).toBe(true);
		expect(setupRequired.value).toBe(true);
		expect(setupError.value).toBe('network down');
		expect(setupFieldConfigured.value).toEqual({
			tenant_id: false,
			client_id: false,
			client_secret: false,
			contact_email: false,
		});
		expect(setupLoading.value).toBe(false);
	});

	it('submitSetupConfig posts setup form and finalizes setup state', async () => {
		setupForm.value = {
			tenant_id: 'tenant-a',
			client_id: 'client-a',
			client_secret: 'secret-a',
			contact_email: 'admin@example.com',
		};

		const fetchSpy = vi
			.spyOn(globalThis, 'fetch')
			.mockResolvedValueOnce(jsonResponse({ ok: true }));

		await submitSetupConfig();

		expect(fetchSpy).toHaveBeenCalledWith('/api/setup/auth-config', {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify(setupForm.value),
		});
		expect(setRuntimeAuthConfigMock).toHaveBeenCalledWith(
			'tenant-a',
			'client-a',
		);
		expect(setupRequired.value).toBe(false);
		expect(setupSubmitting.value).toBe(false);
	});

	it('submitSetupConfig surfaces errors and resets submitting state', async () => {
		vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce({
			ok: false,
			text: vi.fn().mockResolvedValue('invalid config'),
		} as unknown as Response);

		await expect(submitSetupConfig()).rejects.toThrow('invalid config');
		expect(setupError.value).toBe('invalid config');
		expect(setupSubmitting.value).toBe(false);
	});
});
