import { ref, computed, markRaw } from 'vue';
import {
	LayoutDashboard, CalendarDays, BookOpen, Package, Settings,
} from 'lucide-vue-next';
import type { Component } from 'vue';
import type { Camp } from '../types';
import { apiFetch } from './useApi';

// Shared, module-scoped camp navigation context. Drives the DPWeb-logo camp
// dropdown and the dynamic in-camp navigation tabs.

const allCamps = ref<Camp[]>([]);
// The camp currently open in the camp detail view (null when outside a camp).
const activeCampId = ref<string | null>(null);
const activeCampTitle = ref<string>('');

// In-camp navigation tabs, mirroring eCamp's top nav bar (icon + label).
// `key` maps to the ?tab query param.
export interface CampTab { key: string; label: string; icon: Component }
export const CAMP_TABS: readonly CampTab[] = [
	{ key: 'dashboard', label: 'Dashboard', icon: markRaw(LayoutDashboard) },
	{ key: 'programm', label: 'Programm', icon: markRaw(CalendarDays) },
	{ key: 'geschichte', label: 'Geschichte', icon: markRaw(BookOpen) },
	{ key: 'material', label: 'Material', icon: markRaw(Package) },
	{ key: 'admin', label: 'Admin', icon: markRaw(Settings) },
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
