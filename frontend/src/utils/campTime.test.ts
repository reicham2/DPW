import { describe, it, expect } from 'vitest';
import { formatMinuteOfDay, formatDuration } from './campTime';

describe('formatMinuteOfDay', () => {
	it('pads hours and minutes', () => {
		expect(formatMinuteOfDay(0)).toBe('00:00');
		expect(formatMinuteOfDay(540)).toBe('09:00');
		expect(formatMinuteOfDay(575)).toBe('09:35');
		expect(formatMinuteOfDay(1439)).toBe('23:59');
	});
});

describe('formatDuration', () => {
	it('formats minutes, hours, and combinations', () => {
		expect(formatDuration(0)).toBe('0min');
		expect(formatDuration(-5)).toBe('0min');
		expect(formatDuration(45)).toBe('45min');
		expect(formatDuration(60)).toBe('1h');
		expect(formatDuration(90)).toBe('1h 30min');
		expect(formatDuration(120)).toBe('2h');
		expect(formatDuration(185)).toBe('3h 5min');
	});
});
