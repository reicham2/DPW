<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useActivities } from '../composables/useActivities'
import { usePermissions } from '../composables/usePermissions'
import { user } from '../composables/useAuth'
import ActivityList from '../components/ActivityList.vue'
import DepartmentBadge from '../components/DepartmentBadge.vue'
import type { Activity, Department, MaterialItem } from '../types'
import ErrorAlert from '../components/ErrorAlert.vue'

const { departments: deptRecords, fetchDepartments: fetchDeptRecords, myPermissions, fetchMyPermissions, canReadActivity, readableDepts, writableDepts } = usePermissions()
const DEPARTMENTS = computed<Department[]>(() => readableDepts(user.value?.department))

const { activities, loading, error, fetchActivities } = useActivities()

// Permission-based: can user see multiple departments?
const canReadMultipleDepts = computed(() => {
  return DEPARTMENTS.value.length > 1
})

// Can the user create activities? (has write access to at least one dept)
const canCreateActivity = computed(() => {
  return writableDepts(user.value?.department).length > 0
})

const search = ref('')
const activedept = ref<Department | 'Alle'>(user.value?.department ?? 'Alle')

// Date range filter inputs (manual)
const dateFrom = ref('')
const dateTo   = ref('')
const onlyMine = ref(false)

// Sliding window for lazy-loading past / future
function todayStr(): string {
  const d = new Date()
  return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0')
}

function addMonths(dateStr: string, months: number): string {
  const d = new Date(dateStr + 'T00:00:00')
  d.setMonth(d.getMonth() + months)
  return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0')
}

// Default window: today → today+1month; buttons expand by 5 activities beyond window edges
const STEP = 5
const extraEarlier = ref(0) // extra activities to show before today
const extraLater   = ref(0) // extra activities to show after today+1month

const hasDateFilter = computed(() => dateFrom.value !== '' || dateTo.value !== '')

function clearDateFilter() {
  dateFrom.value = ''
  dateTo.value = ''
}

// Reset window when department changes
watch(activedept, () => {
  extraEarlier.value = 0
  extraLater.value = 0
})

function loadLater() {
  extraLater.value += STEP
}

function loadEarlier() {
  extraEarlier.value += STEP
}

onMounted(() => {
  fetchActivities()
  fetchDeptRecords()
  fetchMyPermissions()
})

// "Meine Verantwortung" — enriched list entry
interface ListEntry {
  activity: Activity
  dimmed: boolean             // true = only material match, not main responsible
  myMaterials: MaterialItem[] // materials assigned to me (shown when dimmed)
}

const filteredEntries = computed<ListEntry[]>(() => {
  let list = activities.value

  // Filter by readable departments (client-side safety net)
  list = list.filter(a => user.value ? canReadActivity(a, user.value.display_name, user.value.department) : false)

  // Department filter
  if (activedept.value !== 'Alle') {
    list = list.filter(a => a.department === activedept.value)
  }

  // Text search
  const q = search.value.trim().toLowerCase()
  if (q) {
    list = list.filter(a =>
      a.title.toLowerCase().includes(q) ||
      a.responsible.join(' ').toLowerCase().includes(q) ||
      a.location.toLowerCase().includes(q) ||
      (a.goal ?? '').toLowerCase().includes(q)
    )
  }

  // Sort by date ascending (oldest on top)
  list = [...list].sort((a, b) => a.date < b.date ? -1 : a.date > b.date ? 1 : 0)

  // Apply date filter or default window + expansion
  if (hasDateFilter.value) {
    if (dateFrom.value) list = list.filter(a => a.date >= dateFrom.value)
    if (dateTo.value)   list = list.filter(a => a.date <= dateTo.value)
  } else {
    const today = todayStr()
    const monthLater = addMonths(today, 1)

    // Default window: today → today+1month
    const defaultList = list.filter(a => a.date >= today && a.date <= monthLater)
    // Activities before and after the default window
    const beforeWindow = list.filter(a => a.date < today)
    const afterWindow  = list.filter(a => a.date > monthLater)

    // Take extra items from edges
    const earlierSlice = extraEarlier.value > 0
      ? beforeWindow.slice(-extraEarlier.value)
      : []
    const laterSlice = extraLater.value > 0
      ? afterWindow.slice(0, extraLater.value)
      : []

    list = [...earlierSlice, ...defaultList, ...laterSlice]
  }

  // Build entries
  if (onlyMine.value && user.value) {
    const me = user.value.display_name.toLowerCase()
    const entries: ListEntry[] = []
    for (const a of list) {
      const isResponsible = a.responsible.some(r => r.toLowerCase() === me)
      const myMats = (a.material ?? []).filter(m =>
        (m.responsible ?? []).some(r => r.toLowerCase() === me)
      )
      if (isResponsible) {
        entries.push({ activity: a, dimmed: false, myMaterials: myMats })
      } else if (myMats.length > 0) {
        entries.push({ activity: a, dimmed: true, myMaterials: myMats })
      }
    }
    return entries
  }

  return list.map(a => ({ activity: a, dimmed: false, myMaterials: [] }))
})

// Counts of activities outside the visible window
const baseList = computed(() => {
  let list = activities.value
  // Filter by readable departments
  list = list.filter(a => user.value ? canReadActivity(a, user.value.display_name, user.value.department) : false)
  if (activedept.value !== 'Alle') list = list.filter(a => a.department === activedept.value)
  const q = search.value.trim().toLowerCase()
  if (q) {
    list = list.filter(a =>
      a.title.toLowerCase().includes(q) ||
      a.responsible.join(' ').toLowerCase().includes(q) ||
      a.location.toLowerCase().includes(q) ||
      (a.goal ?? '').toLowerCase().includes(q)
    )
  }
  return [...list].sort((a, b) => a.date < b.date ? -1 : a.date > b.date ? 1 : 0)
})

const laterCount = computed(() => {
  if (hasDateFilter.value) return 0
  const list = baseList.value
  const monthLater = addMonths(todayStr(), 1)
  const afterWindow = list.filter(a => a.date > monthLater)
  return Math.max(0, afterWindow.length - extraLater.value)
})

const earlierCount = computed(() => {
  if (hasDateFilter.value) return 0
  const list = baseList.value
  const today = todayStr()
  const beforeWindow = list.filter(a => a.date < today)
  return Math.max(0, beforeWindow.length - extraEarlier.value)
})
</script>

<template>
  <header class="header">
    <h1>Aktivitäten</h1>
    <div class="header-right">
      <router-link v-if="canCreateActivity" to="/activities/new" class="btn-primary">+ Neue Aktivität</router-link>
      <button v-else class="btn-primary" disabled title="Keine Schreibrechte">+ Neue Aktivität</button>
    </div>
  </header>

  <main class="main">
    <!-- Filter bar -->
    <div class="filter-bar">
      <div class="filter-search">
        <span class="filter-search-icon">🔍</span>
        <input
          v-model="search"
          type="search"
          class="filter-search-input"
          placeholder="Suchen nach Titel, Ort, Verantwortlichen…"
        />
      </div>

      <!-- Date range + my-activities row -->
      <div class="filter-row">
        <div class="filter-dates">
          <label class="filter-date-label">
            Von
            <input v-model="dateFrom" type="date" class="filter-date-input" />
          </label>
          <label class="filter-date-label">
            Bis
            <input v-model="dateTo" type="date" class="filter-date-input" />
          </label>
          <button v-if="hasDateFilter" class="filter-date-clear" @click="clearDateFilter" title="Datumsfilter zurücksetzen">✕</button>
        </div>
        <button
          class="filter-tab"
          :class="{ 'filter-tab--active': onlyMine }"
          @click="onlyMine = !onlyMine"
        >👤 Meine Verantwortung</button>
      </div>

      <!-- Show department filter tabs if user can read multiple departments -->
      <div v-if="canReadMultipleDepts" class="filter-tabs">
        <button
          class="filter-tab"
          :class="{ 'filter-tab--active': activedept === 'Alle' }"
          @click="activedept = 'Alle'"
        >Alle</button>
        <button
          v-for="dep in DEPARTMENTS"
          :key="dep"
          class="filter-tab filter-tab--badge"
          @click="activedept = dep"
        >
          <DepartmentBadge :department="dep" :active="activedept === dep" />
        </button>
      </div>
    </div>

    <ErrorAlert :error="error" />
    <p v-if="loading" class="loading">Laden...</p>

    <template v-if="!loading">
      <!-- Load earlier activities (top — older dates) -->
      <button
        v-if="!hasDateFilter && earlierCount > 0"
        class="btn-load-more btn-load-more--top"
        @click="loadEarlier"
      >⬆ Frühere Aktivitäten laden ({{ earlierCount }})</button>

      <p v-if="filteredEntries.length === 0" class="filter-empty">
        {{ activities.length > 0 ? 'Keine Aktivitäten für diese Filter.' : 'Noch keine Aktivitäten.' }}
      </p>

      <ActivityList :entries="filteredEntries" />

      <!-- Load later activities (bottom — future dates) -->
      <button
        v-if="!hasDateFilter && laterCount > 0"
        class="btn-load-more btn-load-more--bottom"
        @click="loadLater"
      >⬇ Spätere Aktivitäten laden ({{ laterCount }})</button>
    </template>
  </main>
</template>
