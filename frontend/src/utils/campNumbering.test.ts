import { describe, it, expect } from 'vitest';
import { formatNumber, buildActivityNumbers } from './campNumbering';
import type { CampActivity, CampCategory } from '../types';

describe('formatNumber', () => {
	it('arabic', () => {
		expect(formatNumber(1, '1')).toBe('1');
		expect(formatNumber(12, '1')).toBe('12');
	});
	it('latin lower/upper', () => {
		expect(formatNumber(1, 'a')).toBe('a');
		expect(formatNumber(26, 'a')).toBe('z');
		expect(formatNumber(27, 'a')).toBe('aa');
		expect(formatNumber(2, 'A')).toBe('B');
	});
	it('roman lower/upper', () => {
		expect(formatNumber(4, 'i')).toBe('iv');
		expect(formatNumber(9, 'I')).toBe('IX');
		expect(formatNumber(14, 'I')).toBe('XIV');
	});
	it('unknown style falls back to arabic', () => {
		expect(formatNumber(3, 'zzz')).toBe('3');
	});
});

function cat(id: string, short: string, style: string): CampCategory {
	return {
		id, camp_id: 'c', short_name: short, name: short, color: '#000',
		numbering_style: style, position: 0, created_at: '', updated_at: '',
	};
}
function act(id: string, catId: string | null, offset: number): CampActivity {
	return {
		id, camp_id: 'c', category_id: catId, title: id, location: '',
		schedule_entries: offset >= 0
			? [{ id: 's' + id, activity_id: id, period_id: 'p', period_offset: offset, length: 60, left_fraction: 0, width_fraction: 1 }]
			: [],
		responsible_collaboration_ids: [], content_nodes: [], created_at: '2020-01-0' + id,
	};
}

describe('buildActivityNumbers', () => {
	it('numbers per category by chronological offset', () => {
		const cats = [cat('LP', 'LP', '1'), cat('SP', 'SP', 'a')];
		const acts = [
			act('1', 'LP', 600), // LP, later
			act('2', 'LP', 300), // LP, earlier -> LP1
			act('3', 'SP', 100), // SP1 -> 'a'
		];
		const labels = buildActivityNumbers(acts, cats);
		expect(labels['2']).toBe('LP1');
		expect(labels['1']).toBe('LP2');
		expect(labels['3']).toBe('SPa');
	});

	it('handles uncategorised activities without crashing', () => {
		const labels = buildActivityNumbers([act('9', null, 0)], []);
		expect(labels['9']).toBe('1');
	});
});
