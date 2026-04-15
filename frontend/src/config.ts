/**
 * Runtime configuration – values are injected by the container entrypoint
 * via /config.js (see docker-entrypoint.sh).  During local `vite dev` the
 * fallback reads from import.meta.env so the .env workflow still works.
 */

interface AppConfig {
	MSAL_CLIENT_ID: string;
	MSAL_TENANT_ID: string;
	DEBUG: boolean;
	AUTOSAVE_INTERVAL: number;
	AUTOSAVE_DEBOUNCE: boolean;
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
	DEBUG: (w.__APP_CONFIG__?.DEBUG ?? import.meta.env.VITE_DEBUG) === 'true',
	AUTOSAVE_INTERVAL:
		Number(
			w.__APP_CONFIG__?.AUTOSAVE_INTERVAL ??
				import.meta.env.VITE_AUTOSAVE_INTERVAL,
		) || 1500,
	AUTOSAVE_DEBOUNCE:
		(w.__APP_CONFIG__?.AUTOSAVE_DEBOUNCE ??
			import.meta.env.VITE_AUTOSAVE_DEBOUNCE ??
			'true') !== 'false',
};
