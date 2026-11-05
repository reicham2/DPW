<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useActivities } from '../composables/useActivities'
import { usePermissions } from '../composables/usePermissions'
import { activityGuidingThreadSupported } from '../composables/useSetupAuthConfig'
import ErrorAlert from '../components/ErrorAlert.vue'
import BadgeSelect from '../components/BadgeSelect.vue'
import { ArrowLeft } from 'lucide-vue-next'
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
const guiding_thread = ref('')
const createdActivityId = ref<string | null>(null)
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
  if (!department.value || createdActivityId.value) return
  const guidingThread = guiding_thread.value.trim()
  if (guidingThread && !activityGuidingThreadSupported.value) {
    error.value = 'Der rote Faden kann erst nach dem Backend-Update gespeichert werden. Bitte den Text bis dahin kopieren.'
    return
  }
  submitting.value = true
  error.value = null

  const created = await createActivity({
    title:           title.value.trim(),
    date:            date.value,
    start_time:      start_time.value,
    end_time:        end_time.value,
    goal:            '',
    guiding_thread:  guidingThread || null,
    location:        '',
    responsible:     user.value ? [user.value.id] : [],
    department:      department.value,
    material:        [],
    tn_material:     [],
    bad_weather_info: null,
    programs:        []
  })

  submitting.value = false
  if (!error.value && created) {
    if (created.guiding_thread !== undefined && created.guiding_thread !== null && typeof created.guiding_thread !== 'string') {
      createdActivityId.value = created.id
      error.value = 'Die Aktivität wurde erstellt, aber die Serverantwort zum roten Faden ist ungültig. Bitte die Aktivität überprüfen.' +
        (guidingThread ? ' Den Text bitte kopieren, bis er gespeichert ist.' : '')
      return
    }
    if (guidingThread && created.guiding_thread !== guidingThread) {
      createdActivityId.value = created.id
      error.value = 'Die Aktivität wurde erstellt, aber der rote Faden wurde nicht gespeichert. Bitte den Text kopieren und nach dem Backend-Update in der Aktivität ergänzen.'
      return
    }
    router.push(`/activities/${created.id}`)
  }
}
</script>

<template>
  <header class="header">
    <button class="btn-back" @click="router.push('/')"><ArrowLeft class="btn-icon" :size="16" aria-hidden="true" /> Zurück</button>
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

      <div class="form-group">
        <label for="guiding-thread">Roter Faden</label>
        <textarea id="guiding-thread" v-model="guiding_thread" rows="4"
          :disabled="!activityGuidingThreadSupported && !guiding_thread && !createdActivityId"
          :readonly="!activityGuidingThreadSupported || !!createdActivityId"
          placeholder="Geschichte oder thematischer Rahmen der Aktivität (optional)" />
        <p v-if="!activityGuidingThreadSupported && !createdActivityId" class="field-hint">
          Der rote Faden ist nach dem Backend-Update verfügbar. Bitte danach die Seite neu laden.
        </p>
      </div>

      <!-- Error -->
      <ErrorAlert :error="error" />

      <!-- Actions -->
      <div class="form-actions">
        <button type="button" class="btn-secondary" @click="router.push('/')">Abbrechen</button>
        <button v-if="createdActivityId" type="button" class="btn-primary" @click="router.push(`/activities/${createdActivityId}`)">
          Zur erstellten Aktivität
        </button>
        <button v-else type="submit" class="btn-primary" :disabled="submitting">
          {{ submitting ? 'Wird erstellt…' : 'Erstellen' }}
        </button>
      </div>

    </form>
  </main>
</template>
