<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useActivities } from '../composables/useActivities'

const router = useRouter()
const { createActivity, error } = useActivities()

const title       = ref('')
const description = ref('')
const responsible = ref('')
const submitting  = ref(false)

async function submit() {
  if (!title.value.trim()) return
  submitting.value = true
  await createActivity({
    title:       title.value.trim(),
    description: description.value.trim(),
    responsible: responsible.value.trim()
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
      <div class="form-group">
        <label for="title">Titel <span class="required">*</span></label>
        <input
          id="title"
          v-model="title"
          type="text"
          placeholder="Titel der Aktivität"
          required
          autofocus
        />
      </div>

      <div class="form-group">
        <label for="description">Beschreibung</label>
        <textarea
          id="description"
          v-model="description"
          rows="5"
          placeholder="Beschreibung..."
        />
      </div>

      <div class="form-group">
        <label for="responsible">Verantwortlich</label>
        <input
          id="responsible"
          v-model="responsible"
          type="text"
          placeholder="Name der verantwortlichen Person"
        />
      </div>

      <p v-if="error" class="error">{{ error }}</p>

      <div class="form-actions">
        <button type="button" class="btn-secondary" @click="router.push('/')">Abbrechen</button>
        <button type="submit" class="btn-primary" :disabled="!title.trim() || submitting">
          {{ submitting ? 'Wird erstellt...' : 'Erstellen' }}
        </button>
      </div>
    </form>
  </main>
</template>
