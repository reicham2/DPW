import { ref, watch } from 'vue'

export type StatsDisplayMode = 'auto' | 'always-drawer'
export type ThemeMode = 'light' | 'dark'

const STATS_MODE_KEY = 'dpw_stats_display_mode'
const THEME_KEY = 'dpw_theme'

function readStatsMode(): StatsDisplayMode {
	try {
		const v = localStorage.getItem(STATS_MODE_KEY)
		if (v === 'always-drawer') return 'always-drawer'
	} catch {
		/* localStorage unavailable */
	}
	return 'auto'
}

function readTheme(): ThemeMode {
	try {
		const v = localStorage.getItem(THEME_KEY)
		if (v === 'dark') return 'dark'
	} catch {
		/* localStorage unavailable */
	}
	return 'light'
}

function applyTheme(mode: ThemeMode) {
	document.documentElement.setAttribute('data-theme', mode)
}

export const statsDisplayMode = ref<StatsDisplayMode>(readStatsMode())
export const themeMode = ref<ThemeMode>(readTheme())

applyTheme(themeMode.value)

watch(statsDisplayMode, (v) => {
	try {
		localStorage.setItem(STATS_MODE_KEY, v)
	} catch {
		/* localStorage unavailable */
	}
})

watch(themeMode, (v) => {
	try {
		localStorage.setItem(THEME_KEY, v)
	} catch {
		/* localStorage unavailable */
	}
	applyTheme(v)
})
