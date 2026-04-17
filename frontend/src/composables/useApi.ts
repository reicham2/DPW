import { getIdToken } from './useAuth';

export async function apiFetch(
	url: string,
	options: RequestInit = {},
): Promise<Response> {
	const token = await getIdToken();
	return fetch(url, {
		...options,
		headers: {
			...((options.headers as Record<string, string>) ?? {}),
			Authorization: `Bearer ${token}`,
			...(options.body && !(options.body instanceof FormData)
				? { 'Content-Type': 'application/json' }
				: {}),
		},
	});
}
