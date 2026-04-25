import { ref } from 'vue';
import { setRuntimeAuthConfig } from './useAuth';
import { applyRuntimeConfig } from '../config';

export const setupLoading = ref(false);
export const setupSubmitting = ref(false);
export const setupRequired = ref<boolean | null>(null); // null = not determined yet
export const setupError = ref<string | null>(null);
export const setupFieldConfigured = ref({
	tenant_id: false,
	client_id: false,
	client_secret: false,
	contact_email: false,
});
export const setupForm = ref({
	tenant_id: '',
	client_id: '',
	client_secret: '',
	contact_email: '',
});

export async function ensureSetupStatus(force = false): Promise<boolean> {
	// If already determined and not forced, return cached result
	if (setupRequired.value !== null && !force) return setupRequired.value;

	setupLoading.value = true;
	setupError.value = null;
	try {
		const res = await fetch('/api/setup/auth-config');
		if (!res.ok) throw new Error(await res.text());
		const data = (await res.json()) as {
			configured?: boolean;
			tenant_id?: string;
			tenant_id_configured?: boolean;
			client_id?: string;
			client_id_configured?: boolean;
			client_secret_configured?: boolean;
			contact_email?: string;
			contact_email_configured?: boolean;
			autosave_interval_ms?: number;
			autosave_debounce?: boolean;
			midata_weather_refresh_interval_ms?: number;
			midata_configured?: boolean;
			wp_configured?: boolean;
			github_bug_report_configured?: boolean;
			wp_url?: string;
			public_base_url?: string;
		};

		applyRuntimeConfig({
			autosaveIntervalMs: data.autosave_interval_ms,
			autosaveDebounce: data.autosave_debounce,
			midataWeatherRefreshIntervalMs: data.midata_weather_refresh_interval_ms,
			midataEnabled: data.midata_configured,
			wpPublishingEnabled: data.wp_configured,
			githubBugReportEnabled: data.github_bug_report_configured,
			wpUrl: data.wp_url,
			publicBaseUrl: data.public_base_url,
		});

		setupRequired.value = !data.configured;
		setupFieldConfigured.value = {
			tenant_id: !!data.tenant_id_configured,
			client_id: !!data.client_id_configured,
			client_secret: !!data.client_secret_configured,
			contact_email: !!data.contact_email_configured,
		};
		if (data.tenant_id) setupForm.value.tenant_id = data.tenant_id;
		if (data.client_id) setupForm.value.client_id = data.client_id;
		if (data.contact_email) setupForm.value.contact_email = data.contact_email;
		if (data.configured) {
			setRuntimeAuthConfig(data.tenant_id || '', data.client_id || '');
		}
		return setupRequired.value;
	} catch (error) {
		setupError.value = error instanceof Error ? error.message : String(error);
		// On error, assume setup is required (safer default)
		setupRequired.value = true;
		setupFieldConfigured.value = {
			tenant_id: false,
			client_id: false,
			client_secret: false,
			contact_email: false,
		};
		return true;
	} finally {
		setupLoading.value = false;
	}
}

export async function submitSetupConfig(): Promise<void> {
	setupSubmitting.value = true;
	setupError.value = null;
	try {
		const res = await fetch('/api/setup/auth-config', {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify(setupForm.value),
		});
		if (!res.ok) throw new Error(await res.text());
		setRuntimeAuthConfig(setupForm.value.tenant_id, setupForm.value.client_id);
		setupRequired.value = false;
	} catch (error) {
		setupError.value = error instanceof Error ? error.message : String(error);
		throw error;
	} finally {
		setupSubmitting.value = false;
	}
}
