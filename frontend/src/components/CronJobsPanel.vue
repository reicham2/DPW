<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { apiFetch } from '../composables/useApi'
import ErrorAlert from './ErrorAlert.vue'

interface CronJob {
  id: string
  name: string
  interval_minutes: number
  enabled: boolean
  running: boolean
  health: string
  next_run: number | null
  last_started: number | null
  last_finished: number | null
  last_success: number | null
  duration_ms: number
  runs: number
  failures: number
  missed_runs: number
  last_error: string | null
}
interface CronStatus {
  active: boolean
  started_at: number
  intervals_minutes: number[]
  jobs: CronJob[]
}

const status = ref<CronStatus | null>(null)
const error = ref<string | null>(null)
const busy = ref(false)
let timer: ReturnType<typeof setTimeout> | null = null
let disposed = false
const healthLabels: Record<string, string> = {
  ok: 'OK', waiting: 'Noch nicht ausgeführt', running: 'Läuft',
  overdue: 'Überfällig', failed: 'Fehlgeschlagen', paused: 'Pausiert',
}

function date(value: number | null) {
  return value === null ? 'Noch nicht ausgeführt' : new Date(value).toLocaleString('de-DE')
}
function interval(minutes: number) {
  return minutes === 1440 ? 'Täglich' : minutes === 60 ? 'Stündlich' : `Alle ${minutes} Minuten`
}

async function refresh() {
  if (busy.value || disposed) return
  busy.value = true
  try {
    const response = await apiFetch('/api/admin/cron-jobs')
    if (!response.ok) throw new Error(await response.text())
    status.value = await response.json() as CronStatus
    error.value = null
  } catch (e) {
    error.value = String(e)
  } finally {
    busy.value = false
  }
}

async function toggle(job: CronJob) {
  if (busy.value) return
  busy.value = true
  try {
    const response = await apiFetch(`/api/admin/cron-jobs/${encodeURIComponent(job.id)}`, {
      method: 'PUT', body: JSON.stringify({ enabled: !job.enabled }),
    })
    if (!response.ok) throw new Error(await response.text())
    status.value = await response.json() as CronStatus
    error.value = null
  } catch (e) {
    error.value = String(e)
  } finally {
    busy.value = false
  }
}

async function poll() {
  await refresh()
  if (!disposed) timer = setTimeout(() => { void poll() }, 10000)
}
onMounted(() => { void poll() })
onUnmounted(() => {
  disposed = true
  if (timer) clearTimeout(timer)
})
</script>

<template>
  <section>
    <div class="header">
      <h2>Zentrale Cron-Jobs</h2>
      <button class="btn-primary" :disabled="busy" @click="refresh">Aktualisieren</button>
    </div>
    <p>Automatische Aktualisierung alle 10 Sekunden. Ein laufender Job wird beim Pausieren noch beendet.</p>
    <p v-if="status">
      Scheduler: <strong>{{ status.active ? 'Aktiv' : 'Gestoppt' }}</strong>.
      Status seit Backend-Start: {{ date(status.started_at) }}.
      Unterstützte Intervalle: {{ status.intervals_minutes.map(interval).join(', ') }}.
    </p>
    <p>Pausen und Ausführungsstatistiken gelten für diesen Backend-Prozess und werden beim Neustart zurückgesetzt.
      Verpasste Intervalle werden gezählt, aber nicht mehrfach nachgeholt.</p>
    <ErrorAlert v-if="error" :error="error" />
    <p v-if="busy && !status">Lade Cron-Jobs...</p>
    <div v-if="status" class="jobs">
      <article v-for="job in status.jobs" :key="job.id" class="job">
        <div class="header">
          <h3>{{ job.name }}</h3>
          <button class="btn-primary" :disabled="busy" @click="toggle(job)">
            {{ job.enabled ? 'Pausieren' : 'Fortsetzen' }}
          </button>
        </div>
        <p><strong :class="{ problem: job.health === 'failed' || job.health === 'overdue' }">
          {{ healthLabels[job.health] || job.health }}
        </strong> · {{ interval(job.interval_minutes) }}<span v-if="job.running"> · Job läuft</span></p>
        <dl>
          <dt>Nächster Lauf</dt><dd>{{ job.next_run === null ? 'Pausiert' : date(job.next_run) }}</dd>
          <dt>Letzter Start</dt><dd>{{ date(job.last_started) }}</dd>
          <dt>Letztes Ende</dt><dd>{{ date(job.last_finished) }}</dd>
          <dt>Letzter Erfolg</dt><dd>{{ date(job.last_success) }}</dd>
          <dt>Letzte Dauer</dt><dd>{{ job.duration_ms }} ms</dd>
          <dt>Läufe / Fehler / verpasste Intervalle</dt>
          <dd>{{ job.runs }} / {{ job.failures }} / {{ job.missed_runs }}</dd>
        </dl>
        <p v-if="job.last_error" class="problem">{{ job.last_error }}</p>
      </article>
      <p v-if="status.jobs.length === 0">Keine Jobs registriert.</p>
    </div>
  </section>
</template>

<style scoped>
.header { display: flex; align-items: center; justify-content: space-between; gap: 1rem; }
.jobs { display: grid; gap: 1rem; margin-top: 1rem; }
.job { border: 1px solid var(--color-border, #ddd); border-radius: 8px; padding: 1rem; }
.job h3 { margin: 0; }
dl { display: grid; grid-template-columns: minmax(120px, 1fr) 2fr; gap: .5rem; }
dd { margin: 0; overflow-wrap: anywhere; }
.problem { color: var(--color-danger, #b91c1c); }
</style>
