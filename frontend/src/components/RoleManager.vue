<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { usePermissions } from '../composables/usePermissions'
import ErrorAlert from './ErrorAlert.vue'
import type { RolePermission, RoleDeptAccess } from '../types'

const SCOPE_OPTIONS_MAIL_SEND = [
  { value: 'none', label: 'Kein Versand' },
  { value: 'own', label: 'Nur eigene Aktivitäten' },
  { value: 'same_dept', label: 'Alle in eigener Stufe' },
  { value: 'all', label: 'Überall' },
]
const SCOPE_OPTIONS_MAIL_TPL = [
  { value: 'none', label: 'Kein Zugriff' },
  { value: 'own_dept', label: 'Nur eigene Stufe' },
  { value: 'all', label: 'Alle' },
]
const SCOPE_OPTIONS_USER = [
  { value: 'none', label: 'Kein Zugriff' },
  { value: 'own', label: 'Nur eigene' },
  { value: 'own_dept', label: 'Eigene Stufe' },
  { value: 'all', label: 'Alle' },
]

const {
  roles,
  departments,
  rolePermissions,
  fetchAll,
  fetchRolePermissions,
  fetchDepartments,
  createRole,
  updateRole,
  deleteRole,
  updateRolePermission,
  fetchRoleDeptAccess,
  updateRoleDeptAccess,
} = usePermissions()

const loading = ref(false)
const error = ref<string | null>(null)
const saving = ref<string | null>(null)

// Role CRUD
const editingRole = ref<string | null>(null)
const editForm = ref({ name: '', color: '#6b7280' })
const showAdd = ref(false)
const addForm = ref({ name: '', color: '#6b7280' })

// Collapsible role settings
const openRole = ref<string | null>(null)

// Per-role dept access (expanded inside a role)
const expandedDeptAccess = ref<string | null>(null)
const deptAccess = ref<RoleDeptAccess[]>([])

function isProtected(name: string) { return name === 'admin' }

onMounted(async () => {
  loading.value = true
  try {
    await fetchAll()
  } catch (e) {
    error.value = String(e)
  } finally {
    loading.value = false
  }
})

function getPermForRole(role: string): RolePermission | undefined {
  return rolePermissions.value.find(p => p.role === role)
}

function toggleOpen(name: string) {
  openRole.value = openRole.value === name ? null : name
}

// ── Role CRUD ───────────────────────────────────────────────────────────────

function startEditRole(r: { name: string; color: string }) {
  editingRole.value = r.name
  editForm.value = { name: r.name, color: r.color }
}

async function saveRoleEdit() {
  if (!editingRole.value) return
  saving.value = 'role-edit'
  error.value = null
  try {
    await updateRole(editingRole.value, editForm.value)
    await fetchAll()
    editingRole.value = null
  } catch (e) { error.value = String(e) }
  finally { saving.value = null }
}

async function handleAddRole() {
  if (!addForm.value.name.trim()) return
  saving.value = 'role-add'
  error.value = null
  try {
    await createRole(addForm.value)
    await fetchAll()
    addForm.value = { name: '', color: '#6b7280' }
    showAdd.value = false
  } catch (e) { error.value = String(e) }
  finally { saving.value = null }
}

async function handleDeleteRole(name: string) {
  if (!confirm(`Rolle "${name}" wirklich löschen?`)) return
  saving.value = 'role-del'
  error.value = null
  try {
    await deleteRole(name)
    await fetchAll()
  } catch (e) { error.value = String(e) }
  finally { saving.value = null }
}

// ── Dept access level helper ────────────────────────────────────────────────

type DeptAccessLevel = 'none' | 'read' | 'readwrite'

function getDeptAccessLevel(perm: RolePermission, scope: 'own' | 'all'): DeptAccessLevel {
  const read = scope === 'own' ? perm.can_read_own_dept : perm.can_read_all_depts
  const write = scope === 'own' ? perm.can_write_own_dept : perm.can_write_all_depts
  if (write) return 'readwrite'
  if (read) return 'read'
  return 'none'
}

async function setDeptAccessLevel(role: string, scope: 'own' | 'all', level: DeptAccessLevel) {
  const perm = getPermForRole(role)
  if (!perm) return
  const read = level === 'read' || level === 'readwrite'
  const write = level === 'readwrite'
  const readField = scope === 'own' ? 'can_read_own_dept' : 'can_read_all_depts'
  const writeField = scope === 'own' ? 'can_write_own_dept' : 'can_write_all_depts'
  saving.value = `${role}-${scope}-access`
  error.value = null
  try {
    const updated = { ...perm, [readField]: read, [writeField]: write }
    await updateRolePermission(updated)
    await fetchRolePermissions()
  } catch (e) { error.value = String(e) }
  finally { saving.value = null }
}

const SCOPE_OPTIONS_DEPT_ACCESS = [
  { value: 'none', label: 'Kein Zugriff' },
  { value: 'read', label: 'Nur lesen' },
  { value: 'readwrite', label: 'Lesen und schreiben' },
]

// ── Permission editing ──────────────────────────────────────────────────────

async function updatePerm(role: string, field: string, value: any) {
  const perm = getPermForRole(role)
  if (!perm) return
  saving.value = `${role}-${field}`
  error.value = null
  try {
    const updated = { ...perm, [field]: value }
    await updateRolePermission(updated)
    await fetchRolePermissions()
  } catch (e) { error.value = String(e) }
  finally { saving.value = null }
}

// ── Dept access expand/collapse ─────────────────────────────────────────────

async function toggleDeptAccess(role: string) {
  if (expandedDeptAccess.value === role) {
    expandedDeptAccess.value = null
    return
  }
  expandedDeptAccess.value = role
  try {
    deptAccess.value = await fetchRoleDeptAccess(role)
  } catch (e) { error.value = String(e) }
}

function getDeptAccess(dept: string): { can_read: boolean; can_write: boolean } {
  const a = deptAccess.value.find(d => d.department === dept)
  return { can_read: a?.can_read ?? false, can_write: a?.can_write ?? false }
}

async function toggleAccess(role: string, dept: string, field: 'can_read' | 'can_write') {
  const current = getDeptAccess(dept)
  const newVal = field === 'can_read' ? !current.can_read : !current.can_write
  saving.value = `access-${role}-${dept}-${field}`
  error.value = null
  try {
    await updateRoleDeptAccess({
      role,
      department: dept,
      can_read: field === 'can_read' ? newVal : current.can_read,
      can_write: field === 'can_write' ? newVal : current.can_write,
    })
    deptAccess.value = await fetchRoleDeptAccess(role)
  } catch (e) { error.value = String(e) }
  finally { saving.value = null }
}
</script>

<template>
  <div class="role-manager">
    <div class="section-header">
      <h2>Rollen &amp; Berechtigungen</h2>
      <p class="section-desc">Verwalte Rollen und deren Berechtigungen.</p>
    </div>

    <div v-if="loading" class="loading">Lade Rollen...</div>
    <ErrorAlert :error="error" />

    <template v-if="!loading">
      <div v-for="r in roles" :key="r.name" class="role-card">
        <!-- Role header -->
        <div class="role-card-header" @click="editingRole !== r.name && toggleOpen(r.name)">
          <template v-if="editingRole === r.name">
            <form class="edit-form" @submit.prevent="saveRoleEdit" @click.stop>
              <div class="edit-row">
                <input v-model="editForm.name" class="form-input" required />
                <input v-model="editForm.color" type="color" class="color-input" />
              </div>
              <div class="edit-actions">
                <button type="submit" class="btn-save" :disabled="saving === 'role-edit'">Speichern</button>
                <button type="button" class="btn-cancel" @click="editingRole = null">Abbrechen</button>
              </div>
            </form>
          </template>
          <template v-else>
            <div class="role-title-row">
              <div class="role-title-left">
                <span class="collapse-icon">{{ openRole === r.name ? '▾' : '▸' }}</span>
                <span class="role-badge" :style="{ background: r.color + '22', color: r.color }">{{ r.name }}</span>
              </div>
              <div v-if="!isProtected(r.name)" class="item-actions" @click.stop>
                <button class="btn-edit" @click="startEditRole(r)">Bearbeiten</button>
                <button class="btn-delete" @click="handleDeleteRole(r.name)">Löschen</button>
              </div>
              <span v-else class="protected-hint">Standard</span>
            </div>
          </template>
        </div>

        <!-- Collapsible permissions -->
        <template v-if="openRole === r.name && editingRole !== r.name && getPermForRole(r.name)">
          <div class="perm-list">
            <p v-if="isProtected(r.name)" class="protected-perms-hint">Die Admin-Rolle hat immer alle Berechtigungen und kann nicht verändert werden.</p>
            <template v-else>
            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Eigene Stufe</span></div>
              <select class="scope-select" :value="getDeptAccessLevel(getPermForRole(r.name)!, 'own')"
                @change="setDeptAccessLevel(r.name, 'own', ($event.target as HTMLSelectElement).value as DeptAccessLevel)">
                <option v-for="o in SCOPE_OPTIONS_DEPT_ACCESS" :key="o.value" :value="o.value">{{ o.label }}</option>
              </select>
            </div>
            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Alle Stufen</span></div>
              <select class="scope-select" :value="getDeptAccessLevel(getPermForRole(r.name)!, 'all')"
                @change="setDeptAccessLevel(r.name, 'all', ($event.target as HTMLSelectElement).value as DeptAccessLevel)">
                <option v-for="o in SCOPE_OPTIONS_DEPT_ACCESS" :key="o.value" :value="o.value">{{ o.label }}</option>
              </select>
            </div>

            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Mailversand</span></div>
              <select class="scope-select" :value="getPermForRole(r.name)!.mail_send_scope"
                @change="updatePerm(r.name, 'mail_send_scope', ($event.target as HTMLSelectElement).value)">
                <option v-for="o in SCOPE_OPTIONS_MAIL_SEND" :key="o.value" :value="o.value">{{ o.label }}</option>
              </select>
            </div>
            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Mail-Vorlagen</span></div>
              <select class="scope-select" :value="getPermForRole(r.name)!.mail_templates_scope"
                @change="updatePerm(r.name, 'mail_templates_scope', ($event.target as HTMLSelectElement).value)">
                <option v-for="o in SCOPE_OPTIONS_MAIL_TPL" :key="o.value" :value="o.value">{{ o.label }}</option>
              </select>
            </div>
            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Benutzer-Stufen verwalten</span></div>
              <select class="scope-select" :value="getPermForRole(r.name)!.user_dept_scope"
                @change="updatePerm(r.name, 'user_dept_scope', ($event.target as HTMLSelectElement).value)">
                <option v-for="o in SCOPE_OPTIONS_USER" :key="o.value" :value="o.value">{{ o.label }}</option>
              </select>
            </div>
            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Benutzer-Rollen verwalten</span></div>
              <select class="scope-select" :value="getPermForRole(r.name)!.user_role_scope"
                @change="updatePerm(r.name, 'user_role_scope', ($event.target as HTMLSelectElement).value)">
                <option v-for="o in SCOPE_OPTIONS_USER" :key="o.value" :value="o.value">{{ o.label }}</option>
              </select>
            </div>
            </template>
          </div>

          <!-- Cross-department access -->
          <div v-if="!isProtected(r.name)" class="dept-access-section">
            <button class="btn-expand" @click.stop="toggleDeptAccess(r.name)">
              {{ expandedDeptAccess === r.name ? '▾' : '▸' }} Zugriff auf andere Stufen
            </button>
            <template v-if="expandedDeptAccess === r.name">
              <div class="dept-access-grid">
                <div class="perm-grid-header">
                  <span class="perm-grid-label">Stufe</span>
                  <span class="perm-grid-col">Lesen</span>
                  <span class="perm-grid-col">Schreiben</span>
                </div>
                <div v-for="dept in departments" :key="dept.name" class="perm-grid-row">
                  <span class="perm-grid-label">
                    <span class="color-dot" :style="{ background: dept.color }" />
                    {{ dept.name }}
                  </span>
                  <label class="toggle-cell">
                    <input type="checkbox" :checked="getDeptAccess(dept.name).can_read"
                      :disabled="saving?.startsWith(`access-${r.name}-${dept.name}`)"
                      @change="toggleAccess(r.name, dept.name, 'can_read')" />
                    <span class="toggle-slider" />
                  </label>
                  <label class="toggle-cell">
                    <input type="checkbox" :checked="getDeptAccess(dept.name).can_write"
                      :disabled="saving?.startsWith(`access-${r.name}-${dept.name}`)"
                      @change="toggleAccess(r.name, dept.name, 'can_write')" />
                    <span class="toggle-slider" />
                  </label>
                </div>
              </div>
            </template>
          </div>
        </template>
      </div>

      <!-- Add role -->
      <div v-if="showAdd" class="add-form-wrap">
        <form class="edit-form" @submit.prevent="handleAddRole">
          <div class="edit-row">
            <input v-model="addForm.name" class="form-input" placeholder="Neuer Rollenname" required />
            <input v-model="addForm.color" type="color" class="color-input" />
          </div>
          <div class="edit-actions">
            <button type="submit" class="btn-save" :disabled="saving === 'role-add'">Hinzufügen</button>
            <button type="button" class="btn-cancel" @click="showAdd = false">Abbrechen</button>
          </div>
        </form>
      </div>
      <button v-else class="btn-add" @click="showAdd = true">
        + Rolle hinzufügen
      </button>
    </template>
  </div>
</template>

<style scoped>
.role-manager { max-width: 800px; }
.section-header h2 { font-size: 1.15rem; font-weight: 700; color: #1a202c; margin: 0 0 4px; }
.section-desc { font-size: 0.85rem; color: #6b7280; margin: 0 0 20px; }
.loading { padding: 24px 0; color: #6b7280; }

.role-card {
  background: #fff; border-radius: 12px; box-shadow: 0 2px 12px rgba(0,0,0,0.07);
  padding: 20px 24px; margin-bottom: 16px;
}
.role-card-header { cursor: pointer; }
.role-title-row { display: flex; align-items: center; justify-content: space-between; gap: 10px; }
.role-title-left { display: flex; align-items: center; gap: 8px; }
.collapse-icon { font-size: 0.85rem; color: #6b7280; width: 16px; user-select: none; }
.role-badge { display: inline-block; padding: 3px 14px; border-radius: 999px; font-size: 0.85rem; font-weight: 700; }
.protected-hint { font-size: 0.75rem; color: #9ca3af; font-style: italic; }
.protected-perms-hint { font-size: 0.85rem; color: #9ca3af; font-style: italic; margin: 14px 0 0; }
.item-actions { display: flex; gap: 6px; }

.perm-list { display: flex; flex-direction: column; margin-top: 14px; }
.perm-row {
  display: flex; align-items: center; justify-content: space-between;
  padding: 10px 0; border-bottom: 1px solid #f3f4f6;
}
.perm-row:last-child { border-bottom: none; }
.perm-info { display: flex; flex-direction: column; gap: 1px; }
.perm-label { font-size: 0.88rem; font-weight: 600; color: #374151; }

.scope-select {
  padding: 5px 10px; border: 1.5px solid #d1d5db; border-radius: 6px;
  font-size: 0.82rem; color: #374151; background: #fff; cursor: pointer;
}
.scope-select:focus { border-color: #1a56db; outline: none; }

.dept-access-section { margin-top: 12px; padding-top: 12px; border-top: 1px solid #e5e7eb; }
.btn-expand {
  background: none; border: none; padding: 4px 0; font-size: 0.85rem; font-weight: 600;
  color: #6b7280; cursor: pointer;
}
.btn-expand:hover { color: #374151; }

.dept-access-grid { margin-top: 10px; }
.perm-grid-header, .perm-grid-row {
  display: grid; grid-template-columns: 1fr 80px 80px; align-items: center; padding: 8px 0;
}
.perm-grid-header {
  border-bottom: 1px solid #e5e7eb; padding-bottom: 8px; margin-bottom: 2px;
}
.perm-grid-header .perm-grid-col {
  font-size: 0.75rem; font-weight: 600; color: #6b7280; text-transform: uppercase;
  letter-spacing: 0.04em; text-align: center;
}
.perm-grid-row { border-bottom: 1px solid #f3f4f6; }
.perm-grid-row:last-child { border-bottom: none; }
.perm-grid-label { font-size: 0.88rem; color: #374151; font-weight: 500; display: flex; align-items: center; gap: 8px; }
.color-dot { width: 14px; height: 14px; border-radius: 50%; flex-shrink: 0; border: 1.5px solid rgba(0,0,0,0.1); }

.toggle-cell { display: flex; justify-content: center; align-items: center; cursor: pointer; flex-shrink: 0; }
.toggle-cell input { display: none; }
.toggle-slider {
  width: 36px; height: 20px; background: #d1d5db; border-radius: 999px;
  position: relative; transition: background 0.2s;
}
.toggle-slider::after {
  content: ''; position: absolute; top: 2px; left: 2px; width: 16px; height: 16px;
  background: #fff; border-radius: 50%; transition: transform 0.2s;
}
.toggle-cell input:checked + .toggle-slider { background: #1a56db; }
.toggle-cell input:checked + .toggle-slider::after { transform: translateX(16px); }
.toggle-cell input:disabled + .toggle-slider { opacity: 0.5; }

.edit-form { display: flex; flex-direction: column; gap: 10px; }
.edit-row { display: flex; gap: 8px; align-items: center; }
.form-input {
  flex: 1; padding: 7px 10px; border: 1.5px solid #d1d5db; border-radius: 6px;
  font-size: 0.9rem; outline: none;
}
.form-input:focus { border-color: #1a56db; }
.color-input { width: 36px; height: 36px; padding: 0; border: 1.5px solid #d1d5db; border-radius: 6px; cursor: pointer; }
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
</style>
