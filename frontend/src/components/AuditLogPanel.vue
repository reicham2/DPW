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
  details?: {
    kind?: 'created' | 'deleted' | 'changed' | 'saved'
    resource_name?: string
    changes?: { field: string; label: string; redacted: boolean; before?: string | number | boolean | null; after?: string | number | boolean | null }[]
  }
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
const expandedId = ref<string | null>(null)
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
function valueLabel(value: string | number | boolean | null | undefined): string {
  if (value === null || value === undefined || value === '') return '—'
  if (typeof value === 'boolean') return value ? 'Ja' : 'Nein'
  const values: Record<string, string> = {
    none: 'Keine', all: 'Alle', own: 'Eigene', own_dept: 'Eigene Stufe', same_dept: 'Gleiche Stufe',
    up: 'Nach oben', down: 'Nach unten', minutes: 'Minuten', clock: 'Uhrzeit',
  }
  return values[String(value)] || String(value)
}

function summary(entry: AuditEvent): string {
  const changes = entry.details?.changes || []
  if (!changes.length) return entry.details?.kind === 'changed' ? 'Keine Feldänderungen' : 'Anfrage protokolliert'
  const fields = changes.slice(0, 3).map(change => change.label).join(', ')
  return changes.length > 3 ? `${fields} (+${changes.length - 3})` : fields
}

function toggle(entry: AuditEvent) {
  expandedId.value = expandedId.value === entry.id ? null : entry.id
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
    if (!more) expandedId.value = null
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
    <p>Wichtige Benutzeraktionen, neueste zuerst. Eintrag anklicken, um Details zu öffnen. Sensible Inhalte werden nicht übernommen.</p>
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
        <thead><tr><th>Zeit</th><th>Benutzer</th><th>Aktion</th><th>Ressource</th><th>Kurzinfo</th><th>Ergebnis</th></tr></thead>
        <tbody>
          <template v-for="entry in entries" :key="entry.id">
            <tr class="audit-entry" :class="{ 'audit-entry--expanded': expandedId === entry.id }" @click="toggle(entry)">
              <td>{{ new Date(entry.occurred_at).toLocaleString('de-DE') }}</td>
              <td>{{ entry.actor_name || 'Ohne verifizierte Anmeldung' }}</td>
              <td>
                <button class="audit-toggle" :aria-expanded="expandedId === entry.id" :aria-controls="`audit-details-${entry.id}`" @click.stop="toggle(entry)">
                  {{ entry.action }}
                  <small>{{ expandedId === entry.id ? 'Details schließen' : 'Details öffnen' }}</small>
                </button>
              </td>
              <td>{{ resources[entry.resource_type] || entry.resource_type }}<span v-if="entry.details?.resource_name">: {{ entry.details.resource_name }}</span></td>
              <td>{{ summary(entry) }}</td>
              <td>{{ labels[entry.outcome] }}</td>
            </tr>
            <tr v-if="expandedId === entry.id" :id="`audit-details-${entry.id}`" class="audit-expanded">
              <td colspan="6">
                <div class="audit-detail-meta">
                  <span><strong>Eintrag:</strong> {{ entry.id }}</span>
                  <span v-if="entry.actor_id"><strong>Benutzer-ID:</strong> {{ entry.actor_id }}</span>
                  <span v-if="entry.resource_id"><strong>Ressourcen-ID:</strong> {{ entry.resource_id }}</span>
                  <span v-if="entry.http_status"><strong>HTTP:</strong> {{ entry.http_status }}</span>
                </div>
                <div class="audit-details">
                  <template v-if="entry.details?.changes?.length">
                    <div v-for="change in entry.details.changes" :key="change.field">
                      <strong>{{ change.label + ': ' }}</strong>
                      <span v-if="change.redacted"> {{ entry.details.kind === 'deleted' ? 'gelöscht' : entry.details.kind === 'changed' ? 'geändert' : 'gespeichert' }} (Inhalt geschützt)</span>
                      <span v-else-if="entry.details.kind === 'saved' || entry.details.kind === 'created'"> {{ valueLabel(change.after) }}</span>
                      <span v-else-if="entry.details.kind === 'deleted'"> {{ valueLabel(change.before) }} → gelöscht</span>
                      <span v-else> {{ valueLabel(change.before) }} → {{ valueLabel(change.after) }}</span>
                    </div>
                  </template>
                  <span v-else-if="entry.details?.kind === 'changed'">Keine protokollierbaren Feldänderungen.</span>
                  <span v-else class="audit-note">Keine Detaildaten vorhanden (älterer Eintrag oder Aktion ohne Feldvergleich).</span>
                </div>
              </td>
            </tr>
          </template>
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
.audit-entry { cursor: pointer; }
.audit-entry:hover, .audit-entry--expanded, .audit-expanded { background: var(--bg-hover, #f1f5f9); }
.audit-toggle { background: none; border: 0; padding: 0; color: inherit; font: inherit; text-align: left; cursor: pointer; }
.audit-toggle:focus-visible { outline: 2px solid var(--primary, #2563eb); outline-offset: .25rem; }
.audit-detail-meta { display: flex; flex-wrap: wrap; gap: .5rem 1.5rem; margin-bottom: .75rem; overflow-wrap: anywhere; }
.audit-details { overflow-wrap: anywhere; }
</style>
