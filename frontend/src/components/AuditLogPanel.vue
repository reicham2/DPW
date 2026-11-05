<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { apiFetch } from '../composables/useApi'
import ErrorAlert from './ErrorAlert.vue'

interface AuditEvent {
  id: string
  occurred_at: string
  actor_id: string | null
  actor_name: string | null
  action: string
  resource_type: string
  resource_id: string
  outcome: 'pending' | 'completed' | 'rejected' | 'unknown'
  http_status: number | null
}
interface AuditPage {
  entries: AuditEvent[]
  next_cursor: string | null
}

const entries = ref<AuditEvent[]>([])
const cursor = ref<string | null>(null)
const actor = ref('')
const outcome = ref('')
const loading = ref(false)
const error = ref<string | null>(null)
let appliedFilters = { actor: '', outcome: '' }
const labels: Record<AuditEvent['outcome'], string> = {
  completed: 'Erfolgreich',
  rejected: 'Abgelehnt',
  unknown: 'Unklar / Fehler',
  pending: 'Ausgang offen',
}
const resources: Record<string, string> = {
  user: 'Benutzer', system: 'System', role: 'Rolle', department: 'Abteilung',
  activity: 'Aktivität', location: 'Ort', material: 'Material',
  attachment: 'Anhang', template: 'Vorlage', mail: 'Mail', idea: 'Idee',
}

async function load(more = false) {
  if (loading.value) return
  loading.value = true
  error.value = null
  try {
    const params = new URLSearchParams({ limit: '50' })
    const filters = more ? appliedFilters : { actor: actor.value.trim(), outcome: outcome.value }
    if (filters.actor) params.set('actor', filters.actor)
    if (filters.outcome) params.set('outcome', filters.outcome)
    if (more && cursor.value) params.set('before', cursor.value)
    const response = await apiFetch(`/api/admin/audit-log?${params}`)
    if (!response.ok) throw new Error(await response.text())
    const page: AuditPage = await response.json()
    entries.value = more ? [...entries.value, ...page.entries] : page.entries
    cursor.value = page.next_cursor
    appliedFilters = filters
  } catch (e) {
    error.value = String(e)
  } finally {
    loading.value = false
  }
}

onMounted(() => load())
</script>

<template>
  <section class="audit-log">
    <h2>Audit Log</h2>
    <p>Wichtige Benutzeraktionen, neueste zuerst. Einträge zeigen den Ausgang der Anfrage, nicht jede einzelne Datenänderung.</p>
    <p class="audit-note">Bei Fehlern können Teilaktionen bereits ausgeführt sein. „Ausgang offen“ bedeutet laufend oder unterbrochen; vor Wiederholung den Zustand prüfen.</p>
    <form class="audit-filters" @submit.prevent="load()">
      <label>Benutzer (Name oder ID)
        <input v-model="actor" maxlength="200" :disabled="loading" />
      </label>
      <label>Ergebnis
        <select v-model="outcome" :disabled="loading">
          <option value="">Alle</option>
          <option v-for="(label, key) in labels" :key="key" :value="key">{{ label }}</option>
        </select>
      </label>
      <button class="btn btn-primary" :disabled="loading">Filtern / Aktualisieren</button>
    </form>
    <ErrorAlert v-if="error" :error="error" />
    <p v-if="loading" role="status">Audit Log wird geladen…</p>
    <p v-else-if="!entries.length && !error">Keine Audit-Einträge gefunden.</p>
    <div v-if="entries.length" class="audit-table">
      <table>
        <thead><tr><th>Zeit</th><th>Benutzer</th><th>Aktion</th><th>Ressource</th><th>Ergebnis</th></tr></thead>
        <tbody>
          <tr v-for="entry in entries" :key="entry.id">
            <td>{{ new Date(entry.occurred_at).toLocaleString('de-DE') }}</td>
            <td>{{ entry.actor_name || 'Ohne verifizierte Anmeldung' }}<small v-if="entry.actor_id">{{ entry.actor_id }}</small></td>
            <td>{{ entry.action }}</td>
            <td>{{ resources[entry.resource_type] || entry.resource_type }}<small v-if="entry.resource_id">{{ entry.resource_id }}</small></td>
            <td>{{ labels[entry.outcome] }}<small v-if="entry.http_status">HTTP {{ entry.http_status }}</small></td>
          </tr>
        </tbody>
      </table>
    </div>
    <button v-if="cursor" class="btn btn-secondary" :disabled="loading" @click="load(true)">Weitere laden</button>
  </section>
</template>

<style scoped>
.audit-log { display: grid; gap: 1rem; }
.audit-note, small { color: var(--text-muted, #64748b); }
.audit-filters { display: flex; gap: 1rem; flex-wrap: wrap; align-items: end; }
label { display: grid; gap: .4rem; }
input, select { padding: .5rem; border: 1px solid var(--border, #cbd5e1); border-radius: .4rem; background: var(--bg-card, white); color: inherit; }
.audit-table { overflow-x: auto; }
table { width: 100%; border-collapse: collapse; }
th, td { text-align: left; padding: .75rem; border-bottom: 1px solid var(--border, #cbd5e1); vertical-align: top; }
small { display: block; overflow-wrap: anywhere; max-width: 22rem; }
</style>
