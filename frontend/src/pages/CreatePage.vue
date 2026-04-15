<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useActivities } from '../composables/useActivities'
import { usePermissions } from '../composables/usePermissions'
import ErrorAlert from '../components/ErrorAlert.vue'
import BadgeSelect from '../components/BadgeSelect.vue'
import { user } from '../composables/useAuth'
import type { Department } from '../types'

const router = useRouter()
const { createActivity, error } = useActivities()
const { fetchDepartments, fetchMyPermissions, writableDepts } = usePermissions()

// ---- Form state ------------------------------------------------------------
const title      = ref('')
const date       = ref('')
const start_time = ref('')
const end_time   = ref('')
const department = ref<Department | ''>('')
const submitting = ref(false)
const loaded     = ref(false)

const availableDepts = computed(() => writableDepts(user.value?.department))
const onlyOneDept = computed(() => availableDepts.value.length === 1)
const availableDeptItems = computed(() => availableDepts.value.map(d => ({ value: d })))

onMounted(async () => {
  await Promise.all([fetchDepartments(), fetchMyPermissions()])
  const depts = availableDepts.value
  if (depts.length === 0) {
    router.replace('/')
    return
  }
  // Pre-select: own dept if writable, else the single available dept
  const userDept = user.value?.department
  if (userDept && depts.includes(userDept)) {
    department.value = userDept
  } else if (depts.length === 1) {
    department.value = depts[0]
  }
  loaded.value = true
})

// ---- Submit ----------------------------------------------------------------
async function submit() {
  if (!department.value) return
  submitting.value = true
  error.value = null

  const id = await createActivity({
    title:           title.value.trim(),
    date:            date.value,
    start_time:      start_time.value,
    end_time:        end_time.value,
    goal:            '',
    location:        '',
    responsible:     user.value ? [user.value.display_name] : [],
    department:      department.value,
    material:        [],
    bad_weather_info: null,
    programs:        []
  })

  submitting.value = false
  if (!error.value && id) router.push(`/activities/${id}`)
}
</script>

<template>
  <header class="header">
    <button class="btn-back" @click="router.push('/')">← Zurück</button>
    <h1>Neue Aktivität</h1>
  </header>

  <main class="main">
    <p v-if="!loaded" class="loading">Laden…</p>
    <form v-else class="detail-form" @submit.prevent="submit">

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

      <!-- Stufe -->
      <div class="form-group">
        <label for="department">Stufe</label>
        <BadgeSelect
          kind="department"
          :items="availableDeptItems"
          :disabled="onlyOneDept"
          placeholder="Stufe wählen…"
          :model-value="department || null"
          @update:model-value="(v) => department = (v ?? '') as Department | ''"
        />
      </div>

      <!-- Error -->
      <ErrorAlert :error="error" />

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
