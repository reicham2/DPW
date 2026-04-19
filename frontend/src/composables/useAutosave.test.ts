import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

// Mock vue's onUnmounted so the composable works outside a component
vi.mock('vue', async () => {
	const actual = await vi.importActual<typeof import('vue')>('vue');
	return { ...actual, onUnmounted: vi.fn() };
});

// Mock config to control test values
vi.mock('../config', () => ({
	config: { AUTOSAVE_INTERVAL: 100, AUTOSAVE_DEBOUNCE: true },
}));

import { useAutosave } from './useAutosave';

describe('useAutosave (debounce mode)', () => {
	beforeEach(() => {
		vi.useFakeTimers();
	});
	afterEach(() => {
		vi.useRealTimers();
	});

	it('calls saveFn after the debounce interval', () => {
		const save = vi.fn();
		const { scheduleAutoSave } = useAutosave(save);

		scheduleAutoSave();
		expect(save).not.toHaveBeenCalled();

		vi.advanceTimersByTime(100);
		expect(save).toHaveBeenCalledTimes(1);
	});

	it('resets the timer on consecutive calls (debounce)', () => {
		const save = vi.fn();
		const { scheduleAutoSave } = useAutosave(save);

		scheduleAutoSave();
		vi.advanceTimersByTime(50);
		scheduleAutoSave(); // reset

		vi.advanceTimersByTime(50);
		expect(save).not.toHaveBeenCalled();

		vi.advanceTimersByTime(50);
		expect(save).toHaveBeenCalledTimes(1);
	});

	it('flushAutoSave fires immediately and cancels pending timer', () => {
		const save = vi.fn();
		const { scheduleAutoSave, flushAutoSave } = useAutosave(save);

		scheduleAutoSave();
		flushAutoSave();
		expect(save).toHaveBeenCalledTimes(1);

		// Original timer should not fire again
		vi.advanceTimersByTime(200);
		expect(save).toHaveBeenCalledTimes(1);
	});

	it('cancelAutoSave discards pending save', () => {
		const save = vi.fn();
		const { scheduleAutoSave, cancelAutoSave } = useAutosave(save);

		scheduleAutoSave();
		cancelAutoSave();
		vi.advanceTimersByTime(200);
		expect(save).not.toHaveBeenCalled();
	});

	it('cancelAutoSave prevents future schedules', () => {
		const save = vi.fn();
		const { scheduleAutoSave, cancelAutoSave } = useAutosave(save);

		cancelAutoSave();
		scheduleAutoSave();
		vi.advanceTimersByTime(200);
		expect(save).not.toHaveBeenCalled();
	});
});
