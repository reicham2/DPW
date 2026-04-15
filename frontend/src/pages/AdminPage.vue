<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { user as currentUser, getIdToken } from '../composables/useAuth'
import ErrorAlert from '../components/ErrorAlert.vue'
import { useRouter } from 'vue-router'
import type { User, Department, UserRole } from '../types'

const router = useRouter()

const DEPARTMENTS: (Department | '')[] = ['', 'Leiter', 'Pio', 'Pfadi', 'Wölfe', 'Biber']
const ROLES: UserRole[] = ['admin', 'Stufenleiter', 'Leiter', 'Pio']

const isAdmin = computed(() => currentUser.value?.role === 'admin')

const users = ref<User[]>([])
const loading = ref(false)
const error = ref<string | null>(null)

const editingUser = ref<User | null>(null)
const editForm = ref({ display_name: '', department: '' as Department | '', role: 'Leiter' as UserRole })
const saving = ref(false)
const saveError = ref<string | null>(null)

// Admin sees all depts; Stufenleiter is locked to own dept
const filterDept = ref<Department | 'Alle'>(
  currentUser.value?.role === 'Stufenleiter'
    ? (currentUser.value?.department ?? 'Alle') as any
    : 'Alle'
)

onMounted(async () => {
  const role = currentUser.value?.role
  if (!currentUser.value || (role !== 'admin' && role !== 'Stufenleiter')) {
    router.replace('/')
    return
  }
  await fetchUsers()
})

async function fetchUsers() {
  loading.value = true
  error.value = null
  try {
    const token = await getIdToken()
    const res = await fetch('/api/users', {
      headers: { Authorization: `Bearer ${token}` },
    })
    if (!res.ok) throw new Error(await res.text())
    users.value = await res.json()
  } catch (e) {
    error.value = String(e)
  } finally {
    loading.value = false
  }
}

const filtered = computed(() => {
  if (filterDept.value === 'Alle') return users.value
  return users.value.filter(u => u.department === filterDept.value)
})

function openEdit(u: User) {
  editingUser.value = u
  editForm.value = {
    display_name: u.display_name,
    department: u.department ?? '',
    role: u.role,
  }
  saveError.value = null
}

function closeEdit() {
  editingUser.value = null
}

async function saveEdit() {
  if (!editingUser.value) return
  saving.value = true
  saveError.value = null
  try {
    const token = await getIdToken()
    const res = await fetch(`/api/admin/users/${editingUser.value.id}`, {
      method: 'PATCH',
      headers: {
        'Content-Type': 'application/json',
        Authorization: `Bearer ${token}`,
      },
      body: JSON.stringify({
        display_name: editForm.value.display_name,
        department: editForm.value.department || null,
        role: editForm.value.role,
      }),
    })
    if (!res.ok) throw new Error(await res.text())
    const updated: User = await res.json()
    const idx = users.value.findIndex(u => u.id === updated.id)
    if (idx !== -1) users.value[idx] = updated
    closeEdit()
  } catch (e) {
    saveError.value = String(e)
  } finally {
    saving.value = false
  }
}

function roleBadgeClass(role: UserRole) {
  return {
    'badge-admin': role === 'admin',
    'badge-stufenleiter': role === 'Stufenleiter',
    'badge-leiter': role === 'Leiter',
    'badge-pio': role === 'Pio',
  }
}
</script>

<template>
  <header class="header">
    <h1>Benutzerverwaltung</h1>
    <div class="header-right">
      <span v-if="!isAdmin" class="card-dept-badge">Stufe: {{ currentUser?.department ?? '—' }}</span>
      <span class="user-count">{{ users.length }} Benutzer</span>
    </div>
  </header>

  <main class="main">
    <div v-if="loading" class="loading">Lade Benutzer...</div>
    <div v-else-if="error" class="error-msg"><ErrorAlert :error="error" /></div>
    <template v-else>
      <!-- Department filter (admin only; Stufenleiter is locked to own dept) -->
      <div v-if="isAdmin" class="filter-tabs" style="margin-bottom: 16px">
        <button
          v-for="d in ['Alle', 'Leiter', 'Pio', 'Pfadi', 'Wölfe', 'Biber']"
          :key="d"
          class="filter-tab"
          :class="{ 'filter-tab--active': filterDept === d }"
          @click="filterDept = d as any"
        >{{ d }}</button>
      </div>

      <!-- Users table -->
      <div class="table-wrap">
        <table class="users-table">
          <thead>
            <tr>
              <th>Name</th>
              <th>E-Mail</th>
              <th>Stufe</th>
              <th>Rolle</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="u in filtered" :key="u.id">
              <td class="td-name">{{ u.display_name }}</td>
              <td class="td-email">{{ u.email }}</td>
              <td>{{ u.department ?? '—' }}</td>
              <td>
                <span class="role-badge" :class="roleBadgeClass(u.role)">{{ u.role }}</span>
              </td>
              <td>
                <button class="btn-edit" @click="openEdit(u)">Bearbeiten</button>
              </td>
            </tr>
            <tr v-if="filtered.length === 0">
              <td colspan="5" class="td-empty">Keine Benutzer gefunden.</td>
            </tr>
          </tbody>
        </table>
      </div>
    </template>
  </main>

  <!-- Edit modal -->
  <div v-if="editingUser" class="modal-backdrop" @click.self="closeEdit">
    <div class="modal">
      <h2 class="modal-title">Benutzer bearbeiten</h2>
      <p class="modal-email">{{ editingUser.email }}</p>

      <form @submit.prevent="saveEdit" class="modal-form">
        <div class="form-group">
          <label class="form-label">Anzeigename</label>
          <input v-model="editForm.display_name" class="form-input" required />
        </div>

        <div class="form-group">
          <label class="form-label">Stufe</label>
          <select v-if="isAdmin" v-model="editForm.department" class="form-input">
            <option value="">Keine Angabe</option>
            <option v-for="d in DEPARTMENTS.filter(Boolean)" :key="d" :value="d">{{ d }}</option>
          </select>
          <input v-else class="form-input form-input--readonly" type="text"
            :value="editForm.department || 'Keine Angabe'" readonly />
        </div>

        <div v-if="isAdmin" class="form-group">
          <label class="form-label">Rolle</label>
          <select v-model="editForm.role" class="form-input">
            <option v-for="r in ROLES" :key="r" :value="r">{{ r }}</option>
          </select>
        </div>

        <ErrorAlert :error="saveError" />

        <div class="modal-actions">
          <button type="button" class="btn-cancel" @click="closeEdit">Abbrechen</button>
          <button type="submit" class="btn-primary" :disabled="saving">
            {{ saving ? 'Speichern...' : 'Speichern' }}
          </button>
        </div>
      </form>
    </div>
  </div>
</template>

<style scoped>
.header {
  padding: 28px 24px 0;
  display: flex;
  align-items: baseline;
  gap: 12px;
}
.header h1 {
  font-size: 1.5rem;
  font-weight: 700;
  color: #1a202c;
  margin: 0;
}
.header-right {
  display: flex;
  align-items: center;
  gap: 10px;
}
.user-count {
  font-size: 0.85rem;
  color: #6b7280;
}
.form-input--readonly {
  background: #f9fafb;
  color: #6b7280;
  cursor: default;
}
.main {
  padding: 20px 24px;
  max-width: 1000px;
}
.loading, .error-msg {
  padding: 24px;
  color: #6b7280;
}
.error-msg { color: #dc2626; }

/* Table */
.table-wrap {
  overflow-x: auto;
  border-radius: 12px;
  box-shadow: 0 2px 12px rgba(0,0,0,0.07);
}
.users-table {
  width: 100%;
  border-collapse: collapse;
  background: #fff;
  font-size: 0.9rem;
}
.users-table th {
  background: #f9fafb;
  padding: 12px 16px;
  text-align: left;
  font-size: 0.78rem;
  font-weight: 600;
  color: #6b7280;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  border-bottom: 1px solid #e5e7eb;
}
.users-table td {
  padding: 12px 16px;
  border-bottom: 1px solid #f3f4f6;
  color: #1a202c;
  vertical-align: middle;
}
.users-table tr:last-child td { border-bottom: none; }
.td-name { font-weight: 500; }
.td-email { color: #6b7280; font-size: 0.85rem; }
.td-empty { text-align: center; color: #9ca3af; padding: 32px; }

/* Edit button */
.btn-edit {
  padding: 5px 14px;
  border-radius: 6px;
  border: 1.5px solid #d1d5db;
  background: #fff;
  font-size: 0.82rem;
  cursor: pointer;
  color: #374151;
  transition: background 0.12s;
}
.btn-edit:hover { background: #f3f4f6; }

/* Modal */
.modal-backdrop {
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.35);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 100;
}
.modal {
  background: #fff;
  border-radius: 16px;
  padding: 32px;
  width: 100%;
  max-width: 420px;
  box-shadow: 0 8px 40px rgba(0,0,0,0.18);
}
.modal-title {
  font-size: 1.15rem;
  font-weight: 700;
  margin: 0 0 4px;
  color: #1a202c;
}
.modal-email {
  font-size: 0.85rem;
  color: #6b7280;
  margin: 0 0 20px;
}
.modal-form {
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.form-group {
  display: flex;
  flex-direction: column;
  gap: 5px;
}
.form-label {
  font-size: 0.82rem;
  font-weight: 600;
  color: #374151;
}
.form-input {
  padding: 9px 11px;
  border: 1.5px solid #d1d5db;
  border-radius: 8px;
  font-size: 0.93rem;
  color: #1a202c;
  outline: none;
  transition: border-color 0.15s;
}
.form-input:focus { border-color: #1a56db; }
.form-error {
  font-size: 0.85rem;
  color: #dc2626;
  background: #fff5f5;
  padding: 8px 12px;
  border-radius: 6px;
}
.modal-actions {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  margin-top: 4px;
}
.btn-cancel {
  padding: 9px 20px;
  border-radius: 8px;
  border: 1.5px solid #d1d5db;
  background: #fff;
  font-size: 0.9rem;
  cursor: pointer;
  color: #374151;
  transition: background 0.12s;
}
.btn-cancel:hover { background: #f3f4f6; }
.btn-primary {
  padding: 9px 20px;
  background: #1a56db;
  color: #fff;
  border: none;
  border-radius: 8px;
  font-size: 0.9rem;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.15s;
}
.btn-primary:hover:not(:disabled) { background: #1648c0; }
.btn-primary:disabled { opacity: 0.6; cursor: default; }

</style>
