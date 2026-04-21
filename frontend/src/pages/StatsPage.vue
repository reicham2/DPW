<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { useActivities } from '../composables/useActivities'
import { usePermissions } from '../composables/usePermissions'
import { apiFetch } from '../composables/useApi'
import type { Activity, ActivityExpectedWeather } from '../types'
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
const weatherByActivityId = ref<Record<string, ActivityExpectedWeather | null>>({})
const weatherLoading = ref(false)
const weatherPendingIds = new Set<string>()

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
const timeRows = computed(() => {
  const counts = new Map<string, number>()
  for (const activity of filteredActivities.value) {
    const start = parseMinutes(activity.start_time)
    if (start === null) continue
    const label = hourLabel(Math.floor(start / 60))
    counts.set(label, (counts.get(label) ?? 0) + 1)
  }

  return Array.from({ length: 24 }, (_, hour) => {
    const label = hourLabel(hour)
    return {
      label,
      value: counts.get(label) ?? 0,
    }
  })
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

function isFiniteNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value)
}

async function fetchWeatherForActivity(activity: Activity) {
  if (weatherPendingIds.has(activity.id) || weatherByActivityId.value[activity.id] !== undefined) return

  weatherPendingIds.add(activity.id)
  weatherLoading.value = true

  try {
    const res = await apiFetch(`/api/activities/${activity.id}/weather-expected`)
    if (!res.ok) throw new Error(`weather-${res.status}`)
    const payload = (await res.json()) as ActivityExpectedWeather
    weatherByActivityId.value = {
      ...weatherByActivityId.value,
      [activity.id]: payload,
    }
  } catch {
    weatherByActivityId.value = {
      ...weatherByActivityId.value,
      [activity.id]: null,
    }
  } finally {
    weatherPendingIds.delete(activity.id)
    weatherLoading.value = weatherPendingIds.size > 0
  }
}

async function ensureFilteredWeather() {
  const pendingActivities = filteredActivities.value.filter(
    (activity) => weatherByActivityId.value[activity.id] === undefined && !weatherPendingIds.has(activity.id),
  )

  const batchSize = 4
  for (let i = 0; i < pendingActivities.length; i += batchSize) {
    await Promise.all(pendingActivities.slice(i, i + batchSize).map(fetchWeatherForActivity))
  }
}

watch(
  () => filteredActivities.value.map((activity) => activity.id).join('|'),
  () => {
    void ensureFilteredWeather()
  },
  { immediate: true },
)

type WeatherHourRow = {
  label: string;
  tempAvg: number | null;
  rainAvg: number | null;
  tempSamples: number;
  rainSamples: number;
}

const weatherHourRows = computed<WeatherHourRow[]>(() => {
  const tempSums = new Map<number, number>()
  const tempCounts = new Map<number, number>()
  const rainSums = new Map<number, number>()
  const rainCounts = new Map<number, number>()

  for (const activity of filteredActivities.value) {
    const weather = weatherByActivityId.value[activity.id]
    if (!weather?.available) continue

    const rainSamples = Array.isArray(weather.hourly_rain_probability)
      ? weather.hourly_rain_probability.filter(
          (sample) => Number.isFinite(sample.ts_unix) && isFiniteNumber(sample.probability_percent),
        )
      : []

    const rainByTimestamp = new Map<number, number>()
    for (const sample of rainSamples) {
      rainByTimestamp.set(sample.ts_unix, sample.probability_percent)
    }

    const tempSamples = Array.isArray(weather.hourly_temps)
      ? weather.hourly_temps.filter(
          (sample) => Number.isFinite(sample.ts_unix) && isFiniteNumber(sample.temperature_c),
        )
      : []

    if (tempSamples.length) {
      for (const sample of tempSamples) {
        const hour = new Date(sample.ts_unix * 1000).getHours()
        tempSums.set(hour, (tempSums.get(hour) ?? 0) + sample.temperature_c)
        tempCounts.set(hour, (tempCounts.get(hour) ?? 0) + 1)

        const rainValue = rainByTimestamp.get(sample.ts_unix)
          ?? (isFiniteNumber(weather.rain_probability_percent) ? weather.rain_probability_percent : null)
        if (rainValue !== null) {
          rainSums.set(hour, (rainSums.get(hour) ?? 0) + rainValue)
          rainCounts.set(hour, (rainCounts.get(hour) ?? 0) + 1)
        }
      }
      continue
    }

    const start = parseMinutes(activity.start_time)
    if (start === null) continue
    const hour = Math.floor(start / 60)

    if (isFiniteNumber(weather.temperature_c)) {
      tempSums.set(hour, (tempSums.get(hour) ?? 0) + weather.temperature_c)
      tempCounts.set(hour, (tempCounts.get(hour) ?? 0) + 1)
    }

    if (isFiniteNumber(weather.rain_probability_percent)) {
      rainSums.set(hour, (rainSums.get(hour) ?? 0) + weather.rain_probability_percent)
      rainCounts.set(hour, (rainCounts.get(hour) ?? 0) + 1)
    }
  }

  return Array.from({ length: 24 }, (_, hour) => ({
    label: hourLabel(hour),
    tempAvg: tempCounts.get(hour) ? (tempSums.get(hour) ?? 0) / (tempCounts.get(hour) ?? 1) : null,
    rainAvg: rainCounts.get(hour) ? (rainSums.get(hour) ?? 0) / (rainCounts.get(hour) ?? 1) : null,
    tempSamples: tempCounts.get(hour) ?? 0,
    rainSamples: rainCounts.get(hour) ?? 0,
  }))
})

const weatherActivitiesWithData = computed(() => {
  return filteredActivities.value.filter((activity) => weatherByActivityId.value[activity.id]?.available).length
})

const weatherHasData = computed(() => {
  return weatherHourRows.value.some((row) => row.tempAvg !== null || row.rainAvg !== null)
})

function buildLinearTicks(min: number, max: number, steps: number) {
  if (steps <= 0) return [min, max]
  const ticks: number[] = []
  for (let i = 0; i <= steps; i += 1) {
    ticks.push(min + ((max - min) * i) / steps)
  }
  return ticks
}

const weatherChartWidth = 960
const weatherChartHeight = 340
const weatherChartPlotLeft = 64
const weatherChartPlotRight = 904
const weatherChartPlotTop = 18
const weatherChartPlotBottom = 288

const weatherTempDomain = computed(() => {
  const values = weatherHourRows.value
    .map((row) => row.tempAvg)
    .filter((value): value is number => value !== null)

  if (!values.length) {
    return { min: 0, max: 20, span: 20 }
  }

  const rawMin = Math.min(...values, 0)
  const rawMax = Math.max(...values, 0)
  const padding = rawMin === rawMax ? 2 : Math.max(1, (rawMax - rawMin) * 0.12)
  const min = rawMin - padding
  const max = rawMax + padding
  return {
    min,
    max,
    span: Math.max(1, max - min),
  }
})

function weatherTempY(value: number): number {
  const { min, span } = weatherTempDomain.value
  const ratio = (value - min) / span
  return weatherChartPlotBottom - ratio * (weatherChartPlotBottom - weatherChartPlotTop)
}

function weatherRainY(value: number): number {
  const safe = Math.min(100, Math.max(0, value))
  return weatherChartPlotBottom - (safe / 100) * (weatherChartPlotBottom - weatherChartPlotTop)
}

const weatherTempZeroY = computed(() => weatherTempY(0))

const weatherChartBars = computed(() => {
  const plotWidth = weatherChartPlotRight - weatherChartPlotLeft
  const groupWidth = plotWidth / weatherHourRows.value.length
  const barWidth = Math.min(14, Math.max(8, groupWidth * 0.28))

  return weatherHourRows.value.map((row, index) => {
    const groupX = weatherChartPlotLeft + index * groupWidth
    const centerX = groupX + groupWidth / 2
    const tempY = row.tempAvg === null ? null : weatherTempY(row.tempAvg)
    const rainY = row.rainAvg === null ? null : weatherRainY(row.rainAvg)
    return {
      ...row,
      index,
      centerX,
      labelX: centerX,
      showLabel: index % 2 === 0,
      tempX: centerX - barWidth - 2,
      tempY,
      tempHeight: tempY === null ? 0 : Math.abs(weatherTempZeroY.value - tempY),
      tempBarY: tempY === null ? 0 : Math.min(tempY, weatherTempZeroY.value),
      rainX: centerX + 2,
      rainY,
      rainHeight: rainY === null ? 0 : weatherChartPlotBottom - rainY,
      barWidth,
    }
  })
})

const weatherTempTicks = computed(() => {
  return buildLinearTicks(weatherTempDomain.value.min, weatherTempDomain.value.max, 4).map((value) => ({
    value,
    y: weatherTempY(value),
    label: `${Math.round(value)}°`,
  }))
})

const weatherRainTicks = computed(() => {
  return [0, 25, 50, 75, 100].map((value) => ({
    value,
    y: weatherRainY(value),
    label: `${value}%`,
  }))
})

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

        <article class="stats-panel">
          <div class="stats-panel__header">
            <h2>Zeitübersicht</h2>
            <span>Startzeiten pro Stunde</span>
          </div>
          <div class="stats-bars">
            <div v-for="row in timeRows" :key="row.label" class="stats-bar-row">
              <div class="stats-bar-row__top"><span>{{ row.label }}</span><strong>{{ row.value }}</strong></div>
              <div class="stats-bar">
                <div
                  v-if="row.value > 0"
                  class="stats-bar__fill stats-bar__fill--amber"
                  :style="{ width: barWidth(row.value, sectionMax(timeRows)) }"
                />
              </div>
            </div>
          </div>
        </article>

        <article class="stats-panel stats-panel--wide">
          <div class="stats-panel__header stats-panel__header--weather">
            <div>
              <h2>Wetter pro Stunde</h2>
              <span>Temperatur und Regenwahrscheinlichkeit als Mittelwerte je Stunde</span>
            </div>
            <span>{{ weatherActivitiesWithData }} von {{ filteredActivities.length }} Aktivitäten mit Wetterdaten</span>
          </div>

          <div v-if="!weatherHasData && weatherLoading" class="stats-empty stats-empty--inner">
            Wetterdaten werden geladen…
          </div>
          <div v-else-if="!weatherHasData" class="stats-empty stats-empty--inner">
            Für die gewählten Aktivitäten sind keine Wetterdaten verfügbar.
          </div>
          <div v-else class="weather-chart-wrap">
            <div class="weather-chart-legend">
              <span class="weather-chart-legend__item">
                <span class="weather-chart-legend__swatch weather-chart-legend__swatch--temp" /> Temperatur
              </span>
              <span class="weather-chart-legend__item">
                <span class="weather-chart-legend__swatch weather-chart-legend__swatch--rain" /> Regenwahrscheinlichkeit
              </span>
            </div>
            <div class="weather-chart-scroll">
              <svg
                class="weather-chart"
                :viewBox="`0 0 ${weatherChartWidth} ${weatherChartHeight}`"
                role="img"
                aria-label="Temperatur und Regenwahrscheinlichkeit pro Stunde"
              >
                <line
                  v-for="tick in weatherTempTicks"
                  :key="`grid-${tick.label}`"
                  :x1="weatherChartPlotLeft"
                  :x2="weatherChartPlotRight"
                  :y1="tick.y"
                  :y2="tick.y"
                  class="weather-chart__grid"
                />

                <line
                  :x1="weatherChartPlotLeft"
                  :x2="weatherChartPlotRight"
                  :y1="weatherTempZeroY"
                  :y2="weatherTempZeroY"
                  class="weather-chart__axis weather-chart__axis--zero"
                />

                <template v-for="bar in weatherChartBars" :key="bar.label">
                  <rect
                    v-if="bar.tempAvg !== null"
                    :x="bar.tempX"
                    :y="bar.tempBarY"
                    :width="bar.barWidth"
                    :height="bar.tempHeight"
                    rx="4"
                    class="weather-chart__bar weather-chart__bar--temp"
                  />
                  <rect
                    v-if="bar.rainAvg !== null"
                    :x="bar.rainX"
                    :y="bar.rainY"
                    :width="bar.barWidth"
                    :height="bar.rainHeight"
                    rx="4"
                    class="weather-chart__bar weather-chart__bar--rain"
                  />
                  <text
                    v-if="bar.showLabel"
                    :x="bar.labelX"
                    :y="weatherChartPlotBottom + 24"
                    text-anchor="middle"
                    class="weather-chart__x-label"
                  >{{ bar.label }}</text>
                </template>

                <text
                  v-for="tick in weatherTempTicks"
                  :key="`temp-${tick.label}`"
                  :x="weatherChartPlotLeft - 10"
                  :y="tick.y + 4"
                  text-anchor="end"
                  class="weather-chart__y-label"
                >{{ tick.label }}</text>

                <text
                  v-for="tick in weatherRainTicks"
                  :key="`rain-${tick.label}`"
                  :x="weatherChartPlotRight + 10"
                  :y="tick.y + 4"
                  text-anchor="start"
                  class="weather-chart__y-label weather-chart__y-label--rain"
                >{{ tick.label }}</text>
              </svg>
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

.stats-panel__header--weather {
  align-items: flex-start;
}

.stats-panel__header--weather > div {
  display: flex;
  flex-direction: column;
  gap: 6px;
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

.weather-chart-wrap {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.weather-chart-legend {
  display: flex;
  flex-wrap: wrap;
  gap: 14px;
  color: #475569;
  font-size: 0.92rem;
}

.weather-chart-legend__item {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

.weather-chart-legend__swatch {
  width: 12px;
  height: 12px;
  border-radius: 4px;
}

.weather-chart-legend__swatch--temp {
  background: #f97316;
}

.weather-chart-legend__swatch--rain {
  background: #0ea5e9;
}

.weather-chart-scroll {
  overflow-x: auto;
  padding-bottom: 4px;
}

.weather-chart {
  display: block;
  min-width: 820px;
  width: 100%;
  height: auto;
}

.weather-chart__grid {
  stroke: #e2e8f0;
  stroke-width: 1;
}

.weather-chart__axis {
  stroke: #94a3b8;
  stroke-width: 1.2;
}

.weather-chart__axis--zero {
  stroke: #cbd5e1;
  stroke-dasharray: 4 4;
}

.weather-chart__bar--temp {
  fill: #f97316;
}

.weather-chart__bar--rain {
  fill: #0ea5e9;
}

.weather-chart__x-label,
.weather-chart__y-label {
  fill: #64748b;
  font-size: 12px;
  font-weight: 600;
}

.weather-chart__y-label--rain {
  fill: #0369a1;
}

@media (max-width: 900px) {
  .stats-grid {
    grid-template-columns: 1fr;
  }

  .stats-panel--wide {
    grid-column: auto;
  }

  .stats-panel__header--weather {
    flex-direction: column;
  }
}
</style>
