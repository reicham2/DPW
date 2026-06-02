<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { Package } from 'lucide-vue-next'
import { usePermissions } from '../composables/usePermissions'
import ErrorAlert from './ErrorAlert.vue'
import type { MaterialNameRecord } from '../types'

const {
  fetchMaterialsAdmin,
  createMaterial,
  updateMaterial,
  deleteMaterial,
} = usePermissions()

const materials = ref<MaterialNameRecord[]>([])
const loading = ref(false)
const error = ref<string | null>(null)
const saving = ref(false)

const editingId = ref<string | null>(null)
const editName = ref('')
const showAdd = ref(false)
const addName = ref('')
const deleteTarget = ref<MaterialNameRecord | null>(null)

const sortedMaterials = computed(() =>
  [...materials.value].sort((a, b) => a.name.localeCompare(b.name, 'de'))
)

onMounted(async () => {
  loading.value = true
  try {
    materials.value = await fetchMaterialsAdmin()
  } catch (e) {
    error.value = String(e)
  } finally {
    loading.value = false
  }
})

function startEdit(m: MaterialNameRecord) {
  editingId.value = m.id
  editName.value = m.name
}

function cancelEdit() {
  editingId.value = null
}

async function saveEdit() {
  if (!editingId.value) return
  saving.value = true
  error.value = null
  try {
    const updated = await updateMaterial(editingId.value, editName.value.trim())
    const idx = materials.value.findIndex((m: MaterialNameRecord) => m.id === updated.id)
    if (idx !== -1) materials.value[idx] = updated
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
    const created = await createMaterial(addName.value.trim())
    materials.value.push(created)
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
    await deleteMaterial(deleteTarget.value.id)
    materials.value = materials.value.filter((m: MaterialNameRecord) => m.id !== deleteTarget.value!.id)
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
      <h2>Vordefinierte Materialverantwortliche</h2>
      <p class="section-desc">
        Verwalte die vordefinierten Einträge, die beim Erfassen von Aktivitäten im Feld «Verantwortlich» für Materialien ausgewählt werden können (z.B. Einkaufen, Pfadiheim).
        Die Einträge gelten für alle Stufen.
      </p>
    </div>

    <div v-if="loading" class="loading">Lade Einträge...</div>
    <ErrorAlert :error="error" />

    <template v-if="!loading">
      <div class="item-list">
        <div v-for="m in sortedMaterials" :key="m.id" class="item-card">
          <template v-if="editingId === m.id">
            <form class="item-row" @submit.prevent="saveEdit">
              <Package class="mat-icon" :size="16" aria-hidden="true" />
              <input v-model="editName" class="form-input" placeholder="Name" required autofocus />
              <div class="item-actions">
                <button type="submit" class="btn-save" :disabled="saving">Speichern</button>
                <button type="button" class="btn-cancel" @click="cancelEdit">Abbrechen</button>
              </div>
            </form>
          </template>
          <template v-else>
            <div class="item-row">
              <Package class="mat-icon" :size="16" aria-hidden="true" />
              <span class="item-name">{{ m.name }}</span>
              <div class="item-actions">
                <button class="btn-edit" @click="startEdit(m)">Bearbeiten</button>
                <button class="btn-delete" @click="deleteTarget = m">Löschen</button>
              </div>
            </div>
          </template>
        </div>
        <div v-if="sortedMaterials.length === 0 && !showAdd" class="empty-hint">
          Noch keine Einträge definiert.
        </div>
      </div>

      <div v-if="showAdd" class="add-form-wrap">
        <form class="add-form" @submit.prevent="handleAdd">
          <div class="add-row">
            <Package class="mat-icon" :size="16" aria-hidden="true" />
            <input v-model="addName" class="form-input" placeholder="Neuer Eintrag (z.B. Einkaufen)" required autofocus />
          </div>
          <div class="item-actions">
            <button type="submit" class="btn-save" :disabled="saving">Hinzufügen</button>
            <button type="button" class="btn-cancel" @click="showAdd = false; addName = ''">Abbrechen</button>
          </div>
        </form>
      </div>
      <button v-else class="btn-add" @click="showAdd = true">
        + Eintrag hinzufügen
      </button>
    </template>

    <!-- Delete confirmation modal -->
    <div v-if="deleteTarget" class="modal-backdrop" @click.self="deleteTarget = null">
      <div class="modal">
        <h2 class="modal-title">Eintrag löschen</h2>
        <p class="modal-desc">
          «<strong>{{ deleteTarget.name }}</strong>» wirklich löschen?
          Bestehende Aktivitäten mit diesem Eintrag behalten ihre Verantwortlichen.
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

.mat-icon {
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
  border: 1.5px solid var(--error-border);
  background: var(--bg-surface);
  font-size: 0.82rem;
  cursor: pointer;
  color: var(--error-color);
}

.btn-delete:hover {
  background: var(--error-bg);
}

.btn-save {
  padding: 5px 14px;
  border-radius: 6px;
  border: none;
  background: var(--btn-primary-bg);
  color: var(--btn-primary-color);
  font-size: 0.82rem;
  font-weight: 600;
  cursor: pointer;
}

.btn-save:hover:not(:disabled) {
  background: var(--btn-primary-bg-hover);
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
  background: var(--btn-danger-bg-hover);
}

.btn-danger:disabled {
  opacity: 0.6;
}
</style>
