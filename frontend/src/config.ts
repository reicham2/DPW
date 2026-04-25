/**
 * Runtime configuration – values are injected by the container entrypoint
 * via /config.js (see docker-entrypoint.sh).  During local `vite dev` the
 * fallback reads from import.meta.env so the .env workflow still works.
 */

import { reactive } from 'vue';

interface AppConfig {
	AZURE_CLIENT_ID: string;
	AZURE_TENANT_ID: string;
	AUTOSAVE_INTERVAL: number;
	AUTOSAVE_DEBOUNCE: boolean;
	MIDATA_WEATHER_REFRESH_INTERVAL: number;
	MIDATA_ENABLED: boolean;
	WP_PUBLISHING_ENABLED: boolean;
	GITHUB_BUG_REPORT_ENABLED: boolean;
	WP_URL: string;
	PUBLIC_BASE_URL: string;
}

interface RuntimeConfigUpdate {
	autosaveIntervalMs?: number;
	autosaveDebounce?: boolean;
	midataWeatherRefreshIntervalMs?: number;
	midataEnabled?: boolean;
	wpPublishingEnabled?: boolean;
	githubBugReportEnabled?: boolean;
	wpUrl?: string;
	publicBaseUrl?: string;
}

const w = window as unknown as { __APP_CONFIG__?: Record<string, string> };

export const config = reactive<AppConfig>({
	AZURE_CLIENT_ID:
		w.__APP_CONFIG__?.AZURE_CLIENT_ID ??
		import.meta.env.VITE_AZURE_CLIENT_ID ??
		import.meta.env.VITE_MSAL_CLIENT_ID ??
		'',
	AZURE_TENANT_ID:
		w.__APP_CONFIG__?.AZURE_TENANT_ID ??
		import.meta.env.VITE_AZURE_TENANT_ID ??
		import.meta.env.VITE_MSAL_TENANT_ID ??
		'',
	AUTOSAVE_INTERVAL:
		Number(
			w.__APP_CONFIG__?.AUTOSAVE_INTERVAL ??
				import.meta.env.VITE_AUTOSAVE_INTERVAL,
		) || 1500,
	AUTOSAVE_DEBOUNCE:
		(w.__APP_CONFIG__?.AUTOSAVE_DEBOUNCE ??
			import.meta.env.VITE_AUTOSAVE_DEBOUNCE ??
			'true') !== 'false',
	MIDATA_WEATHER_REFRESH_INTERVAL:
		Number(
			w.__APP_CONFIG__?.MIDATA_WEATHER_REFRESH_INTERVAL ??
				import.meta.env.VITE_MIDATA_WEATHER_REFRESH_INTERVAL,
		) || 900000,
	MIDATA_ENABLED: false,
	WP_PUBLISHING_ENABLED: false,
	GITHUB_BUG_REPORT_ENABLED: false,
	WP_URL: w.__APP_CONFIG__?.WP_URL ?? '',
	PUBLIC_BASE_URL: w.__APP_CONFIG__?.DPW_PUBLIC_URL ?? '',
});

export function applyRuntimeConfig(update: RuntimeConfigUpdate): void {
	if (
		typeof update.autosaveIntervalMs === 'number' &&
		Number.isFinite(update.autosaveIntervalMs)
	) {
		config.AUTOSAVE_INTERVAL = Math.max(
			250,
			Math.floor(update.autosaveIntervalMs),
		);
	}
	if (typeof update.autosaveDebounce === 'boolean') {
		config.AUTOSAVE_DEBOUNCE = update.autosaveDebounce;
	}
	if (
		typeof update.midataWeatherRefreshIntervalMs === 'number' &&
		Number.isFinite(update.midataWeatherRefreshIntervalMs)
	) {
		config.MIDATA_WEATHER_REFRESH_INTERVAL = Math.max(
			10000,
			Math.floor(update.midataWeatherRefreshIntervalMs),
		);
	}
	if (typeof update.midataEnabled === 'boolean') {
		config.MIDATA_ENABLED = update.midataEnabled;
	}
	if (typeof update.wpPublishingEnabled === 'boolean') {
		config.WP_PUBLISHING_ENABLED = update.wpPublishingEnabled;
	}
	if (typeof update.githubBugReportEnabled === 'boolean') {
		config.GITHUB_BUG_REPORT_ENABLED = update.githubBugReportEnabled;
	}
	if (typeof update.wpUrl === 'string') {
		config.WP_URL = update.wpUrl;
	}
	if (typeof update.publicBaseUrl === 'string') {
		const normalized = update.publicBaseUrl.trim().replace(/\/+$/, '');
		config.PUBLIC_BASE_URL = normalized;
	}
}
