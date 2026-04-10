import { ref } from 'vue';
import { getGraphAccessToken } from './useAuth';

export interface Contact {
	displayName: string;
	email: string;
}

export function useContactSearch() {
	const results = ref<Contact[]>([]);
	const searching = ref(false);

	let abortController: AbortController | null = null;
	let debounceTimer: ReturnType<typeof setTimeout> | null = null;

	async function search(query: string) {
		if (debounceTimer) clearTimeout(debounceTimer);
		if (abortController) abortController.abort();

		const q = query.trim();
		if (q.length < 2) {
			results.value = [];
			return;
		}

		debounceTimer = setTimeout(async () => {
			searching.value = true;
			abortController = new AbortController();

			try {
				const token = await getGraphAccessToken();
				const url =
					`https://graph.microsoft.com/v1.0/me/people` +
					`?$search="${encodeURIComponent(q)}"` +
					`&$top=10` +
					`&$select=displayName,scoredEmailAddresses`;

				const res = await fetch(url, {
					headers: { Authorization: `Bearer ${token}` },
					signal: abortController.signal,
				});

				if (!res.ok) {
					results.value = [];
					return;
				}

				const data = await res.json();
				const contacts: Contact[] = [];
				for (const p of data.value ?? []) {
					const email = p.scoredEmailAddresses?.[0]?.address as
						| string
						| undefined;
					if (email) {
						contacts.push({
							displayName: p.displayName ?? email,
							email,
						});
					}
				}
				results.value = contacts;
			} catch (e) {
				if ((e as Error).name !== 'AbortError') {
					results.value = [];
				}
			} finally {
				searching.value = false;
			}
		}, 300);
	}

	function clear() {
		if (debounceTimer) clearTimeout(debounceTimer);
		if (abortController) abortController.abort();
		results.value = [];
	}

	return { results, searching, search, clear };
}
