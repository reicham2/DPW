import { describe, expect, it } from 'vitest';
import { replaceDateOffsets, shiftIsoDate } from './templateDateVars';

describe('activity date offsets', () => {
	it.each([
		['2026-07-15', -7, '2026-07-08'],
		['2026-07-15', 7, '2026-07-22'],
		['2026-07-15', 0, '2026-07-15'],
		['2026-07-01', -1, '2026-06-30'],
		['2026-01-01', -1, '2025-12-31'],
		['2024-03-01', -1, '2024-02-29'],
		['2026-03-01', -1, '2026-02-28'],
		['2026-03-30', -2, '2026-03-28'],
		['2026-10-26', -2, '2026-10-24'],
		['0001-01-01', 0, '0001-01-01'],
		['9999-12-31', 0, '9999-12-31'],
	] as const)('shifts %s by %i calendar days', (date, days, expected) => {
		expect(shiftIsoDate(date, days)).toBe(expected);
	});

	it.each([
		['', -7], ['invalid', -7], ['2026-02-30', -7], ['2026-13-01', -7],
		['0000-01-01', 0], ['2026-7-15', -7], ['2026-07-15T12:00:00', -7],
		['0001-01-01', -1], ['9999-12-31', 1],
		['2026-07-15', Number.MAX_SAFE_INTEGER], ['2026-07-15', 1.5],
	] as const)('rejects invalid or out-of-range input %s / %s', (date, days) => {
		expect(shiftIsoDate(date, days)).toBeNull();
	});

	it('expands repeated long and short dates without replacing existing variables', () => {
		expect(replaceDateOffsets(
			'{{datum|-7}} / {{datum_kurz|-7}} / {{datum_kurz|-7}} / {{datum_kurz|0}} / {{datum_kurz|7}} / {{datum}} / {{titel}}',
			'2026-07-15',
		)).toBe('Mittwoch, 8. Juli 2026 / 08.07.2026 / 08.07.2026 / 15.07.2026 / 22.07.2026 / {{datum}} / {{titel}}');
	});

	it('supports case-insensitive names and pads early years consistently with the backend', () => {
		expect(replaceDateOffsets('{{DATUM_KURZ|-7}}', '2026-07-15')).toBe('08.07.2026');
		expect(replaceDateOffsets('{{datum|0}}', '0001-01-01')).toBe('Montag, 1. Januar 0001');
	});

	it.each(['abc', '', '-1.5', '+7', ' 7', '7 ', '--7', '99999999999999999999'])(
		'leaves invalid offset %s visible and still expands a following valid token', offset => {
			const token = `{{datum_kurz|${offset}}}`;
			expect(replaceDateOffsets(`${token} {{datum_kurz|-7}}`, '2026-07-15'))
				.toBe(`${token} 08.07.2026`);
		},
	);

	it('leaves invalid dates and year underflow visible', () => {
		expect(replaceDateOffsets('{{datum|-7}} {{datum_kurz|-7}}', '2026-02-30'))
			.toBe('{{datum|-7}} {{datum_kurz|-7}}');
		expect(replaceDateOffsets('{{datum_kurz|-1}}', '0001-01-01')).toBe('{{datum_kurz|-1}}');
	});
});
