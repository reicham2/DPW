<script setup lang="ts">
import { ref, watch, onUnmounted, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useActivities } from '../composables/useActivities'
import type { Activity, Department, ProgramInput } from '../types'

const route  = useRoute()
const router = useRouter()
const id = route.params.id as string

const {
  fetchActivity, fetchDepartments, updateActivity, deleteActivity,
  error, lastUpdatedActivity, departments
} = useActivities()

const activity = ref<Activity | null>(null)
const loading  = ref(true)
const mode     = ref<'view' | 'edit'>('view')
const saving   = ref(false)

// ---- Edit state ------------------------------------------------------------
const editTitle       = ref('')
const editDate        = ref('')
const editStartTime   = ref('')
const editEndTime     = ref('')
const editGoal        = ref('')
const editLocation    = ref('')
const editResponsible = ref('')
const editDepartment  = ref<Department | ''>('')
const editMaterial    = ref<string[]>([''])
const editNeedsSiko   = ref(false)
const editSikoFile    = ref<File | null>(null)
const editSikoBase64  = ref<string | null>(null)
const editBadWeather  = ref('')
const editPrograms    = ref<ProgramInput[]>([])

// ---- Load ------------------------------------------------------------------
onMounted(async () => {
  await fetchDepartments()
  activity.value = await fetchActivity(id)
  loading.value  = false
})

// ---- Sync edit fields from an activity (used by enterEdit + WS) ------------
function syncEditFields(a: typeof activity.value) {
  if (!a) return
  editTitle.value       = a.title
  editDate.value        = a.date
  editStartTime.value   = a.start_time
  editEndTime.value     = a.end_time
  editGoal.value        = a.goal
  editLocation.value    = a.location
  editResponsible.value = a.responsible
  editDepartment.value  = a.department ?? ''
  editMaterial.value    = [...a.material, '']  // trailing empty = sentinel input
  editNeedsSiko.value   = a.needs_siko
  editBadWeather.value  = a.bad_weather_info ?? ''
  editPrograms.value    = a.programs.map(p => ({
    time: p.time, title: p.title, description: p.description, responsible: p.responsible
  }))
}

// ---- WS live update — field-level merge in edit mode -----------------------
// Compare incoming update against last known saved state (prev).
// Only fields that changed remotely are patched into the edit form,
// so Tab B's own uncommitted edits in other fields are never overwritten.
watch(lastUpdatedActivity, (updated) => {
  if (!updated || updated.id !== id) return

  const prev = activity.value   // last known saved snapshot
  activity.value = updated       // always advance source-of-truth

  if (mode.value === 'edit' && prev) {
    if (updated.title             !== prev.title)             editTitle.value       = updated.title
    if (updated.date              !== prev.date)              editDate.value        = updated.date
    if (updated.start_time        !== prev.start_time)        editStartTime.value   = updated.start_time
    if (updated.end_time          !== prev.end_time)          editEndTime.value     = updated.end_time
    if (updated.goal              !== prev.goal)              editGoal.value        = updated.goal
    if (updated.location          !== prev.location)          editLocation.value    = updated.location
    if (updated.responsible       !== prev.responsible)       editResponsible.value = updated.responsible
    if (updated.department        !== prev.department)        editDepartment.value  = updated.department ?? ''
    if (updated.needs_siko        !== prev.needs_siko)        editNeedsSiko.value   = updated.needs_siko
    if (updated.bad_weather_info  !== prev.bad_weather_info)  editBadWeather.value  = updated.bad_weather_info ?? ''
    if (JSON.stringify(updated.material) !== JSON.stringify(prev.material))
      editMaterial.value = [...updated.material, '']
    if (JSON.stringify(updated.programs) !== JSON.stringify(prev.programs))
      editPrograms.value = updated.programs.map(p => ({
        time: p.time, title: p.title, description: p.description, responsible: p.responsible
      }))
  }
})

// ---- Helpers ---------------------------------------------------------------
function formatDate(d: string): string {
  return new Date(d + 'T00:00:00').toLocaleDateString('de-DE', {
    weekday: 'long', day: 'numeric', month: 'long', year: 'numeric'
  })
}

// ---- Enter edit mode -------------------------------------------------------
function enterEdit() {
  if (!activity.value) return
  syncEditFields(activity.value)
  editSikoFile.value   = null
  editSikoBase64.value = null
  error.value = null
  mode.value  = 'edit'
}

// ---- Material --------------------------------------------------------------
// Sentinel pattern: array always ends with one empty string.
// @input  → if user typed in the last (sentinel) field, grow the list
// @blur   → if a non-sentinel field is empty when leaving, remove it
function onMaterialInput(i: number) {
  const isLast = i === editMaterial.value.length - 1
  if (isLast && editMaterial.value[i] !== '') {
    editMaterial.value.push('')
  }
}
function onMaterialBlur(i: number) {
  const isLast = i === editMaterial.value.length - 1
  if (!isLast && editMaterial.value[i] === '') {
    editMaterial.value.splice(i, 1)
  }
}

// ---- Programs --------------------------------------------------------------
function addProgram() {
  editPrograms.value.push({ time: '', title: '', description: '', responsible: '' })
}
function removeProgram(i: number) { editPrograms.value.splice(i, 1) }

// ---- SiKo file change ------------------------------------------------------
async function onSikoFileChange(e: Event) {
  const file = (e.target as HTMLInputElement).files?.[0]
  if (!file) { editSikoFile.value = null; editSikoBase64.value = null; return }
  editSikoFile.value = file
  const buf   = await file.arrayBuffer()
  const bytes = new Uint8Array(buf)
  let binary  = ''
  for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i])
  editSikoBase64.value = btoa(binary)
  scheduleAutoSave() // file upload triggers save like any other field change
}

// ---- Auto-save (debounced 1.5 s) -------------------------------------------
let autoSaveTimer: ReturnType<typeof setTimeout> | null = null

function scheduleAutoSave() {
  if (mode.value !== 'edit') return
  if (autoSaveTimer) clearTimeout(autoSaveTimer)
  autoSaveTimer = setTimeout(doSave, 1500)
}

onUnmounted(() => { if (autoSaveTimer) clearTimeout(autoSaveTimer) })

watch(
  [editTitle, editDate, editStartTime, editEndTime, editGoal, editLocation,
   editResponsible, editDepartment, editMaterial, editNeedsSiko, editBadWeather, editPrograms],
  scheduleAutoSave,
  { deep: true }
)

// ---- Core save (used by auto-save and the button) --------------------------
async function doSave() {
  if (!activity.value) return
  saving.value = true
  error.value  = null

  await updateActivity(id, {
    title:            editTitle.value.trim(),
    date:             editDate.value,
    start_time:       editStartTime.value,
    end_time:         editEndTime.value,
    goal:             editGoal.value.trim(),
    location:         editLocation.value.trim(),
    responsible:      editResponsible.value.trim(),
    department:       editDepartment.value || null,
    material:         editMaterial.value.filter(m => m.trim()),
    needs_siko:       editNeedsSiko.value,
    siko_base64:      editSikoBase64.value ?? undefined,
    bad_weather_info: editBadWeather.value.trim() || null,
    programs:         editPrograms.value
  })

  saving.value = false
}

// ---- Delete ----------------------------------------------------------------
async function doDelete() {
  if (!confirm(`Aktivität "${activity.value?.title || id}" wirklich löschen?`)) return
  await deleteActivity(id)
  router.push('/')
}
</script>

<template>
  <header class="header">
    <button class="btn-back" @click="router.push('/')">← Zurück</button>
    <h1>{{ activity?.title || 'Aktivität' }}</h1>
    <div class="header-right">
      <span v-if="saving" class="saving-badge">Speichert…</span>
      <button
        v-if="activity"
        class="btn-toggle"
        :class="mode === 'edit' ? 'btn-secondary' : 'btn-primary'"
        @click="mode === 'view' ? enterEdit() : (mode = 'view')"
      >
        {{ mode === 'view' ? 'Bearbeiten' : 'Ansicht' }}
      </button>
    </div>
  </header>

  <main class="main">
    <p v-if="loading" class="loading">Laden…</p>
    <p v-else-if="!activity" class="error">Aktivität nicht gefunden.</p>

    <!-- ================================================================ VIEW -->
    <div v-else-if="mode === 'view'" class="detail-view">

      <!-- Hero: Titel + Datum + Abteilung -->
      <div class="detail-section">
        <div class="detail-hero">
          <div>
            <h2 class="detail-hero-title">{{ activity.title }}</h2>
            <p class="detail-hero-time">
              {{ formatDate(activity.date) }} &middot;
              {{ activity.start_time }}–{{ activity.end_time }}
            </p>
          </div>
          <span v-if="activity.department" class="card-dept-badge">
            {{ activity.department }}
          </span>
        </div>
      </div>

      <!-- Ziel -->
      <div class="detail-section">
        <p class="detail-section-title">Ziel</p>
        <p class="detail-value detail-value--multiline">{{ activity.goal || '—' }}</p>
      </div>

      <!-- Ort / Verantwortlich -->
      <div class="detail-section">
        <div class="detail-grid detail-grid--3">
          <div class="detail-field">
            <span class="detail-label">Ort</span>
            <span class="detail-value">{{ activity.location || '—' }}</span>
          </div>
          <div class="detail-field">
            <span class="detail-label">Verantwortlich</span>
            <span class="detail-value">{{ activity.responsible || '—' }}</span>
          </div>
          <div class="detail-field">
            <span class="detail-label">Abteilung</span>
            <span class="detail-value">{{ activity.department || '—' }}</span>
          </div>
        </div>
      </div>

      <!-- Programmpunkte -->
      <div class="detail-section">
        <p class="detail-section-title">Programmpunkte</p>
        <div v-if="activity.programs.length" class="program-timeline">
          <div v-for="prog in activity.programs" :key="prog.id" class="program-item">
            <span class="program-time">{{ prog.time ? prog.time + ' min' : '—' }}</span>
            <div class="program-body">
              <p class="program-title">{{ prog.title }}</p>
              <p v-if="prog.description" class="program-desc">{{ prog.description }}</p>
              <p v-if="prog.responsible" class="program-resp">{{ prog.responsible }}</p>
            </div>
          </div>
        </div>
        <span v-else class="detail-value detail-value--muted">—</span>
      </div>

      <!-- Material -->
      <div class="detail-section">
        <p class="detail-section-title">Material</p>
        <div v-if="activity.material.length" class="material-chips">
          <span v-for="(m, i) in activity.material" :key="i" class="material-chip">{{ m }}</span>
        </div>
        <span v-else class="detail-value detail-value--muted">—</span>
      </div>

      <!-- SiKo -->
      <div class="detail-section">
        <p class="detail-section-title">Sicherheitskonzept</p>
        <div class="detail-grid">
          <div class="detail-field">
            <span class="detail-label">Benötigt</span>
            <span class="detail-value">{{ activity.needs_siko ? 'Ja' : 'Nein' }}</span>
          </div>
          <div class="detail-field">
            <span class="detail-label">Datei</span>
            <a
              v-if="activity.has_siko"
              :href="`/api/activities/${activity.id}/siko`"
              download
              class="siko-link"
            >📄 SiKo herunterladen</a>
            <span v-else class="detail-value detail-value--muted">—</span>
          </div>
        </div>
      </div>

      <!-- Schlechtwetter -->
      <div class="detail-section">
        <p class="detail-section-title">Schlechtwetter-Info</p>
        <p class="detail-value detail-value--multiline">
          {{ activity.bad_weather_info || '—' }}
        </p>
      </div>

      <!-- Meta -->
      <div class="detail-meta">
        <span>Erstellt: {{ new Date(activity.created_at).toLocaleString('de-DE') }}</span>
        <span>Geändert: {{ new Date(activity.updated_at).toLocaleString('de-DE') }}</span>
      </div>
    </div>

    <!-- =============================================================== EDIT -->
    <div v-else class="detail-form">

      <!-- Titel -->
      <div class="form-group">
        <label for="edit-title">Titel</label>
        <input id="edit-title" v-model="editTitle" type="text"
          placeholder="Titel der Aktivität" autofocus />
      </div>

      <!-- Datum + Zeiten -->
      <div class="form-row form-row--3">
        <div class="form-group">
          <label for="edit-date">Datum</label>
          <input id="edit-date" v-model="editDate" type="date" />
        </div>
        <div class="form-group">
          <label for="edit-start">Startzeit</label>
          <input id="edit-start" v-model="editStartTime" type="time" />
        </div>
        <div class="form-group">
          <label for="edit-end">Endzeit</label>
          <input id="edit-end" v-model="editEndTime" type="time" />
        </div>
      </div>

      <!-- Ziel -->
      <div class="form-group">
        <label for="edit-goal">Ziel</label>
        <textarea id="edit-goal" v-model="editGoal" rows="3"
          placeholder="Was soll erreicht werden?" />
      </div>

      <!-- Ort + Verantwortlich + Abteilung -->
      <div class="form-row form-row--3">
        <div class="form-group">
          <label for="edit-location">Ort</label>
          <input id="edit-location" v-model="editLocation" type="text"
            placeholder="Veranstaltungsort" />
        </div>
        <div class="form-group">
          <label for="edit-responsible">Verantwortlich</label>
          <input id="edit-responsible" v-model="editResponsible" type="text"
            placeholder="Name" />
        </div>
        <div class="form-group">
          <label for="edit-department">Abteilung</label>
          <select id="edit-department" v-model="editDepartment">
            <option value="">Bitte wählen</option>
            <option v-for="dep in departments" :key="dep" :value="dep">{{ dep }}</option>
          </select>
        </div>
      </div>

      <!-- Programmpunkte -->
      <div class="form-section">
        <p class="form-section-title">Programmpunkte</p>
        <div style="display: flex; flex-direction: column; gap: 10px;">
          <div v-for="(prog, i) in editPrograms" :key="i" class="program-card">
            <button type="button" class="program-card__remove" @click="removeProgram(i)">✕</button>
            <div class="program-card__fields">
              <div class="form-group">
                <label>Dauer (Minuten)</label>
                <input
                  type="number" min="0" placeholder="z.B. 30"
                  :value="prog.time"
                  @input="prog.time = ($event.target as HTMLInputElement).value"
                />
              </div>
              <div class="form-group">
                <label>Titel</label>
                <input v-model="prog.title" type="text" placeholder="Programmpunkt-Titel" />
              </div>
              <div class="form-group program-card__full">
                <label>Beschreibung</label>
                <textarea v-model="prog.description" rows="2" placeholder="Beschreibung…" />
              </div>
              <div class="form-group program-card__full">
                <label>Verantwortlich</label>
                <input v-model="prog.responsible" type="text" placeholder="Name" />
              </div>
            </div>
          </div>
          <button type="button" class="btn-add" @click="addProgram">+ Programmpunkt</button>
        </div>
      </div>

      <!-- Material -->
      <div class="form-section">
        <p class="form-section-title">Material</p>
        <div class="material-grid">
          <input
            v-for="(_, i) in editMaterial"
            :key="i"
            v-model="editMaterial[i]"
            type="text"
            placeholder="Material…"
            @input="onMaterialInput(i)"
            @blur="onMaterialBlur(i)"
          />
        </div>
      </div>

      <!-- SiKo -->
      <div class="form-section">
        <p class="form-section-title">Sicherheitskonzept</p>
        <label class="form-check">
          <input type="checkbox" v-model="editNeedsSiko" />
          <span>Sicherheitskonzept benötigt?</span>
        </label>
        <div v-if="editNeedsSiko" style="margin-top: 12px;">
          <div class="form-group">
            <label for="edit-siko">SiKo (PDF)
              <span v-if="activity.has_siko" class="file-hint"> — aktuell vorhanden</span>
            </label>
            <input id="edit-siko" type="file" accept=".pdf" @change="onSikoFileChange" />
            <span v-if="editSikoFile" class="file-name">{{ editSikoFile.name }}</span>
          </div>
        </div>
      </div>

      <!-- Schlechtwetter -->
      <div class="form-group">
        <label for="edit-weather">Schlechtwetter-Info</label>
        <textarea id="edit-weather" v-model="editBadWeather" rows="2"
          placeholder="Was passiert bei schlechtem Wetter?" />
      </div>

      <!-- Error -->
      <p v-if="error" class="error">{{ error }}</p>

      <!-- Actions -->
      <div class="form-actions">
        <button type="button" class="btn-danger" @click="doDelete">Löschen</button>
        <button type="button" class="btn-secondary" @click="mode = 'view'">Schliessen</button>
      </div>

    </div>
  </main>
</template>
