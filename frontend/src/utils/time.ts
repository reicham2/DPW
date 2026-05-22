export function addMinutesToClock(start: string, minutes: number): string {
	const m = /^(\d{1,2}):(\d{2})$/.exec(start.trim());
	if (!m) return '';
	const total = parseInt(m[1], 10) * 60 + parseInt(m[2], 10) + minutes;
	const h = ((Math.floor(total / 60) % 24) + 24) % 24;
	const mm = ((total % 60) + 60) % 60;
	return `${String(h).padStart(2, '0')}:${String(mm).padStart(2, '0')}`;
}

export function formatDuration(min: number): string {
	if (!Number.isFinite(min) || min <= 0) return '0 min';
	if (min < 60) return `${min} min`;
	const h = Math.floor(min / 60);
	const m = min % 60;
	return m === 0 ? `${h} h` : `${h} h ${m} min`;
}
