import { onUnmounted } from 'vue';
import { config } from '../config';

/**
 * Reusable autosave composable.
 *
 * Returns `scheduleAutoSave()` — call it whenever data changes.
 * Also returns `flushAutoSave()` — call it on "Zurück" to save immediately.
 * Call `cancelAutoSave()` on "Abbrechen" to discard pending saves.
 */
export function useAutosave(saveFn: () => void | Promise<void>) {
	let timer: ReturnType<typeof setTimeout> | null = null;
	let interval: ReturnType<typeof setInterval> | null = null;
	let hasPendingChanges = false;
	let enabled = true;
	let pendingSave: Promise<void> | null = null;

	const intervalMs = () => config.AUTOSAVE_INTERVAL;
	const debounceEnabled = () => config.AUTOSAVE_DEBOUNCE;

	function triggerSave(): Promise<void> {
		let run: Promise<void>;
		try {
			run = Promise.resolve(saveFn())
				.then(() => undefined)
				.catch(() => undefined);
		} catch {
			run = Promise.resolve();
		}

		pendingSave = run.finally(() => {
			if (pendingSave === run) pendingSave = null;
		});

		return pendingSave;
	}

	function scheduleAutoSave() {
		if (!enabled) return;

		if (debounceEnabled()) {
			if (timer) clearTimeout(timer);
			timer = setTimeout(() => {
				void triggerSave();
			}, intervalMs());
		} else {
			hasPendingChanges = true;
			if (!interval) {
				interval = setInterval(() => {
					if (hasPendingChanges) {
						hasPendingChanges = false;
						void triggerSave();
					}
				}, intervalMs());
			}
		}
	}

	/** Save immediately if there are pending changes (use on "Zurück"). */
	function flushAutoSave(): Promise<void> {
		const flushTasks: Promise<void>[] = [];

		if (timer) {
			clearTimeout(timer);
			timer = null;
			flushTasks.push(triggerSave());
		}
		if (hasPendingChanges) {
			hasPendingChanges = false;
			flushTasks.push(triggerSave());
		}

		if (flushTasks.length === 0) {
			return pendingSave ?? Promise.resolve();
		}

		return Promise.allSettled(flushTasks).then(() => undefined);
	}

	function onBeforeMaintenanceRedirect(event: Event) {
		const detail = (event as CustomEvent<{ waitUntil?: (promise: Promise<unknown>) => void }>).detail;
		if (!detail?.waitUntil) return;
		detail.waitUntil(flushAutoSave());
	}

	/** Discard pending saves without executing them (use on "Abbrechen"). */
	function cancelAutoSave() {
		enabled = false;
		if (timer) {
			clearTimeout(timer);
			timer = null;
		}
		if (interval) {
			clearInterval(interval);
			interval = null;
		}
		hasPendingChanges = false;
	}

	function cleanup() {
		if (timer) clearTimeout(timer);
		if (interval) clearInterval(interval);
		if (typeof window !== 'undefined') {
			window.removeEventListener('dpw:before-maintenance-redirect', onBeforeMaintenanceRedirect);
		}
		timer = null;
		interval = null;
		hasPendingChanges = false;
	}

	if (typeof window !== 'undefined') {
		window.addEventListener('dpw:before-maintenance-redirect', onBeforeMaintenanceRedirect);
	}

	onUnmounted(cleanup);

	return { scheduleAutoSave, flushAutoSave, cancelAutoSave };
}
