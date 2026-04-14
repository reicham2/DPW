<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useActivities } from '../composables/useActivities'
import ErrorAlert from '../components/ErrorAlert.vue'
import { user } from '../composables/useAuth'

const router = useRouter()
const { createActivity, error } = useActivities()

// ---- Form state ------------------------------------------------------------
const title      = ref('')
const date       = ref('')
const start_time = ref('')
const end_time   = ref('')
const submitting = ref(false)

// ---- Submit ----------------------------------------------------------------
async function submit() {
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
    department:      user.value?.department ?? null,
    material:        [],
    needs_siko:      false,
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
