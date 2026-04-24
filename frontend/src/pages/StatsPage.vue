<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useActivities } from '../composables/useActivities'
import { usePermissions } from '../composables/usePermissions'
import type { Activity } from '../types'
import ErrorAlert from '../components/ErrorAlert.vue'
import BadgeSelect from '../components/BadgeSelect.vue'
import DepartmentBadge from '../components/DepartmentBadge.vue'

const { activities, loading, error, fetchActivities } = useActivities()
const { departments, fetchDepartments } = usePermissions()

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
const selectedStage = ref<string | null>(null)

onMounted(() => {
  document.title = 'Statistik – DPWeb'
  void Promise.all([
    fetchActivities(),
    fetchDepartments().catch(() => undefined),
  ])
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
  const uniqueStages = new Set<string>()

  for (const department of departments.value) {
    if (department.name?.trim()) uniqueStages.add(department.name.trim())
  }

  for (const activity of pastActivities.value) {
    const department = activity.department?.trim()
    if (department) uniqueStages.add(department)
  }

  return Array.from(uniqueStages).sort((a, b) => a.localeCompare(b, 'de'))
})

const stageItems = computed(() => stageOptions.value.map((value) => ({ value })))

const stageColors = computed(() => {
  const map = new Map<string, string>()
  for (const department of departments.value) {
    if (department.name && department.color) map.set(department.name, department.color)
  }
  return map
})

const filteredActivities = computed(() => {
  const { from, to } = normalizedRange.value
  return pastActivities.value.filter((activity) => {
    if (from && activity.date < from) return false
    if (activity.date > to) return false
    if (selectedStage.value && (activity.department ?? '') !== selectedStage.value) return false
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
  if (value <= 0 || max <= 0) return '0%'
  return `${((value / max) * 100).toFixed(1)}%`
}

function weekdayLabel(isoDate: string): string {
  const date = new Date(`${isoDate}T00:00:00`)
  return new Intl.DateTimeFormat('de-DE', { weekday: 'long' }).format(date)
}

function monthLabel(isoDate: string): string {
  const date = new Date(`${isoDate}T00:00:00`)
  return new Intl.DateTimeFormat('de-DE', { month: 'short', year: '2-digit' }).format(date)
}

function hourLabel(hour: number): string {
  return `${String(hour).padStart(2, '0')}:00`
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

const departmentRows = computed(() => {
  const counts = new Map<string, number>()
  for (const activity of filteredActivities.value) {
    const department = activity.department?.trim()
    if (!department) continue
    counts.set(department, (counts.get(department) ?? 0) + 1)
  }

  return stageOptions.value.map((label) => ({
    label,
    value: counts.get(label) ?? 0,
  }))
})
const locationRows = computed(() => buildCountRows(filteredActivities.value.map((activity) => activity.location || 'Ohne Ort')).slice(0, 8))
const responsibleRows = computed(() => buildCountRows(filteredActivities.value.flatMap((activity) => activity.responsible)).slice(0, 10))
const weekdayOrder = ['Montag', 'Dienstag', 'Mittwoch', 'Donnerstag', 'Freitag', 'Samstag', 'Sonntag']
const weekdayRows = computed(() => {
  const rows = buildCountRows(filteredActivities.value.map((activity) => weekdayLabel(activity.date)))
  return weekdayOrder
    .map((label) => rows.find((row) => row.label === label) ?? { label, value: 0 })
})
type TimeWindow = { key: string; label: string; shortLabel: string; fromHour: number; toHour: number }

const timeWindows: TimeWindow[] = Array.from({ length: 24 }, (_, h) => ({
  key: String(h),
  label: `${String(h).padStart(2, '0')}:00–${String(h).padStart(2, '0')}:59`,
  shortLabel: h % 3 === 0 ? `${h}h` : '',
  fromHour: h,
  toHour: h,
}))

const timeChartSvg = computed(() => {
  const W = 600, H = 230
  const padL = 44, padR = 16, padT = 20, padB = 42
  const chartW = W - padL - padR
  const chartH = H - padT - padB

  const counts = new Map<string, number>()
  for (const w of timeWindows) counts.set(w.key, 0)
  for (const activity of filteredActivities.value) {
    const start = parseMinutes(activity.start_time)
    const end = parseMinutes(activity.end_time)
    if (start === null) continue
    const startHour = Math.floor(start / 60)
    const endHour = end !== null ? Math.floor(end / 60) : startHour
    // increment every hour the activity spans
    for (let h = startHour; h <= Math.min(endHour, 23); h++) {
      counts.set(String(h), (counts.get(String(h)) ?? 0) + 1)
    }
  }

  const values = timeWindows.map((w) => counts.get(w.key) ?? 0)
  const maxValue = Math.max(...values, 1)
  const slotW = chartW / timeWindows.length
  const barW = slotW * 0.55

  const bars = timeWindows.map((w, i) => {
    const val = counts.get(w.key) ?? 0
    const cx = padL + i * slotW + slotW / 2
    const barH = (val / maxValue) * chartH
    return {
      label: w.label,
      shortLabel: w.shortLabel,
      value: val,
      cx,
      barX: cx - barW / 2,
      barY: padT + chartH - barH,
      barW,
      barH,
      isPeak: val > 0 && val === maxValue,
    }
  })

  // smooth cubic bezier curve through bar tops
  const pts = bars.map((b) => ({ x: b.cx, y: b.barY }))
  let curvePath = ''
  if (pts.length > 1) {
    curvePath = `M ${pts[0].x.toFixed(1)},${pts[0].y.toFixed(1)}`
    for (let i = 1; i < pts.length; i++) {
      const p = pts[i - 1], n = pts[i]
      const cpx = ((p.x + n.x) / 2).toFixed(1)
      curvePath += ` C ${cpx},${p.y.toFixed(1)} ${cpx},${n.y.toFixed(1)} ${n.x.toFixed(1)},${n.y.toFixed(1)}`
    }
  }

  // y-axis ticks
  const tickStep = Math.ceil(maxValue / 4) || 1
  const tickVals = new Set<number>()
  for (let v = 0; v <= maxValue; v += tickStep) tickVals.add(v)
  tickVals.add(maxValue)
  const ticks = [...tickVals].map((v) => ({ val: v, y: padT + chartH - (v / maxValue) * chartH }))

  return { W, H, padL, padR, padT, padB, chartH, chartW, bars, curvePath, ticks, baselineY: padT + chartH }
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

function stageBarStyle(stage: string) {
  const color = stageColors.value.get(stage) ?? '#64748b'
  return {
    background: `linear-gradient(90deg, ${color}, ${color}cc)`,
  }
}

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
          <BadgeSelect
            kind="department"
            :items="stageItems"
            allow-empty
            placeholder="Alle Stufen"
            :model-value="selectedStage"
            @update:model-value="(value) => selectedStage = value"
          />
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
      <section v-if="filteredActivities.length" class="stats-summary">
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
        Keine Aktivitäten im gewählten Zeitraum.
      </section>

      <section v-else class="stats-grid">
        <article class="stats-panel">
          <div class="stats-panel__header">
            <h2>Nach Stufe</h2>
            <span>{{ filteredActivities.length }} Aktivitäten</span>
          </div>
          <div class="stats-bars">
            <div v-for="row in departmentRows" :key="row.label" class="stats-bar-row">
              <div class="stats-bar-row__top">
                <DepartmentBadge :department="row.label" />
                <strong>{{ row.value }}</strong>
              </div>
              <div class="stats-bar">
                <div
                  v-if="row.value > 0"
                  class="stats-bar__fill"
                  :style="{ ...stageBarStyle(row.label), width: barWidth(row.value, sectionMax(departmentRows)) }"
                />
              </div>
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
              <div class="stats-bar">
                <div
                  v-if="row.value > 0"
                  class="stats-bar__fill stats-bar__fill--teal"
                  :style="{ width: barWidth(row.value, sectionMax(weekdayRows)) }"
                />
              </div>
            </div>
          </div>
        </article>

        <article class="stats-panel stats-panel--wide">
          <div class="stats-panel__header">
            <h2>Zeitverteilung</h2>
            <span>Aktivitäten nach Tageszeit</span>
          </div>
          <svg
            :viewBox="`0 0 ${timeChartSvg.W} ${timeChartSvg.H}`"
            class="time-dist-svg"
            aria-hidden="true"
          >
            <!-- grid + y-axis -->
            <g v-for="tick in timeChartSvg.ticks" :key="tick.val">
              <line
                :x1="timeChartSvg.padL" :y1="tick.y"
                :x2="timeChartSvg.W - timeChartSvg.padR" :y2="tick.y"
                class="time-dist-grid"
              />
              <text :x="timeChartSvg.padL - 6" :y="tick.y + 4" class="time-dist-axis" text-anchor="end">{{ tick.val }}</text>
            </g>
            <!-- baseline -->
            <line
              :x1="timeChartSvg.padL" :y1="timeChartSvg.baselineY"
              :x2="timeChartSvg.W - timeChartSvg.padR" :y2="timeChartSvg.baselineY"
              class="time-dist-baseline"
            />
            <!-- bars -->
            <g v-for="bar in timeChartSvg.bars" :key="bar.label">
              <title>{{ bar.label }}: {{ bar.value }}</title>
              <rect
                :x="bar.barX" :y="bar.barY"
                :width="bar.barW" :height="bar.barH"
                rx="4"
                :class="['time-dist-bar', bar.isPeak ? 'time-dist-bar--peak' : '']"
              />
            </g>
            <!-- distribution curve -->
            <path v-if="timeChartSvg.curvePath" :d="timeChartSvg.curvePath" class="time-dist-curve" />
            <!-- dots on curve -->
            <circle
              v-for="bar in timeChartSvg.bars" :key="`dot${bar.label}`"
              :cx="bar.cx" :cy="bar.barY"
              :r="bar.isPeak ? 5 : 3.5"
              :class="['time-dist-dot', bar.isPeak ? 'time-dist-dot--peak' : '']"
            />
            <!-- x-axis labels -->
            <text
              v-for="bar in timeChartSvg.bars" :key="`x${bar.label}`"
              :x="bar.cx" :y="timeChartSvg.baselineY + 16"
              class="time-dist-axis time-dist-axis--x"
              text-anchor="middle"
            >{{ bar.shortLabel }}</text>
          </svg>
        </article>

        <article class="stats-panel">
          <div class="stats-panel__header">
            <h2>Monatsverlauf</h2>
            <span>Aktivitäten pro Monat</span>
          </div>
          <div class="stats-bars">
            <div v-for="row in monthRows" :key="row.label" class="stats-bar-row">
              <div class="stats-bar-row__top"><span>{{ row.label }}</span><strong>{{ row.value }}</strong></div>
              <div class="stats-bar">
                <div
                  v-if="row.value > 0"
                  class="stats-bar__fill stats-bar__fill--violet"
                  :style="{ width: barWidth(row.value, sectionMax(monthRows)) }"
                />
              </div>
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
              <div class="stats-bar">
                <div
                  v-if="row.value > 0"
                  class="stats-bar__fill stats-bar__fill--green"
                  :style="{ width: barWidth(row.value, sectionMax(locationRows)) }"
                />
              </div>
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
              <div class="stats-bar">
                <div
                  v-if="row.value > 0"
                  class="stats-bar__fill stats-bar__fill--rose"
                  :style="{ width: barWidth(row.value, sectionMax(responsibleRows)) }"
                />
              </div>
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
.stats-toolbar__field :deep(.badge-select-button),
.stats-toolbar__field select {
  min-width: 180px;
  border: 1px solid #cbd5e1;
  border-radius: 10px;
  font: inherit;
  background: #fff;
}

.stats-toolbar__field input,
.stats-toolbar__field select {
  padding: 10px 12px;
}

.stats-toolbar__field :deep(.badge-select-button) {
  min-height: 42px;
  padding: 7px 10px;
  box-sizing: border-box;
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
  align-items: center;
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

.time-dist-svg {
  width: 100%;
  display: block;
  overflow: visible;
  margin-top: 8px;
}

.time-dist-grid {
  stroke: #e5e7eb;
  stroke-width: 1;
}

.time-dist-baseline {
  stroke: #9ca3af;
  stroke-width: 1.5;
}

.time-dist-bar {
  fill: #fbbf24;
  opacity: 0.7;
  transition: opacity 0.15s;
}

.time-dist-bar--peak {
  fill: #f59e0b;
  opacity: 1;
}

.time-dist-curve {
  fill: none;
  stroke: #b45309;
  stroke-width: 2.5;
  stroke-linejoin: round;
  stroke-linecap: round;
}

.time-dist-dot {
  fill: #b45309;
  stroke: white;
  stroke-width: 1.5;
}

.time-dist-dot--peak {
  fill: #92400e;
}

.time-dist-axis {
  font-size: 11px;
  fill: #6b7280;
  font-family: inherit;
}

.time-dist-axis--x {
  font-size: 11px;
  font-weight: 600;
}


.stats-empty,
.stats-loading {
  padding: 20px;
  border: 1px dashed #cbd5e1;
  border-radius: 16px;
  background: #fff;
  color: #64748b;
}

.stats-empty--inner {
  margin-top: 4px;
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
