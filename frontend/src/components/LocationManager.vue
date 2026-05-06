<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { MapPin } from 'lucide-vue-next'
import { usePermissions } from '../composables/usePermissions'
import ErrorAlert from './ErrorAlert.vue'
import type { LocationRecord } from '../types'

const {
  fetchLocationsAdmin,
  createLocation,
  updateLocation,
  deleteLocation,
} = usePermissions()

const locations = ref<LocationRecord[]>([])
const loading = ref(false)
const error = ref<string | null>(null)
const saving = ref(false)

const editingId = ref<string | null>(null)
const editName = ref('')
const showAdd = ref(false)
const addName = ref('')
const deleteTarget = ref<LocationRecord | null>(null)

const sortedLocations = computed(() =>
  [...locations.value].sort((a, b) => a.name.localeCompare(b.name, 'de'))
)

onMounted(async () => {
  loading.value = true
  try {
    locations.value = await fetchLocationsAdmin()
  } catch (e) {
    error.value = String(e)
  } finally {
    loading.value = false
  }
})

function startEdit(loc: LocationRecord) {
  editingId.value = loc.id
  editName.value = loc.name
}

function cancelEdit() {
  editingId.value = null
}

async function saveEdit() {
  if (!editingId.value) return
  saving.value = true
  error.value = null
  try {
    const updated = await updateLocation(editingId.value, editName.value.trim())
    const idx = locations.value.findIndex((l: LocationRecord) => l.id === updated.id)
    if (idx !== -1) locations.value[idx] = updated
    editingId.value = null
  } catch (e) {
    error.value = String(e)
  } finally {
    saving.value = false
  }
}

async function handleAdd() {
  if (!addName.value.trim()) return
  saving.value = true
  error.value = null
  try {
    const created = await createLocation(addName.value.trim())
    locations.value.push(created)
    addName.value = ''
    showAdd.value = false
  } catch (e) {
    error.value = String(e)
  } finally {
    saving.value = false
  }
}

async function confirmDelete() {
  if (!deleteTarget.value) return
  saving.value = true
  error.value = null
  try {
    await deleteLocation(deleteTarget.value.id)
    locations.value = locations.value.filter((l: LocationRecord) => l.id !== deleteTarget.value!.id)
    deleteTarget.value = null
  } catch (e) {
    error.value = String(e)
  } finally {
    saving.value = false
  }
}
</script>

<template>
  <div class="dept-manager">
    <div class="section-header">
      <h2>Vordefinierte Orte</h2>
      <p class="section-desc">
        Verwalte die Orte, die beim Erfassen von Aktivitäten aus einer Liste ausgewählt werden können.
        Die Orte gelten für alle Stufen.
      </p>
    </div>

    <div v-if="loading" class="loading">Lade Orte...</div>
    <ErrorAlert :error="error" />

    <template v-if="!loading">
      <div class="item-list">
        <div v-for="loc in sortedLocations" :key="loc.id" class="item-card">
          <template v-if="editingId === loc.id">
            <form class="item-row" @submit.prevent="saveEdit">
              <MapPin class="loc-icon" :size="16" aria-hidden="true" />
              <input v-model="editName" class="form-input" placeholder="Name" required autofocus />
              <div class="item-actions">
                <button type="submit" class="btn-save" :disabled="saving">Speichern</button>
                <button type="button" class="btn-cancel" @click="cancelEdit">Abbrechen</button>
              </div>
            </form>
          </template>
          <template v-else>
            <div class="item-row">
              <MapPin class="loc-icon" :size="16" aria-hidden="true" />
              <span class="item-name">{{ loc.name }}</span>
              <div class="item-actions">
                <button class="btn-edit" @click="startEdit(loc)">Bearbeiten</button>
                <button class="btn-delete" @click="deleteTarget = loc">Löschen</button>
              </div>
            </div>
          </template>
        </div>
        <div v-if="sortedLocations.length === 0 && !showAdd" class="empty-hint">
          Noch keine Orte definiert.
        </div>
      </div>

      <div v-if="showAdd" class="add-form-wrap">
        <form class="add-form" @submit.prevent="handleAdd">
          <div class="add-row">
            <MapPin class="loc-icon" :size="16" aria-hidden="true" />
            <input v-model="addName" class="form-input" placeholder="Neuer Ort" required autofocus />
          </div>
          <div class="item-actions">
            <button type="submit" class="btn-save" :disabled="saving">Hinzufügen</button>
            <button type="button" class="btn-cancel" @click="showAdd = false; addName = ''">Abbrechen</button>
          </div>
        </form>
      </div>
      <button v-else class="btn-add" @click="showAdd = true">
        + Ort hinzufügen
      </button>
    </template>

    <!-- Delete confirmation modal -->
    <div v-if="deleteTarget" class="modal-backdrop" @click.self="deleteTarget = null">
      <div class="modal">
        <h2 class="modal-title">Ort löschen</h2>
        <p class="modal-desc">
          «<strong>{{ deleteTarget.name }}</strong>» wirklich löschen?
          Bestehende Aktivitäten mit diesem Ort behalten ihren Ortseintrag.
        </p>
        <div class="modal-actions">
          <button class="btn-danger" :disabled="saving" @click="confirmDelete">Löschen</button>
          <button class="btn-cancel" @click="deleteTarget = null">Abbrechen</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.dept-manager { max-width: 800px; }

.section-header h2 {
  font-size: 1.15rem;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0 0 4px;
}

.section-desc {
  font-size: 0.85rem;
  color: var(--text-muted);
  margin: 0 0 20px;
}

.loading {
  padding: 24px 0;
  color: var(--text-muted);
}

.item-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.item-card {
  background: var(--card-bg);
  border-radius: 10px;
  box-shadow: 0 1px 6px rgba(0, 0, 0, 0.06);
  padding: 14px 18px;
}

.item-row {
  display: flex;
  align-items: center;
  gap: 12px;
}

.loc-icon {
  color: var(--text-muted);
  flex-shrink: 0;
}

.item-name {
  font-weight: 600;
  font-size: 0.95rem;
  color: var(--text-primary);
  flex: 1;
}

.item-actions {
  display: flex;
  gap: 6px;
}

.form-input {
  flex: 1;
  min-width: 0;
  padding: 7px 10px;
  border: 1.5px solid var(--input-border);
  border-radius: 6px;
  font-size: 0.9rem;
  outline: none;
  background: var(--input-bg);
  color: var(--input-color);
}

.form-input:focus {
  border-color: var(--accent);
}

.btn-edit,
.btn-cancel {
  padding: 5px 12px;
  border-radius: 6px;
  border: 1.5px solid var(--border-strong);
  background: var(--btn-secondary-bg);
  font-size: 0.82rem;
  cursor: pointer;
  color: var(--text-secondary);
}

.btn-edit:hover,
.btn-cancel:hover {
  background: var(--bg-hover);
}

.btn-delete {
  padding: 5px 12px;
  border-radius: 6px;
  border: 1.5px solid #fca5a5;
  background: var(--bg-surface);
  font-size: 0.82rem;
  cursor: pointer;
  color: #dc2626;
}

.btn-delete:hover {
  background: #fef2f2;
}

.btn-save {
  padding: 5px 14px;
  border-radius: 6px;
  border: none;
  background: var(--btn-primary-bg);
  color: #fff;
  font-size: 0.82rem;
  font-weight: 600;
  cursor: pointer;
}

.btn-save:hover:not(:disabled) {
  background: #1648c0;
}

.btn-save:disabled {
  opacity: 0.6;
}

.btn-add {
  margin-top: 12px;
  padding: 10px 18px;
  border-radius: 8px;
  border: 1.5px dashed var(--border-strong);
  background: var(--bg-surface);
  font-size: 0.88rem;
  color: var(--text-muted);
  cursor: pointer;
  width: 100%;
}

.btn-add:hover {
  background: var(--bg-elevated);
  border-color: var(--text-subtle);
}

.add-form-wrap {
  margin-top: 12px;
  background: var(--card-bg);
  border-radius: 10px;
  box-shadow: 0 1px 6px rgba(0, 0, 0, 0.06);
  padding: 14px 18px;
}

.add-form {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.add-row {
  display: flex;
  gap: 8px;
  align-items: center;
}

.empty-hint {
  font-size: 0.9rem;
  color: var(--text-muted);
  padding: 12px 0;
}

.modal-backdrop {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.4);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 100;
}

.modal {
  background: var(--modal-bg);
  border-radius: 16px;
  padding: 28px 32px;
  width: 100%;
  max-width: 500px;
  box-shadow: 0 8px 40px rgba(0, 0, 0, 0.2);
}

.modal-title {
  font-size: 1.1rem;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0 0 6px;
}

.modal-desc {
  font-size: 0.9rem;
  color: var(--text-muted);
  margin: 0 0 18px;
  line-height: 1.5;
}

.modal-actions {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
}

.btn-danger {
  padding: 8px 18px;
  border-radius: 8px;
  border: none;
  background: var(--btn-danger-bg);
  color: var(--btn-danger-color);
  font-size: 0.88rem;
  font-weight: 600;
  cursor: pointer;
}

.btn-danger:hover:not(:disabled) {
  background: #b91c1c;
}

.btn-danger:disabled {
  opacity: 0.6;
}
</style>
