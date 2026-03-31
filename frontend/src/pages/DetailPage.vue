<script setup lang="ts">
import { ref, watch, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useActivities } from '../composables/useActivities'
import type { Activity } from '../types'

const route  = useRoute()
const router = useRouter()
const id = route.params.id as string

const { fetchActivity, updateActivity, deleteActivity, error, lastUpdatedActivity } = useActivities()

const activity  = ref<Activity | null>(null)
const loading   = ref(true)
const mode      = ref<'view' | 'edit'>('view')
const saving    = ref(false)

const editTitle       = ref('')
const editDescription = ref('')
const editResponsible = ref('')

let debounceTimer: ReturnType<typeof setTimeout> | null = null

onMounted(async () => {
  activity.value = await fetchActivity(id)
  loading.value = false
  syncEditFields()
})

function syncEditFields() {
  if (!activity.value) return
  editTitle.value       = activity.value.title
  editDescription.value = activity.value.description
  editResponsible.value = activity.value.responsible
}

function enterEdit() {
  syncEditFields()
  mode.value = 'edit'
}

// Apply incoming WS updates only in view mode (don't disrupt active editing)
watch(lastUpdatedActivity, (updated) => {
  if (updated && updated.id === id && mode.value === 'view') {
    activity.value = updated
  }
})

function scheduleSave() {
  if (debounceTimer) clearTimeout(debounceTimer)
  debounceTimer = setTimeout(save, 400)
}

async function save() {
  if (!activity.value) return
  saving.value = true
  await updateActivity(id, {
    text:        activity.value.text,
    title:       editTitle.value,
    description: editDescription.value,
    responsible: editResponsible.value
  })
  saving.value = false
}

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
      <span v-if="saving" class="saving-badge">Speichert...</span>
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
    <p v-if="loading" class="loading">Laden...</p>
    <p v-else-if="!activity" class="error">Aktivität nicht gefunden.</p>

    <!-- View mode -->
    <div v-else-if="mode === 'view'" class="detail-view">
      <div class="detail-field">
        <span class="detail-label">Titel</span>
        <p class="detail-value">{{ activity.title || '—' }}</p>
      </div>
      <div class="detail-field">
        <span class="detail-label">Beschreibung</span>
        <p class="detail-value detail-value--multiline">{{ activity.description || '—' }}</p>
      </div>
      <div class="detail-field">
        <span class="detail-label">Verantwortlich</span>
        <p class="detail-value">{{ activity.responsible || '—' }}</p>
      </div>
      <div class="detail-meta">
        <span>Erstellt: {{ new Date(activity.created_at).toLocaleString('de-DE') }}</span>
        <span>Geändert: {{ new Date(activity.updated_at).toLocaleString('de-DE') }}</span>
      </div>
    </div>

    <!-- Edit mode -->
    <form v-else class="detail-form" @submit.prevent="save">
      <div class="form-group">
        <label for="edit-title">Titel</label>
        <input
          id="edit-title"
          v-model="editTitle"
          type="text"
          placeholder="Titel"
          @input="scheduleSave"
        />
      </div>

      <div class="form-group">
        <label for="edit-desc">Beschreibung</label>
        <textarea
          id="edit-desc"
          v-model="editDescription"
          rows="6"
          placeholder="Beschreibung..."
          @input="scheduleSave"
        />
      </div>

      <div class="form-group">
        <label for="edit-resp">Verantwortlich</label>
        <input
          id="edit-resp"
          v-model="editResponsible"
          type="text"
          placeholder="Name"
          @input="scheduleSave"
        />
      </div>

      <p v-if="error" class="error">{{ error }}</p>

      <div class="form-actions">
        <button type="button" class="btn-danger" @click="doDelete">Löschen</button>
        <button type="submit" class="btn-primary" :disabled="saving">Speichern</button>
      </div>
    </form>
  </main>
</template>
