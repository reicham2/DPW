import { ref, computed } from 'vue';
import type { Camp } from '../types';
import { apiFetch } from './useApi';

// Shared, module-scoped camp navigation context. Drives the DPWeb-logo camp
// dropdown and the dynamic in-camp navigation tabs.

const allCamps = ref<Camp[]>([]);
// The camp currently open in the camp detail view (null when outside a camp).
const activeCampId = ref<string | null>(null);
const activeCampTitle = ref<string>('');

// In-camp navigation tabs. `key` maps to the ?tab query param.
export const CAMP_TABS = [
	{ key: 'dashboard', label: 'Dashboard' },
	{ key: 'programm', label: 'Programm' },
	{ key: 'geschichte', label: 'Geschichte' },
	{ key: 'material', label: 'Material' },
	{ key: 'admin', label: 'Admin' },
] as const;

export type CampTabKey = (typeof CAMP_TABS)[number]['key'];

export function useCampContext() {
	async function loadCampList(): Promise<void> {
		try {
			const res = await apiFetch('/api/camps');
			if (res.ok) allCamps.value = (await res.json()) as Camp[];
		} catch {
			/* non-critical */
		}
	}

	function setActiveCamp(id: string | null, title = '') {
		activeCampId.value = id;
		activeCampTitle.value = title;
	}

	const inCamp = computed(() => activeCampId.value !== null);

	return {
		allCamps,
		activeCampId,
		activeCampTitle,
		inCamp,
		loadCampList,
		setActiveCamp,
	};
}
