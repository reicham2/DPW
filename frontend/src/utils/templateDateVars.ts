// Calendar-day arithmetic in UTC avoids DST and local timezone shifts.
export function shiftIsoDate(iso: string, days: number): string | null {
	if (!/^\d{4}-\d{2}-\d{2}$/.test(iso) || !Number.isSafeInteger(days)) return null;
	const date = new Date(`${iso}T00:00:00Z`);
	if (!Number.isFinite(date.getTime()) || iso.startsWith('0000') ||
		date.toISOString().slice(0, 10) !== iso) return null;
	const first = new Date('0001-01-01T00:00:00Z').getTime();
	const last = new Date('9999-12-31T00:00:00Z').getTime();
	const dayMs = 86400000;
	if (days < (first - date.getTime()) / dayMs || days > (last - date.getTime()) / dayMs) return null;
	date.setUTCDate(date.getUTCDate() + days);
	return date.toISOString().slice(0, 10);
}

export function replaceDateOffsets(text: string, activityDate: string): string {
	return text.replace(/\{\{(datum|datum_kurz)\|(-?\d+)\}\}/gi, (token, name: string, number: string) => {
		const iso = shiftIsoDate(activityDate, Number(number));
		if (!iso) return token;
		if (name.toLowerCase() === 'datum_kurz') {
			return `${iso.slice(8, 10)}.${iso.slice(5, 7)}.${iso.slice(0, 4)}`;
		}
		return new Intl.DateTimeFormat('de-DE', {
			timeZone: 'UTC', weekday: 'long', day: 'numeric', month: 'long', year: 'numeric',
		}).formatToParts(new Date(`${iso}T00:00:00Z`))
			.map(part => part.type === 'year' ? iso.slice(0, 4) : part.value).join('');
	});
}
