<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useActivities } from '../composables/useActivities'
import type { Activity } from '../types'
import ErrorAlert from '../components/ErrorAlert.vue'

const { activities, loading, error, fetchActivities } = useActivities()

function formatIsoDate(date: Date): string {
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')}`
}

function addMonths(date: Date, months: number): Date {
  const next = new Date(date)
  next.setMonth(next.getMonth() + months)
  return next
}

const today = new Date()
const todayIso = formatIsoDate(today)
const defaultFromIso = formatIsoDate(addMonths(today, -1))

const rangeFrom = ref(defaultFromIso)
const rangeTo = ref(todayIso)
const selectedStage = ref('all')

onMounted(() => {
  document.title = 'Statistik – DPWeb'
  void fetchActivities()
})

const normalizedRange = computed(() => {
  const safeTo = rangeTo.value && rangeTo.value <= todayIso ? rangeTo.value : todayIso
  const safeFrom = rangeFrom.value && rangeFrom.value <= safeTo ? rangeFrom.value : ''
  return { from: safeFrom, to: safeTo }
})

const pastActivities = computed(() => {
  return activities.value.filter((activity) => activity.date <= todayIso)
})

const stageOptions = computed(() => {
  const uniqueStages = new Set(
    pastActivities.value
      .map((activity) => activity.department?.trim())
      .filter((department): department is string => !!department),
  )

  return Array.from(uniqueStages).sort((a, b) => a.localeCompare(b, 'de'))
})

const filteredActivities = computed(() => {
  const { from, to } = normalizedRange.value
  return pastActivities.value.filter((activity) => {
    if (from && activity.date < from) return false
    if (activity.date > to) return false
    if (selectedStage.value !== 'all' && (activity.department ?? '') !== selectedStage.value) return false
    return true
  })
})

function parseMinutes(time: string): number | null {
  const [hours, minutes] = time.split(':').map(Number)
  if (!Number.isFinite(hours) || !Number.isFinite(minutes)) return null
  return hours * 60 + minutes
}

function durationMinutes(activity: Activity): number | null {
  const start = parseMinutes(activity.start_time)
  const end = parseMinutes(activity.end_time)
  if (start === null || end === null || end < start) return null
  return end - start
}

function formatDuration(totalMinutes: number | null): string {
  if (totalMinutes === null || !Number.isFinite(totalMinutes)) return '—'
  const hours = Math.floor(totalMinutes / 60)
  const minutes = Math.round(totalMinutes % 60)
  if (!hours) return `${minutes} Min.`
  if (!minutes) return `${hours} Std.`
  return `${hours} Std. ${minutes} Min.`
}

function barWidth(value: number, max: number): string {
  if (max <= 0) return '0%'
  return `${Math.max(8, Math.round((value / max) * 100))}%`
}

function weekdayLabel(isoDate: string): string {
  const date = new Date(`${isoDate}T00:00:00`)
  return new Intl.DateTimeFormat('de-DE', { weekday: 'long' }).format(date)
}

function monthLabel(isoDate: string): string {
  const date = new Date(`${isoDate}T00:00:00`)
  return new Intl.DateTimeFormat('de-DE', { month: 'short', year: '2-digit' }).format(date)
}

function timeBucket(activity: Activity): string {
  const start = parseMinutes(activity.start_time)
  if (start === null) return 'Unbekannt'
  if (start < 9 * 60) return 'Früh'
  if (start < 12 * 60) return 'Vormittag'
  if (start < 17 * 60) return 'Nachmittag'
  if (start < 20 * 60) return 'Abend'
  return 'Nacht'
}

function buildCountRows(entries: string[]): Array<{ label: string; value: number }> {
  const counts = new Map<string, number>()
  for (const entry of entries) {
    const label = entry.trim() || 'Ohne Angabe'
    counts.set(label, (counts.get(label) ?? 0) + 1)
  }
  return Array.from(counts.entries())
    .map(([label, value]) => ({ label, value }))
    .sort((a, b) => b.value - a.value || a.label.localeCompare(b.label, 'de'))
}

const departmentRows = computed(() => buildCountRows(filteredActivities.value.map((activity) => activity.department ?? 'Ohne Abteilung')))
const locationRows = computed(() => buildCountRows(filteredActivities.value.map((activity) => activity.location || 'Ohne Ort')).slice(0, 8))
const responsibleRows = computed(() => buildCountRows(filteredActivities.value.flatMap((activity) => activity.responsible)).slice(0, 10))
const weekdayOrder = ['Montag', 'Dienstag', 'Mittwoch', 'Donnerstag', 'Freitag', 'Samstag', 'Sonntag']
const weekdayRows = computed(() => {
  const rows = buildCountRows(filteredActivities.value.map((activity) => weekdayLabel(activity.date)))
  return weekdayOrder
    .map((label) => rows.find((row) => row.label === label) ?? { label, value: 0 })
})
const timeBucketOrder = ['Früh', 'Vormittag', 'Nachmittag', 'Abend', 'Nacht', 'Unbekannt']
const timeBucketRows = computed(() => {
  const rows = buildCountRows(filteredActivities.value.map((activity) => timeBucket(activity)))
  return timeBucketOrder
    .map((label) => rows.find((row) => row.label === label) ?? { label, value: 0 })
})
const monthRows = computed(() => {
  const sorted = [...filteredActivities.value].sort((a, b) => a.date.localeCompare(b.date))
  const rows = buildCountRows(sorted.map((activity) => monthLabel(activity.date)))
  return rows.reverse()
})

const durationValues = computed(() => filteredActivities.value.map(durationMinutes).filter((value): value is number => value !== null))
const participantValues = computed(() => filteredActivities.value
  .map((activity) => activity.planned_participants_estimate)
  .filter((value): value is number => typeof value === 'number'))

const statsSummary = computed(() => {
  const totalActivities = filteredActivities.value.length
  const totalDuration = durationValues.value.reduce((sum, value) => sum + value, 0)
  const avgDuration = durationValues.value.length ? Math.round(totalDuration / durationValues.value.length) : null
  const avgParticipants = participantValues.value.length
    ? Math.round(participantValues.value.reduce((sum, value) => sum + value, 0) / participantValues.value.length)
    : null
  const uniqueLocations = new Set(filteredActivities.value.map((activity) => activity.location).filter(Boolean)).size
  const uniqueResponsible = new Set(filteredActivities.value.flatMap((activity) => activity.responsible)).size
  const withSiko = filteredActivities.value.filter((activity) => !!activity.siko_text?.trim()).length
  const withBadWeather = filteredActivities.value.filter((activity) => !!activity.bad_weather_info?.trim()).length
  return {
    totalActivities,
    totalDuration,
    avgDuration,
    avgParticipants,
    uniqueLocations,
    uniqueResponsible,
    withSiko,
    withBadWeather,
  }
})

const sectionMax = (rows: Array<{ value: number }>) => rows.reduce((max, row) => Math.max(max, row.value), 0)

function applyPreset(days: number) {
  rangeTo.value = todayIso
  const nextFrom = new Date(today)
  nextFrom.setDate(nextFrom.getDate() - days)
  rangeFrom.value = formatIsoDate(nextFrom)
}
</script>

<template>
  <header class="stats-header">
    <div>
      <h1>Statistik</h1>
      <p class="stats-header__text">Übersicht über vergangene Aktivitäten im gewählten Zeitraum.</p>
    </div>
  </header>

  <main class="stats-page">
    <section class="stats-toolbar">
      <div class="stats-toolbar__dates">
        <label class="stats-toolbar__field">
          <span>Von</span>
          <input v-model="rangeFrom" type="date" :max="todayIso" />
        </label>
        <label class="stats-toolbar__field">
          <span>Bis</span>
          <input v-model="rangeTo" type="date" :max="todayIso" />
        </label>
        <label class="stats-toolbar__field">
          <span>Stufe</span>
          <select v-model="selectedStage">
            <option value="all">Alle Stufen</option>
            <option v-for="stage in stageOptions" :key="stage" :value="stage">{{ stage }}</option>
          </select>
        </label>
      </div>
      <div class="stats-toolbar__presets">
        <button type="button" class="stats-preset" @click="applyPreset(30)">30 Tage</button>
        <button type="button" class="stats-preset" @click="applyPreset(90)">90 Tage</button>
        <button type="button" class="stats-preset" @click="applyPreset(365)">1 Jahr</button>
      </div>
    </section>

    <ErrorAlert :error="error" />
    <p v-if="loading" class="stats-loading">Statistiken werden geladen…</p>

    <template v-else>
      <section class="stats-summary">
        <article class="stats-card">
          <p class="stats-card__label">Aktivitäten</p>
          <p class="stats-card__value">{{ statsSummary.totalActivities }}</p>
        </article>
        <article class="stats-card">
          <p class="stats-card__label">Ø Dauer</p>
          <p class="stats-card__value">{{ formatDuration(statsSummary.avgDuration) }}</p>
        </article>
        <article class="stats-card">
          <p class="stats-card__label">Durchschnittliche Teilnehmerzahl</p>
          <p class="stats-card__value">{{ statsSummary.avgParticipants ?? '—' }}</p>
        </article>
        <article class="stats-card">
          <p class="stats-card__label">Total Dauer</p>
          <p class="stats-card__value">{{ formatDuration(statsSummary.totalDuration) }}</p>
        </article>
        <article class="stats-card">
          <p class="stats-card__label">Unterschiedliche Orte</p>
          <p class="stats-card__value">{{ statsSummary.uniqueLocations }}</p>
        </article>
        <article class="stats-card">
          <p class="stats-card__label">Unterschiedliche Verantwortliche</p>
          <p class="stats-card__value">{{ statsSummary.uniqueResponsible }}</p>
        </article>
        <article class="stats-card stats-card--soft">
          <p class="stats-card__label">Mit Sicherheitskonzept</p>
          <p class="stats-card__value">{{ statsSummary.withSiko }}</p>
        </article>
        <article class="stats-card stats-card--soft">
          <p class="stats-card__label">Mit Schlechtwetter-Info</p>
          <p class="stats-card__value">{{ statsSummary.withBadWeather }}</p>
        </article>
      </section>

      <section v-if="!filteredActivities.length" class="stats-empty">
        Für diesen Zeitraum gibt es keine vergangenen Aktivitäten.
      </section>

      <section v-else class="stats-grid">
        <article class="stats-panel">
          <div class="stats-panel__header">
            <h2>Nach Abteilung</h2>
            <span>{{ filteredActivities.length }} Aktivitäten</span>
          </div>
          <div class="stats-bars">
            <div v-for="row in departmentRows" :key="row.label" class="stats-bar-row">
              <div class="stats-bar-row__top"><span>{{ row.label }}</span><strong>{{ row.value }}</strong></div>
              <div class="stats-bar"><div class="stats-bar__fill" :style="{ width: barWidth(row.value, sectionMax(departmentRows)) }" /></div>
            </div>
          </div>
        </article>

        <article class="stats-panel">
          <div class="stats-panel__header">
            <h2>Wochentage</h2>
            <span>Verteilung</span>
          </div>
          <div class="stats-bars">
            <div v-for="row in weekdayRows" :key="row.label" class="stats-bar-row">
              <div class="stats-bar-row__top"><span>{{ row.label }}</span><strong>{{ row.value }}</strong></div>
              <div class="stats-bar"><div class="stats-bar__fill stats-bar__fill--teal" :style="{ width: barWidth(row.value, sectionMax(weekdayRows)) }" /></div>
            </div>
          </div>
        </article>

        <article class="stats-panel">
          <div class="stats-panel__header">
            <h2>Tageszeit</h2>
            <span>Startzeiten</span>
          </div>
          <div class="stats-bars">
            <div v-for="row in timeBucketRows" :key="row.label" class="stats-bar-row">
              <div class="stats-bar-row__top"><span>{{ row.label }}</span><strong>{{ row.value }}</strong></div>
              <div class="stats-bar"><div class="stats-bar__fill stats-bar__fill--amber" :style="{ width: barWidth(row.value, sectionMax(timeBucketRows)) }" /></div>
            </div>
          </div>
        </article>

        <article class="stats-panel">
          <div class="stats-panel__header">
            <h2>Monatsverlauf</h2>
            <span>Aktivitäten pro Monat</span>
          </div>
          <div class="stats-bars">
            <div v-for="row in monthRows" :key="row.label" class="stats-bar-row">
              <div class="stats-bar-row__top"><span>{{ row.label }}</span><strong>{{ row.value }}</strong></div>
              <div class="stats-bar"><div class="stats-bar__fill stats-bar__fill--violet" :style="{ width: barWidth(row.value, sectionMax(monthRows)) }" /></div>
            </div>
          </div>
        </article>

        <article class="stats-panel">
          <div class="stats-panel__header">
            <h2>Häufigste Orte</h2>
            <span>Top 8</span>
          </div>
          <div class="stats-bars">
            <div v-for="row in locationRows" :key="row.label" class="stats-bar-row">
              <div class="stats-bar-row__top"><span>{{ row.label }}</span><strong>{{ row.value }}</strong></div>
              <div class="stats-bar"><div class="stats-bar__fill stats-bar__fill--green" :style="{ width: barWidth(row.value, sectionMax(locationRows)) }" /></div>
            </div>
          </div>
        </article>

        <article class="stats-panel">
          <div class="stats-panel__header">
            <h2>Häufigste Verantwortliche</h2>
            <span>Top 10</span>
          </div>
          <div class="stats-bars">
            <div v-for="row in responsibleRows" :key="row.label" class="stats-bar-row">
              <div class="stats-bar-row__top"><span>{{ row.label }}</span><strong>{{ row.value }}</strong></div>
              <div class="stats-bar"><div class="stats-bar__fill stats-bar__fill--rose" :style="{ width: barWidth(row.value, sectionMax(responsibleRows)) }" /></div>
            </div>
          </div>
        </article>
      </section>
    </template>
  </main>
</template>

<style scoped>
.stats-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 16px;
  margin-bottom: 20px;
}

.stats-header h1 {
  margin: 0;
}

.stats-header__text {
  margin: 8px 0 0;
  max-width: 760px;
  color: #64748b;
}

.stats-page {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.stats-toolbar {
  display: flex;
  justify-content: space-between;
  align-items: end;
  gap: 16px;
  flex-wrap: wrap;
  padding: 18px;
  background: linear-gradient(135deg, #f8fafc 0%, #eef2ff 100%);
  border: 1px solid #e2e8f0;
  border-radius: 16px;
}

.stats-toolbar__dates,
.stats-toolbar__presets {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.stats-toolbar__field {
  display: flex;
  flex-direction: column;
  gap: 6px;
  color: #475569;
  font-size: 0.9rem;
  font-weight: 600;
}

.stats-toolbar__field input,
.stats-toolbar__field select {
  min-width: 180px;
  padding: 10px 12px;
  border: 1px solid #cbd5e1;
  border-radius: 10px;
  font: inherit;
  background: #fff;
}

.stats-preset {
  padding: 10px 14px;
  border: 1px solid #cbd5e1;
  border-radius: 999px;
  background: #fff;
  color: #0f172a;
  cursor: pointer;
  font: inherit;
  font-weight: 600;
}

.stats-summary {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 14px;
}

.stats-card {
  padding: 16px 18px;
  background: #fff;
  border: 1px solid #e2e8f0;
  border-radius: 16px;
  box-shadow: 0 8px 24px rgba(15, 23, 42, 0.05);
}

.stats-card--soft {
  background: #f8fafc;
}

.stats-card__label {
  margin: 0 0 8px;
  font-size: 0.82rem;
  font-weight: 700;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: #64748b;
}

.stats-card__value {
  margin: 0;
  font-size: 1.65rem;
  font-weight: 800;
  color: #0f172a;
  line-height: 1.15;
}

.stats-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 18px;
}

.stats-panel {
  padding: 18px;
  background: #fff;
  border: 1px solid #e2e8f0;
  border-radius: 18px;
  box-shadow: 0 8px 24px rgba(15, 23, 42, 0.05);
}

.stats-panel--wide {
  grid-column: 1 / -1;
}

.stats-panel__header {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  gap: 12px;
  margin-bottom: 16px;
}

.stats-panel__header h2 {
  margin: 0;
  font-size: 1.05rem;
  color: #0f172a;
}

.stats-panel__header span {
  color: #64748b;
  font-size: 0.9rem;
}

.stats-bars {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.stats-bar-row {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.stats-bar-row__top {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  color: #334155;
  font-size: 0.94rem;
}

.stats-bar-row__top strong {
  color: #0f172a;
}

.stats-bar {
  width: 100%;
  height: 10px;
  background: #e2e8f0;
  border-radius: 999px;
  overflow: hidden;
}

.stats-bar__fill {
  height: 100%;
  border-radius: inherit;
  background: linear-gradient(90deg, #2563eb, #60a5fa);
}

.stats-bar__fill--teal {
  background: linear-gradient(90deg, #0f766e, #2dd4bf);
}

.stats-bar__fill--amber {
  background: linear-gradient(90deg, #b45309, #f59e0b);
}

.stats-bar__fill--violet {
  background: linear-gradient(90deg, #6d28d9, #a78bfa);
}

.stats-bar__fill--green {
  background: linear-gradient(90deg, #15803d, #4ade80);
}

.stats-bar__fill--rose {
  background: linear-gradient(90deg, #be123c, #fb7185);
}

.stats-bar__fill--slate {
  background: linear-gradient(90deg, #334155, #94a3b8);
}

.stats-empty,
.stats-loading {
  padding: 20px;
  border: 1px dashed #cbd5e1;
  border-radius: 16px;
  background: #fff;
  color: #64748b;
}

@media (max-width: 900px) {
  .stats-grid {
    grid-template-columns: 1fr;
  }

  .stats-panel--wide {
    grid-column: auto;
  }
}
</style>
