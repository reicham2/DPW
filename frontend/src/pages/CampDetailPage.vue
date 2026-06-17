<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useCamps } from '../composables/useCamps'
import { useCampContext, CAMP_TABS } from '../composables/useCampContext'
import CampActivityEditor from '../components/CampActivityEditor.vue'
import CampActivityView from '../components/CampActivityView.vue'
import CampRfListe from '../components/CampRfListe.vue'
import ResponsibleAvatars from '../components/ResponsibleAvatars.vue'
import ErrorAlert from '../components/ErrorAlert.vue'
import {
  CalendarDays, List, Plus, Users, Clock, MapPin, Lock, Unlock, BookOpen, Package, Tag, Printer,
} from 'lucide-vue-next'
import { buildActivityNumbers } from '../utils/campNumbering'
import { formatMinuteOfDay, formatDuration } from '../utils/campTime'
import { printCamp } from '../utils/campPrint'
import { useUserResolver } from '../composables/useUserResolver'
import { useUsers } from '../composables/useUsers'
import { useWebSocket } from '../composables/useWebSocket'
import type {
  CampActivity, CampActivityInput, CampCategory, CampDetail, CampPeriod,
} from '../types'

const route = useRoute()
const router = useRouter()
const campId = computed(() => String(route.params.id))
const tab = computed(() => (route.query.tab as string) || 'dashboard')

const {
  currentCamp, error, fetchCamp,
  createActivity, updateActivity, deleteActivity, updateScheduleEntry,
  addDayResponsible, deleteDayResponsible,
} = useCamps()
const { setActiveCamp } = useCampContext()
const { resolveResponsibleName } = useUserResolver()
const { users, fetchUsers } = useUsers()

const camp = computed<CampDetail | null>(() => currentCamp.value)
const loading = ref(true)

// Programm sub-view: calendar | list
type ProgMode = 'calendar' | 'list'
const progMode = ref<ProgMode>('calendar')
const activePeriodId = ref<string>('')
const showRf = ref(false)

// Calendar lock: when locked, no time-shift / no drag-create. Details stay editable.
const locked = ref(true)

// ── Detail panel state (split: slim single day left, panel right) ─────
// panelOpen: a side panel is shown. editMode: true = editor, false = read-only view.
const editorOpen = ref(false)        // any panel open (view or edit)
const editMode = ref(false)          // false = view, true = edit
const editingActivity = ref<CampActivity | null>(null)
const prefillSchedule = ref<{ period_id: string; period_offset: number; length: number } | null>(null)
// When the panel is open, the calendar collapses to this single day index.
const focusedDayIndex = ref(0)

async function reload() {
  loading.value = true
  const c = await fetchCamp(campId.value)
  loading.value = false
  if (c) {
    setActiveCamp(c.id, c.title)
    if (!activePeriodId.value && c.periods.length) activePeriodId.value = c.periods[0].id
  }
}

onMounted(() => {
  reload()
  fetchUsers()
  // Refresh the current-time indicator every minute.
  nowTimer = setInterval(() => {
    nowMinute.value = currentMinuteOfDay()
    todayIso.value = currentIsoDate()
  }, 60_000)
})
watch(campId, reload)
onUnmounted(() => {
  setActiveCamp(null)
  if (nowTimer) clearInterval(nowTimer)
})

function setTab(key: string) {
  router.push(`/camps/${campId.value}?tab=${key}`)
}

// Realtime: when another client changes this camp, pull the latest graph.
// Skip while actively editing so an open editor isn't clobbered mid-typing.
useWebSocket((e) => {
  if (e.event === 'camp_updated' && e.camp_id === campId.value) {
    if (editMode.value) return
    void reloadSilent()
  }
})
// Reload the camp graph without toggling the loading splash (for realtime).
async function reloadSilent() {
  const c = await fetchCamp(campId.value)
  if (c) setActiveCamp(c.id, c.title)
}

// ── Period helpers ──────────────────────────────────────────────────────────
const periods = computed<CampPeriod[]>(() => camp.value?.periods ?? [])
const activePeriod = computed(() =>
  periods.value.find((p) => p.id === activePeriodId.value) ?? periods.value[0] ?? null,
)
function daysInPeriod(p: CampPeriod): string[] {
  const out: string[] = []
  const start = new Date(p.start_date + 'T00:00:00')
  const end = new Date(p.end_date + 'T00:00:00')
  for (let d = new Date(start); d <= end; d.setDate(d.getDate() + 1)) {
    out.push(
      d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0'),
    )
  }
  return out
}
const periodDays = computed(() => (activePeriod.value ? daysInPeriod(activePeriod.value) : []))
// When a panel is open the calendar collapses to the single focused day;
// otherwise it shows the whole period. Values are indexes into periodDays.
const visibleDayIndexes = computed<number[]>(() => {
  if (editorOpen.value) {
    const i = Math.min(Math.max(focusedDayIndex.value, 0), Math.max(periodDays.value.length - 1, 0))
    return periodDays.value.length ? [i] : []
  }
  return periodDays.value.map((_, i) => i)
})
function dayLabel(iso: string): string {
  const d = new Date(iso + 'T00:00:00')
  const wd = ['So', 'Mo', 'Di', 'Mi', 'Do', 'Fr', 'Sa'][d.getDay()]
  return `${wd} ${d.getDate()}.${d.getMonth() + 1}.`
}

// ── Lookups ───────────────────────────────────────────────────────────────────
const categoryById = computed<Record<string, CampCategory>>(() => {
  const m: Record<string, CampCategory> = {}
  for (const c of camp.value?.categories ?? []) m[c.id] = c
  return m
})
function categoryColor(a: CampActivity): string {
  return a.category_id ? categoryById.value[a.category_id]?.color ?? '#0080ff' : '#9ca3af'
}
// Per-category sequential numbers (LP1, LP2, …).
const activityNumbers = computed(() =>
  buildActivityNumbers(camp.value?.activities ?? [], camp.value?.categories ?? []),
)
// Short label shown on cards: the numbered label (e.g. "LP1"), falling back to
// the bare category short name when no number could be derived.
function categoryShort(a: CampActivity): string {
  return activityNumbers.value[a.id]
    ?? (a.category_id ? categoryById.value[a.category_id]?.short_name ?? '' : '')
}
// Category filter: set of hidden category ids. Empty = show all.
// '__none__' represents activities without a category.
const hiddenCats = ref<Set<string>>(new Set())
function toggleCat(id: string) {
  const next = new Set(hiddenCats.value)
  if (next.has(id)) next.delete(id)
  else next.add(id)
  hiddenCats.value = next
}
function catVisible(a: CampActivity): boolean {
  return !hiddenCats.value.has(a.category_id ?? '__none__')
}
const anyCatHidden = computed(() => hiddenCats.value.size > 0)

const collabById = computed<Record<string, string>>(() => {
  const m: Record<string, string> = {}
  for (const c of camp.value?.collaborations ?? []) m[c.id] = c.abbreviation || c.display_name
  return m
})
function responsibleAbbrs(a: CampActivity): string[] {
  return a.responsible_collaboration_ids.map((id) => collabById.value[id]).filter(Boolean)
}

// ── Calendar layout ──────────────────────────────────────────────────────────
const HOUR_START = 6
const HOUR_END = 24
const PX_PER_MIN = 1
const hours = computed(() => {
  const out: number[] = []
  for (let h = HOUR_START; h <= HOUR_END; h++) out.push(h)
  return out
})

interface PlacedEntry {
  activity: CampActivity
  scheduleId: string
  dayIndex: number
  top: number
  height: number
  left: number
  width: number
}
const placedByDay = computed<PlacedEntry[][]>(() => {
  const cols: PlacedEntry[][] = periodDays.value.map(() => [])
  if (!activePeriod.value) return cols
  for (const a of camp.value?.activities ?? []) {
    if (!catVisible(a)) continue
    for (const se of a.schedule_entries) {
      if (se.period_id !== activePeriod.value.id) continue
      const dayIndex = Math.floor(se.period_offset / (24 * 60))
      if (dayIndex < 0 || dayIndex >= cols.length) continue
      const minuteOfDay = se.period_offset - dayIndex * 24 * 60
      cols[dayIndex].push({
        activity: a,
        scheduleId: se.id,
        dayIndex,
        top: (minuteOfDay - HOUR_START * 60) * PX_PER_MIN,
        height: Math.max(20, se.length * PX_PER_MIN),
        left: se.left_fraction,
        width: se.width_fraction,
      })
    }
  }
  return cols
})

// ── List view ────────────────────────────────────────────────────────────────
interface ListRow { activity: CampActivity; dayIndex: number; minuteOfDay: number; length: number }
const listRows = computed<ListRow[]>(() => {
  if (!activePeriod.value) return []
  const rows: ListRow[] = []
  for (const a of camp.value?.activities ?? []) {
    if (!catVisible(a)) continue
    for (const se of a.schedule_entries) {
      if (se.period_id !== activePeriod.value.id) continue
      const dayIndex = Math.floor(se.period_offset / (24 * 60))
      rows.push({ activity: a, dayIndex, minuteOfDay: se.period_offset - dayIndex * 24 * 60, length: se.length })
    }
  }
  rows.sort((x, y) => (x.dayIndex !== y.dayIndex ? x.dayIndex - y.dayIndex : x.minuteOfDay - y.minuteOfDay))
  return rows
})
const listByDay = computed(() =>
  periodDays.value.map((day, idx) => {
    const rows = listRows.value.filter((r) => r.dayIndex === idx)
    const totalMin = rows.reduce((sum, r) => sum + r.length, 0)
    return { day, rows, count: rows.length, totalMin }
  }),
)

const fmtDuration = formatDuration
const fmtMin = formatMinuteOfDay

// Sequential clock range for a Programmpunkt within an activity, based on the
// activity start minute-of-day and cumulative durations (mirrors activities).
function programClock(startMin: number, progs: { duration_minutes: number }[], i: number): string {
  let acc = 0
  for (let k = 0; k < i; k++) acc += Number(progs[k].duration_minutes) || 0
  const s = startMin + acc
  const e = s + (Number(progs[i].duration_minutes) || 0)
  return `${fmtMin(((s % 1440) + 1440) % 1440)}–${fmtMin(((e % 1440) + 1440) % 1440)}`
}

// ── "Now" indicator (red current-time line) ──────────────────────────────
const nowMinute = ref(currentMinuteOfDay())
const todayIso = ref(currentIsoDate())
let nowTimer: ReturnType<typeof setInterval> | null = null
function currentMinuteOfDay(): number {
  const d = new Date()
  return d.getHours() * 60 + d.getMinutes()
}
function currentIsoDate(): string {
  const d = new Date()
  return d.getFullYear() + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0')
}
// Pixel offset of the now-line within a day column; null if outside the visible band.
const nowTop = computed(() => {
  if (nowMinute.value < HOUR_START * 60 || nowMinute.value > HOUR_END * 60) return null
  return (nowMinute.value - HOUR_START * 60) * PX_PER_MIN
})
function isToday(dayIso: string): boolean {
  return dayIso === todayIso.value
}

// Day responsibles (Tagesverantwortliche) for the active period, keyed by day
// index. Holds the raw records so we can render avatars and remove entries.
const dayRespByIndex = computed<Record<number, { id: string; responsible: string }[]>>(() => {
  const m: Record<number, { id: string; responsible: string }[]> = {}
  if (!activePeriod.value) return m
  for (const dr of camp.value?.day_responsibles ?? []) {
    if (dr.period_id !== activePeriod.value.id) continue
    ;(m[dr.day_offset] = m[dr.day_offset] || []).push({ id: dr.id, responsible: dr.responsible })
  }
  return m
})
// Just the responsible ids/names for a day (for ResponsibleAvatars).
function dayRespNames(dayIndex: number): string[] {
  return (dayRespByIndex.value[dayIndex] ?? []).map((d) => d.responsible)
}

// ── Inline day-responsible editing (calendar header, when unlocked) ───────────
const dayRespEditing = ref<number | null>(null)   // which day index has the picker open
const dayRespSearch = ref('')
const dayRespDropdownUsers = computed(() => {
  const q = dayRespSearch.value.toLowerCase()
  const cur = dayRespEditing.value !== null ? dayRespNames(dayRespEditing.value) : []
  return users.value.filter((u) => !cur.includes(u.id) && (q === '' || u.display_name.toLowerCase().includes(q)))
})
function openDayRespPicker(dayIndex: number) {
  dayRespEditing.value = dayIndex
  dayRespSearch.value = ''
}
async function addDayResp(dayIndex: number, responsible: string) {
  if (!activePeriod.value || !responsible.trim()) return
  await addDayResponsible(campId.value, { period_id: activePeriod.value.id, day_offset: dayIndex, responsible })
  dayRespSearch.value = ''
  dayRespEditing.value = null
  await reloadSilent()
}
async function removeDayResp(id: string) {
  await deleteDayResponsible(campId.value, id)
  await reloadSilent()
}

// ── Detail panel actions ────────────────────────────────────────────────────
// The day index of an activity's first schedule entry in the active period.
function activityDayIndex(a: CampActivity): number {
  const se = a.schedule_entries.find((s) => s.period_id === activePeriod.value?.id) ?? a.schedule_entries[0]
  return se ? Math.floor(se.period_offset / (24 * 60)) : 0
}
function closePanel() {
  editorOpen.value = false
  editMode.value = false
  editingActivity.value = null
  prefillSchedule.value = null
}
function openNew() {
  editingActivity.value = null
  prefillSchedule.value = null
  editMode.value = true        // new always starts in edit mode
  editorOpen.value = true
}
// Click an activity → open read-only view first, in the Programm split view.
function openActivity(a: CampActivity) {
  editingActivity.value = a
  prefillSchedule.value = null
  focusedDayIndex.value = activityDayIndex(a)
  editMode.value = false
  editorOpen.value = true
  // The detail pane lives in the Programm tab; switch there if elsewhere.
  if (tab.value !== 'programm') setTab('programm')
}
// Calendar drag-create → straight into edit mode with prefilled time.
function openCreateAt(schedule: { period_id: string; period_offset: number; length: number }, dayIndex: number) {
  editingActivity.value = null
  prefillSchedule.value = schedule
  focusedDayIndex.value = dayIndex
  editMode.value = true
  editorOpen.value = true
}
function startEdit() {
  editMode.value = true
}
// Closing the editor: for an existing activity, drop back to the read-only
// view; for a brand-new (unsaved) one, close the panel entirely.
function closeEditor() {
  if (editingActivity.value) {
    editMode.value = false
  } else {
    closePanel()
  }
}
function dayNav(delta: number) {
  const next = focusedDayIndex.value + delta
  if (next >= 0 && next < periodDays.value.length) focusedDayIndex.value = next
}
async function onSave(input: CampActivityInput) {
  if (editingActivity.value) await updateActivity(campId.value, editingActivity.value.id, input)
  else await createActivity(campId.value, input)
  closePanel()
  await reload()
}
// Autosave: persist in place, keep the editor open, refresh the graph quietly so
// the calendar/list reflect the change without disrupting the open editor.
async function onAutosave(id: string, input: CampActivityInput) {
  await updateActivity(campId.value, id, input)
  await reloadSilent()
}
async function onDelete(id: string) {
  if (!window.confirm('Aktivität löschen?')) return
  await deleteActivity(campId.value, id)
  closePanel()
  await reload()
}

// ── Drag to move (only when unlocked) ──────────────────────────────────────────
const dragState = ref<{ scheduleId: string; activity: CampActivity } | null>(null)
function onDragStart(p: PlacedEntry, e: DragEvent) {
  if (locked.value) { e.preventDefault(); return }
  dragState.value = { scheduleId: p.scheduleId, activity: p.activity }
  e.dataTransfer?.setData('text/plain', p.scheduleId)
}
async function onDropDay(dayIndex: number, e: DragEvent) {
  if (locked.value || !dragState.value || !activePeriod.value) return
  const rect = (e.currentTarget as HTMLElement).getBoundingClientRect()
  const minuteOfDay = Math.max(0, Math.round(((e.clientY - rect.top) / PX_PER_MIN + HOUR_START * 60) / 5) * 5)
  const newOffset = dayIndex * 24 * 60 + minuteOfDay
  const se = dragState.value.activity.schedule_entries.find((s) => s.id === dragState.value!.scheduleId)
  if (!se) return
  await updateScheduleEntry(campId.value, se.id, {
    period_id: activePeriod.value.id,
    period_offset: newOffset,
    length: se.length,
    left_fraction: se.left_fraction,
    width_fraction: se.width_fraction,
  })
  dragState.value = null
  await reload()
}

// ── Click-drag on empty slot to create (only when unlocked) ─────────────────────
const creating = ref<{ dayIndex: number; startMin: number; curMin: number } | null>(null)
function colMinuteFromEvent(e: MouseEvent): number {
  const rect = (e.currentTarget as HTMLElement).getBoundingClientRect()
  return Math.max(0, Math.round(((e.clientY - rect.top) / PX_PER_MIN + HOUR_START * 60) / 15) * 15)
}
function onSlotMouseDown(dayIndex: number, e: MouseEvent) {
  if (locked.value) return
  // Ignore clicks that originate on an existing event.
  if ((e.target as HTMLElement).closest('.cal-event')) return
  const m = colMinuteFromEvent(e)
  creating.value = { dayIndex, startMin: m, curMin: m + 30 }
}
function onSlotMouseMove(e: MouseEvent) {
  if (!creating.value) return
  creating.value.curMin = colMinuteFromEvent(e)
}
function ghostStyle() {
  if (!creating.value) return {}
  const a = Math.min(creating.value.startMin, creating.value.curMin)
  const b = Math.max(creating.value.startMin, creating.value.curMin)
  return {
    top: (a - HOUR_START * 60) * PX_PER_MIN + 'px',
    height: Math.max(15, (b - a)) * PX_PER_MIN + 'px',
  }
}
function onSlotMouseUp() {
  if (!creating.value || !activePeriod.value) { creating.value = null; return }
  const a = Math.min(creating.value.startMin, creating.value.curMin)
  const b = Math.max(creating.value.startMin, creating.value.curMin)
  const len = Math.max(15, b - a)
  const dayIndex = creating.value.dayIndex
  openCreateAt({
    period_id: activePeriod.value.id,
    period_offset: dayIndex * 24 * 60 + a,
    length: len,
  }, dayIndex)
  creating.value = null
}

// ── Geschichte (Story) aggregation ──────────────────────────────────────────────
interface StorySection { time: string; text: string }
function activityStory(a: CampActivity): StorySection[] {
  const node = a.content_nodes.find((n) => n.content_type === 'Storyboard')
  if (!node) return []
  const secs = (node.data as { sections?: { column1: string; column2: string }[] }).sections ?? []
  return secs.map((s) => ({ time: s.column1, text: s.column2 }))
}
const activitiesWithStory = computed(() =>
  (camp.value?.activities ?? []).filter((a) => activityStory(a).length > 0),
)

function activityText(a: CampActivity): string {
  const node = a.content_nodes.find((n) => n.content_type === 'SingleText')
  return node ? String((node.data as { html?: string }).html ?? '') : ''
}

// PDF / Drucken — open print-optimized program document.
function doPrint() {
  if (camp.value) printCamp(camp.value, resolveResponsibleName)
}
</script>

<template>
  <main class="main camp-detail">
    <ErrorAlert :error="error" />
    <p v-if="loading" class="loading">Laden...</p>

    <template v-if="!loading && camp">
      <!-- In-page tab strip — mobile only; desktop uses the global nav tabs. -->
      <div class="camp-tabstrip camp-tabstrip--mobile">
        <button
          v-for="t in CAMP_TABS"
          :key="t.key"
          class="ct-tab"
          :class="{ 'ct-tab--active': tab === t.key }"
          @click="setTab(t.key)"
        >{{ t.label }}</button>
      </div>

      <!-- ══ DASHBOARD ══════════════════════════════════════════════════════ -->
      <section v-if="tab === 'dashboard'" class="dash">
        <div class="dash-head">
          <div>
            <h1>{{ camp.title }}</h1>
            <p v-if="camp.motto" class="camp-motto">«{{ camp.motto }}»</p>
          </div>
          <span class="dash-badge" :style="{ background: camp.color }">{{ camp.kind || 'Lager' }}</span>
        </div>

        <div class="dash-stats">
          <div class="stat"><span class="stat-num">{{ camp.periods.length }}</span><span class="stat-lbl">Lagerabschnitte</span></div>
          <div class="stat"><span class="stat-num">{{ camp.activities.length }}</span><span class="stat-lbl">Aktivitäten</span></div>
          <div class="stat"><span class="stat-num">{{ camp.collaborations.length }}</span><span class="stat-lbl">Mitarbeitende</span></div>
          <div class="stat"><span class="stat-num">{{ camp.categories.length }}</span><span class="stat-lbl">Kategorien</span></div>
        </div>

        <h2 class="dash-sub">Programm-Übersicht</h2>
        <div v-for="p in camp.periods" :key="p.id" class="dash-period">
          <h3 class="dash-period-title">{{ p.description || p.start_date }} <span class="dash-period-dates">{{ p.start_date }} → {{ p.end_date }}</span></h3>
          <button
            v-for="a in camp.activities.filter(a => a.schedule_entries.some(s => s.period_id === p.id))"
            :key="a.id"
            class="dash-item"
            :style="{ borderLeftColor: categoryColor(a) }"
            @click="activePeriodId = p.id; openActivity(a)"
          >
            <span v-if="categoryShort(a)" class="dash-item-cat" :style="{ background: categoryColor(a) }">{{ categoryShort(a) }}</span>
            <span class="dash-item-title">{{ a.title }}</span>
            <span v-if="a.location" class="dash-item-loc"><MapPin :size="12" /> {{ a.location }}</span>
            <ResponsibleAvatars v-if="a.responsible?.length" :names="a.responsible" />
            <span v-if="responsibleAbbrs(a).length" class="dash-item-resp"><Users :size="12" /> {{ responsibleAbbrs(a).join(', ') }}</span>
          </button>
          <p v-if="!camp.activities.some(a => a.schedule_entries.some(s => s.period_id === p.id))" class="hint">Noch keine Aktivitäten.</p>
        </div>
        <p v-if="camp.periods.length === 0" class="hint">Lege zuerst einen Lagerabschnitt an (Admin → Perioden).</p>
      </section>

      <!-- ══ PROGRAMM (calendar + list) ════════════════════════════════════ -->
      <section v-else-if="tab === 'programm'">
        <div class="toolbar">
          <div class="period-tabs">
            <button
              v-for="p in periods" :key="p.id"
              class="period-tab" :class="{ 'period-tab--active': p.id === activePeriodId }"
              @click="activePeriodId = p.id"
            >{{ p.description || p.start_date }}</button>
            <span v-if="periods.length === 0" class="hint">Keine Perioden – lege im Admin-Tab eine Periode an.</span>
          </div>
          <div class="toolbar-right">
            <button class="lock-btn" :class="{ 'lock-btn--locked': locked }" @click="locked = !locked" :title="locked ? 'Kalender entsperren' : 'Kalender sperren'">
              <component :is="locked ? Lock : Unlock" :size="15" /> {{ locked ? 'Gesperrt' : 'Entsperrt' }}
            </button>
            <div class="view-toggle">
              <button class="vt-btn" :class="{ 'vt-btn--active': progMode === 'calendar' }" @click="progMode = 'calendar'"><CalendarDays :size="16" /> Kalender</button>
              <button class="vt-btn" :class="{ 'vt-btn--active': progMode === 'list' }" @click="progMode = 'list'"><List :size="16" /> Liste</button>
            </div>
            <button class="btn-ghost" @click="doPrint" title="Programm drucken / als PDF"><Printer :size="16" /></button>
            <button class="btn-primary" @click="openNew"><Plus :size="16" /> Aktivität</button>
          </div>
        </div>
        <p v-if="!locked" class="lock-hint">Entsperrt: Aktivitäten verschieben (Drag &amp; Drop) oder über leere Flächen aufziehen, um neue zu erstellen.</p>

        <!-- Category filter (legend + toggle) -->
        <div v-if="camp.categories.length" class="cat-filter">
          <button
            v-for="c in camp.categories"
            :key="c.id"
            class="cat-chip"
            :class="{ 'cat-chip--off': hiddenCats.has(c.id) }"
            :style="!hiddenCats.has(c.id) ? { background: c.color + '22', borderColor: c.color, color: c.color } : {}"
            @click="toggleCat(c.id)"
            :title="hiddenCats.has(c.id) ? 'Einblenden' : 'Ausblenden'"
          >
            <span class="cat-chip-dot" :style="{ background: c.color }" />
            {{ c.short_name }} · {{ c.name }}
          </button>
          <button v-if="anyCatHidden" class="cat-reset" @click="hiddenCats = new Set()">Alle anzeigen</button>
        </div>

        <!-- Split: calendar/list left, detail panel right when open -->
        <div class="prog-split" :class="{ 'prog-split--editing': editorOpen }">
        <div class="prog-split-main">
        <!-- Calendar (full period, or slim single day when a panel is open) -->
        <div v-if="progMode === 'calendar' && activePeriod" class="calendar" :class="{ 'calendar--slim': editorOpen }">
          <!-- Day switcher shown only in slim (panel-open) mode -->
          <div v-if="editorOpen" class="cal-daynav">
            <button class="cal-daynav-btn" :disabled="focusedDayIndex <= 0" @click="dayNav(-1)" aria-label="Vorheriger Tag">‹</button>
            <span class="cal-daynav-label">{{ periodDays[focusedDayIndex] ? dayLabel(periodDays[focusedDayIndex]) : '' }}</span>
            <button class="cal-daynav-btn" :disabled="focusedDayIndex >= periodDays.length - 1" @click="dayNav(1)" aria-label="Nächster Tag">›</button>
          </div>
          <div class="cal-scroll">
            <div class="cal-gutter">
              <div class="cal-corner" />
              <div v-for="h in hours" :key="h" class="cal-hour-label" :style="{ height: '60px' }">{{ String(h).padStart(2, '0') }}:00</div>
            </div>
            <div class="cal-days">
              <div class="cal-day-headers">
                <div v-for="i in visibleDayIndexes" :key="periodDays[i]" class="cal-day-header">
                  <span class="cal-day-date">{{ dayLabel(periodDays[i]) }}</span>
                  <!-- Tagesverantwortliche: avatars; editable when unlocked -->
                  <div class="cal-day-resp-row">
                    <ResponsibleAvatars v-if="dayRespNames(i).length" :names="dayRespNames(i)" />
                    <template v-if="!locked">
                      <button
                        v-for="dr in (dayRespByIndex[i] ?? [])" :key="dr.id"
                        class="cal-day-resp-del" title="Entfernen"
                        @click="removeDayResp(dr.id)"
                      >×</button>
                      <button class="cal-day-resp-add" title="Tagesverantwortliche hinzufügen" @click="openDayRespPicker(i)">＋</button>
                    </template>
                  </div>
                  <!-- inline user picker -->
                  <div v-if="dayRespEditing === i && !locked" class="cal-day-resp-picker">
                    <input
                      v-model="dayRespSearch" type="text" class="cal-day-resp-input"
                      placeholder="Person suchen oder eingeben…" autofocus
                      @keydown.enter.prevent="addDayResp(i, dayRespSearch.trim())"
                      @keydown.esc="dayRespEditing = null"
                    />
                    <div v-if="dayRespDropdownUsers.length" class="cal-day-resp-dropdown">
                      <button
                        v-for="u in dayRespDropdownUsers" :key="u.id"
                        class="cal-day-resp-option" @click="addDayResp(i, u.id)"
                      >{{ u.display_name }}</button>
                    </div>
                  </div>
                </div>
              </div>
              <div class="cal-grid">
                <div
                  v-for="dayIndex in visibleDayIndexes" :key="periodDays[dayIndex]"
                  class="cal-col"
                  :class="{ 'cal-col--editable': !locked }"
                  :style="{ height: (HOUR_END - HOUR_START) * 60 + 'px' }"
                  @dragover.prevent
                  @drop="onDropDay(dayIndex, $event)"
                  @mousedown="onSlotMouseDown(dayIndex, $event)"
                  @mousemove="onSlotMouseMove($event)"
                  @mouseup="onSlotMouseUp()"
                  @mouseleave="creating && creating.dayIndex === dayIndex ? (creating = null) : null"
                >
                  <div v-for="h in hours" :key="h" class="cal-hline" :style="{ top: (h - HOUR_START) * 60 + 'px' }" />
                  <!-- now indicator (only today, only if within visible band) -->
                  <div v-if="isToday(periodDays[dayIndex]) && nowTop !== null" class="cal-now" :style="{ top: nowTop + 'px' }">
                    <span class="cal-now-dot" />
                  </div>
                  <!-- create ghost -->
                  <div v-if="creating && creating.dayIndex === dayIndex" class="cal-ghost" :style="ghostStyle()" />
                  <!-- events -->
                  <div
                    v-for="p in placedByDay[dayIndex]" :key="p.scheduleId"
                    class="cal-event"
                    :class="{ 'cal-event--active': editingActivity && p.activity.id === editingActivity.id }"
                    :draggable="!locked"
                    :style="{
                      top: p.top + 'px', height: p.height + 'px',
                      left: 'calc(' + (p.left * 100) + '% + 2px)',
                      width: 'calc(' + (p.width * 100) + '% - 4px)',
                      background: categoryColor(p.activity) + '22',
                      borderLeftColor: categoryColor(p.activity),
                      cursor: locked ? 'pointer' : 'grab',
                    }"
                    @click="openActivity(p.activity)"
                    @dragstart="onDragStart(p, $event)"
                  >
                    <span class="cal-event-cat" :style="{ color: categoryColor(p.activity) }">{{ categoryShort(p.activity) }}</span>
                    <span class="cal-event-title">{{ p.activity.title }}</span>
                    <span v-if="responsibleAbbrs(p.activity).length" class="cal-event-resp">{{ responsibleAbbrs(p.activity).join(', ') }}</span>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>

        <!-- List -->
        <div v-else-if="progMode === 'list'" class="list-view">
          <div v-for="grp in listByDay" :key="grp.day" class="list-day">
            <h3 class="list-day-title">
              {{ dayLabel(grp.day) }}
              <span v-if="grp.count" class="list-day-sum">{{ grp.count }} Punkte · {{ fmtDuration(grp.totalMin) }}</span>
              <ResponsibleAvatars v-if="dayRespNames(listByDay.indexOf(grp)).length" :names="dayRespNames(listByDay.indexOf(grp))" />
            </h3>
            <p v-if="grp.rows.length === 0" class="hint list-empty">Keine Aktivitäten.</p>
            <div v-for="row in grp.rows" :key="row.activity.id + row.minuteOfDay" class="list-entry">
              <div
                class="list-row" :style="{ borderLeftColor: categoryColor(row.activity) }"
                @click="openActivity(row.activity)"
              >
                <div class="list-time"><Clock :size="13" /> {{ fmtMin(row.minuteOfDay) }}–{{ fmtMin(row.minuteOfDay + row.length) }}</div>
                <div class="list-main">
                  <span v-if="categoryShort(row.activity)" class="list-cat" :style="{ background: categoryColor(row.activity) }">{{ categoryShort(row.activity) }}</span>
                  <span class="list-title">{{ row.activity.title }}</span>
                </div>
                <div class="list-meta">
                  <span v-if="row.activity.location" class="list-loc"><MapPin :size="12" /> {{ row.activity.location }}</span>
                  <ResponsibleAvatars v-if="row.activity.responsible?.length" :names="row.activity.responsible" />
                  <span v-if="responsibleAbbrs(row.activity).length" class="list-resp"><Users :size="12" /> {{ responsibleAbbrs(row.activity).join(', ') }}</span>
                </div>
              </div>
              <!-- Programmpunkte timeline (same as activity) -->
              <div v-if="row.activity.programs?.length" class="prog-timeline">
                <div v-for="(prog, pi) in row.activity.programs" :key="prog.id || pi" class="prog-tl-item">
                  <span class="prog-tl-dot" :style="{ background: categoryColor(row.activity) }" />
                  <div class="prog-tl-body">
                    <div class="prog-tl-head">
                      <span class="prog-tl-time">{{ programClock(row.minuteOfDay, row.activity.programs, pi) }}</span>
                      <span class="prog-tl-title">{{ prog.title || '—' }}</span>
                      <ResponsibleAvatars v-if="prog.responsible?.length" :names="prog.responsible" />
                    </div>
                    <div v-if="prog.description" class="prog-tl-desc" v-html="prog.description" />
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
        </div><!-- /prog-split-main -->

        <!-- Detail pane (detail-on-the-right): view first, edit on demand -->
        <aside v-if="editorOpen && camp" class="prog-split-aside">
          <CampActivityView
            v-if="!editMode && editingActivity"
            :activity="editingActivity"
            :categories="camp.categories"
            :number-label="categoryShort(editingActivity)"
            @edit="startEdit"
            @close="closePanel"
          />
          <CampActivityEditor
            v-else
            :activity="editingActivity"
            :categories="camp.categories"
            :collaborations="camp.collaborations"
            :periods="camp.periods"
            :default-period-id="activePeriodId"
            :prefill-schedule="prefillSchedule"
            inline
            @save="onSave"
            @autosave="onAutosave"
            @delete="onDelete"
            @close="closeEditor"
          />
        </aside>
        </div><!-- /prog-split -->
      </section>

      <!-- ══ GESCHICHTE (Story) ════════════════════════════════════════════ -->
      <section v-else-if="tab === 'geschichte'" class="story-view">
        <h1>Geschichte</h1>
        <p class="hint">Erzählstrang über alle Programmpunkte mit Storyboard.</p>
        <div v-for="a in activitiesWithStory" :key="a.id" class="story-card" :style="{ borderLeftColor: categoryColor(a) }">
          <div class="story-card-head">
            <BookOpen :size="16" :style="{ color: categoryColor(a) }" />
            <strong>{{ a.title }}</strong>
            <button class="btn-link" @click="openActivity(a)">Bearbeiten</button>
          </div>
          <div v-for="(s, i) in activityStory(a)" :key="i" class="story-line">
            <span class="story-time">{{ s.time }}</span>
            <span class="story-text" v-html="s.text" />
          </div>
        </div>
        <p v-if="activitiesWithStory.length === 0" class="hint">Noch keine Geschichte erfasst. Füge einem Programmpunkt das Element «Geschichte» hinzu.</p>
      </section>

      <!-- ══ MATERIAL ══════════════════════════════════════════════════════ -->
      <section v-else-if="tab === 'material'" class="material-view">
        <div class="mv-head">
          <h1>Material</h1>
          <button class="btn-ghost" @click="showRf = true"><Package :size="16" /> Materiallisten verwalten</button>
        </div>
        <div v-for="l in camp.material_lists" :key="l.id" class="mv-list">
          <h3 class="mv-list-title"><Package :size="15" /> {{ l.name }}</h3>
          <table v-if="l.items.length" class="mv-table">
            <thead><tr><th>Anzahl</th><th>Einheit</th><th>Artikel</th></tr></thead>
            <tbody>
              <tr v-for="it in l.items" :key="it.id">
                <td>{{ it.quantity ?? '' }}</td><td>{{ it.unit }}</td><td>{{ it.article_name }}</td>
              </tr>
            </tbody>
          </table>
          <p v-else class="hint">Keine Artikel.</p>
        </div>
        <p v-if="camp.material_lists.length === 0" class="hint">Noch keine Materiallisten. Lege welche im Verwaltungs-Dialog an.</p>
      </section>

      <!-- ══ ADMIN ═════════════════════════════════════════════════════════ -->
      <section v-else-if="tab === 'admin'" class="admin-view">
        <h1>Admin</h1>
        <div class="admin-grid">
          <button class="admin-card" @click="showRf = true"><Users :size="22" /><span>Mitarbeitende (RF-Liste)</span></button>
          <button class="admin-card" @click="showRf = true"><Tag :size="22" /><span>Kategorien</span></button>
          <button class="admin-card" @click="showRf = true"><CalendarDays :size="22" /><span>Lagerabschnitte</span></button>
          <button class="admin-card" @click="showRf = true"><Package :size="22" /><span>Materiallisten</span></button>
          <button class="admin-card" @click="doPrint"><Printer :size="22" /><span>PDF / Drucken</span></button>
        </div>
        <div class="admin-info">
          <h3>Lagerinfos</h3>
          <dl>
            <div><dt>Titel</dt><dd>{{ camp.title }}</dd></div>
            <div><dt>Motto</dt><dd>{{ camp.motto || '–' }}</dd></div>
            <div><dt>Art</dt><dd>{{ camp.kind || '–' }}</dd></div>
            <div><dt>Organisator</dt><dd>{{ camp.organizer || '–' }}</dd></div>
            <div><dt>Ort</dt><dd>{{ camp.address_city || '–' }}</dd></div>
            <div><dt>Abteilung</dt><dd>{{ camp.department || '–' }}</dd></div>
          </dl>
        </div>
      </section>
    </template>

    <CampRfListe
      v-if="showRf && camp"
      :camp="camp"
      @close="showRf = false"
      @changed="reload"
    />
  </main>
</template>

<style scoped>
.camp-detail { padding-top: 12px; }
.hint { font-size: 0.82rem; color: var(--text-subtle); }
h1 { font-size: 1.4rem; font-weight: 800; color: var(--text-primary); margin: 0; }

/* Tab strip */
.camp-tabstrip {
  display: flex;
  gap: 4px;
  border-bottom: 1px solid var(--border);
  margin-bottom: 18px;
  overflow-x: auto;
}
/* Desktop shows the global nav tabs; hide the in-page strip to avoid doubling. */
@media (min-width: 768px) {
  .camp-tabstrip--mobile { display: none; }
}
.ct-tab {
  padding: 10px 16px;
  border: none;
  background: transparent;
  font-size: 0.9rem;
  font-weight: 600;
  color: var(--text-muted);
  cursor: pointer;
  border-bottom: 2px solid transparent;
  white-space: nowrap;
}
.ct-tab:hover { color: var(--accent); }
.ct-tab--active { color: var(--accent); border-bottom-color: var(--accent); }

/* Dashboard */
.dash-head { display: flex; justify-content: space-between; align-items: flex-start; gap: 12px; }
.camp-motto { font-style: italic; color: var(--text-muted); margin: 4px 0 0; }
.dash-badge { color: #fff; font-size: 0.78rem; font-weight: 700; padding: 5px 12px; border-radius: 999px; }
.dash-stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap: 12px; margin: 18px 0 24px; }
.stat { background: var(--bg-surface); border: 1px solid var(--border); border-radius: 12px; padding: 16px; text-align: center; }
.stat-num { display: block; font-size: 1.7rem; font-weight: 800; color: var(--accent); }
.stat-lbl { font-size: 0.78rem; color: var(--text-muted); }
.dash-sub { font-size: 1.05rem; font-weight: 700; color: var(--text-primary); margin: 0 0 12px; }
.dash-period { margin-bottom: 18px; }
.dash-period-title { font-size: 0.95rem; font-weight: 700; color: var(--text-secondary); margin: 0 0 8px; }
.dash-period-dates { font-weight: 500; font-size: 0.8rem; color: var(--text-subtle); margin-left: 8px; }
.dash-item {
  display: flex; align-items: center; gap: 10px; width: 100%;
  padding: 9px 12px; border: 1px solid var(--border); border-left: 4px solid var(--accent);
  border-radius: 8px; background: var(--bg-surface); margin-bottom: 6px; cursor: pointer; text-align: left;
}
.dash-item:hover { box-shadow: 0 2px 10px rgba(0,0,0,0.06); }
.dash-item-cat { font-size: 0.68rem; font-weight: 800; color: #fff; padding: 2px 7px; border-radius: 5px; }
.dash-item-title { font-weight: 600; color: var(--text-primary); flex: 1; }
.dash-item-loc, .dash-item-resp { display: inline-flex; align-items: center; gap: 3px; font-size: 0.78rem; color: var(--text-muted); }

/* Toolbar */
.toolbar { display: flex; flex-wrap: wrap; gap: 12px; justify-content: space-between; align-items: center; margin-bottom: 10px; }
.toolbar-right { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
.period-tabs { display: flex; flex-wrap: wrap; gap: 6px; }
.period-tab { padding: 6px 14px; border-radius: 8px; border: 1px solid var(--border); background: var(--bg-surface); color: var(--text-secondary); font-size: 0.85rem; font-weight: 600; cursor: pointer; }
.period-tab--active { background: var(--accent); color: #fff; border-color: var(--accent); }
.lock-btn {
  display: inline-flex; align-items: center; gap: 5px;
  padding: 7px 12px; border-radius: 8px; font-size: 0.82rem; font-weight: 600; cursor: pointer;
  border: 1px solid var(--border-strong); background: var(--bg-surface); color: var(--text-secondary);
}
.lock-btn--locked { background: var(--warning-bg); color: var(--warning-color); border-color: var(--warning-color); }
.lock-hint { font-size: 0.8rem; color: var(--accent); margin: 0 0 10px; }
.cat-filter { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 12px; }
.cat-chip {
  display: inline-flex; align-items: center; gap: 6px;
  padding: 4px 11px; border-radius: 999px; cursor: pointer;
  border: 1px solid var(--border-strong); background: var(--bg-surface);
  font-size: 0.8rem; font-weight: 600; color: var(--text-secondary);
  transition: opacity 0.12s, filter 0.12s;
}
.cat-chip--off { opacity: 0.45; text-decoration: line-through; }
.cat-chip-dot { width: 9px; height: 9px; border-radius: 50%; flex-shrink: 0; }
.cat-reset { background: none; border: none; color: var(--accent); font-weight: 600; font-size: 0.8rem; cursor: pointer; }
.view-toggle { display: flex; gap: 4px; background: var(--bg-hover); padding: 3px; border-radius: 9px; }
.vt-btn { display: inline-flex; align-items: center; gap: 5px; padding: 6px 12px; border: none; background: transparent; border-radius: 7px; font-size: 0.85rem; font-weight: 600; color: var(--text-muted); cursor: pointer; }
.vt-btn--active { background: var(--bg-surface); color: var(--accent); box-shadow: 0 1px 3px rgba(0,0,0,0.1); }

/* Split view: calendar/list left, detail panel right */
.prog-split { display: flex; gap: 16px; align-items: flex-start; }
.prog-split-main { flex: 1; min-width: 0; }
/* When a panel is open the slim calendar is fixed width and the panel grows. */
.prog-split--editing .prog-split-main { flex: 0 0 auto; }
.prog-split--editing .prog-split-aside { flex: 1 1 auto; }
.prog-split-aside {
  flex: 0 0 clamp(380px, 42%, 560px);
  position: sticky;
  top: 12px;
  max-height: calc(100vh - 90px);
  display: flex;
}
.prog-split-aside > * { width: 100%; }
@media (max-width: 900px) {
  /* Stack on narrow screens; editor drops below the calendar. */
  .prog-split { flex-direction: column; }
  .prog-split-aside { flex-basis: auto; width: 100%; position: static; max-height: none; }
}

/* Calendar */
.calendar { border: 1px solid var(--border); border-radius: 12px; background: var(--bg-surface); overflow: hidden; }
/* Slim single-day calendar shown while the detail panel is open. */
.calendar--slim { flex: 0 0 auto; }
.calendar--slim .cal-day-header,
.calendar--slim .cal-col { min-width: 160px; }
.cal-daynav {
  display: flex; align-items: center; justify-content: space-between; gap: 8px;
  padding: 8px 10px; border-bottom: 1px solid var(--border); background: var(--bg-elevated);
}
.cal-daynav-label { font-size: 0.86rem; font-weight: 700; color: var(--text-primary); }
.cal-daynav-btn {
  width: 30px; height: 30px; border-radius: 7px; border: 1px solid var(--border-strong);
  background: var(--bg-surface); color: var(--text-secondary); cursor: pointer; font-size: 1.1rem; line-height: 1;
}
.cal-daynav-btn:disabled { opacity: 0.4; cursor: default; }
.cal-daynav-btn:hover:not(:disabled) { background: var(--bg-hover); color: var(--accent); }
.cal-event--active { outline: 2px solid var(--accent); outline-offset: -1px; z-index: 5; }
.cal-scroll { display: flex; overflow: auto; max-height: 70vh; }
.cal-gutter { flex-shrink: 0; width: 52px; position: sticky; left: 0; background: var(--bg-surface); z-index: 2; }
.cal-corner { min-height: 46px; border-bottom: 1px solid var(--border); }
.cal-hour-label { font-size: 0.7rem; color: var(--text-subtle); text-align: right; padding-right: 6px; box-sizing: border-box; transform: translateY(-6px); }
.cal-days { flex: 1; min-width: 0; }
.cal-day-headers { display: flex; position: sticky; top: 0; z-index: 1; background: var(--bg-surface); }
.cal-day-header { flex: 1; min-width: 120px; min-height: 46px; position: relative; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 3px; padding: 5px 4px; font-size: 0.8rem; font-weight: 700; color: var(--text-secondary); border-bottom: 1px solid var(--border); border-left: 1px solid var(--border); }
.cal-day-date { font-size: 0.8rem; }
.cal-day-resp-row { display: inline-flex; align-items: center; gap: 4px; flex-wrap: wrap; justify-content: center; }
.cal-day-resp-del {
  width: 16px; height: 16px; border-radius: 50%; border: none; cursor: pointer;
  background: var(--btn-danger-bg); color: var(--btn-danger-color); font-size: 0.7rem; line-height: 1; padding: 0;
}
.cal-day-resp-add {
  width: 18px; height: 18px; border-radius: 50%; cursor: pointer; padding: 0;
  border: 1px dashed var(--border-strong); background: var(--bg-surface); color: var(--text-muted); font-size: 0.8rem; line-height: 1;
}
.cal-day-resp-add:hover { border-color: var(--accent); color: var(--accent); }
.cal-day-resp-picker { position: absolute; top: 100%; left: 4px; right: 4px; z-index: 20; }
.cal-day-resp-input { width: 100%; padding: 5px 8px; border: 1px solid var(--accent); border-radius: 6px; font-size: 0.78rem; background: var(--input-bg, var(--bg-surface)); color: var(--text-primary); }
.cal-day-resp-dropdown { margin-top: 2px; background: var(--bg-surface); border: 1px solid var(--border); border-radius: 6px; box-shadow: 0 4px 16px rgba(0,0,0,0.12); max-height: 180px; overflow-y: auto; }
.cal-day-resp-option { display: block; width: 100%; text-align: left; padding: 6px 10px; border: none; background: transparent; cursor: pointer; font-size: 0.8rem; font-weight: 500; color: var(--text-secondary); }
.cal-day-resp-option:hover { background: var(--bg-hover); color: var(--accent); }
.cal-grid { display: flex; }
.cal-col { flex: 1; min-width: 120px; position: relative; border-left: 1px solid var(--border); }
.cal-col--editable { cursor: crosshair; }
.cal-hline { position: absolute; left: 0; right: 0; border-top: 1px solid var(--border); opacity: 0.6; }
.cal-ghost { position: absolute; left: 2px; right: 2px; background: var(--accent-bg-hover); border: 1px dashed var(--accent); border-radius: 5px; opacity: 0.7; }
.cal-now { position: absolute; left: 0; right: 0; height: 0; border-top: 2px solid #ef4444; z-index: 4; pointer-events: none; }
.cal-now-dot { position: absolute; left: -4px; top: -4px; width: 8px; height: 8px; border-radius: 50%; background: #ef4444; }
.cal-event { position: absolute; border-left: 3px solid var(--accent); border-radius: 5px; padding: 3px 6px; overflow: hidden; font-size: 0.72rem; display: flex; flex-direction: column; gap: 1px; box-shadow: 0 1px 2px rgba(0,0,0,0.08); }
.cal-event:hover { filter: brightness(0.96); z-index: 3; }
.cal-event-cat { font-weight: 800; font-size: 0.64rem; }
.cal-event-title { font-weight: 600; color: var(--text-primary); line-height: 1.1; }
.cal-event-resp { color: var(--text-muted); font-size: 0.66rem; }

/* List */
.list-view { display: flex; flex-direction: column; gap: 18px; }
.list-day-title { font-size: 0.95rem; font-weight: 700; color: var(--text-primary); margin: 0 0 8px; padding-bottom: 6px; border-bottom: 2px solid var(--border); }
.list-day-resp { font-weight: 600; font-size: 0.8rem; color: var(--accent); }
.list-day-sum { font-weight: 500; font-size: 0.78rem; color: var(--text-muted); margin-left: 8px; }
.list-empty { padding: 4px 0 8px; }
.list-row { display: flex; flex-wrap: wrap; align-items: center; gap: 12px; padding: 10px 14px; border: 1px solid var(--border); border-left: 4px solid var(--accent); border-radius: 8px; background: var(--bg-surface); margin-bottom: 8px; cursor: pointer; }
.list-row:hover { box-shadow: 0 2px 10px rgba(0,0,0,0.06); }
.list-time { display: inline-flex; align-items: center; gap: 4px; font-size: 0.8rem; font-weight: 600; color: var(--text-muted); min-width: 120px; }
.list-main { display: flex; align-items: center; gap: 8px; flex: 1; min-width: 0; }
.list-cat { font-size: 0.68rem; font-weight: 800; color: #fff; padding: 2px 7px; border-radius: 5px; }
.list-title { font-weight: 600; color: var(--text-primary); }
.list-meta { display: flex; gap: 12px; flex-wrap: wrap; }
.list-loc, .list-resp { display: inline-flex; align-items: center; gap: 3px; font-size: 0.78rem; color: var(--text-muted); }
.list-entry { margin-bottom: 8px; }
.prog-timeline { margin: 2px 0 4px 18px; padding-left: 14px; border-left: 2px solid var(--border); display: flex; flex-direction: column; gap: 6px; }
.prog-tl-item { display: flex; align-items: flex-start; gap: 8px; position: relative; }
.prog-tl-dot { width: 9px; height: 9px; border-radius: 50%; margin-top: 4px; margin-left: -19px; flex-shrink: 0; box-shadow: 0 0 0 2px var(--bg-base); }
.prog-tl-body { flex: 1; min-width: 0; }
.prog-tl-head { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.prog-tl-time { font-size: 0.74rem; font-weight: 700; color: var(--program-time-color, var(--accent)); background: var(--program-time-bg, var(--accent-bg)); padding: 1px 7px; border-radius: 999px; }
.prog-tl-title { font-size: 0.84rem; font-weight: 600; color: var(--text-primary); }
.prog-tl-desc { font-size: 0.8rem; color: var(--text-muted); margin-top: 2px; }

/* Story */
.story-card { border: 1px solid var(--border); border-left: 4px solid var(--accent); border-radius: 10px; padding: 14px; margin: 12px 0; background: var(--bg-surface); }
.story-card-head { display: flex; align-items: center; gap: 8px; margin-bottom: 10px; }
.story-card-head strong { flex: 1; color: var(--text-primary); }
.story-line { display: grid; grid-template-columns: 90px 1fr; gap: 12px; padding: 6px 0; border-top: 1px solid var(--border); }
.story-time { font-weight: 700; color: var(--text-muted); font-size: 0.85rem; }
.story-text { color: var(--text-secondary); font-size: 0.9rem; }
.btn-link { background: none; border: none; color: var(--accent); font-weight: 600; cursor: pointer; font-size: 0.82rem; }

/* Material */
.mv-head { display: flex; justify-content: space-between; align-items: center; gap: 12px; margin-bottom: 16px; }
.mv-list { border: 1px solid var(--border); border-radius: 10px; padding: 14px; margin-bottom: 14px; background: var(--bg-surface); }
.mv-list-title { display: flex; align-items: center; gap: 6px; font-size: 0.95rem; font-weight: 700; color: var(--text-primary); margin: 0 0 10px; }
.mv-table { width: 100%; border-collapse: collapse; font-size: 0.88rem; }
.mv-table th { text-align: left; font-size: 0.72rem; text-transform: uppercase; color: var(--text-subtle); padding: 4px 8px; border-bottom: 1px solid var(--border); }
.mv-table td { padding: 6px 8px; border-bottom: 1px solid var(--border); color: var(--text-secondary); }

/* Admin */
.admin-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 12px; margin: 16px 0 24px; }
.admin-card { display: flex; flex-direction: column; align-items: center; gap: 10px; padding: 22px 16px; border: 1px solid var(--border); border-radius: 12px; background: var(--bg-surface); color: var(--text-secondary); font-weight: 600; font-size: 0.88rem; cursor: pointer; text-align: center; }
.admin-card:hover { border-color: var(--accent); color: var(--accent); box-shadow: 0 4px 14px rgba(0,0,0,0.06); }
.admin-info { border: 1px solid var(--border); border-radius: 12px; padding: 16px 18px; background: var(--bg-surface); }
.admin-info h3 { margin: 0 0 12px; font-size: 1rem; color: var(--text-primary); }
.admin-info dl { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 10px; margin: 0; }
.admin-info dt { font-size: 0.72rem; text-transform: uppercase; color: var(--text-subtle); font-weight: 700; }
.admin-info dd { margin: 2px 0 0; color: var(--text-primary); font-weight: 600; }

/* Shared buttons */
.btn-ghost { display: inline-flex; align-items: center; gap: 5px; background: transparent; border: 1px solid var(--border-strong); color: var(--text-secondary); padding: 8px 14px; border-radius: 8px; font-weight: 600; font-size: 0.88rem; cursor: pointer; }
.btn-ghost:hover { background: var(--bg-hover); }
</style>
