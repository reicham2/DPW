<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useActivities } from '../composables/useActivities'
import { useUsers } from '../composables/useUsers'
import { user } from '../composables/useAuth'
import type { Department, ProgramInput, MaterialItem } from '../types'

const router = useRouter()
const { createActivity, fetchDepartments, departments, error } = useActivities()
const { users, fetchUsers } = useUsers()

onMounted(async () => {
  await Promise.all([fetchDepartments(), fetchUsers()])
})

// ---- Form state ------------------------------------------------------------
const title          = ref('')
const date           = ref('')
const start_time     = ref('')
const end_time       = ref('')
const goal           = ref('')
const location       = ref('')
const responsible    = ref<string[]>(user.value ? [user.value.display_name] : [])
const department     = ref<Department | ''>('Pfadi')
const material       = ref<MaterialItem[]>([{ name: '', responsible: '' }])
const needs_siko     = ref(false)
const sikoFile       = ref<File | null>(null)
const sikoBase64     = ref<string | null>(null)
const bad_weather    = ref('')
const programs       = ref<ProgramInput[]>([])
const submitting     = ref(false)

// ---- Material --------------------------------------------------------------
function onMaterialInput(i: number) {
  const isLast = i === material.value.length - 1
  if (isLast && material.value[i].name !== '') {
    material.value.push({ name: '', responsible: '' })
  }
}
function onMaterialBlur(i: number) {
  const isLast = i === material.value.length - 1
  if (!isLast && material.value[i].name === '') {
    material.value.splice(i, 1)
  }
}

// ---- Material responsible search -------------------------------------------
const materialRespSearch = ref<Record<number, string>>({})
const materialRespDropdown = ref<number | null>(null)

function materialRespFiltered(i: number) {
  const q = (materialRespSearch.value[i] ?? '').toLowerCase()
  return users.value.filter(u => q === '' || u.display_name.toLowerCase().includes(q))
}
function setMaterialResp(i: number, name: string) {
  material.value[i].responsible = name
  materialRespSearch.value[i] = ''
  materialRespDropdown.value = null
}
function clearMaterialResp(i: number) {
  material.value[i].responsible = ''
}
function onMaterialRespBlur(i: number) {
  setTimeout(() => {
    materialRespDropdown.value = null
    delete materialRespSearch.value[i]
  }, 200)
}

// ---- Responsible search ----------------------------------------------------
const responsibleSearch = ref('')
const showResponsibleDropdown = ref(false)

const filteredResponsibleUsers = computed(() => {
  const q = responsibleSearch.value.toLowerCase()
  return users.value.filter(
    (u) =>
      !responsible.value.includes(u.display_name) &&
      (q === '' || u.display_name.toLowerCase().includes(q)),
  )
})

function addResponsible(name: string) {
  if (!responsible.value.includes(name)) {
    responsible.value.push(name)
  }
  responsibleSearch.value = ''
  showResponsibleDropdown.value = false
}

function removeResponsible(i: number) {
  responsible.value.splice(i, 1)
}

function onResponsibleBlur() {
  setTimeout(() => {
    showResponsibleDropdown.value = false
    responsibleSearch.value = ''
  }, 200)
}

// ---- Programs --------------------------------------------------------------
function addProgram() {
  programs.value.push({ time: '', title: '', description: '', responsible: responsible.value[0] ?? '' })
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
    responsible:     responsible.value,
    department:      department.value || null,
    material:        material.value.filter(m => m.name.trim()).map(m => ({ name: m.name.trim(), ...(m.responsible?.trim() ? { responsible: m.responsible.trim() } : {}) })),
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

      <!-- Ort + Verantwortlich + Abteilung -->
      <div class="form-row form-row--3">
        <div class="form-group">
          <label for="location">Ort</label>
          <input id="location" v-model="location" type="text" placeholder="Veranstaltungsort" />
        </div>
        <div class="form-group user-search-group">
          <label>Verantwortlich</label>
          <div class="user-chips" v-if="responsible.length">
            <span v-for="(name, i) in responsible" :key="name" class="user-chip">
              {{ name }}
              <button type="button" class="user-chip-remove" @click="removeResponsible(i)">✕</button>
            </span>
          </div>
          <div class="user-search-wrapper">
            <input
              type="text"
              v-model="responsibleSearch"
              placeholder="Person suchen…"
              @focus="showResponsibleDropdown = true"
              @blur="onResponsibleBlur"
            />
            <div v-if="showResponsibleDropdown && filteredResponsibleUsers.length" class="user-dropdown">
              <div
                v-for="u in filteredResponsibleUsers"
                :key="u.id"
                class="user-dropdown-item"
                @mousedown.prevent="addResponsible(u.display_name)"
              >
                {{ u.display_name }}
              </div>
            </div>
          </div>
        </div>
        <div class="form-group">
          <label for="department">Abteilung</label>
          <select id="department" v-model="department">
            <option value="">Bitte wählen</option>
            <option v-for="dep in departments" :key="dep" :value="dep">{{ dep }}</option>
          </select>
        </div>
      </div>

      <!-- Programmpunkte -->
      <div class="form-section">
        <p class="form-section-title">Programmpunkte</p>
        <div style="display: flex; flex-direction: column; gap: 10px;">
          <div v-for="(prog, i) in programs" :key="i" class="program-card">
            <button type="button" class="program-card__remove" @click="removeProgram(i)">✕</button>
            <div class="program-card__fields">
              <div class="form-group">
                <label>Minuten</label>
                <input
                  type="number" min="0" placeholder="30"
                  :value="prog.time"
                  @input="prog.time = ($event.target as HTMLInputElement).value"
                />
              </div>
              <div class="form-group">
                <label>Titel</label>
                <input v-model="prog.title" type="text" placeholder="Titel" />
              </div>
              <div class="form-group">
                <label>Verantwortlich</label>
                <select v-model="prog.responsible">
                  <option value="" disabled>Bitte wählen</option>
                  <option v-for="u in users" :key="u.id" :value="u.display_name">{{ u.display_name }}</option>
                </select>
              </div>
              <div class="form-group program-card__full">
                <label>Beschreibung</label>
                <textarea v-model="prog.description" rows="2" placeholder="Beschreibung..." />
              </div>
            </div>
          </div>
          <button type="button" class="btn-add" @click="addProgram">+ Programmpunkt</button>
        </div>
      </div>

      <!-- Material -->
      <div class="form-section">
        <p class="form-section-title">Material</p>
        <div class="material-list">
          <div v-for="(_, i) in material" :key="i" class="material-row">
            <input
              v-model="material[i].name"
              type="text"
              placeholder="Material…"
              class="material-row__name"
              @input="onMaterialInput(i)"
              @blur="onMaterialBlur(i)"
            />
            <div class="material-row__responsible user-search-wrapper">
              <template v-if="material[i].responsible">
                <span class="material-resp-chip">
                  {{ material[i].responsible }}
                  <button type="button" class="user-chip-remove" @click="clearMaterialResp(i)">✕</button>
                </span>
              </template>
              <template v-else>
                <input
                  type="text"
                  :value="materialRespSearch[i] ?? ''"
                  @input="materialRespSearch[i] = ($event.target as HTMLInputElement).value"
                  placeholder="Verantwortlich (optional)"
                  @focus="materialRespDropdown = i"
                  @blur="onMaterialRespBlur(i)"
                />
                <div v-if="materialRespDropdown === i && materialRespFiltered(i).length" class="user-dropdown">
                  <div
                    v-for="u in materialRespFiltered(i)"
                    :key="u.id"
                    class="user-dropdown-item"
                    @mousedown.prevent="setMaterialResp(i, u.display_name)"
                  >
                    {{ u.display_name }}
                  </div>
                </div>
              </template>
            </div>
          </div>
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

      <!-- Ziel + Schlechtwetter -->
      <div class="form-row">
        <div class="form-group">
          <label for="goal">Ziel</label>
          <textarea id="goal" v-model="goal" rows="3"
            placeholder="Was soll erreicht werden?" />
        </div>
        <div class="form-group">
          <label for="bad_weather">Schlechtwetter-Info</label>
          <textarea id="bad_weather" v-model="bad_weather" rows="3"
            placeholder="Was passiert bei schlechtem Wetter?" />
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
