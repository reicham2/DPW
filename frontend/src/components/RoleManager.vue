<script setup lang="ts">
import { ref, onMounted, computed, watch } from 'vue'
import { usePermissions } from '../composables/usePermissions'
import ErrorAlert from './ErrorAlert.vue'
import RoleBadge from './RoleBadge.vue'
import { getIdToken } from '../composables/useAuth'
import type { RolePermission, RoleDeptAccess } from '../types'

const SCOPE_OPTIONS_MAIL_SEND = [
  { value: 'none', label: 'Kein Zugriff' },
  { value: 'own', label: 'Nur eigene Aktivitäten' },
  { value: 'same_dept', label: 'Alle in eigener Stufe' },
  { value: 'all', label: 'Alle' },
]
const SCOPE_OPTIONS_ACTIVITY_EDIT = [
  { value: 'none', label: 'Keine Bearbeitung' },
  { value: 'own', label: 'Nur eigene Aktivitäten' },
  { value: 'same_dept', label: 'Alle in eigener Stufe' },
  { value: 'all', label: 'Alle' },
]
const SCOPE_OPTIONS_ACTIVITY_READ = [
  { value: 'none', label: 'Kein Zugriff' },
  { value: 'same_dept', label: 'Alle in eigener Stufe' },
  { value: 'all', label: 'Alle' },
]
const SCOPE_OPTIONS_ACTIVITY_CREATE = [
  { value: 'none', label: 'Kein Zugriff' },
  { value: 'own_dept', label: 'In eigener Stufe' },
  { value: 'all', label: 'Alle' },
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
  fetchMyPermissions,
  fetchDepartments,
  createRole,
  updateRole,
  moveRole,
  reorderRoles,
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

// ── Delete flow state ────────────────────────────────────────────────────────
const deleteTarget = ref<string | null>(null)
const deleteStep = ref<1 | 2>(1)
const userAction = ref<'transfer' | 'delete'>('transfer')
const userTransferTo = ref('')
const otherRoles = computed(() => sortedRoles.value.filter(r => r.name !== deleteTarget.value && r.name !== 'admin'))

function isProtected(name: string) { return name === 'admin' }

const sortedRoles = computed(() => [...roles.value].sort((a, b) => a.sort_order - b.sort_order || a.name.localeCompare(b.name, 'de')))
const movableRoles = computed(() => sortedRoles.value.filter(r => !isProtected(r.name)))

// ── Drag & Drop (clone-based with TransitionGroup) ───────────────────────────

const draggedRole = ref<string | null>(null)
const localOrder = ref<string[]>([])

// Keep localOrder in sync with server data when not dragging
watch(sortedRoles, (val) => {
  if (!draggedRole.value) {
    localOrder.value = val.map(r => r.name)
  }
}, { immediate: true })

const displayRoles = computed(() => {
  if (localOrder.value.length === 0) return sortedRoles.value
  return localOrder.value
    .map(name => sortedRoles.value.find(r => r.name === name))
    .filter((r): r is (typeof sortedRoles.value)[number] => !!r)
})

let cloneEl: HTMLElement | null = null
let grabOffsetY = 0

function onHandleDown(e: PointerEvent, roleName: string) {
  if (isProtected(roleName)) return

  openRole.value = null
  expandedDeptAccess.value = null

  const cardEl = (e.target as HTMLElement).closest('.role-card') as HTMLElement
  if (!cardEl) return

  const rect = cardEl.getBoundingClientRect()
  grabOffsetY = e.clientY - rect.top

  // Init local order from current sorted state
  localOrder.value = sortedRoles.value.map(r => r.name)
  draggedRole.value = roleName

  // Create floating clone
  cloneEl = cardEl.cloneNode(true) as HTMLElement
  cloneEl.style.cssText = [
    `position: fixed`,
    `left: ${rect.left}px`,
    `top: ${rect.top}px`,
    `width: ${rect.width}px`,
    `z-index: 1000`,
    `box-shadow: 0 12px 40px rgba(0,0,0,0.22)`,
    `pointer-events: none`,
    `border-radius: 12px`,
    `border: 2px solid #1a56db`,
    `background: #fff`,
    `margin: 0`,
    `transition: none`,
  ].join(';')
  document.body.appendChild(cloneEl)

  document.addEventListener('pointermove', onPointerMove)
  document.addEventListener('pointerup', onPointerUp)
  e.preventDefault()
}

function onPointerMove(e: PointerEvent) {
  if (!cloneEl || !draggedRole.value) return

  cloneEl.style.top = `${e.clientY - grabOffsetY}px`

  const container = document.querySelector('.role-list-container') as HTMLElement
  if (!container) return

  const cards = Array.from(container.children) as HTMLElement[]
  const dragged = draggedRole.value

  // Collect midpoints of non-dragged, non-protected cards from DOM (using data-role)
  const others: { name: string; midY: number }[] = []
  for (const card of cards) {
    const name = card.dataset.role
    if (!name || name === dragged || isProtected(name)) continue
    const rect = card.getBoundingClientRect()
    others.push({ name, midY: rect.top + rect.height / 2 })
  }

  // Determine insertion point among visible non-dragged/non-protected cards
  let insertAt = others.length
  for (let i = 0; i < others.length; i++) {
    if (e.clientY < others[i].midY) {
      insertAt = i
      break
    }
  }

  // Rebuild full order: protected first, then movable with dragged inserted
  const order = [...localOrder.value]
  const protectedNames = order.filter(n => isProtected(n))
  const movable = others.map(o => o.name) // actual DOM-visible order
  movable.splice(insertAt, 0, dragged)
  const newOrder = [...protectedNames, ...movable]

  if (newOrder.some((n, i) => n !== localOrder.value[i])) {
    localOrder.value = newOrder
  }
}

async function onPointerUp() {
  document.removeEventListener('pointermove', onPointerMove)
  document.removeEventListener('pointerup', onPointerUp)

  if (cloneEl) {
    cloneEl.remove()
    cloneEl = null
  }

  const newOrder = localOrder.value.filter(n => !isProtected(n))
  draggedRole.value = null

  // Persist to backend
  saving.value = 'role-reorder'
  error.value = null
  try {
    await reorderRoles(newOrder)
    await fetchAll()
  } catch (e) { error.value = String(e) }
  finally { saving.value = null }
}

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
    await fetchMyPermissions()
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
    await fetchMyPermissions()
    addForm.value = { name: '', color: '#6b7280' }
    showAdd.value = false
  } catch (e) { error.value = String(e) }
  finally { saving.value = null }
}

async function handleDeleteRole(name: string) {
  deleteTarget.value = name
  deleteStep.value = 1
  userAction.value = 'transfer'
  userTransferTo.value = otherRoles.value[0]?.name ?? ''
}

function cancelDelete() {
  deleteTarget.value = null
}

function proceedToConfirm() {
  if (userAction.value === 'transfer' && !userTransferTo.value) return
  deleteStep.value = 2
}

async function confirmDelete() {
  if (!deleteTarget.value) return
  saving.value = 'role-del'
  error.value = null
  try {
    const token = await getIdToken()
    const body: any = {}
    if (userAction.value === 'transfer') body.transfer_users_to = userTransferTo.value
    else body.delete_users = true

    const res = await fetch(`/api/admin/roles/${encodeURIComponent(deleteTarget.value)}`, {
      method: 'DELETE',
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${token}` },
      body: JSON.stringify(body),
    })
    if (!res.ok) throw new Error(await res.text())
    deleteTarget.value = null
    await fetchAll()
    await fetchMyPermissions()
  } catch (e) { error.value = String(e) }
  finally { saving.value = null }
}

async function handleMoveRole(name: string, direction: 'up' | 'down') {
  saving.value = `role-move-${name}-${direction}`
  error.value = null
  try {
    await moveRole(name, direction)
    await fetchAll()
  } catch (e) { error.value = String(e) }
  finally { saving.value = null }
}

function canMoveRole(name: string, direction: 'up' | 'down') {
  const index = movableRoles.value.findIndex(r => r.name === name)
  if (index === -1) return false
  return direction === 'up' ? index > 0 : index < movableRoles.value.length - 1
}

const SCOPE_OPTIONS_DEPT_ACCESS = [
  { value: 'none', label: 'Kein Zugriff' },
  { value: 'read', label: 'Nur lesen' },
  { value: 'readwrite', label: 'Lesen und erstellen' },
]

// ── Cascade: global scope overrides dept access display ─────────────────────

const activeRolePerm = computed(() =>
  expandedDeptAccess.value ? getPermForRole(expandedDeptAccess.value) : undefined
)
const globalReadAll = computed(() => activeRolePerm.value?.activity_read_scope === 'all')
const globalCreateAll = computed(() => activeRolePerm.value?.activity_create_scope === 'all')

function effectiveDeptLevel(dept: string): 'none' | 'read' | 'readwrite' {
  const actual = getDeptAccessLevel(dept)
  if (globalCreateAll.value) return 'readwrite'
  if (globalReadAll.value) return actual === 'readwrite' ? 'readwrite' : 'read'
  return actual
}

// ── Scope contradiction constraints ─────────────────────────────────────────

const RANK_READ: Record<string, number> = { none: 0, same_dept: 1, all: 2 }

function readRankOf(role: string): number {
  return RANK_READ[getPermForRole(role)?.activity_read_scope ?? 'none'] ?? 0
}

function minReadRankFor(role: string): number {
  const perm = getPermForRole(role)
  if (!perm) return 0
  let min = 0
  min = Math.max(min, RANK_READ[perm.activity_edit_scope] ?? 0)
  min = Math.max(min, RANK_READ[perm.mail_send_scope] ?? 0)
  if (perm.activity_create_scope !== 'none') min = Math.max(min, 1) // same_dept minimum
  return min
}

function isReadOptionDisabled(role: string, value: string): boolean {
  return (RANK_READ[value] ?? 0) < minReadRankFor(role)
}

function isActionOptionDisabled(role: string, value: string): boolean {
  if (value === 'none') return false
  return (RANK_READ[value] ?? 0) > readRankOf(role)
}

function isCreateOptionDisabled(role: string, value: string): boolean {
  if (value === 'none') return false
  return readRankOf(role) === 0
}

function readConstraintHint(role: string): string | null {
  if (minReadRankFor(role) <= 0) return null
  return 'Bearbeiten, Erstellen und Mailversand setzen eine Mindest-Lesestufe voraus. Reduziere zuerst die abhängigen Berechtigungen.'
}

function actionConstraintHint(role: string): string | null {
  const max = readRankOf(role)
  if (max >= 2) return null
  if (max <= 0) return 'Erfordert Lese-Berechtigung. Setze zuerst «Aktivitäten lesen» auf mindestens «Alle in eigener Stufe».'
  return 'Kann die Lese-Berechtigung nicht überschreiten. Erhöhe zuerst «Aktivitäten lesen».'
}

function createConstraintHint(role: string): string | null {
  if (readRankOf(role) > 0) return null
  return 'Erfordert Lese-Berechtigung. Setze zuerst «Aktivitäten lesen» auf mindestens «Alle in eigener Stufe».'
}

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
    await fetchMyPermissions()
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

function getDeptAccessLevel(dept: string): 'none' | 'read' | 'readwrite' {
  const current = getDeptAccess(dept)
  if (current.can_write) return 'readwrite'
  if (current.can_read) return 'read'
  return 'none'
}

async function setDeptAccessLevel(role: string, dept: string, level: 'none' | 'read' | 'readwrite') {
  const can_read = level === 'read' || level === 'readwrite'
  const can_write = level === 'readwrite'
  saving.value = `access-${role}-${dept}`
  error.value = null
  try {
    await updateRoleDeptAccess({
      role,
      department: dept,
      can_read,
      can_write,
    })
    deptAccess.value = await fetchRoleDeptAccess(role)
    await fetchMyPermissions()
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
      <TransitionGroup name="role-list" tag="div" class="role-list-container">
        <div v-for="r in displayRoles" :key="r.name"
          class="role-card"
          :data-role="r.name"
          :class="{ 'role-card--placeholder': draggedRole === r.name }">
        <!-- Role header -->
        <div class="role-card-header" @click="editingRole !== r.name && toggleOpen(r.name)">
          <template v-if="editingRole === r.name">
            <form class="role-title-row" @submit.prevent="saveRoleEdit" @click.stop>
              <div class="role-title-left role-title-left--editing">
                <span class="collapse-icon">{{ openRole === r.name ? '▾' : '▸' }}</span>
                <input v-model="editForm.color" type="color" class="color-input" />
                <input v-model="editForm.name" class="form-input" required />
                <span v-if="isProtected(r.name)" class="protected-hint">Standard</span>
              </div>
              <div class="item-actions">
                <button type="submit" class="btn-save" :disabled="saving === 'role-edit'">Speichern</button>
                <button type="button" class="btn-cancel" @click="editingRole = null">Abbrechen</button>
              </div>
            </form>
          </template>
          <template v-else>
            <div class="role-title-row">
              <div class="role-title-left">
                <span v-if="!isProtected(r.name)" class="drag-handle"
                  @pointerdown="onHandleDown($event, r.name)"
                  title="Reihenfolge ändern">⠿</span>
                <span class="collapse-icon">{{ openRole === r.name ? '▾' : '▸' }}</span>
                <RoleBadge :role="r.name" />
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
              <div class="perm-info"><span class="perm-label">Aktivitäten lesen</span></div>
              <div class="scope-select-wrap">
                <span v-if="readConstraintHint(r.name)" class="scope-hint-icon">
                  ?
                  <span class="scope-hint-tooltip">{{ readConstraintHint(r.name) }}</span>
                </span>
                <select class="scope-select" :value="getPermForRole(r.name)!.activity_read_scope"
                  @change="updatePerm(r.name, 'activity_read_scope', ($event.target as HTMLSelectElement).value)">
                  <option v-for="o in SCOPE_OPTIONS_ACTIVITY_READ" :key="o.value" :value="o.value"
                    :disabled="isReadOptionDisabled(r.name, o.value)">{{ o.label }}</option>
                </select>
              </div>
            </div>
            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Aktivitäten erstellen</span></div>
              <div class="scope-select-wrap">
                <span v-if="createConstraintHint(r.name)" class="scope-hint-icon">
                  ?
                  <span class="scope-hint-tooltip">{{ createConstraintHint(r.name) }}</span>
                </span>
                <select class="scope-select" :value="getPermForRole(r.name)!.activity_create_scope"
                  @change="updatePerm(r.name, 'activity_create_scope', ($event.target as HTMLSelectElement).value)">
                  <option v-for="o in SCOPE_OPTIONS_ACTIVITY_CREATE" :key="o.value" :value="o.value"
                    :disabled="isCreateOptionDisabled(r.name, o.value)">{{ o.label }}</option>
                </select>
              </div>
            </div>

            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Aktivitäten bearbeiten</span></div>
              <div class="scope-select-wrap">
                <span v-if="actionConstraintHint(r.name)" class="scope-hint-icon">
                  ?
                  <span class="scope-hint-tooltip">{{ actionConstraintHint(r.name) }}</span>
                </span>
                <select class="scope-select" :value="getPermForRole(r.name)!.activity_edit_scope"
                  @change="updatePerm(r.name, 'activity_edit_scope', ($event.target as HTMLSelectElement).value)">
                  <option v-for="o in SCOPE_OPTIONS_ACTIVITY_EDIT" :key="o.value" :value="o.value"
                    :disabled="isActionOptionDisabled(r.name, o.value)">{{ o.label }}</option>
                </select>
              </div>
            </div>
            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Mailversand</span></div>
              <div class="scope-select-wrap">
                <span v-if="actionConstraintHint(r.name)" class="scope-hint-icon">
                  ?
                  <span class="scope-hint-tooltip">{{ actionConstraintHint(r.name) }}</span>
                </span>
                <select class="scope-select" :value="getPermForRole(r.name)!.mail_send_scope"
                  @change="updatePerm(r.name, 'mail_send_scope', ($event.target as HTMLSelectElement).value)">
                  <option v-for="o in SCOPE_OPTIONS_MAIL_SEND" :key="o.value" :value="o.value"
                    :disabled="isActionOptionDisabled(r.name, o.value)">{{ o.label }}</option>
                </select>
              </div>
            </div>
            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Mail-Vorlagen</span></div>
              <select class="scope-select" :value="getPermForRole(r.name)!.mail_templates_scope"
                @change="updatePerm(r.name, 'mail_templates_scope', ($event.target as HTMLSelectElement).value)">
                <option v-for="o in SCOPE_OPTIONS_MAIL_TPL" :key="o.value" :value="o.value">{{ o.label }}</option>
              </select>
            </div>
            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Formulare</span></div>
              <div class="scope-select-wrap">
                <span v-if="actionConstraintHint(r.name)" class="scope-hint-icon">
                  ?
                  <span class="scope-hint-tooltip">{{ actionConstraintHint(r.name) }}</span>
                </span>
                <select class="scope-select" :value="getPermForRole(r.name)!.form_scope"
                  @change="updatePerm(r.name, 'form_scope', ($event.target as HTMLSelectElement).value)">
                  <option v-for="o in SCOPE_OPTIONS_MAIL_SEND" :key="o.value" :value="o.value"
                    :disabled="isActionOptionDisabled(r.name, o.value)">{{ o.label }}</option>
                </select>
              </div>
            </div>
            <div class="perm-row">
              <div class="perm-info"><span class="perm-label">Formular-Vorlagen</span></div>
              <select class="scope-select" :value="getPermForRole(r.name)!.form_templates_scope"
                @change="updatePerm(r.name, 'form_templates_scope', ($event.target as HTMLSelectElement).value)">
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
              <div v-if="globalCreateAll" class="scope-override-hint">
                ℹ️ Globale Erstellberechtigung <strong>Alle</strong> gilt für alle Stufen — Mindest-Zugriff ist «Lesen und erstellen».
              </div>
              <div v-else-if="globalReadAll" class="scope-override-hint">
                ℹ️ Globale Leseberechtigung <strong>Alle</strong> gilt für alle Stufen — Mindest-Zugriff ist «Nur lesen».
              </div>
              <div class="dept-access-grid">
                <div class="perm-grid-header">
                  <span class="perm-grid-label">Stufe</span>
                  <span class="perm-grid-col">Zugriff</span>
                </div>
                <div v-for="dept in departments" :key="dept.name" class="perm-grid-row">
                  <span class="perm-grid-label">
                    <span class="color-dot" :style="{ background: dept.color }" />
                    {{ dept.name }}
                  </span>
                  <select class="scope-select"
                    :value="effectiveDeptLevel(dept.name)"
                    :disabled="saving?.startsWith(`access-${r.name}-${dept.name}`)"
                    @change="setDeptAccessLevel(r.name, dept.name, ($event.target as HTMLSelectElement).value as 'none' | 'read' | 'readwrite')">
                    <option value="none" :disabled="globalReadAll || globalCreateAll">Kein Zugriff</option>
                    <option value="read" :disabled="globalCreateAll">Nur lesen</option>
                    <option value="readwrite">Lesen und erstellen</option>
                  </select>
                </div>
              </div>
            </template>
          </div>
        </template>
        </div>
      </TransitionGroup>

      <!-- Add role -->
      <div v-if="showAdd" class="add-form-wrap">
        <form class="add-form" @submit.prevent="handleAddRole">
          <div class="add-row">
            <input v-model="addForm.name" class="form-input" placeholder="Neuer Rollenname" required />
            <input v-model="addForm.color" type="color" class="color-input" />
          </div>
          <div class="item-actions">
            <button type="submit" class="btn-save" :disabled="saving === 'role-add'">Hinzufügen</button>
            <button type="button" class="btn-cancel" @click="showAdd = false">Abbrechen</button>
          </div>
        </form>
      </div>
      <button v-else class="btn-add" @click="showAdd = true">
        + Rolle hinzufügen
      </button>
    </template>

    <!-- Delete modal: step 1 – transfer options -->
    <div v-if="deleteTarget && deleteStep === 1" class="modal-backdrop" @click.self="cancelDelete">
      <div class="modal">
        <h2 class="modal-title">Rolle «{{ deleteTarget }}» löschen</h2>
        <p class="modal-desc">Wähle, was mit den Benutzern dieser Rolle geschehen soll.</p>

        <div class="delete-section">
          <h3 class="delete-section-title">Benutzer</h3>
          <label class="radio-row">
            <input type="radio" v-model="userAction" value="transfer" />
            <span>An andere Rolle verschieben:</span>
          </label>
          <select v-if="userAction === 'transfer'" v-model="userTransferTo" class="form-input modal-select">
            <option v-for="r in otherRoles" :key="r.name" :value="r.name">{{ r.name }}</option>
          </select>
          <label class="radio-row">
            <input type="radio" v-model="userAction" value="delete" />
            <span>Alle Benutzer dieser Rolle löschen</span>
          </label>
        </div>

        <p class="delete-note">Berechtigungen und Stufen-Zugriffe dieser Rolle werden automatisch gelöscht.</p>

        <div class="modal-actions">
          <button class="btn-cancel" @click="cancelDelete">Abbrechen</button>
          <button class="btn-danger" @click="proceedToConfirm">Weiter</button>
        </div>
      </div>
    </div>

    <!-- Delete modal: step 2 – final confirmation -->
    <div v-if="deleteTarget && deleteStep === 2" class="modal-backdrop" @click.self="cancelDelete">
      <div class="modal modal--danger">
        <h2 class="modal-title modal-title--danger">⚠️ Rolle endgültig löschen?</h2>
        <p class="modal-desc">Folgende Aktionen werden ausgeführt:</p>

        <ul class="confirm-list">
          <li>Rolle <strong>«{{ deleteTarget }}»</strong> wird unwiderruflich gelöscht.</li>
          <li v-if="userAction === 'transfer'">
            Benutzer → verschoben zu <strong>«{{ userTransferTo }}»</strong>
          </li>
          <li v-else>Alle Benutzer dieser Rolle werden <strong>gelöscht</strong>.</li>
          <li>Berechtigungen und Stufen-Zugriffe werden <strong>gelöscht</strong>.</li>
        </ul>

        <div class="modal-actions">
          <button class="btn-cancel" @click="deleteStep = 1">Zurück</button>
          <button class="btn-danger" :disabled="saving === 'role-del'" @click="confirmDelete">
            {{ saving === 'role-del' ? 'Löschen...' : 'Endgültig löschen' }}
          </button>
        </div>
      </div>
    </div>
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
  border: 2px solid transparent;
}
.role-card--placeholder {
  opacity: 0 !important;
  pointer-events: none;
}
.role-list-move {
  transition: transform 0.3s ease;
}
.role-card-header { cursor: pointer; }
.role-title-row { display: flex; align-items: center; justify-content: space-between; gap: 10px; }
.role-title-left { display: flex; align-items: center; gap: 8px; }
.collapse-icon { font-size: 0.85rem; color: #6b7280; width: 16px; user-select: none; }
.drag-handle {
  cursor: grab; font-size: 1.1rem; color: #9ca3af; user-select: none;
  display: inline-flex; align-items: center; width: 18px; letter-spacing: -1px;
  touch-action: none;
}
.drag-handle:hover { color: #6b7280; }
.drag-handle:active { cursor: grabbing; color: #374151; }
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
  display: grid; grid-template-columns: 1fr 220px; align-items: center; padding: 8px 0;
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
.scope-override-hint {
  font-size: 0.82rem; color: #374151; background: #eff6ff; border: 1px solid #bfdbfe;
  border-radius: 6px; padding: 7px 10px; margin-bottom: 8px;
}

.scope-select-wrap { display: flex; align-items: center; gap: 6px; }
.scope-hint-icon {
  position: relative; display: inline-flex; align-items: center; justify-content: center;
  width: 18px; height: 18px; border-radius: 50%; background: #e5e7eb; color: #6b7280;
  font-size: 0.7rem; font-weight: 700; cursor: help; flex-shrink: 0; user-select: none;
}
.scope-hint-icon:hover { background: #d1d5db; color: #374151; }
.scope-hint-tooltip {
  display: none; position: absolute; left: 0; bottom: calc(100% + 8px);
  width: 260px; padding: 8px 10px; background: #1f2937; color: #f9fafb;
  font-size: 0.78rem; font-weight: 400; border-radius: 6px; z-index: 20;
  line-height: 1.4; white-space: normal; box-shadow: 0 4px 12px rgba(0,0,0,0.2);
}
.scope-hint-tooltip::after {
  content: ''; position: absolute; top: 100%; left: 6px;
  border: 5px solid transparent; border-top-color: #1f2937;
}
.scope-hint-icon:hover .scope-hint-tooltip { display: block; }

.role-title-left--editing { flex: 1; min-width: 0; }
.form-input {
  flex: 1; min-width: 0; padding: 7px 10px; border: 1.5px solid #d1d5db; border-radius: 6px;
  font-size: 0.9rem; outline: none;
}
.form-input:focus { border-color: #1a56db; }
.color-input { width: 36px; height: 36px; padding: 0; border: 1.5px solid #d1d5db; border-radius: 8px; cursor: pointer; -webkit-appearance: none; appearance: none; background: none; }
.color-input::-webkit-color-swatch-wrapper { padding: 0; }
.color-input::-webkit-color-swatch { border: none; border-radius: 6px; }
.color-input::-moz-color-swatch { border: none; border-radius: 6px; }

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
.add-form { display: flex; flex-direction: column; gap: 10px; }
.add-row { display: flex; gap: 8px; align-items: center; }

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
