import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

const { mockedConfig } = vi.hoisted(() => ({
	mockedConfig: { AUTOSAVE_INTERVAL: 100, AUTOSAVE_DEBOUNCE: true },
}));

// Mock vue's onUnmounted so the composable works outside a component
vi.mock('vue', async () => {
	const actual = await vi.importActual<typeof import('vue')>('vue');
	return { ...actual, onUnmounted: vi.fn() };
});

// Mock config to control test values
vi.mock('../config', () => ({
	config: mockedConfig,
}));

import { useAutosave } from './useAutosave';

describe('useAutosave (debounce mode)', () => {
	beforeEach(() => {
		mockedConfig.AUTOSAVE_INTERVAL = 100;
		mockedConfig.AUTOSAVE_DEBOUNCE = true;
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

	it('flushAutoSave returns pending save when already running', async () => {
		let resolveSave: (() => void) | null = null;
		const save = vi.fn(
			() =>
				new Promise<void>((resolve) => {
					resolveSave = resolve;
				}),
		);
		const { scheduleAutoSave, flushAutoSave } = useAutosave(save);

		scheduleAutoSave();
		vi.advanceTimersByTime(100);
		expect(save).toHaveBeenCalledTimes(1);

		const flushed = flushAutoSave();
		let finished = false;
		void flushed.then(() => {
			finished = true;
		});

		await vi.advanceTimersByTimeAsync(0);
		expect(finished).toBe(false);

		resolveSave?.();
		await flushed;
		expect(finished).toBe(true);
	});

	it('swallows synchronous save exceptions', () => {
		const save = vi.fn(() => {
			throw new Error('save failed');
		});
		const { scheduleAutoSave } = useAutosave(save);

		expect(() => {
			scheduleAutoSave();
			vi.advanceTimersByTime(100);
		}).not.toThrow();
		expect(save).toHaveBeenCalledTimes(1);
	});

	it('flushAutoSave via maintenance event calls waitUntil and saves immediately', async () => {
		const save = vi.fn();
		const { scheduleAutoSave } = useAutosave(save);
		scheduleAutoSave();

		const waitUntil = vi.fn();
		window.dispatchEvent(
			new CustomEvent('dpw:before-maintenance-redirect', {
				detail: { waitUntil },
			}),
		);

		expect(waitUntil.mock.calls.length).toBeGreaterThan(0);
		await Promise.all(waitUntil.mock.calls.map(([p]) => p as Promise<unknown>));
		expect(save).toHaveBeenCalledTimes(1);
	});
});

describe('useAutosave (interval mode)', () => {
	beforeEach(() => {
		mockedConfig.AUTOSAVE_INTERVAL = 100;
		mockedConfig.AUTOSAVE_DEBOUNCE = false;
		vi.useFakeTimers();
	});

	afterEach(() => {
		vi.useRealTimers();
	});

	it('does not save before first interval tick', () => {
		const save = vi.fn();
		const { scheduleAutoSave } = useAutosave(save);

		scheduleAutoSave();
		vi.advanceTimersByTime(99);
		expect(save).not.toHaveBeenCalled();
	});

	it('coalesces multiple schedules into one save per tick', () => {
		const save = vi.fn();
		const { scheduleAutoSave } = useAutosave(save);

		scheduleAutoSave();
		scheduleAutoSave();
		scheduleAutoSave();
		vi.advanceTimersByTime(100);
		expect(save).toHaveBeenCalledTimes(1);
	});

	it('flushAutoSave triggers pending interval save immediately', async () => {
		const save = vi.fn();
		const { scheduleAutoSave, flushAutoSave } = useAutosave(save);

		scheduleAutoSave();
		await flushAutoSave();
		expect(save).toHaveBeenCalledTimes(1);

		vi.advanceTimersByTime(200);
		expect(save).toHaveBeenCalledTimes(1);
	});

	it('cancelAutoSave prevents interval saves', () => {
		const save = vi.fn();
		const { scheduleAutoSave, cancelAutoSave } = useAutosave(save);

		scheduleAutoSave();
		cancelAutoSave();
		vi.advanceTimersByTime(300);
		expect(save).not.toHaveBeenCalled();
	});
});
