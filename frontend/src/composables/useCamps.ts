import { ref } from 'vue';
import type {
	Camp,
	CampInput,
	CampDetail,
	CampActivity,
	CampActivityInput,
	CampCategory,
	CampCollaboration,
	CampPeriod,
	CampMaterialList,
	CampMaterialItem,
	CampDayResponsible,
	ScheduleEntryInput,
	ScheduleEntry,
} from '../types';
import { apiFetch, formatApiError } from './useApi';

const BASE = '/api';

// ---- Shared state (module scope) -------------------------------------------
const camps = ref<Camp[]>([]);
const currentCamp = ref<CampDetail | null>(null);

async function readJson<T>(res: Response): Promise<T> {
	if (!res.ok) throw new Error(await res.text());
	return (await res.json()) as T;
}

export function useCamps() {
	const loading = ref(false);
	const error = ref<string | null>(null);

	// ---- Camps ---------------------------------------------------------------

	async function fetchCamps(): Promise<void> {
		loading.value = true;
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/camps`);
			camps.value = await readJson<Camp[]>(res);
		} catch (e) {
			error.value = formatApiError(e);
		} finally {
			loading.value = false;
		}
	}

	async function fetchCamp(id: string): Promise<CampDetail | null> {
		loading.value = true;
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/camps/${id}`);
			if (res.status === 404) return null;
			const camp = await readJson<CampDetail>(res);
			currentCamp.value = camp;
			return camp;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		} finally {
			loading.value = false;
		}
	}

	async function createCamp(input: CampInput): Promise<Camp | null> {
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/camps`, {
				method: 'POST',
				body: JSON.stringify(input),
			});
			const camp = await readJson<Camp>(res);
			camps.value.unshift(camp);
			return camp;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function updateCamp(
		id: string,
		input: CampInput,
	): Promise<Camp | null> {
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/camps/${id}`, {
				method: 'PATCH',
				body: JSON.stringify(input),
			});
			const camp = await readJson<Camp>(res);
			const idx = camps.value.findIndex((c) => c.id === id);
			if (idx !== -1) camps.value[idx] = camp;
			return camp;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function deleteCamp(id: string): Promise<boolean> {
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/camps/${id}`, { method: 'DELETE' });
			if (!res.ok) throw new Error(await res.text());
			camps.value = camps.value.filter((c) => c.id !== id);
			return true;
		} catch (e) {
			error.value = formatApiError(e);
			return false;
		}
	}

	// ---- Collaborations (RF-Liste) -------------------------------------------

	async function createCollaboration(
		campId: string,
		payload: Partial<CampCollaboration>,
	): Promise<CampCollaboration | null> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/collaborations`, {
				method: 'POST',
				body: JSON.stringify(payload),
			});
			return await readJson<CampCollaboration>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function updateCollaboration(
		campId: string,
		id: string,
		payload: Partial<CampCollaboration>,
	): Promise<CampCollaboration | null> {
		try {
			const res = await apiFetch(
				`${BASE}/camps/${campId}/collaborations/${id}`,
				{ method: 'PATCH', body: JSON.stringify(payload) },
			);
			return await readJson<CampCollaboration>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function deleteCollaboration(
		campId: string,
		id: string,
	): Promise<boolean> {
		try {
			const res = await apiFetch(
				`${BASE}/camps/${campId}/collaborations/${id}`,
				{ method: 'DELETE' },
			);
			return res.ok;
		} catch (e) {
			error.value = formatApiError(e);
			return false;
		}
	}

	// ---- Categories ----------------------------------------------------------

	async function createCategory(
		campId: string,
		payload: Partial<CampCategory>,
	): Promise<CampCategory | null> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/categories`, {
				method: 'POST',
				body: JSON.stringify(payload),
			});
			return await readJson<CampCategory>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function updateCategory(
		campId: string,
		id: string,
		payload: Partial<CampCategory>,
	): Promise<CampCategory | null> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/categories/${id}`, {
				method: 'PATCH',
				body: JSON.stringify(payload),
			});
			return await readJson<CampCategory>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function deleteCategory(campId: string, id: string): Promise<boolean> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/categories/${id}`, {
				method: 'DELETE',
			});
			return res.ok;
		} catch (e) {
			error.value = formatApiError(e);
			return false;
		}
	}

	// ---- Periods -------------------------------------------------------------

	async function createPeriod(
		campId: string,
		payload: Partial<CampPeriod>,
	): Promise<CampPeriod | null> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/periods`, {
				method: 'POST',
				body: JSON.stringify(payload),
			});
			return await readJson<CampPeriod>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function updatePeriod(
		campId: string,
		id: string,
		payload: Partial<CampPeriod>,
	): Promise<CampPeriod | null> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/periods/${id}`, {
				method: 'PATCH',
				body: JSON.stringify(payload),
			});
			return await readJson<CampPeriod>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function deletePeriod(campId: string, id: string): Promise<boolean> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/periods/${id}`, {
				method: 'DELETE',
			});
			return res.ok;
		} catch (e) {
			error.value = formatApiError(e);
			return false;
		}
	}

	// ---- Activities ----------------------------------------------------------

	async function createActivity(
		campId: string,
		input: CampActivityInput,
	): Promise<CampActivity | null> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/activities`, {
				method: 'POST',
				body: JSON.stringify(input),
			});
			return await readJson<CampActivity>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function updateActivity(
		campId: string,
		id: string,
		input: CampActivityInput,
	): Promise<CampActivity | null> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/activities/${id}`, {
				method: 'PATCH',
				body: JSON.stringify(input),
			});
			return await readJson<CampActivity>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function deleteActivity(campId: string, id: string): Promise<boolean> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/activities/${id}`, {
				method: 'DELETE',
			});
			return res.ok;
		} catch (e) {
			error.value = formatApiError(e);
			return false;
		}
	}

	// Lightweight reposition/resize of a single placement (drag-drop).
	async function updateScheduleEntry(
		campId: string,
		id: string,
		input: ScheduleEntryInput,
	): Promise<ScheduleEntry | null> {
		try {
			const res = await apiFetch(
				`${BASE}/camps/${campId}/schedule-entries/${id}`,
				{ method: 'PATCH', body: JSON.stringify(input) },
			);
			return await readJson<ScheduleEntry>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	// ---- Day responsibles (Tagesverantwortliche) ----------------------------

	async function addDayResponsible(
		campId: string,
		payload: { period_id: string; day_offset: number; collaboration_id: string },
	): Promise<CampDayResponsible | null> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/day-responsibles`, {
				method: 'POST',
				body: JSON.stringify(payload),
			});
			return await readJson<CampDayResponsible>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function deleteDayResponsible(
		campId: string,
		id: string,
	): Promise<boolean> {
		try {
			const res = await apiFetch(
				`${BASE}/camps/${campId}/day-responsibles/${id}`,
				{ method: 'DELETE' },
			);
			return res.ok;
		} catch (e) {
			error.value = formatApiError(e);
			return false;
		}
	}

	// ---- Material lists ------------------------------------------------------

	async function createMaterialList(
		campId: string,
		payload: { name: string; collaboration_id?: string | null },
	): Promise<CampMaterialList | null> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/material-lists`, {
				method: 'POST',
				body: JSON.stringify(payload),
			});
			return await readJson<CampMaterialList>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function deleteMaterialList(
		campId: string,
		id: string,
	): Promise<boolean> {
		try {
			const res = await apiFetch(
				`${BASE}/camps/${campId}/material-lists/${id}`,
				{ method: 'DELETE' },
			);
			return res.ok;
		} catch (e) {
			error.value = formatApiError(e);
			return false;
		}
	}

	async function createMaterialItem(
		campId: string,
		listId: string,
		payload: Partial<CampMaterialItem>,
	): Promise<CampMaterialItem | null> {
		try {
			const res = await apiFetch(
				`${BASE}/camps/${campId}/material-lists/${listId}/items`,
				{ method: 'POST', body: JSON.stringify(payload) },
			);
			return await readJson<CampMaterialItem>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function updateMaterialItem(
		campId: string,
		id: string,
		payload: Partial<CampMaterialItem>,
	): Promise<CampMaterialItem | null> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/material-items/${id}`, {
				method: 'PATCH',
				body: JSON.stringify(payload),
			});
			return await readJson<CampMaterialItem>(res);
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function deleteMaterialItem(
		campId: string,
		id: string,
	): Promise<boolean> {
		try {
			const res = await apiFetch(`${BASE}/camps/${campId}/material-items/${id}`, {
				method: 'DELETE',
			});
			return res.ok;
		} catch (e) {
			error.value = formatApiError(e);
			return false;
		}
	}

	return {
		camps,
		currentCamp,
		loading,
		error,
		fetchCamps,
		fetchCamp,
		createCamp,
		updateCamp,
		deleteCamp,
		createCollaboration,
		updateCollaboration,
		deleteCollaboration,
		createCategory,
		updateCategory,
		deleteCategory,
		createPeriod,
		updatePeriod,
		deletePeriod,
		createActivity,
		updateActivity,
		deleteActivity,
		updateScheduleEntry,
		createMaterialList,
		deleteMaterialList,
		createMaterialItem,
		updateMaterialItem,
		deleteMaterialItem,
		addDayResponsible,
		deleteDayResponsible,
	};
}
