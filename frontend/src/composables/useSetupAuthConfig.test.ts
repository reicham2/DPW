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
	activityGuidingThreadSupported,
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
		activityGuidingThreadSupported.value = false;
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

	it('enables the guiding thread only when the backend explicitly supports it', async () => {
		const configured = { configured: true, tenant_id: 'tenant', client_id: 'client' };
		vi.spyOn(globalThis, 'fetch')
			.mockResolvedValueOnce(jsonResponse({ ...configured, activity_guiding_thread_supported: true }))
			.mockResolvedValueOnce(jsonResponse(configured))
			.mockResolvedValueOnce(jsonResponse({ ...configured, activity_guiding_thread_supported: true }))
			.mockResolvedValueOnce(jsonResponse({ ...configured, activity_guiding_thread_supported: 'yes' }));

		expect(await ensureSetupStatus(true)).toBe(false);
		expect(activityGuidingThreadSupported.value).toBe(true);
		expect(await ensureSetupStatus(true)).toBe(false);
		expect(activityGuidingThreadSupported.value).toBe(false);
		expect(await ensureSetupStatus(true)).toBe(false);
		expect(activityGuidingThreadSupported.value).toBe(true);
		expect(await ensureSetupStatus(true)).toBe(true);
		expect(activityGuidingThreadSupported.value).toBe(false);
		expect(setupError.value).toBe('Ungültige Setup-Antwort des Servers.');
	});

	it('fails closed without caching a failed setup status and can recover', async () => {
		const fetchSpy = vi.spyOn(globalThis, 'fetch')
			.mockRejectedValueOnce(new Error('network down'))
			.mockResolvedValueOnce(jsonResponse({
				configured: true,
				tenant_id: 'tenant',
				client_id: 'client',
			}));

		const result = await ensureSetupStatus(true);

		expect(result).toBe(true);
		expect(setupRequired.value).toBeNull();
		expect(setupError.value).toBe('network down');
		expect(setupFieldConfigured.value).toEqual({
			tenant_id: false,
			client_id: false,
			client_secret: false,
			contact_email: false,
		});
		expect(setupLoading.value).toBe(false);
		expect(await ensureSetupStatus()).toBe(false);
		expect(fetchSpy).toHaveBeenCalledTimes(2);
		expect(setupError.value).toBeNull();
	});

	it('shares a pending status request between concurrent callers', async () => {
		let resolveFetch!: (response: Response) => void;
		const fetchSpy = vi.spyOn(globalThis, 'fetch').mockReturnValueOnce(
			new Promise<Response>((resolve) => { resolveFetch = resolve; }),
		);

		const first = ensureSetupStatus();
		const second = ensureSetupStatus(true);
		expect(setupLoading.value).toBe(true);
		expect(fetchSpy).toHaveBeenCalledTimes(1);

		resolveFetch(jsonResponse({ configured: false }));
		expect(await Promise.all([first, second])).toEqual([true, true]);
		expect(setupLoading.value).toBe(false);
	});

	it('rejects malformed or incomplete configured status responses', async () => {
		vi.spyOn(globalThis, 'fetch')
			.mockResolvedValueOnce(jsonResponse({}))
			.mockResolvedValueOnce(jsonResponse({ configured: true, client_id: 'client' }))
			.mockResolvedValueOnce(jsonResponse({ configured: false, tenant_id: 123 }));

		expect(await ensureSetupStatus()).toBe(true);
		expect(setupRequired.value).toBeNull();
		expect(setupError.value).toBe('Ungültige Setup-Antwort des Servers.');
		expect(await ensureSetupStatus()).toBe(true);
		expect(await ensureSetupStatus()).toBe(true);
		expect(setupRequired.value).toBeNull();
		expect(setRuntimeAuthConfigMock).not.toHaveBeenCalled();
	});

	it('shows an explicit error for a setup failure with an empty response body', async () => {
		vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
			new Response('', { status: 502 }),
		);

		expect(await ensureSetupStatus()).toBe(true);
		expect(setupRequired.value).toBeNull();
		expect(setupError.value).toBe('Setup-Status konnte nicht geladen werden (HTTP 502).');
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
