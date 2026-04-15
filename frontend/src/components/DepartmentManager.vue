<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { usePermissions } from '../composables/usePermissions'
import ErrorAlert from './ErrorAlert.vue'
import { getIdToken } from '../composables/useAuth'

const DEFAULT_DEPT = 'Allgemein'

const {
  departments,
  fetchDepartments,
  createDepartment,
  updateDepartment,
  deleteDepartment,
} = usePermissions()

const loading = ref(false)
const error = ref<string | null>(null)
const saving = ref(false)

const editingName = ref<string | null>(null)
const editForm = ref({ name: '', color: '#6b7280', sort_order: 0 })
const showAdd = ref(false)
const addForm = ref({ name: '', color: '#6b7280', sort_order: 0 })

// ── Delete flow state ───────────────────────────────────────────────────────
const deleteTarget = ref<string | null>(null)
const deleteStep = ref<1 | 2>(1)
const activityAction = ref<'transfer' | 'delete'>('transfer')
const activityTransferTo = ref('')
const userAction = ref<'transfer' | 'delete'>('transfer')
const userTransferTo = ref('')

const otherDepts = computed(() => departments.value.filter(d => d.name !== deleteTarget.value))

function isDefault(name: string) { return name === DEFAULT_DEPT }

onMounted(async () => {
  loading.value = true
  try {
    await fetchDepartments()
  } catch (e) {
    error.value = String(e)
  } finally {
    loading.value = false
  }
})

function startEdit(dept: { name: string; color: string; sort_order: number }) {
  editingName.value = dept.name
  editForm.value = { name: dept.name, color: dept.color, sort_order: dept.sort_order }
}

function cancelEdit() {
  editingName.value = null
}

async function saveEdit() {
  if (!editingName.value) return
  saving.value = true
  error.value = null
  try {
    await updateDepartment(editingName.value, editForm.value)
    await fetchDepartments()
    editingName.value = null
  } catch (e) {
    error.value = String(e)
  } finally {
    saving.value = false
  }
}

async function handleAdd() {
  if (!addForm.value.name.trim()) return
  saving.value = true
  error.value = null
  try {
    await createDepartment(addForm.value)
    await fetchDepartments()
    addForm.value = { name: '', color: '#6b7280', sort_order: departments.value.length }
    showAdd.value = false
  } catch (e) {
    error.value = String(e)
  } finally {
    saving.value = false
  }
}

// ── Delete flow ─────────────────────────────────────────────────────────────

function startDelete(name: string) {
  deleteTarget.value = name
  deleteStep.value = 1
  activityAction.value = 'transfer'
  userAction.value = 'transfer'
  activityTransferTo.value = otherDepts.value[0]?.name ?? ''
  userTransferTo.value = otherDepts.value[0]?.name ?? ''
}

function cancelDelete() {
  deleteTarget.value = null
}

function proceedToConfirm() {
  if (activityAction.value === 'transfer' && !activityTransferTo.value) return
  if (userAction.value === 'transfer' && !userTransferTo.value) return
  deleteStep.value = 2
}

async function confirmDelete() {
  if (!deleteTarget.value) return
  saving.value = true
  error.value = null
  try {
    const token = await getIdToken()
    const body: any = {}
    if (activityAction.value === 'transfer') body.transfer_activities_to = activityTransferTo.value
    else body.delete_activities = true
    if (userAction.value === 'transfer') body.transfer_users_to = userTransferTo.value
    else body.delete_users = true

    const res = await fetch(`/api/admin/departments/${encodeURIComponent(deleteTarget.value)}`, {
      method: 'DELETE',
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${token}` },
      body: JSON.stringify(body),
    })
    if (!res.ok) throw new Error(await res.text())
    deleteTarget.value = null
    await fetchDepartments()
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
      <h2>Stufen</h2>
      <p class="section-desc">Verwalte die Stufen. Jede Stufe hat einen Namen und eine Farbe.</p>
    </div>

    <div v-if="loading" class="loading">Lade Stufen...</div>
    <ErrorAlert :error="error" />

    <template v-if="!loading">
      <div class="item-list">
        <div v-for="dept in departments" :key="dept.name" class="item-card">
          <template v-if="editingName === dept.name">
            <form class="edit-form" @submit.prevent="saveEdit">
              <div class="edit-row">
                <input v-model="editForm.name" class="form-input" placeholder="Name" required />
                <input v-model="editForm.color" type="color" class="color-input" />
                <input v-model.number="editForm.sort_order" type="number" class="sort-input" placeholder="#" />
              </div>
              <div class="edit-actions">
                <button type="submit" class="btn-save" :disabled="saving">Speichern</button>
                <button type="button" class="btn-cancel" @click="cancelEdit">Abbrechen</button>
              </div>
            </form>
          </template>
          <template v-else>
            <div class="item-row">
              <span class="color-dot" :style="{ background: dept.color }" />
              <span class="item-name">{{ dept.name }}</span>
              <span v-if="isDefault(dept.name)" class="protected-hint">Standard</span>
              <span class="item-sort">#{{ dept.sort_order }}</span>
              <div class="item-actions">
                <button class="btn-edit" @click="startEdit(dept)">Bearbeiten</button>
                <button v-if="!isDefault(dept.name)" class="btn-delete" @click="startDelete(dept.name)">Löschen</button>
              </div>
            </div>
          </template>
        </div>
      </div>

      <div v-if="showAdd" class="add-form-wrap">
        <form class="edit-form" @submit.prevent="handleAdd">
          <div class="edit-row">
            <input v-model="addForm.name" class="form-input" placeholder="Neuer Name" required />
            <input v-model="addForm.color" type="color" class="color-input" />
            <input v-model.number="addForm.sort_order" type="number" class="sort-input" placeholder="#" />
          </div>
          <div class="edit-actions">
            <button type="submit" class="btn-save" :disabled="saving">Hinzufügen</button>
            <button type="button" class="btn-cancel" @click="showAdd = false">Abbrechen</button>
          </div>
        </form>
      </div>
      <button v-else class="btn-add" @click="showAdd = true; addForm.sort_order = departments.length">
        + Stufe hinzufügen
      </button>
    </template>

    <!-- Delete modal: step 1 – transfer options -->
    <div v-if="deleteTarget && deleteStep === 1" class="modal-backdrop" @click.self="cancelDelete">
      <div class="modal">
        <h2 class="modal-title">Stufe «{{ deleteTarget }}» löschen</h2>
        <p class="modal-desc">Wähle, was mit den zugehörigen Daten geschehen soll.</p>

        <div class="delete-section">
          <h3 class="delete-section-title">Aktivitäten</h3>
          <label class="radio-row">
            <input type="radio" v-model="activityAction" value="transfer" />
            <span>An andere Stufe übertragen:</span>
          </label>
          <select v-if="activityAction === 'transfer'" v-model="activityTransferTo" class="form-input modal-select">
            <option v-for="d in otherDepts" :key="d.name" :value="d.name">{{ d.name }}</option>
          </select>
          <label class="radio-row">
            <input type="radio" v-model="activityAction" value="delete" />
            <span>Alle Aktivitäten löschen</span>
          </label>
        </div>

        <div class="delete-section">
          <h3 class="delete-section-title">Benutzer</h3>
          <label class="radio-row">
            <input type="radio" v-model="userAction" value="transfer" />
            <span>An andere Stufe verschieben:</span>
          </label>
          <select v-if="userAction === 'transfer'" v-model="userTransferTo" class="form-input modal-select">
            <option v-for="d in otherDepts" :key="d.name" :value="d.name">{{ d.name }}</option>
          </select>
          <label class="radio-row">
            <input type="radio" v-model="userAction" value="delete" />
            <span>Alle Benutzer dieser Stufe löschen</span>
          </label>
        </div>

        <p class="delete-note">Mail-Vorlagen dieser Stufe werden automatisch gelöscht.</p>

        <div class="modal-actions">
          <button class="btn-cancel" @click="cancelDelete">Abbrechen</button>
          <button class="btn-danger" @click="proceedToConfirm">Weiter</button>
        </div>
      </div>
    </div>

    <!-- Delete modal: step 2 – final confirmation -->
    <div v-if="deleteTarget && deleteStep === 2" class="modal-backdrop" @click.self="cancelDelete">
      <div class="modal modal--danger">
        <h2 class="modal-title modal-title--danger">⚠️ Stufe endgültig löschen?</h2>
        <p class="modal-desc">Folgende Aktionen werden ausgeführt:</p>

        <ul class="confirm-list">
          <li>Stufe <strong>«{{ deleteTarget }}»</strong> wird unwiderruflich gelöscht.</li>
          <li v-if="activityAction === 'transfer'">
            Aktivitäten → übertragen an <strong>«{{ activityTransferTo }}»</strong>
          </li>
          <li v-else>Alle Aktivitäten dieser Stufe werden <strong>gelöscht</strong>.</li>
          <li v-if="userAction === 'transfer'">
            Benutzer → verschoben zu <strong>«{{ userTransferTo }}»</strong>
          </li>
          <li v-else>Alle Benutzer dieser Stufe werden <strong>gelöscht</strong>.</li>
          <li>Mail-Vorlagen dieser Stufe werden <strong>gelöscht</strong>.</li>
        </ul>

        <div class="modal-actions">
          <button class="btn-cancel" @click="deleteStep = 1">Zurück</button>
          <button class="btn-danger" :disabled="saving" @click="confirmDelete">
            {{ saving ? 'Löschen...' : 'Endgültig löschen' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.dept-manager { max-width: 800px; }
.section-header h2 { font-size: 1.15rem; font-weight: 700; color: #1a202c; margin: 0 0 4px; }
.section-desc { font-size: 0.85rem; color: #6b7280; margin: 0 0 20px; }
.loading { padding: 24px 0; color: #6b7280; }

.item-list { display: flex; flex-direction: column; gap: 8px; }
.item-card {
  background: #fff; border-radius: 10px; box-shadow: 0 1px 6px rgba(0,0,0,0.06);
  padding: 14px 18px;
}
.item-row { display: flex; align-items: center; gap: 12px; }
.color-dot { width: 18px; height: 18px; border-radius: 50%; flex-shrink: 0; border: 2px solid rgba(0,0,0,0.1); }
.item-name { font-weight: 600; font-size: 0.95rem; color: #1a202c; flex: 1; }
.item-sort { font-size: 0.78rem; color: #9ca3af; min-width: 30px; }
.item-actions { display: flex; gap: 6px; }
.protected-hint { font-size: 0.75rem; color: #9ca3af; font-style: italic; }

.edit-form { display: flex; flex-direction: column; gap: 10px; }
.edit-row { display: flex; gap: 8px; align-items: center; }
.form-input {
  flex: 1; padding: 7px 10px; border: 1.5px solid #d1d5db; border-radius: 6px;
  font-size: 0.9rem; outline: none;
}
.form-input:focus { border-color: #1a56db; }
.color-input { width: 36px; height: 36px; padding: 0; border: 1.5px solid #d1d5db; border-radius: 6px; cursor: pointer; }
.sort-input { width: 56px; padding: 7px 6px; border: 1.5px solid #d1d5db; border-radius: 6px; font-size: 0.88rem; text-align: center; }
.edit-actions { display: flex; gap: 8px; }

.btn-edit, .btn-cancel {
  padding: 5px 12px; border-radius: 6px; border: 1.5px solid #d1d5db; background: #fff;
  font-size: 0.82rem; cursor: pointer; color: #374151;
}
.btn-edit:hover, .btn-cancel:hover { background: #f3f4f6; }
.btn-delete {
  padding: 5px 12px; border-radius: 6px; border: 1.5px solid #fca5a5; background: #fff;
  font-size: 0.82rem; cursor: pointer; color: #dc2626;
}
.btn-delete:hover { background: #fef2f2; }
.btn-save {
  padding: 5px 14px; border-radius: 6px; border: none; background: #1a56db; color: #fff;
  font-size: 0.82rem; font-weight: 600; cursor: pointer;
}
.btn-save:hover:not(:disabled) { background: #1648c0; }
.btn-save:disabled { opacity: 0.6; }

.btn-add {
  margin-top: 12px; padding: 10px 18px; border-radius: 8px; border: 1.5px dashed #d1d5db;
  background: #fff; font-size: 0.88rem; color: #6b7280; cursor: pointer; width: 100%;
}
.btn-add:hover { background: #f9fafb; border-color: #9ca3af; }
.add-form-wrap {
  margin-top: 12px; background: #fff; border-radius: 10px; box-shadow: 0 1px 6px rgba(0,0,0,0.06);
  padding: 14px 18px;
}

/* Modal */
.modal-backdrop {
  position: fixed; inset: 0; background: rgba(0,0,0,0.4);
  display: flex; align-items: center; justify-content: center; z-index: 100;
}
.modal {
  background: #fff; border-radius: 16px; padding: 28px 32px;
  width: 100%; max-width: 500px; box-shadow: 0 8px 40px rgba(0,0,0,0.2);
}
.modal--danger { border: 2px solid #fca5a5; }
.modal-title { font-size: 1.1rem; font-weight: 700; color: #1a202c; margin: 0 0 6px; }
.modal-title--danger { color: #dc2626; }
.modal-desc { font-size: 0.88rem; color: #6b7280; margin: 0 0 18px; }

.delete-section { margin-bottom: 18px; }
.delete-section-title { font-size: 0.88rem; font-weight: 700; color: #374151; margin: 0 0 8px; }
.radio-row {
  display: flex; align-items: center; gap: 8px; padding: 5px 0;
  font-size: 0.88rem; color: #374151; cursor: pointer;
}
.modal-select { margin: 4px 0 4px 24px; max-width: 220px; }
.delete-note { font-size: 0.82rem; color: #9ca3af; font-style: italic; margin: 0 0 18px; }

.confirm-list {
  list-style: disc; padding-left: 20px; font-size: 0.88rem; color: #374151;
  margin: 0 0 20px; line-height: 1.7;
}

.modal-actions { display: flex; justify-content: flex-end; gap: 10px; }
.btn-danger {
  padding: 8px 18px; border-radius: 8px; border: none;
  background: #dc2626; color: #fff; font-size: 0.88rem; font-weight: 600; cursor: pointer;
}
.btn-danger:hover:not(:disabled) { background: #b91c1c; }
.btn-danger:disabled { opacity: 0.6; }
</style>
