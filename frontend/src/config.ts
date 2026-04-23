/**
 * Runtime configuration – values are injected by the container entrypoint
 * via /config.js (see docker-entrypoint.sh).  During local `vite dev` the
 * fallback reads from import.meta.env so the .env workflow still works.
 */

interface AppConfig {
	MSAL_CLIENT_ID: string;
	MSAL_TENANT_ID: string;
	AUTOSAVE_INTERVAL: number;
	AUTOSAVE_DEBOUNCE: boolean;
	MIDATA_WEATHER_REFRESH_INTERVAL: number;
	WP_URL: string;
}

const w = window as unknown as { __APP_CONFIG__?: Record<string, string> };

export const config: AppConfig = {
	MSAL_CLIENT_ID:
		w.__APP_CONFIG__?.MSAL_CLIENT_ID ??
		import.meta.env.VITE_MSAL_CLIENT_ID ??
		'',
	MSAL_TENANT_ID:
		w.__APP_CONFIG__?.MSAL_TENANT_ID ??
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
	WP_URL:
		w.__APP_CONFIG__?.WP_URL ?? '',
};
