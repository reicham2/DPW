import { ref } from 'vue';
import { apiFetch } from './useApi';

interface MidataDepartmentCount {
	configured: boolean;
	children_count: number | null;
	error?: string;
}

interface MidataChildrenCountsResponse {
	departments?: Record<string, MidataDepartmentCount>;
}

const midataByDepartment = ref<Record<string, MidataDepartmentCount>>({});
const midataLoading = ref(false);
const midataLastError = ref<string | null>(null);
let midataRefreshTimer: ReturnType<typeof setInterval> | null = null;
let midataInitialRefreshTimer: ReturnType<typeof setTimeout> | null = null;

export function useMidataCounts() {
	async function refreshMidataCounts(): Promise<void> {
		if (midataLoading.value) return;
		midataLoading.value = true;
		midataLastError.value = null;
		try {
			const res = await apiFetch('/api/midata/children-counts');
			if (!res.ok) throw new Error(await res.text());
			const data = (await res.json()) as MidataChildrenCountsResponse;
			midataByDepartment.value = data.departments ?? {};
		} catch (e) {
			midataLastError.value = String(e);
		} finally {
			midataLoading.value = false;
		}
	}

	function startMidataAutoRefresh(
		intervalMs = 120000,
		initialDelayMs = 5000,
	): void {
		if (midataInitialRefreshTimer) {
			clearTimeout(midataInitialRefreshTimer);
			midataInitialRefreshTimer = null;
		}
		if (midataRefreshTimer) clearInterval(midataRefreshTimer);

		const beginRefreshing = () => {
			void refreshMidataCounts();
			midataRefreshTimer = setInterval(() => {
				void refreshMidataCounts();
			}, intervalMs);
		};

		if (initialDelayMs <= 0) {
			beginRefreshing();
			return;
		}

		midataInitialRefreshTimer = setTimeout(() => {
			midataInitialRefreshTimer = null;
			beginRefreshing();
		}, initialDelayMs);
	}

	function stopMidataAutoRefresh(): void {
		if (midataInitialRefreshTimer) {
			clearTimeout(midataInitialRefreshTimer);
			midataInitialRefreshTimer = null;
		}
		if (midataRefreshTimer) {
			clearInterval(midataRefreshTimer);
			midataRefreshTimer = null;
		}
	}

	function resetMidataCounts(): void {
		midataByDepartment.value = {};
		midataLastError.value = null;
	}

	return {
		midataByDepartment,
		midataLoading,
		midataLastError,
		refreshMidataCounts,
		startMidataAutoRefresh,
		stopMidataAutoRefresh,
		resetMidataCounts,
	};
}
