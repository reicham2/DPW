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
	const INTERVAL = config.AUTOSAVE_INTERVAL;
	const DEBOUNCE = config.AUTOSAVE_DEBOUNCE;

	let timer: ReturnType<typeof setTimeout> | null = null;
	let interval: ReturnType<typeof setInterval> | null = null;
	let hasPendingChanges = false;
	let enabled = true;

	function scheduleAutoSave() {
		if (!enabled) return;

		if (DEBOUNCE) {
			if (timer) clearTimeout(timer);
			timer = setTimeout(saveFn, INTERVAL);
		} else {
			hasPendingChanges = true;
			if (!interval) {
				interval = setInterval(() => {
					if (hasPendingChanges) {
						hasPendingChanges = false;
						saveFn();
					}
				}, INTERVAL);
			}
		}
	}

	/** Save immediately if there are pending changes (use on "Zurück"). */
	function flushAutoSave() {
		if (timer) {
			clearTimeout(timer);
			timer = null;
			saveFn();
		}
		if (hasPendingChanges) {
			hasPendingChanges = false;
			saveFn();
		}
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
		timer = null;
		interval = null;
		hasPendingChanges = false;
	}

	onUnmounted(cleanup);

	return { scheduleAutoSave, flushAutoSave, cancelAutoSave };
}
