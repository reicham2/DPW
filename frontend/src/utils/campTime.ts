// Time/duration formatting helpers for the camp planning views.

// Minute-of-day -> "HH:MM".
export function formatMinuteOfDay(m: number): string {
	const h = Math.floor(m / 60);
	const mm = m % 60;
	return `${String(h).padStart(2, '0')}:${String(mm).padStart(2, '0')}`;
}

// Duration in minutes -> human readable, e.g. 90 -> "1h 30min", 120 -> "2h".
export function formatDuration(min: number): string {
	if (min <= 0) return '0min';
	const h = Math.floor(min / 60);
	const m = min % 60;
	if (h === 0) return `${m}min`;
	if (m === 0) return `${h}h`;
	return `${h}h ${m}min`;
}
