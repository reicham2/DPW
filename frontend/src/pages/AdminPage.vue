<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed, watch, nextTick } from 'vue'
import { ChevronDown, Cog, Lock, MapPin, Trash2, Users } from 'lucide-vue-next'
import { user as currentUser } from '../composables/useAuth'
import { apiFetch } from '../composables/useApi'
import ErrorAlert from '../components/ErrorAlert.vue'
import DepartmentManager from '../components/DepartmentManager.vue'
import RoleManager from '../components/RoleManager.vue'
import LocationManager from '../components/LocationManager.vue'
import SystemConfigManager from '../components/SystemConfigManager.vue'
import RoleBadge from '../components/RoleBadge.vue'
import DepartmentBadge from '../components/DepartmentBadge.vue'
import BadgeSelect from '../components/BadgeSelect.vue'
import { useRouter } from 'vue-router'
import { usePermissions } from '../composables/usePermissions'
import { useActivities } from '../composables/useActivities'
import type { User, Department, UserRole, DeletedActivityRecord } from '../types'

const router = useRouter()
const { departments: deptRecords, roles: roleRecords, fetchDepartments, fetchRoles, myPermissions, fetchMyPermissions, canManageUsers, canManageSystem, canManageLocations } = usePermissions()
const { fetchDeletedActivities, restoreActivity, deleteDeletedActivity } = useActivities()

const DEPARTMENTS = computed(() => deptRecords.value.map(d => d.name))
const orderedRoles = computed(() => [...roleRecords.value].sort((a, b) => a.sort_order - b.sort_order || a.name.localeCompare(b.name, 'de')))
const ROLES = computed(() => orderedRoles.value.map(r => r.name))

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
const canSeeLocationsTab = computed(() => canManageLocations())
const canSeeTrashTab = computed(() => currentUser.value?.role === 'admin')
const canSeeLogsTab = computed(() => currentUser.value?.role === 'admin')
const systemTabLabel = computed(() => {
  if (activeTab.value !== 'system') return 'System'
  return systemTab.value === 'logs' ? 'System: Container Logs' : 'System: Einstellungen'
})

// ── Tab management ──────────────────────────────────────────────────────────
const activeTab = ref<'users' | 'permissions' | 'locations' | 'trash' | 'system'>('users')
const systemTab = ref<'settings' | 'logs'>('settings')
const systemMenuOpen = ref(false)

// ── User management state ───────────────────────────────────────────────────
const users = ref<User[]>([])
const loading = ref(false)
const error = ref<string | null>(null)

const editingUser = ref<User | null>(null)
const editForm = ref({ display_name: '', department: '' as Department | '', role: '' as UserRole })
const saving = ref(false)
const saveError = ref<string | null>(null)
const deleteTarget = ref<User | null>(null)

const deletedActivities = ref<DeletedActivityRecord[]>([])
const trashLoading = ref(false)
const trashError = ref<string | null>(null)
const restoringActivityId = ref<string | null>(null)
const deletingActivityId = ref<string | null>(null)

const containerLogs = ref('')
const containerLogsSource = ref('')
const containerLogsLoading = ref(false)
const containerLogsError = ref<string | null>(null)
const containerLogsTail = ref(300)
const containerServices = ref<string[]>([])
const selectedContainerService = ref('')
const containerLogsViewer = ref<HTMLElement | null>(null)
const followLatestLogs = ref(true)
const autoRefreshLogs = ref(true)
let containerLogsPollTimer: ReturnType<typeof setInterval> | null = null
const LOG_FOLLOW_THRESHOLD_PX = 24

const filterDept = ref<Department | 'Alle'>('Alle')

onMounted(async () => {
  await fetchMyPermissions()
  if (!currentUser.value || (!canManageUsers() && !canManageLocations())) {
    router.replace('/')
    return
  }
  // Set default tab based on permissions
  if (!canManageUsers() && canManageLocations()) {
    activeTab.value = 'locations'
  }
  // Lock filter to own dept if user can only manage own dept
  if (myPermissions.value?.user_dept_scope === 'own_dept') {
    filterDept.value = (currentUser.value?.department ?? 'Alle') as any
  }

  const tasks = [fetchUsers(), fetchDepartments()]
  if (canEditRoles.value || canSeePermissionsTab.value) {
    tasks.push(fetchRoles())
  }
  if (canSeeTrashTab.value) {
    tasks.push(loadDeletedActivities())
  }
  await Promise.all(tasks)
})

function formatDateTime(iso: string) {
  return new Date(iso).toLocaleString('de-DE')
}

function formatAutoDeleteDate(iso: string) {
  const date = new Date(iso)
  date.setDate(date.getDate() + 90)
  return date.toLocaleString('de-DE')
}

async function loadDeletedActivities() {
  if (!canSeeTrashTab.value) return
  trashLoading.value = true
  trashError.value = null
  try {
    deletedActivities.value = await fetchDeletedActivities()
  } catch (e) {
    trashError.value = String(e)
  } finally {
    trashLoading.value = false
  }
}

async function restoreDeletedActivity(entry: DeletedActivityRecord) {
  if (!canSeeTrashTab.value) return
  restoringActivityId.value = entry.activity.id
  trashError.value = null
  try {
    const restored = await restoreActivity(entry.activity.id)
    if (!restored) throw new Error('Wiederherstellung fehlgeschlagen')
    deletedActivities.value = deletedActivities.value.filter((item) => item.activity.id !== entry.activity.id)
  } catch (e) {
    trashError.value = String(e)
  } finally {
    restoringActivityId.value = null
  }
}

async function permanentlyDeleteActivity(entry: DeletedActivityRecord) {
  if (!canSeeTrashTab.value) return
  if (!window.confirm(`Aktivität "${entry.activity.title}" endgültig löschen? Diese Aktion kann nicht rückgängig gemacht werden.`)) {
    return
  }

  deletingActivityId.value = entry.activity.id
  trashError.value = null
  try {
    const deleted = await deleteDeletedActivity(entry.activity.id)
    if (!deleted) throw new Error('Endgültiges Löschen fehlgeschlagen')
    deletedActivities.value = deletedActivities.value.filter((item) => item.activity.id !== entry.activity.id)
  } catch (e) {
    trashError.value = String(e)
  } finally {
    deletingActivityId.value = null
  }
}

async function loadContainerLogs(options: { silent?: boolean } = {}) {
  if (!canSeeLogsTab.value) return
  const silent = options.silent ?? false
  if (!silent) {
    containerLogsLoading.value = true
    containerLogsError.value = null
  }
  try {
    const tail = Math.max(50, Math.min(3000, containerLogsTail.value || 300))
    containerLogsTail.value = tail
    const params = new URLSearchParams({ tail: String(tail) })
    if (selectedContainerService.value) params.set('service', selectedContainerService.value)
    const res = await apiFetch(`/api/admin/container-logs?${params.toString()}`)
    if (!res.ok) throw new Error(await res.text())
    const data = await res.json() as {
      logs?: string
      source?: string
      services?: string[]
      selected_service?: string | null
    }
    containerServices.value = data.services || []
    if (data.selected_service) selectedContainerService.value = data.selected_service
    else if (!selectedContainerService.value && containerServices.value.length > 0) selectedContainerService.value = containerServices.value[0]
    containerLogs.value = data.logs || ''
    containerLogsSource.value = data.source || ''
  } catch (e) {
    containerLogsError.value = String(e)
  } finally {
    if (!silent) containerLogsLoading.value = false
  }
}

async function scrollLogsToBottom() {
  await nextTick()
  if (!containerLogsViewer.value || !followLatestLogs.value) return
  containerLogsViewer.value.scrollTop = containerLogsViewer.value.scrollHeight
}

function jumpToLatestLogEntry() {
  followLatestLogs.value = true
  void scrollLogsToBottom()
}

function handleLogsScroll() {
  const viewer = containerLogsViewer.value
  if (!viewer) return
  const distanceFromBottom = viewer.scrollHeight - viewer.scrollTop - viewer.clientHeight
  if (distanceFromBottom > LOG_FOLLOW_THRESHOLD_PX) {
    followLatestLogs.value = false
    return
  }
  followLatestLogs.value = true
}

function stopContainerLogPolling() {
  if (containerLogsPollTimer) {
    clearInterval(containerLogsPollTimer)
    containerLogsPollTimer = null
  }
}

function closeSystemMenu() {
  systemMenuOpen.value = false
}

function toggleSystemMenu() {
  systemMenuOpen.value = !systemMenuOpen.value
}

function selectSystemTab(tab: 'settings' | 'logs') {
  systemTab.value = tab
  activeTab.value = 'system'
  systemMenuOpen.value = false
}

function handleDocumentClick(event: MouseEvent) {
  const target = event.target
  if (!(target instanceof Element)) return
  if (target.closest('.system-tab-menu')) return
  closeSystemMenu()
}

function startContainerLogPolling() {
  stopContainerLogPolling()
  if (!(activeTab.value === 'system' && systemTab.value === 'logs' && canSeeLogsTab.value && autoRefreshLogs.value)) return
  containerLogsPollTimer = setInterval(() => {
    void loadContainerLogs({ silent: true })
  }, 2000)
}

function toggleFollowLatestLogs() {
  followLatestLogs.value = !followLatestLogs.value
  if (followLatestLogs.value) void scrollLogsToBottom()
}

function toggleAutoRefreshLogs() {
  autoRefreshLogs.value = !autoRefreshLogs.value
  if (autoRefreshLogs.value) {
    void loadContainerLogs({ silent: true })
    startContainerLogPolling()
    return
  }
  stopContainerLogPolling()
}

async function fetchUsers() {
  loading.value = true
  error.value = null
  try {
    const res = await apiFetch('/api/users')
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

const assignableRoles = computed(() => {
  if (!editingUser.value || !currentUser.value) return ROLES.value
  if (currentUser.value.role === 'admin') return ROLES.value

  const currentIndex = orderedRoles.value.findIndex(r => r.name === currentUser.value?.role)
  return orderedRoles.value
    .filter((role, index) => index > currentIndex || role.name === editingUser.value?.role)
    .map(r => r.name)
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
    const res = await apiFetch(`/api/admin/users/${editingUser.value.id}`, {
      method: 'PATCH',
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
    const res = await apiFetch(`/api/admin/users/${deleteTarget.value.id}`, {
      method: 'DELETE',
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

const deptItems = computed(() => deptRecords.value.map(d => ({ value: d.name })))
const roleItems = computed(() => assignableRoles.value.map(name => ({ value: name })))

watch([activeTab, systemTab], async ([tab, subtab]) => {
  if (tab === 'system' && subtab === 'logs' && canSeeLogsTab.value) {
    if (!containerServices.value.length) await loadContainerLogs()
    startContainerLogPolling()
    return
  }
  stopContainerLogPolling()
})

watch(selectedContainerService, async (service, previous) => {
  if (!service || service === previous) return
  if (activeTab.value === 'system' && systemTab.value === 'logs') {
    await loadContainerLogs()
  }
})

watch(containerLogs, async () => {
  await scrollLogsToBottom()
})

onUnmounted(() => {
  stopContainerLogPolling()
  document.removeEventListener('click', handleDocumentClick)
})

onMounted(() => {
  document.addEventListener('click', handleDocumentClick)
})
</script>

<template>
  <header class="header">
    <h1>Administration</h1>
  </header>

  <!-- Tab navigation -->
  <nav class="tab-bar">
    <button
      v-if="canManageUsers()"
      class="tab-btn"
      :class="{ 'tab-btn--active': activeTab === 'users' }"
      @click="activeTab = 'users'"
    >
      <Users :size="16" aria-hidden="true" />
      Benutzerverwaltung
    </button>
    <button
      v-if="canSeePermissionsTab"
      class="tab-btn"
      :class="{ 'tab-btn--active': activeTab === 'permissions' }"
      @click="activeTab = 'permissions'"
    >
      <Lock :size="16" aria-hidden="true" />
      Stufen &amp; Rollen
    </button>
    <button
      v-if="canSeeLocationsTab"
      class="tab-btn"
      :class="{ 'tab-btn--active': activeTab === 'locations' }"
      @click="activeTab = 'locations'"
    >
      <MapPin :size="16" aria-hidden="true" />
      Orte
    </button>
    <button
      v-if="canSeeTrashTab"
      class="tab-btn"
      :class="{ 'tab-btn--active': activeTab === 'trash' }"
      @click="activeTab = 'trash'; void loadDeletedActivities()"
    >
      <Trash2 :size="16" aria-hidden="true" />
      Papierkorb
    </button>
    <div v-if="canSeePermissionsTab" class="system-tab-menu">
      <button
        class="tab-btn"
        :class="{ 'tab-btn--active': activeTab === 'system' || systemMenuOpen }"
        @click.stop="toggleSystemMenu()"
      >
        <Cog :size="16" aria-hidden="true" />
        {{ systemTabLabel }}
        <ChevronDown :size="14" aria-hidden="true" class="system-tab-caret" :class="{ 'system-tab-caret--open': systemMenuOpen }" />
      </button>

      <div v-if="systemMenuOpen" class="system-tab-dropdown">
        <button
          type="button"
          class="system-tab-dropdown-item"
          :class="{ 'system-tab-dropdown-item--active': activeTab === 'system' && systemTab === 'settings' }"
          @click="selectSystemTab('settings')"
        >
          Einstellungen
        </button>
        <button
          v-if="canSeeLogsTab"
          type="button"
          class="system-tab-dropdown-item"
          :class="{ 'system-tab-dropdown-item--active': activeTab === 'system' && systemTab === 'logs' }"
          @click="selectSystemTab('logs')"
        >
          Container Logs
        </button>
      </div>
    </div>
  </nav>

  <!-- Tab: Benutzerverwaltung -->
  <main v-if="activeTab === 'users'" class="main">
    <div class="tab-header">
      <div class="tab-header-left">
        <template v-if="!canSeeAllDepts">
          <span class="dept-label">Stufe:</span>
          <DepartmentBadge v-if="currentUser?.department" :department="currentUser.department" />
          <span v-else>—</span>
        </template>
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
            type="button"
            class="dept-pill-btn"
            :class="{ 'dept-pill-btn--active': filterDept === 'Alle' }"
            @click="filterDept = 'Alle'"
          >Alle</button>
          <button
            v-for="d in DEPARTMENTS"
            :key="d"
            type="button"
            class="dept-pill-btn dept-pill-btn--bare"
            @click="filterDept = d as any"
          >
            <DepartmentBadge :department="d" :active="filterDept === d" />
          </button>
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
              <td class="td-name" data-label="Name">{{ u.display_name }}</td>
              <td class="td-email" data-label="E-Mail">{{ u.email }}</td>
              <td data-label="Stufe">
                <DepartmentBadge v-if="u.department" :department="u.department" />
                <span v-else>—</span>
              </td>
              <td data-label="Rolle">
                <RoleBadge :role="u.role" />
              </td>
              <td data-label="Aktionen">
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

  <!-- Tab: Orte (locations_manage_scope = all) -->
  <main v-else-if="activeTab === 'locations' && canSeeLocationsTab" class="main">
    <LocationManager />
  </main>

  <main v-else-if="activeTab === 'trash' && canSeeTrashTab" class="main">
    <div class="tab-header">
      <div class="tab-header-left">
        <h2 class="trash-title">Gelöschte Aktivitäten</h2>
        <p class="trash-note">Einträge im Papierkorb werden nach 90 Tagen automatisch endgültig gelöscht.</p>
      </div>
      <span class="user-count">{{ deletedActivities.length }} Einträge</span>
    </div>

    <div v-if="trashLoading" class="loading">Lade Papierkorb...</div>
    <div v-else-if="trashError" class="error-msg"><ErrorAlert :error="trashError" /></div>
    <div v-else class="table-wrap">
      <table class="users-table">
        <thead>
          <tr>
            <th>Titel</th>
            <th>Datum</th>
            <th>Gelöscht am</th>
            <th>Auto-Löschung</th>
            <th>Gelöscht von</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="entry in deletedActivities" :key="entry.activity.id">
            <td class="td-name" data-label="Titel">{{ entry.activity.title }}</td>
            <td data-label="Datum">{{ entry.activity.date }}</td>
            <td data-label="Gelöscht am">{{ formatDateTime(entry.deleted_at) }}</td>
            <td data-label="Auto-Löschung">{{ formatAutoDeleteDate(entry.deleted_at) }}</td>
            <td class="td-email" data-label="Gelöscht von">{{ entry.deleted_by.display_name || entry.deleted_by.email || 'Unbekannt' }}</td>
            <td data-label="Aktionen">
              <div class="trash-actions">
                <button
                  class="btn-primary btn-restore"
                  :disabled="restoringActivityId === entry.activity.id || deletingActivityId === entry.activity.id"
                  @click="restoreDeletedActivity(entry)"
                >
                  {{ restoringActivityId === entry.activity.id ? 'Wird wiederhergestellt...' : 'Wiederherstellen' }}
                </button>
                <button
                  class="btn-danger btn-delete-forever"
                  :disabled="deletingActivityId === entry.activity.id || restoringActivityId === entry.activity.id"
                  @click="permanentlyDeleteActivity(entry)"
                >
                  {{ deletingActivityId === entry.activity.id ? 'Wird gelöscht...' : 'Endgültig löschen' }}
                </button>
              </div>
            </td>
          </tr>
          <tr v-if="deletedActivities.length === 0">
            <td colspan="6" class="td-empty">Keine gelöschten Aktivitäten.</td>
          </tr>
        </tbody>
      </table>
    </div>
  </main>

  <main v-else-if="activeTab === 'system' && canSeePermissionsTab" class="main">
    <SystemConfigManager v-if="systemTab === 'settings'" />

    <section v-else-if="systemTab === 'logs' && canSeeLogsTab">
      <div class="tab-header">
        <div class="tab-header-left">
          <h2 class="trash-title">DPW Container Logs</h2>
        </div>
        <span class="user-count">Quelle: {{ containerLogsSource || '—' }}</span>
      </div>

      <div class="logs-toolbar">
        <div class="logs-field">
          <label class="filter-label" for="container-service-select">Container</label>
          <select id="container-service-select" v-model="selectedContainerService" class="form-input logs-service-select">
            <option v-for="service in containerServices" :key="service" :value="service">
              {{ service }}
            </option>
          </select>
        </div>

        <div class="logs-field">
          <label class="filter-label" for="tail-input">Tail</label>
          <input
            id="tail-input"
            v-model.number="containerLogsTail"
            class="form-input logs-tail-input"
            type="number"
            min="50"
            max="3000"
            step="50"
          />
        </div>

        <div class="logs-actions">
          <button class="btn-primary logs-action-btn logs-refresh-btn" :disabled="containerLogsLoading" @click="void loadContainerLogs()">
            {{ containerLogsLoading ? 'Lade...' : 'Aktualisieren' }}
          </button>
          <button
            type="button"
            class="btn-edit logs-action-btn logs-toggle-btn"
            :class="{ 'logs-toggle-btn--active': autoRefreshLogs }"
            @click="toggleAutoRefreshLogs()"
          >
            Auto-Refresh {{ autoRefreshLogs ? 'AN (2s)' : 'AUS' }}
          </button>
          <button
            type="button"
            class="btn-edit logs-action-btn logs-toggle-btn"
            :class="{ 'logs-toggle-btn--active': followLatestLogs }"
            @click="toggleFollowLatestLogs()"
          >
            Follow Latest {{ followLatestLogs ? 'AN' : 'AUS' }}
          </button>
          <button
            type="button"
            class="btn-edit logs-action-btn logs-jump-btn"
            :disabled="followLatestLogs"
            @click="jumpToLatestLogEntry()"
          >
            Zum neuesten Eintrag springen
          </button>
        </div>
      </div>

      <div v-if="containerLogsError" class="error-msg"><ErrorAlert :error="containerLogsError" /></div>
      <div v-else-if="containerLogsLoading && !containerLogs" class="loading">Lade Container-Logs...</div>
      <div v-else-if="containerServices.length === 0" class="loading">Keine DPW-Container gefunden.</div>
      <pre v-else ref="containerLogsViewer" class="logs-viewer" @scroll="handleLogsScroll()">{{ containerLogs || 'Keine Logs verfügbar.' }}</pre>
    </section>
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
          <BadgeSelect
            v-if="canEditDepts"
            kind="department"
            :items="deptItems"
            allow-empty
            placeholder="Keine Angabe"
            :model-value="editForm.department || null"
            @update:model-value="(v: string | null) => editForm.department = (v ?? '') as any"
          />
          <input v-else class="form-input form-input--readonly" type="text"
            :value="editForm.department || 'Keine Angabe'" readonly />
        </div>

        <div v-if="canEditRoles" class="form-group">
          <label class="form-label">Rolle</label>
          <BadgeSelect
            kind="role"
            :items="roleItems"
            placeholder="Rolle wählen…"
            :model-value="editForm.role"
            @update:model-value="(v: string | null) => editForm.role = (v ?? '') as any"
          />
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
  color: var(--text-primary);
  margin: 0;
}

/* Tabs */
.tab-bar {
  display: flex;
  gap: 4px;
  padding: 16px 24px 0;
  border-bottom: 1px solid var(--border);
}
.tab-btn {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 10px 18px;
  font-size: 0.88rem;
  font-weight: 600;
  color: var(--text-muted);
  background: none;
  border: none;
  border-bottom: 2px solid transparent;
  cursor: pointer;
  transition: color 0.15s, border-color 0.15s;
  margin-bottom: -1px;
}
.tab-btn:hover {
  color: var(--text-secondary);
}
.tab-btn--active {
  color: var(--accent);
  border-bottom-color: var(--accent);
}

.system-tab-menu {
  position: relative;
}

.system-tab-caret {
  transition: transform 0.15s;
}

.system-tab-caret--open {
  transform: rotate(180deg);
}

.system-tab-dropdown {
  position: absolute;
  top: calc(100% + 8px);
  right: 0;
  min-width: 190px;
  padding: 8px;
  border: 1px solid var(--border);
  border-radius: 12px;
  background: var(--card-bg);
  box-shadow: 0 10px 28px rgba(0, 0, 0, 0.12);
  z-index: 20;
}

.system-tab-dropdown-item {
  display: block;
  width: 100%;
  padding: 10px 12px;
  border: none;
  border-radius: 8px;
  background: transparent;
  color: var(--text-primary);
  text-align: left;
  font-size: 0.9rem;
  cursor: pointer;
}

.system-tab-dropdown-item:hover {
  background: var(--bg-hover);
}

.system-tab-dropdown-item--active {
  background: var(--bg-elevated);
  color: var(--accent);
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
  color: var(--text-muted);
}
.form-input--readonly {
  background: var(--input-readonly-bg);
  color: var(--input-readonly-color);
  cursor: default;
}
.main {
  padding: 20px 24px;
  max-width: 1000px;
}
.loading, .error-msg {
  padding: 24px;
  color: var(--text-muted);
}
.error-msg { color: var(--error-color); }

.section-divider {
  height: 1px;
  background: var(--border);
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
  color: var(--text-secondary);
}
.dept-pills {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
  align-items: center;
}
.dept-pill-btn {
  padding: 4px 12px;
  border-radius: 999px;
  border: 1.5px solid var(--border-strong);
  background: var(--bg-surface);
  font-size: 0.82rem;
  cursor: pointer;
  color: var(--text-secondary);
  transition: background 0.12s, border-color 0.12s;
}
.dept-pill-btn:hover { background: var(--bg-hover); }
.dept-pill-btn--active {
  background: var(--accent);
  border-color: var(--accent);
  color: #fff;
}
.dept-pill-btn--bare {
  padding: 2px;
  border: none;
  background: transparent;
}
.dept-pill-btn--bare:hover { background: transparent; }
.dept-label {
  font-size: 0.85rem;
  font-weight: 600;
  color: var(--text-secondary);
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
  background: var(--card-bg);
  font-size: 0.9rem;
}
.users-table th {
  background: var(--bg-elevated);
  padding: 12px 16px;
  text-align: left;
  font-size: 0.78rem;
  font-weight: 600;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 0.04em;
  border-bottom: 1px solid var(--border);
}
.users-table td {
  padding: 12px 16px;
  border-bottom: 1px solid var(--bg-hover);
  color: var(--text-primary);
  vertical-align: middle;
}
.users-table tr:last-child td { border-bottom: none; }
.td-name { font-weight: 500; }
.td-email { color: var(--text-muted); font-size: 0.85rem; }
.td-empty { text-align: center; color: var(--text-subtle); padding: 32px; }

.item-actions {
  display: flex;
  gap: 6px;
}

/* Edit button */
.btn-edit {
  padding: 5px 14px;
  border-radius: 6px;
  border: 1.5px solid var(--border-strong);
  background: var(--btn-secondary-bg);
  font-size: 0.82rem;
  cursor: pointer;
  color: var(--text-secondary);
  transition: background 0.12s;
}
.btn-edit:hover { background: var(--bg-hover); }

.btn-delete {
  padding: 5px 12px;
  border-radius: 6px;
  border: 1.5px solid var(--error-border);
  background: var(--bg-surface);
  font-size: 0.82rem;
  cursor: pointer;
  color: var(--error-color);
}
.btn-delete:hover:not(:disabled) { background: var(--error-bg); }
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
  background: var(--modal-bg);
  border-radius: 16px;
  padding: 32px;
  width: 100%;
  max-width: 420px;
  box-shadow: 0 8px 40px rgba(0,0,0,0.18);
}
.modal--danger { border: 2px solid var(--error-border); }
.modal-title {
  font-size: 1.15rem;
  font-weight: 700;
  margin: 0 0 4px;
  color: var(--text-primary);
}
.modal-title--danger { color: var(--error-color); }
.modal-email {
  font-size: 0.85rem;
  color: var(--text-muted);
  margin: 0 0 20px;
}
.modal-warning {
  font-size: 0.88rem;
  color: var(--text-muted);
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
  color: var(--text-secondary);
}
.form-input {
  padding: 9px 11px;
  border: 1.5px solid var(--input-border);
  border-radius: 8px;
  font-size: 0.93rem;
  color: var(--input-color);
  background: var(--input-bg);
  outline: none;
  transition: border-color 0.15s;
}
.form-input:focus { border-color: var(--accent); }
.form-error {
  font-size: 0.85rem;
  color: var(--error-color);
  background: var(--error-bg);
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
  border: 1.5px solid var(--error-border);
  background: var(--bg-surface);
  font-size: 0.9rem;
  cursor: pointer;
  color: var(--error-color);
  transition: background 0.12s;
}
.btn-danger:hover:not(:disabled) { background: var(--btn-danger-bg-hover); }
.btn-danger:disabled { opacity: 0.4; cursor: default; }
.btn-cancel {
  padding: 9px 20px;
  border-radius: 8px;
  border: 1.5px solid var(--border-strong);
  background: var(--btn-secondary-bg);
  font-size: 0.9rem;
  cursor: pointer;
  color: var(--text-secondary);
  transition: background 0.12s;
}
.btn-cancel:hover { background: var(--bg-hover); }
.btn-primary {
  padding: 9px 20px;
  background: var(--btn-primary-bg);
  color: #fff;
  border: none;
  border-radius: 8px;
  font-size: 0.9rem;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.15s;
}
.btn-primary:hover:not(:disabled) { background: var(--btn-primary-bg-hover); }
.btn-primary:disabled { opacity: 0.6; cursor: default; }

.trash-title {
  margin: 0;
  font-size: 1rem;
  font-weight: 700;
  color: var(--text-primary);
}

.trash-note {
  margin: 6px 0 0;
  color: var(--text-muted);
  font-size: 0.9rem;
}

.trash-actions {
  display: flex;
  flex-direction: column;
  align-items: stretch;
  gap: 8px;
  min-width: 180px;
}

.btn-restore {
  padding: 6px 12px;
  font-size: 0.82rem;
  width: 100%;
}

.btn-delete-forever {
  padding: 6px 12px;
  font-size: 0.82rem;
  width: 100%;
}

.logs-toolbar {
  display: flex;
  align-items: center;
  gap: 14px;
  margin-bottom: 14px;
  flex-wrap: wrap;
}

.logs-field {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.logs-service-select {
  min-width: 180px;
}

.logs-tail-input {
  width: 120px;
}

.logs-actions {
  display: flex;
  align-items: end;
  gap: 10px;
  flex-wrap: wrap;
}

.logs-action-btn {
  min-height: 38px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  box-sizing: border-box;
}

.logs-refresh-btn {
  min-width: 148px;
}

.logs-toggle-btn--active {
  background: var(--bg-elevated);
  border-color: var(--accent);
  color: var(--accent);
}

.logs-jump-btn:disabled {
  opacity: 0.45;
  cursor: default;
}

.logs-viewer {
  margin: 0;
  padding: 14px;
  min-height: 280px;
  max-height: 62vh;
  overflow: auto;
  border-radius: 10px;
  border: 1px solid var(--border);
  background: #0b1220;
  color: #d7e0ea;
  font-size: 0.78rem;
  line-height: 1.35;
  white-space: pre-wrap;
  word-break: break-word;
}

@media (max-width: 599px) {
  .header {
    padding: 20px 16px 0;
  }
  .header h1 {
    font-size: 1.3rem;
  }
  .tab-bar {
    padding: 12px 8px 0;
    gap: 2px;
    overflow-x: auto;
    -webkit-overflow-scrolling: touch;
    scrollbar-width: none;
    flex-wrap: nowrap;
  }
  .tab-bar::-webkit-scrollbar { display: none; }
  .tab-btn {
    padding: 10px 12px;
    font-size: 0.82rem;
    white-space: nowrap;
    flex-shrink: 0;
  }
  .system-tab-dropdown {
    right: auto;
    left: 0;
  }
  .tab-header {
    flex-wrap: wrap;
    gap: 8px;
  }
  .logs-toolbar {
    flex-direction: column;
    align-items: stretch;
  }
  .logs-service-select,
  .logs-tail-input {
    width: 100%;
  }
  .logs-actions {
    align-items: stretch;
  }
  .users-table {
    font-size: 0.82rem;
    border-collapse: separate;
    border-spacing: 0 10px;
    background: transparent;
  }
  .users-table thead {
    display: none;
  }
  .users-table tbody,
  .users-table tr,
  .users-table td {
    display: block;
    width: 100%;
  }
  .users-table tr {
    border: 1px solid var(--border);
    border-radius: 12px;
    background: var(--card-bg);
    box-shadow: 0 2px 10px rgba(0, 0, 0, 0.04);
    padding: 8px 10px;
  }
  .users-table td {
    padding: 8px 6px;
    border-bottom: 1px dashed #eef2f7;
  }
  .users-table td:last-child {
    border-bottom: none;
    padding-bottom: 4px;
  }
  .users-table td::before {
    content: attr(data-label);
    display: block;
    font-size: 0.72rem;
    font-weight: 700;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    color: #94a3b8;
    margin-bottom: 4px;
  }
  .td-email {
    font-size: 0.78rem;
    overflow-wrap: anywhere;
  }
  .td-empty {
    text-align: center;
    border: 1px dashed var(--border);
    border-radius: 10px;
    background: var(--card-bg);
  }
  .td-empty::before {
    display: none;
  }
  .item-actions {
    justify-content: flex-start;
    gap: 8px;
  }
  .trash-actions {
    min-width: 0;
  }
  .btn-edit,
  .btn-delete {
    min-height: 38px;
    padding: 7px 12px;
    font-size: 0.8rem;
  }
}
</style>
