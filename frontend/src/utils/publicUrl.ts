import { config } from '../config';

export function publicUrl(path: string): string {
	const base = config.PUBLIC_BASE_URL.trim().replace(/\/+$/, '');
	return `${base}${path}`;
}
