import { ref } from 'vue';
import { apiFetch } from './useApi';
import { formatApiError } from './useApi';
import type { IdeenkisteItem, IdeenkisteInput } from '../types';

const items = ref<IdeenkisteItem[]>([]);
const loading = ref(false);
const error = ref<string | null>(null);

export function useIdeenkiste() {
	async function fetchItems(): Promise<void> {
		loading.value = true;
		error.value = null;
		try {
			const res = await apiFetch('/api/ideenkiste');
			if (!res.ok) throw new Error(await res.text());
			items.value = await res.json();
		} catch (e) {
			error.value = formatApiError(e);
		} finally {
			loading.value = false;
		}
	}

	async function createItem(input: IdeenkisteInput): Promise<IdeenkisteItem | null> {
		error.value = null;
		try {
			const res = await apiFetch('/api/ideenkiste', {
				method: 'POST',
				body: JSON.stringify(input),
			});
			if (!res.ok) throw new Error(await res.text());
			const item: IdeenkisteItem = await res.json();
			items.value.unshift(item);
			return item;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function updateItem(id: string, input: IdeenkisteInput): Promise<IdeenkisteItem | null> {
		error.value = null;
		try {
			const res = await apiFetch(`/api/ideenkiste/${id}`, {
				method: 'PUT',
				body: JSON.stringify(input),
			});
			if (!res.ok) throw new Error(await res.text());
			const updated: IdeenkisteItem = await res.json();
			const idx = items.value.findIndex((i) => i.id === id);
			if (idx !== -1) items.value[idx] = updated;
			return updated;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function deleteItem(id: string): Promise<boolean> {
		error.value = null;
		try {
			const res = await apiFetch(`/api/ideenkiste/${id}`, { method: 'DELETE' });
			if (!res.ok) throw new Error(await res.text());
			items.value = items.value.filter((i) => i.id !== id);
			return true;
		} catch (e) {
			error.value = formatApiError(e);
			return false;
		}
	}

	return { items, loading, error, fetchItems, createItem, updateItem, deleteItem };
}
