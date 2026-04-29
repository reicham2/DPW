<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { Plus, Pencil, Trash2, X, Check } from 'lucide-vue-next'
import { useRouter } from 'vue-router'
import { usePermissions } from '../composables/usePermissions'
import { useIdeenkiste } from '../composables/useIdeenkiste'
import DepartmentBadge from '../components/DepartmentBadge.vue'
import BadgeSelect from '../components/BadgeSelect.vue'
import ErrorAlert from '../components/ErrorAlert.vue'
import type { IdeenkisteItem } from '../types'

const router = useRouter()
const { myPermissions, fetchMyPermissions, departments, fetchDepartments } = usePermissions()
const { items, loading, error, fetchItems, createItem, updateItem, deleteItem } = useIdeenkiste()

const isOwnDeptOnly = computed(() => myPermissions.value?.ideenkiste_scope === 'own_dept')
const canAll = computed(() => myPermissions.value?.ideenkiste_scope === 'all')

const searchQuery = ref('')
const filterDept = ref<string | null>(null)
const deptItems = computed(() => departments.value.map(d => ({ value: d.name })))

const filtered = computed(() => {
  let list = items.value
  if (filterDept.value)
    list = list.filter(i => i.department === filterDept.value)
  if (searchQuery.value.trim()) {
    const q = searchQuery.value.toLowerCase()
    list = list.filter(i => i.title.toLowerCase().includes(q) || i.description.toLowerCase().includes(q))
  }
  return list
})

// New item form
const showNewForm = ref(false)
const newTitle = ref('')
const newDuration = ref(0)
const newDescription = ref('')
const newDepartment = ref<string | null>(null)
const saving = ref(false)
const saveError = ref<string | null>(null)

// Edit inline
const editingId = ref<string | null>(null)
const editTitle = ref('')
const editDuration = ref(0)
const editDescription = ref('')
const editDepartment = ref<string | null>(null)
const editSaving = ref(false)
const editError = ref<string | null>(null)

// Delete confirm
const deleteTarget = ref<IdeenkisteItem | null>(null)
const deleteError = ref<string | null>(null)

function openEdit(item: IdeenkisteItem) {
  editingId.value = item.id
  editTitle.value = item.title
  editDuration.value = item.duration_minutes
  editDescription.value = item.description
  editDepartment.value = item.department
  editError.value = null
}

function cancelEdit() {
  editingId.value = null
}

async function saveEdit() {
  if (!editingId.value) return
  editSaving.value = true
  editError.value = null
  const result = await updateItem(editingId.value, {
    title: editTitle.value.trim(),
    duration_minutes: editDuration.value,
    description: editDescription.value,
    department: isOwnDeptOnly.value ? myPermissions.value?.role : editDepartment.value,
  })
  editSaving.value = false
  if (result) editingId.value = null
  else editError.value = 'Fehler beim Speichern.'
}

async function submitNew() {
  saving.value = true
  saveError.value = null
  const result = await createItem({
    title: newTitle.value.trim(),
    duration_minutes: newDuration.value,
    description: newDescription.value,
    department: newDepartment.value,
  })
  saving.value = false
  if (result) {
    showNewForm.value = false
    newTitle.value = ''
    newDuration.value = 0
    newDescription.value = ''
    newDepartment.value = null
  } else {
    saveError.value = 'Fehler beim Erstellen.'
  }
}

async function confirmDelete() {
  if (!deleteTarget.value) return
  deleteError.value = null
  const ok = await deleteItem(deleteTarget.value.id)
  if (ok) deleteTarget.value = null
  else deleteError.value = 'Fehler beim Löschen.'
}

function formatDuration(minutes: number): string {
  if (minutes === 0) return '–'
  if (minutes < 60) return `${minutes} min`
  const h = Math.floor(minutes / 60)
  const m = minutes % 60
  return m === 0 ? `${h} h` : `${h} h ${m} min`
}

onMounted(async () => {
  await fetchMyPermissions()
  if (!myPermissions.value || myPermissions.value.ideenkiste_scope === 'none') {
    router.replace('/')
    return
  }
  await Promise.all([fetchItems(), fetchDepartments()])
  if (isOwnDeptOnly.value && myPermissions.value) {
    // pre-set filter/department to own dept
  }
})
</script>

<template>
  <header class="header">
    <h1>Ideenkiste</h1>
  </header>

  <main class="main">
    <!-- Toolbar -->
    <div class="ideenkiste-toolbar">
      <input
        v-model="searchQuery"
        class="ideenkiste-search"
        type="search"
        placeholder="Suchen…"
      />
      <BadgeSelect
        v-if="canAll"
        kind="department"
        :items="deptItems"
        allow-empty
        placeholder="Alle Stufen"
        :model-value="filterDept"
        @update:model-value="(v) => filterDept = v"
      />
      <button class="btn-primary ideenkiste-add-btn" @click="showNewForm = true">
        <Plus :size="16" aria-hidden="true" />
        Neuer Eintrag
      </button>
    </div>

    <!-- New item form -->
    <div v-if="showNewForm" class="ideenkiste-form-card">
      <h3 class="ideenkiste-form-title">Neuer Eintrag</h3>
      <form @submit.prevent="submitNew" class="ideenkiste-form">
        <div class="form-group">
          <label class="form-label">Titel *</label>
          <input v-model="newTitle" class="form-input" required placeholder="Titel" />
        </div>
        <div class="form-group">
          <label class="form-label">Dauer (Minuten)</label>
          <input v-model.number="newDuration" type="number" min="0" class="form-input" />
        </div>
        <div v-if="canAll" class="form-group">
          <label class="form-label">Stufe</label>
          <select v-model="newDepartment" class="form-input">
            <option :value="null">Keine Angabe</option>
            <option v-for="d in departments" :key="d.name" :value="d.name">{{ d.name }}</option>
          </select>
        </div>
        <div class="form-group">
          <label class="form-label">Beschreibung</label>
          <textarea v-model="newDescription" class="form-input ideenkiste-textarea" rows="4" placeholder="Beschreibung…" />
        </div>
        <div v-if="saveError" class="error-msg"><ErrorAlert :error="saveError" /></div>
        <div class="ideenkiste-form-actions">
          <button type="button" class="btn-cancel" @click="showNewForm = false">Abbrechen</button>
          <button type="submit" class="btn-primary" :disabled="saving || !newTitle.trim()">
            {{ saving ? 'Speichern…' : 'Erstellen' }}
          </button>
        </div>
      </form>
    </div>

    <!-- Loading / error -->
    <div v-if="loading" class="loading">Lade Ideenkiste…</div>
    <div v-else-if="error" class="error-msg"><ErrorAlert :error="error" /></div>

    <!-- Item list -->
    <template v-else>
      <p v-if="filtered.length === 0" class="ideenkiste-empty">Keine Einträge gefunden.</p>
      <div v-else class="ideenkiste-list">
        <div v-for="item in filtered" :key="item.id" class="ideenkiste-card">
          <!-- Edit mode -->
          <template v-if="editingId === item.id">
            <form @submit.prevent="saveEdit" class="ideenkiste-edit-form">
              <input v-model="editTitle" class="form-input" required />
              <div class="ideenkiste-edit-row">
                <div class="form-group">
                  <label class="form-label">Dauer (min)</label>
                  <input v-model.number="editDuration" type="number" min="0" class="form-input" />
                </div>
                <div v-if="canAll" class="form-group">
                  <label class="form-label">Stufe</label>
                  <select v-model="editDepartment" class="form-input">
                    <option :value="null">Keine</option>
                    <option v-for="d in departments" :key="d.name" :value="d.name">{{ d.name }}</option>
                  </select>
                </div>
              </div>
              <textarea v-model="editDescription" class="form-input ideenkiste-textarea" rows="4" />
              <div v-if="editError" class="error-msg"><ErrorAlert :error="editError" /></div>
              <div class="ideenkiste-card-actions">
                <button type="button" class="btn-cancel" @click="cancelEdit">
                  <X :size="14" /> Abbrechen
                </button>
                <button type="submit" class="btn-primary" :disabled="editSaving">
                  <Check :size="14" /> {{ editSaving ? 'Speichern…' : 'Speichern' }}
                </button>
              </div>
            </form>
          </template>

          <!-- View mode -->
          <template v-else>
            <div class="ideenkiste-card-header">
              <span class="ideenkiste-card-title">{{ item.title }}</span>
              <div class="ideenkiste-card-meta">
                <span class="ideenkiste-duration">{{ formatDuration(item.duration_minutes) }}</span>
                <DepartmentBadge v-if="item.department" :department="item.department" />
              </div>
            </div>
            <p v-if="item.description" class="ideenkiste-card-desc">{{ item.description }}</p>
            <div class="ideenkiste-card-actions">
              <button class="btn-edit" @click="openEdit(item)">
                <Pencil :size="14" /> Bearbeiten
              </button>
              <button class="btn-delete" @click="deleteTarget = item">
                <Trash2 :size="14" /> Löschen
              </button>
            </div>
          </template>
        </div>
      </div>
    </template>
  </main>

  <!-- Delete confirm modal -->
  <div v-if="deleteTarget" class="modal-backdrop" @click.self="deleteTarget = null">
    <div class="modal">
      <h2 class="modal-title">Eintrag löschen?</h2>
      <p>«{{ deleteTarget.title }}» wird unwiderruflich gelöscht.</p>
      <div v-if="deleteError" class="error-msg"><ErrorAlert :error="deleteError" /></div>
      <div class="modal-actions">
        <button class="btn-cancel" @click="deleteTarget = null">Abbrechen</button>
        <button class="btn-danger" @click="confirmDelete">Löschen</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.ideenkiste-toolbar {
  display: flex;
  flex-wrap: wrap;
  gap: 0.75rem;
  align-items: center;
  margin-bottom: 1.25rem;
}

.ideenkiste-search {
  flex: 1;
  min-width: 180px;
  padding: 0.4rem 0.75rem;
  border: 1px solid var(--color-border);
  border-radius: 6px;
  font-size: 0.875rem;
  background: var(--color-surface);
  color: var(--color-text);
}

.ideenkiste-add-btn {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 0.4rem;
}

.ideenkiste-form-card {
  background: var(--color-surface);
  border: 1px solid var(--color-border);
  border-radius: 8px;
  padding: 1.25rem;
  margin-bottom: 1.5rem;
}

.ideenkiste-form-title {
  margin: 0 0 1rem;
  font-size: 1rem;
  font-weight: 600;
}

.ideenkiste-form {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.ideenkiste-form-actions {
  display: flex;
  gap: 0.5rem;
  justify-content: flex-end;
  margin-top: 0.25rem;
}

.ideenkiste-textarea {
  resize: vertical;
  font-family: inherit;
}

.ideenkiste-empty {
  color: var(--color-text-muted);
  text-align: center;
  padding: 2rem 0;
}

.ideenkiste-list {
  display: grid;
  gap: 0.75rem;
}

.ideenkiste-card {
  background: var(--color-surface);
  border: 1px solid var(--color-border);
  border-radius: 8px;
  padding: 1rem 1.25rem;
}

.ideenkiste-card-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 0.75rem;
  flex-wrap: wrap;
}

.ideenkiste-card-title {
  font-weight: 600;
  font-size: 0.95rem;
}

.ideenkiste-card-meta {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  flex-shrink: 0;
}

.ideenkiste-duration {
  font-size: 0.8rem;
  color: var(--color-text-muted);
}

.ideenkiste-card-desc {
  margin: 0.5rem 0 0.75rem;
  font-size: 0.875rem;
  color: var(--color-text-secondary);
  white-space: pre-wrap;
  line-height: 1.5;
}

.ideenkiste-card-actions {
  display: flex;
  gap: 0.5rem;
  justify-content: flex-end;
  margin-top: 0.5rem;
}

.ideenkiste-edit-form {
  display: flex;
  flex-direction: column;
  gap: 0.6rem;
}

.ideenkiste-edit-row {
  display: flex;
  gap: 1rem;
  flex-wrap: wrap;
}

.ideenkiste-edit-row .form-group {
  flex: 1;
  min-width: 120px;
}
</style>
