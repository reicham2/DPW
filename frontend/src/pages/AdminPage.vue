<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { user as currentUser, getIdToken } from '../composables/useAuth'
import ErrorAlert from '../components/ErrorAlert.vue'
import DepartmentManager from '../components/DepartmentManager.vue'
import RoleManager from '../components/RoleManager.vue'
import { useRouter } from 'vue-router'
import { usePermissions } from '../composables/usePermissions'
import type { User, Department, UserRole } from '../types'

const router = useRouter()
const { departments: deptRecords, roles: roleRecords, fetchDepartments, fetchRoles, myPermissions, fetchMyPermissions, canManageUsers, canManageSystem } = usePermissions()

const DEPARTMENTS = computed(() => deptRecords.value.map(d => d.name))
const ROLES = computed(() => roleRecords.value.map(r => r.name))

const canSeeAllDepts = computed(() => myPermissions.value?.user_dept_scope === 'all')
const canEditDepts = computed(() => {
  const scope = myPermissions.value?.user_dept_scope
  return scope === 'all' || scope === 'own_dept' || scope === 'own'
})
const canEditRoles = computed(() => {
  const scope = myPermissions.value?.user_role_scope
  return scope === 'all' || scope === 'own_dept' || scope === 'own'
})
const canSeePermissionsTab = computed(() => canManageSystem())

// ── Tab management ──────────────────────────────────────────────────────────
const activeTab = ref<'users' | 'permissions'>('users')

// ── User management state ───────────────────────────────────────────────────
const users = ref<User[]>([])
const loading = ref(false)
const error = ref<string | null>(null)

const editingUser = ref<User | null>(null)
const editForm = ref({ display_name: '', department: '' as Department | '', role: '' as UserRole })
const saving = ref(false)
const saveError = ref<string | null>(null)
const deleteTarget = ref<User | null>(null)

const filterDept = ref<Department | 'Alle'>('Alle')

onMounted(async () => {
  await fetchMyPermissions()
  if (!currentUser.value || !canManageUsers()) {
    router.replace('/')
    return
  }
  // Lock filter to own dept if user can only manage own dept
  if (myPermissions.value?.user_dept_scope === 'own_dept') {
    filterDept.value = (currentUser.value?.department ?? 'Alle') as any
  }

  const tasks = [fetchUsers(), fetchDepartments()]
  if (canEditRoles.value || canSeePermissionsTab.value) {
    tasks.push(fetchRoles())
  }
  await Promise.all(tasks)
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

function canManageTargetUser(u: User) {
  const p = myPermissions.value
  const me = currentUser.value
  if (!p || !me) return false

  if (p.user_dept_scope === 'all' || p.user_role_scope === 'all') return true

  const isSelf = u.id === me.id
  if (p.user_dept_scope === 'own' || p.user_role_scope === 'own') return isSelf

  const sameDept = !!(u.department && me.department && u.department === me.department)
  if (p.user_dept_scope === 'own_dept' || p.user_role_scope === 'own_dept') return sameDept

  return false
}

const filtered = computed(() => {
  let list = users.value.filter(canManageTargetUser)
  if (filterDept.value !== 'Alle') {
    list = list.filter(u => u.department === filterDept.value)
  }
  return list
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
    if (currentUser.value && currentUser.value.id === updated.id) {
      currentUser.value = {
        ...currentUser.value,
        display_name: updated.display_name,
        department: updated.department,
        role: updated.role,
      }
      await fetchMyPermissions()
    }
    closeEdit()
  } catch (e) {
    saveError.value = String(e)
  } finally {
    saving.value = false
  }
}

async function deleteUser() {
  if (!deleteTarget.value) return
  saving.value = true
  saveError.value = null
  try {
    const token = await getIdToken()
    const res = await fetch(`/api/admin/users/${deleteTarget.value.id}`, {
      method: 'DELETE',
      headers: { Authorization: `Bearer ${token}` },
    })
    if (!res.ok) throw new Error(await res.text())
    users.value = users.value.filter(u => u.id !== deleteTarget.value!.id)
    deleteTarget.value = null
  } catch (e) {
    saveError.value = String(e)
  } finally {
    saving.value = false
  }
}

function openDeleteConfirm(u: User) {
  deleteTarget.value = u
  saveError.value = null
}

function closeDeleteConfirm() {
  deleteTarget.value = null
}

function roleBadgeStyle(role: UserRole) {
  const rec = roleRecords.value.find(r => r.name === role)
  if (!rec) return {}
  return { background: rec.color + '22', color: rec.color }
}
</script>

<template>
  <header class="header">
    <h1>Administration</h1>
  </header>

  <!-- Tab navigation -->
  <nav class="tab-bar">
    <button
      class="tab-btn"
      :class="{ 'tab-btn--active': activeTab === 'users' }"
      @click="activeTab = 'users'"
    >
      <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
        <circle cx="8" cy="5" r="3" />
        <path d="M2 14c0-2.8 2.7-5 6-5s6 2.2 6 5" />
      </svg>
      Benutzerverwaltung
    </button>
    <button
      v-if="canSeePermissionsTab"
      class="tab-btn"
      :class="{ 'tab-btn--active': activeTab === 'permissions' }"
      @click="activeTab = 'permissions'"
    >
      <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
        <rect x="2" y="7" width="12" height="7" rx="1.5" />
        <path d="M5 7V5a3 3 0 0 1 6 0v2" />
      </svg>
      Stufen &amp; Rollen
    </button>
  </nav>

  <!-- Tab: Benutzerverwaltung -->
  <main v-if="activeTab === 'users'" class="main">
    <div class="tab-header">
      <div class="tab-header-left">
        <span v-if="!canSeeAllDepts" class="dept-badge">Stufe: {{ currentUser?.department ?? '—' }}</span>
      </div>
      <span class="user-count">{{ filtered.length }} Benutzer</span>
    </div>

    <div v-if="loading" class="loading">Lade Benutzer...</div>
    <div v-else-if="error" class="error-msg"><ErrorAlert :error="error" /></div>
    <template v-else>
      <!-- Department filter (visible when user can see all depts) -->
      <div v-if="canSeeAllDepts" class="filter-bar">
        <label class="filter-label">Stufe:</label>
        <div class="dept-pills">
          <button
            class="dept-pill"
            :class="{ 'dept-pill--active': filterDept === 'Alle' }"
            @click="filterDept = 'Alle'"
          >Alle</button>
          <button
            v-for="d in DEPARTMENTS"
            :key="d"
            class="dept-pill"
            :class="{ 'dept-pill--active': filterDept === d }"
            @click="filterDept = d as any"
          >{{ d }}</button>
        </div>
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
                <span class="role-badge" :style="roleBadgeStyle(u.role)">{{ u.role }}</span>
              </td>
              <td>
                <div class="item-actions">
                  <button class="btn-edit" @click="openEdit(u)">Bearbeiten</button>
                  <button class="btn-delete" :disabled="u.id === currentUser?.id" @click="openDeleteConfirm(u)">Löschen</button>
                </div>
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

  <!-- Tab: Stufen & Rollen (admin only) -->
  <main v-else-if="activeTab === 'permissions' && canSeePermissionsTab" class="main">
    <DepartmentManager />
    <div class="section-divider" />
    <RoleManager />
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
          <select v-if="canEditDepts" v-model="editForm.department" class="form-input">
            <option value="">Keine Angabe</option>
            <option v-for="d in DEPARTMENTS" :key="d" :value="d">{{ d }}</option>
          </select>
          <input v-else class="form-input form-input--readonly" type="text"
            :value="editForm.department || 'Keine Angabe'" readonly />
        </div>

        <div v-if="canEditRoles" class="form-group">
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

  <!-- Delete confirmation modal -->
  <div v-if="deleteTarget" class="modal-backdrop" @click.self="closeDeleteConfirm">
    <div class="modal modal--danger">
      <h2 class="modal-title modal-title--danger">Benutzer löschen</h2>
      <p class="modal-email">{{ deleteTarget.display_name }} ({{ deleteTarget.email }})</p>
      <p class="modal-warning">Dieser Benutzer wird dauerhaft gelöscht. Verantwortlichkeiten in Aktivitäten bleiben zur Nachverfolgbarkeit bestehen.</p>

      <ErrorAlert :error="saveError" />

      <div class="modal-actions">
        <button type="button" class="btn-cancel" @click="closeDeleteConfirm">Abbrechen</button>
        <button type="button" class="btn-danger" :disabled="saving || deleteTarget.id === currentUser?.id" @click="deleteUser">
          {{ saving ? 'Löschen...' : 'Löschen' }}
        </button>
      </div>
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

/* Tabs */
.tab-bar {
  display: flex;
  gap: 4px;
  padding: 16px 24px 0;
  border-bottom: 1px solid #e5e7eb;
}
.tab-btn {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 10px 18px;
  font-size: 0.88rem;
  font-weight: 600;
  color: #6b7280;
  background: none;
  border: none;
  border-bottom: 2px solid transparent;
  cursor: pointer;
  transition: color 0.15s, border-color 0.15s;
  margin-bottom: -1px;
}
.tab-btn:hover {
  color: #374151;
}
.tab-btn--active {
  color: #1a56db;
  border-bottom-color: #1a56db;
}

.tab-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 16px;
}
.tab-header-left {
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

.section-divider {
  height: 1px;
  background: #e5e7eb;
  margin: 32px 0;
}

/* Filter */
.filter-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 16px;
  flex-wrap: wrap;
}
.filter-label {
  font-size: 0.85rem;
  font-weight: 600;
  color: #374151;
}
.dept-pills {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}
.dept-pill {
  padding: 4px 12px;
  border-radius: 999px;
  border: 1.5px solid #d1d5db;
  background: #fff;
  font-size: 0.82rem;
  cursor: pointer;
  color: #374151;
  transition: background 0.12s, border-color 0.12s;
}
.dept-pill:hover { background: #f3f4f6; }
.dept-pill--active {
  background: #1a56db;
  border-color: #1a56db;
  color: #fff;
}

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

/* Role badges */
.role-badge {
  display: inline-block;
  padding: 2px 10px;
  border-radius: 999px;
  font-size: 0.78rem;
  font-weight: 600;
}

.item-actions {
  display: flex;
  gap: 6px;
}

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

.btn-delete {
  padding: 5px 12px;
  border-radius: 6px;
  border: 1.5px solid #fca5a5;
  background: #fff;
  font-size: 0.82rem;
  cursor: pointer;
  color: #dc2626;
}
.btn-delete:hover:not(:disabled) { background: #fef2f2; }
.btn-delete:disabled { opacity: 0.45; cursor: default; }

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
.modal--danger { border: 2px solid #fca5a5; }
.modal-title {
  font-size: 1.15rem;
  font-weight: 700;
  margin: 0 0 4px;
  color: #1a202c;
}
.modal-title--danger { color: #dc2626; }
.modal-email {
  font-size: 0.85rem;
  color: #6b7280;
  margin: 0 0 20px;
}
.modal-warning {
  font-size: 0.88rem;
  color: #6b7280;
  margin: 0 0 16px;
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
  justify-content: space-between;
  align-items: center;
  gap: 10px;
  margin-top: 4px;
}
.modal-actions-right {
  display: flex;
  gap: 10px;
}
.btn-danger {
  padding: 9px 20px;
  border-radius: 8px;
  border: 1.5px solid #fca5a5;
  background: #fff;
  font-size: 0.9rem;
  cursor: pointer;
  color: #dc2626;
  transition: background 0.12s;
}
.btn-danger:hover:not(:disabled) { background: #fef2f2; }
.btn-danger:disabled { opacity: 0.4; cursor: default; }
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
