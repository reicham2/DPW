<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useActivities } from '../composables/useActivities'
import type { Department, ProgramInput } from '../types'

const router = useRouter()
const { createActivity, fetchDepartments, departments, error } = useActivities()

onMounted(fetchDepartments)

// ---- Form state ------------------------------------------------------------
const title          = ref('')
const date           = ref('')
const start_time     = ref('')
const end_time       = ref('')
const goal           = ref('')
const location       = ref('')
const responsible    = ref('')
const department     = ref<Department | ''>('Pfadi')
const material       = ref<string[]>([''])
const needs_siko     = ref(false)
const sikoFile       = ref<File | null>(null)
const sikoBase64     = ref<string | null>(null)
const bad_weather    = ref('')
const programs       = ref<ProgramInput[]>([])
const submitting     = ref(false)

// ---- Material --------------------------------------------------------------
function onMaterialInput(i: number) {
  const val    = material.value[i]
  const isLast = i === material.value.length - 1
  if (isLast && val !== '') {
    material.value.push('')
  } else if (!isLast && val === '') {
    material.value.splice(i, 1)
  }
}

// ---- Programs --------------------------------------------------------------
function addProgram() {
  programs.value.push({ time: '', title: '', description: '', responsible: '' })
}
function removeProgram(i: number) { programs.value.splice(i, 1) }

// ---- SiKo file -------------------------------------------------------------
async function onSikoFileChange(e: Event) {
  const file = (e.target as HTMLInputElement).files?.[0]
  if (!file) { sikoFile.value = null; sikoBase64.value = null; return }
  sikoFile.value = file
  const buf = await file.arrayBuffer()
  const bytes = new Uint8Array(buf)
  let binary = ''
  for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i])
  sikoBase64.value = btoa(binary)
}

// ---- Submit ----------------------------------------------------------------
async function submit() {
  submitting.value = true
  error.value = null

  await createActivity({
    title:           title.value.trim(),
    date:            date.value,
    start_time:      start_time.value,
    end_time:        end_time.value,
    goal:            goal.value.trim(),
    location:        location.value.trim(),
    responsible:     responsible.value.trim(),
    department:      department.value || null,
    material:        material.value.filter(m => m.trim()),
    needs_siko:      needs_siko.value,
    siko_base64:     sikoBase64.value ?? undefined,
    bad_weather_info: bad_weather.value.trim() || null,
    programs:        programs.value
  })

  submitting.value = false
  if (!error.value) router.push('/')
}
</script>

<template>
  <header class="header">
    <button class="btn-back" @click="router.push('/')">← Zurück</button>
    <h1>Neue Aktivität</h1>
  </header>

  <main class="main">
    <form class="detail-form" @submit.prevent="submit">

      <!-- Titel -->
      <div class="form-group">
        <label for="title">Titel</label>
        <input id="title" v-model="title" type="text" placeholder="Titel der Aktivität"
          autofocus />
      </div>

      <!-- Datum + Zeiten -->
      <div class="form-row form-row--3">
        <div class="form-group">
          <label for="date">Datum</label>
          <input id="date" v-model="date" type="date" />
        </div>
        <div class="form-group">
          <label for="start_time">Startzeit</label>
          <input id="start_time" v-model="start_time" type="time" />
        </div>
        <div class="form-group">
          <label for="end_time">Endzeit</label>
          <input id="end_time" v-model="end_time" type="time" />
        </div>
      </div>

      <!-- Ziel -->
      <div class="form-group">
        <label for="goal">Ziel</label>
        <textarea id="goal" v-model="goal" rows="3"
          placeholder="Was soll erreicht werden?" />
      </div>

      <!-- Ort + Verantwortlich + Abteilung -->
      <div class="form-row form-row--3">
        <div class="form-group">
          <label for="location">Ort</label>
          <input id="location" v-model="location" type="text" placeholder="Veranstaltungsort" />
        </div>
        <div class="form-group">
          <label for="responsible">Verantwortlich</label>
          <input id="responsible" v-model="responsible" type="text" placeholder="Name" />
        </div>
        <div class="form-group">
          <label for="department">Abteilung</label>
          <select id="department" v-model="department">
            <option value="">Bitte wählen</option>
            <option v-for="dep in departments" :key="dep" :value="dep">{{ dep }}</option>
          </select>
        </div>
      </div>

      <!-- Material -->
      <div class="form-section">
        <p class="form-section-title">Material</p>
        <div class="material-grid">
          <input
            v-for="(_, i) in material"
            :key="i"
            v-model="material[i]"
            type="text"
            placeholder="Material…"
            @input="onMaterialInput(i)"
          />
        </div>
      </div>

      <!-- SiKo -->
      <div class="form-section">
        <p class="form-section-title">Sicherheitskonzept</p>
        <label class="form-check">
          <input type="checkbox" v-model="needs_siko" />
          <span>Sicherheitskonzept benötigt?</span>
        </label>
        <div v-if="needs_siko" style="margin-top: 12px;">
          <div class="form-group">
            <label for="siko_file">SiKo (PDF)</label>
            <input id="siko_file" type="file" accept=".pdf" @change="onSikoFileChange" />
            <span v-if="sikoFile" class="file-name">{{ sikoFile.name }}</span>
          </div>
        </div>
      </div>

      <!-- Schlechtwetter -->
      <div class="form-group">
        <label for="bad_weather">Schlechtwetter-Info</label>
        <textarea id="bad_weather" v-model="bad_weather" rows="2"
          placeholder="Was passiert bei schlechtem Wetter?" />
      </div>

      <!-- Programmpunkte -->
      <div class="form-section">
        <p class="form-section-title">Programmpunkte</p>
        <div style="display: flex; flex-direction: column; gap: 10px;">
          <div v-for="(prog, i) in programs" :key="i" class="program-card">
            <button type="button" class="program-card__remove" @click="removeProgram(i)">✕</button>
            <div class="program-card__fields">
              <div class="form-group">
                <label>Zeit</label>
                <input v-model="prog.time" type="time" />
              </div>
              <div class="form-group">
                <label>Titel</label>
                <input v-model="prog.title" type="text" placeholder="Programmpunkt-Titel" />
              </div>
              <div class="form-group program-card__full">
                <label>Beschreibung</label>
                <textarea v-model="prog.description" rows="2" placeholder="Beschreibung..." />
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

      <!-- Error -->
      <p v-if="error" class="error">{{ error }}</p>

      <!-- Actions -->
      <div class="form-actions">
        <button type="button" class="btn-secondary" @click="router.push('/')">Abbrechen</button>
        <button type="submit" class="btn-primary" :disabled="submitting">
          {{ submitting ? 'Wird erstellt…' : 'Erstellen' }}
        </button>
      </div>

    </form>
  </main>
</template>
