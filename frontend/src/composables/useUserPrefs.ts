import { ref, watch } from 'vue'

export type StatsDisplayMode = 'auto' | 'always-drawer'

const STATS_MODE_KEY = 'dpw_stats_display_mode'

function readStatsMode(): StatsDisplayMode {
	try {
		const v = localStorage.getItem(STATS_MODE_KEY)
		if (v === 'always-drawer') return 'always-drawer'
	} catch {
		/* localStorage unavailable */
	}
	return 'auto'
}

export const statsDisplayMode = ref<StatsDisplayMode>(readStatsMode())

watch(statsDisplayMode, (v) => {
	try {
		localStorage.setItem(STATS_MODE_KEY, v)
	} catch {
		/* localStorage unavailable */
	}
})
