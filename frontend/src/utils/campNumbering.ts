import type { CampActivity, CampCategory } from '../types';

// Activities are numbered sequentially within their category, formatted by
// the category's numbering style (arabic, latin letters, or roman numerals).

function toLatin(n: number, upper: boolean): string {
	// 1 -> a, 26 -> z, 27 -> aa …
	let s = '';
	let x = n;
	while (x > 0) {
		const rem = (x - 1) % 26;
		s = String.fromCharCode(97 + rem) + s;
		x = Math.floor((x - 1) / 26);
	}
	return upper ? s.toUpperCase() : s;
}

function toRoman(n: number, upper: boolean): string {
	const map: [number, string][] = [
		[1000, 'm'], [900, 'cm'], [500, 'd'], [400, 'cd'],
		[100, 'c'], [90, 'xc'], [50, 'l'], [40, 'xl'],
		[10, 'x'], [9, 'ix'], [5, 'v'], [4, 'iv'], [1, 'i'],
	];
	let x = n;
	let s = '';
	for (const [val, sym] of map) {
		while (x >= val) {
			s += sym;
			x -= val;
		}
	}
	return upper ? s.toUpperCase() : s;
}

// Formats the 1-based ordinal according to the category's numbering style.
export function formatNumber(ordinal: number, style: string): string {
	switch (style) {
		case 'a':
			return toLatin(ordinal, false);
		case 'A':
			return toLatin(ordinal, true);
		case 'i':
			return toRoman(ordinal, false);
		case 'I':
			return toRoman(ordinal, true);
		default:
			return String(ordinal); // '1'
	}
}

// Builds a map of activityId -> full label (e.g. "LP1") for all activities.
// Ordering within a category follows the earliest schedule offset, falling back
// to creation order, so the numbers stay stable and chronological.
export function buildActivityNumbers(
	activities: CampActivity[],
	categories: CampCategory[],
): Record<string, string> {
	const catById: Record<string, CampCategory> = {};
	for (const c of categories) catById[c.id] = c;

	function earliestOffset(a: CampActivity): number {
		if (!a.schedule_entries.length) return Number.MAX_SAFE_INTEGER;
		return Math.min(...a.schedule_entries.map((s) => s.period_offset));
	}

	// Group activities by category.
	const byCat: Record<string, CampActivity[]> = {};
	for (const a of activities) {
		const key = a.category_id ?? '__none__';
		(byCat[key] = byCat[key] || []).push(a);
	}

	const labels: Record<string, string> = {};
	for (const [catId, list] of Object.entries(byCat)) {
		const sorted = [...list].sort((x, y) => {
			const dx = earliestOffset(x) - earliestOffset(y);
			if (dx !== 0) return dx;
			return (x.created_at ?? '').localeCompare(y.created_at ?? '');
		});
		const cat = catById[catId];
		const short = cat?.short_name ?? '';
		const style = cat?.numbering_style ?? '1';
		sorted.forEach((a, i) => {
			labels[a.id] = short + formatNumber(i + 1, style);
		});
	}
	return labels;
}
