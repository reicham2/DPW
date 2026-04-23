<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { useEventTemplates } from '../composables/useEventTemplates'
import { user } from '../composables/useAuth'
import { usePermissions } from '../composables/usePermissions'
import { wsSend, wsRegister, wsJoin, wsLeave, useWebSocket } from '../composables/useWebSocket'
import DepartmentBadge from '../components/DepartmentBadge.vue'
import TemplateVarsDropdown from '../components/TemplateVarsDropdown.vue'
import ErrorAlert from '../components/ErrorAlert.vue'
import type { Department, EditSection, EventTemplate } from '../types'
import { config } from '../config'

const router = useRouter()
const { departments: deptRecords, fetchDepartments, myPermissions, fetchMyPermissions } = usePermissions()
const ALL_DEPARTMENTS = computed<Department[]>(() => deptRecords.value.map(d => d.name))
const { fetchTemplates, fetchTemplate, saveTemplate, templates, loading, error } = useEventTemplates()

const canEditAll = computed(() => myPermissions.value?.event_templates_scope === 'all')
const canEditOwnDept = computed(() => {
  const scope = myPermissions.value?.event_templates_scope
  return scope === 'own_dept' || scope === 'all'
})

const visibleDepartments = computed<Department[]>(() => {
  if (canEditAll.value) return ALL_DEPARTMENTS.value
  if (canEditOwnDept.value) {
    const own = user.value?.department as Department | undefined
    return own ? ALL_DEPARTMENTS.value.filter(d => d === own) : []
  }
  return []
})

const activeDept = ref<Department>(
  user.value?.department
    ? user.value.department as Department
    : '' as Department
)

const title = ref('')
const body = ref('')
const editorRef = ref<HTMLElement | null>(null)

// ---- Dirty tracking & save indicator ----------------------------------------
const dirtyFields = new Set<string>()
const savedFields = ref<Record<string, number>>({})
let savedTimer: ReturnType<typeof setTimeout> | null = null
let suppressDirtyTracking = true
let applyingRemote = false

function markDirty(...fields: string[]) {
  if (suppressDirtyTracking || applyingRemote) return
  for (const f of fields) dirtyFields.add(f)
}

function showSavedIndicator() {
  if (savedTimer) clearTimeout(savedTimer)
  const snap: Record<string, number> = {}
  for (const f of dirtyFields) snap[f] = Date.now()
  dirtyFields.clear()
  savedFields.value = snap
  savedTimer = setTimeout(() => { savedFields.value = {} }, 2000)
}

// ---- Collaborative editing state -------------------------------------------
const activeEditors = ref<string[]>([])
const sectionLocks = ref<Map<EditSection, string>>(new Map())
const myLockedSection = ref<EditSection | null>(null)

function tplId() { return `evt_tpl_${activeDept.value}` }

function isLockedByOther(section: EditSection): boolean {
  const locker = sectionLocks.value.get(section)
  return !!locker && locker !== user.value?.display_name
}
function lockedBy(section: EditSection): string | null {
  const locker = sectionLocks.value.get(section)
  return locker && locker !== user.value?.display_name ? locker : null
}

function lockSection(section: EditSection) {
  if (isLockedByOther(section)) return
  if (myLockedSection.value === section) return
  if (myLockedSection.value) {
    wsSend({ type: 'unlock', activity_id: tplId(), section: myLockedSection.value })
  }
  myLockedSection.value = section
  wsSend({ type: 'lock', activity_id: tplId(), section })
}

let unlockTimer: ReturnType<typeof setTimeout> | null = null
function unlockSection(section: EditSection, event?: FocusEvent) {
  if (myLockedSection.value !== section) return
  if (event?.relatedTarget instanceof HTMLElement) {
    const wrapper = (event.currentTarget as HTMLElement)
    if (wrapper?.contains(event.relatedTarget)) return
  }
  if (unlockTimer) clearTimeout(unlockTimer)
  unlockTimer = setTimeout(() => {
    if (myLockedSection.value === section) {
      myLockedSection.value = null
      wsSend({ type: 'unlock', activity_id: tplId(), section })
    }
  }, 100)
}

// ---- Auto-save --------------------------------------------------------------
const AUTOSAVE_INTERVAL = config.AUTOSAVE_INTERVAL
const AUTOSAVE_DEBOUNCE = config.AUTOSAVE_DEBOUNCE

let autoSaveTimer: ReturnType<typeof setTimeout> | null = null
let autoSaveInterval: ReturnType<typeof setInterval> | null = null
let hasPendingChanges = false

interface LocalEventTemplateDraft {
  version: 1;
  department: Department;
  savedAt: number;
  title: string;
  body: string;
}

const pendingLocalDraft = ref<LocalEventTemplateDraft | null>(null)
const localDraftRestoredAt = ref<number | null>(null)
let localDraftWriteTimer: ReturnType<typeof setTimeout> | null = null
const LOCAL_DRAFT_WRITE_DEBOUNCE_MS = 350

function localDraftKey(dept = activeDept.value) {
  return `dpw:event-template-draft:${dept}`
}

function buildLocalDraft(dept = activeDept.value): LocalEventTemplateDraft {
  return {
    version: 1,
    department: dept,
    savedAt: Date.now(),
    title: title.value,
    body: body.value,
  }
}

function clearLocalDraft(dept = activeDept.value) {
  if (localDraftWriteTimer) { clearTimeout(localDraftWriteTimer); localDraftWriteTimer = null }
  try { window.localStorage.removeItem(localDraftKey(dept)) } catch { /* ignore */ }
}

function writeLocalDraftNow() {
  if (!activeDept.value) return
  try { window.localStorage.setItem(localDraftKey(), JSON.stringify(buildLocalDraft())) } catch { /* ignore */ }
}

function scheduleLocalDraftWrite() {
  if (!activeDept.value || suppressDirtyTracking || applyingRemote) return
  if (localDraftWriteTimer) clearTimeout(localDraftWriteTimer)
  localDraftWriteTimer = setTimeout(writeLocalDraftNow, LOCAL_DRAFT_WRITE_DEBOUNCE_MS)
}

function loadPendingLocalDraft(dept: Department) {
  try {
    const raw = window.localStorage.getItem(localDraftKey(dept))
    if (!raw) { pendingLocalDraft.value = null; return }
    const d: LocalEventTemplateDraft = JSON.parse(raw)
    if (!d || d.version !== 1 || d.department !== dept) { pendingLocalDraft.value = null; return }
    const match = templates.value.find(t => t.department === dept)
    if (match && d.title === match.title && d.body === match.body) {
      clearLocalDraft(dept); pendingLocalDraft.value = null; return
    }
    pendingLocalDraft.value = d
  } catch { pendingLocalDraft.value = null }
}

function restoreLocalDraft() {
  const d = pendingLocalDraft.value
  if (!d) return
  suppressDirtyTracking = true
  title.value = d.title
  body.value = d.body
  if (editorRef.value) editorRef.value.innerHTML = d.body
  pendingLocalDraft.value = null
  localDraftRestoredAt.value = Date.now()
  suppressDirtyTracking = false
  hasPendingChanges = true
  scheduleAutoSave()
}

function discardLocalDraft() {
  clearLocalDraft()
  pendingLocalDraft.value = null
  localDraftRestoredAt.value = null
}

async function doSave() {
  if (!activeDept.value) return
  const result = await saveTemplate(activeDept.value, title.value, body.value)
  if (result) {
    clearLocalDraft()
    localDraftRestoredAt.value = null
    showSavedIndicator()
    const idx = templates.value.findIndex(t => t.department === activeDept.value)
    if (idx >= 0) templates.value[idx] = result
    else templates.value.push(result)
  }
  hasPendingChanges = false
}

function scheduleAutoSave() {
  hasPendingChanges = true
  if (AUTOSAVE_DEBOUNCE) {
    if (autoSaveTimer) clearTimeout(autoSaveTimer)
    autoSaveTimer = setTimeout(doSave, AUTOSAVE_INTERVAL)
  }
}

function startAutoSaveInterval() {
  if (!AUTOSAVE_DEBOUNCE) {
    autoSaveInterval = setInterval(() => {
      if (hasPendingChanges) doSave()
    }, AUTOSAVE_INTERVAL)
  }
}

function stopAutoSave() {
  if (autoSaveTimer) { clearTimeout(autoSaveTimer); autoSaveTimer = null }
  if (autoSaveInterval) { clearInterval(autoSaveInterval); autoSaveInterval = null }
}

// ---- Watch fields -----------------------------------------------------------
watch(title, () => { markDirty('title'); scheduleAutoSave(); scheduleLocalDraftWrite() })
watch(body, () => { markDirty('body'); scheduleAutoSave(); scheduleLocalDraftWrite() })

// ---- Load template for department -------------------------------------------
async function loadDept(dept: Department) {
  stopAutoSave()
  if (hasPendingChanges && activeDept.value) await doSave()

  if (myLockedSection.value) {
    wsSend({ type: 'unlock', activity_id: tplId(), section: myLockedSection.value })
    myLockedSection.value = null
  }
  wsLeave(tplId())
  sectionLocks.value = new Map()
  activeEditors.value = []

  activeDept.value = dept
  suppressDirtyTracking = true

  const tpl = templates.value.find(t => t.department === dept)
  title.value = tpl?.title ?? ''
  body.value = tpl?.body ?? ''
  if (editorRef.value) editorRef.value.innerHTML = body.value

  loadPendingLocalDraft(dept)

  suppressDirtyTracking = false
  hasPendingChanges = false
  startAutoSaveInterval()

  wsJoin(tplId())
}

// ---- WebSocket events -------------------------------------------------------
const { isConnected } = useWebSocket()
let unregister: (() => void) | null = null

function handleWs(e: any) {
  if (e.event === 'lock' && e.activity_id === tplId()) {
    sectionLocks.value.set(e.section, e.user)
    sectionLocks.value = new Map(sectionLocks.value)
  }
  if (e.event === 'unlock' && e.activity_id === tplId()) {
    sectionLocks.value.delete(e.section)
    sectionLocks.value = new Map(sectionLocks.value)
  }
  if (e.event === 'editors' && e.activity_id === tplId()) {
    activeEditors.value = (e.users as string[]).filter(u => u !== user.value?.display_name)
  }
  if (e.event === 'locks_state' && e.activity_id === tplId()) {
    const m = new Map<EditSection, string>()
    if (e.locks && typeof e.locks === 'object') {
      for (const [k, v] of Object.entries(e.locks)) m.set(k as EditSection, v as string)
    }
    sectionLocks.value = m
  }
  if (e.event === 'event_template_updated') {
    const tpl = e.template as EventTemplate
    if (!tpl) return
    const idx = templates.value.findIndex(t => t.department === tpl.department)
    if (idx >= 0) templates.value[idx] = tpl
    else templates.value.push(tpl)
    if (tpl.department === activeDept.value) {
      applyingRemote = true
      if (myLockedSection.value !== ('evt_tpl_title' as EditSection)) title.value = tpl.title
      if (myLockedSection.value !== ('evt_tpl_body' as EditSection)) {
        body.value = tpl.body
        if (editorRef.value && myLockedSection.value !== ('evt_tpl_body' as EditSection))
          editorRef.value.innerHTML = tpl.body
      }
      applyingRemote = false
    }
  }
}

// ---- Rich text helpers ------------------------------------------------------
function execCmd(cmd: string, val?: string) {
  document.execCommand(cmd, false, val)
  syncBody()
}
function syncBody() {
  if (editorRef.value) body.value = editorRef.value.innerHTML
}

// ---- Lifecycle --------------------------------------------------------------
onMounted(async () => {
  await Promise.all([fetchMyPermissions(), fetchDepartments()])
  if (!myPermissions.value || myPermissions.value.event_templates_scope === 'none') {
    router.replace('/')
    return
  }

  await fetchTemplates()

  const dept = visibleDepartments.value.length
    ? (visibleDepartments.value.includes(activeDept.value) ? activeDept.value : visibleDepartments.value[0])
    : '' as Department
  activeDept.value = dept

  if (dept) {
    suppressDirtyTracking = true
    const tpl = templates.value.find(t => t.department === dept)
    title.value = tpl?.title ?? ''
    body.value = tpl?.body ?? ''
    if (editorRef.value) editorRef.value.innerHTML = body.value
    loadPendingLocalDraft(dept)
    suppressDirtyTracking = false
  }

  unregister = wsRegister(handleWs)
  if (dept) wsJoin(tplId())
  startAutoSaveInterval()
})

onUnmounted(() => {
  stopAutoSave()
  if (localDraftWriteTimer) clearTimeout(localDraftWriteTimer)
  if (hasPendingChanges && activeDept.value) doSave()
  if (myLockedSection.value) {
    wsSend({ type: 'unlock', activity_id: tplId(), section: myLockedSection.value })
  }
  if (activeDept.value) wsLeave(tplId())
  if (unregister) unregister()
})
</script>

<template>
  <header class="header">
    <h1>Event-Vorlagen</h1>
  </header>

  <ErrorAlert v-if="error" :message="error" @close="error = null" />

  <!-- Local-draft recovery banner -->
  <div v-if="pendingLocalDraft" class="draft-banner">
    <span>Ungespeicherter Entwurf gefunden ({{ new Date(pendingLocalDraft.savedAt).toLocaleTimeString('de-CH') }})</span>
    <button class="btn-restore" @click="restoreLocalDraft">Wiederherstellen</button>
    <button class="btn-discard" @click="discardLocalDraft">Verwerfen</button>
  </div>

  <div v-if="loading" class="loading">Laden...</div>

  <template v-else>
    <!-- Active editors indicator -->
    <div v-if="activeEditors.length" class="active-editors">
      {{ activeEditors.join(', ') }} {{ activeEditors.length === 1 ? 'bearbeitet' : 'bearbeiten' }} ebenfalls
    </div>

    <!-- Department tabs -->
    <div v-if="visibleDepartments.length > 1" class="dept-tabs">
      <button
        v-for="dept in visibleDepartments"
        :key="dept"
        class="dept-tab"
        :class="{ 'dept-tab--active': dept === activeDept }"
        @click="loadDept(dept)"
      >
        <DepartmentBadge :department="dept" :small="true" />
      </button>
    </div>

    <!-- Template editor -->
    <div class="form-card">
      <TemplateVarsDropdown hint="Verwende diese Variablen im Titel oder Inhalt. Sie werden beim Veröffentlichen durch die Aktivitätsdaten ersetzt." />

      <!-- Title -->
      <div class="form-group" @focusin="lockSection('evt_tpl_title' as EditSection)" @focusout="unlockSection('evt_tpl_title' as EditSection, $event)">
        <label>
          Titel
          <span v-if="lockedBy('evt_tpl_title' as EditSection)" class="lock-badge">{{ lockedBy('evt_tpl_title' as EditSection) }}</span>
          <span v-if="savedFields['title']" class="saved-badge">gespeichert</span>
        </label>
        <input
          v-model="title"
          type="text"
          class="input"
          placeholder="z.B. {{abteilung}}: {{titel}}"
          :disabled="isLockedByOther('evt_tpl_title' as EditSection)"
        />
      </div>

      <!-- Body (rich text editor) -->
      <div class="form-group" @focusin="lockSection('evt_tpl_body' as EditSection)" @focusout="unlockSection('evt_tpl_body' as EditSection, $event)">
        <label>
          Inhalt
          <span v-if="lockedBy('evt_tpl_body' as EditSection)" class="lock-badge">{{ lockedBy('evt_tpl_body' as EditSection) }}</span>
          <span v-if="savedFields['body']" class="saved-badge">gespeichert</span>
        </label>
        <div class="editor-toolbar" v-if="!isLockedByOther('evt_tpl_body' as EditSection)">
          <button type="button" title="Fett" @mousedown.prevent="execCmd('bold')"><b>B</b></button>
          <button type="button" title="Kursiv" @mousedown.prevent="execCmd('italic')"><i>I</i></button>
          <button type="button" title="Unterstrichen" @mousedown.prevent="execCmd('underline')"><u>U</u></button>
          <span class="toolbar-sep"></span>
          <button type="button" title="Aufzählung" @mousedown.prevent="execCmd('insertUnorderedList')">&#8226;</button>
          <button type="button" title="Nummerierung" @mousedown.prevent="execCmd('insertOrderedList')">1.</button>
          <span class="toolbar-sep"></span>
          <button type="button" title="Formatierung entfernen" @mousedown.prevent="execCmd('removeFormat')">&#x2715;</button>
        </div>
        <div
          ref="editorRef"
          class="rich-editor"
          contenteditable
          :class="{ 'editor-locked': isLockedByOther('evt_tpl_body' as EditSection) }"
          @input="syncBody"
        ></div>
      </div>
    </div>
  </template>
</template>

<style scoped>
.header { padding: 28px 24px 0; display: flex; align-items: baseline; gap: 12px; }
.header h1 { font-size: 1.5rem; font-weight: 700; color: #1a202c; margin: 0; }

.loading { padding: 24px; color: #6b7280; }

.active-editors {
  margin: 8px 24px 0; padding: 6px 12px; background: #fef3c7;
  border-radius: 6px; font-size: 0.82rem; color: #92400e;
}

.draft-banner {
  margin: 12px 24px 0; padding: 10px 14px; background: #fef9c3;
  border: 1px solid #fde68a; border-radius: 8px;
  display: flex; align-items: center; gap: 10px; font-size: 0.85rem;
}
.btn-restore { background: #2563eb; color: #fff; border: none; padding: 4px 12px; border-radius: 6px; cursor: pointer; font-size: 0.82rem; }
.btn-discard { background: none; border: 1px solid #d1d5db; padding: 4px 12px; border-radius: 6px; cursor: pointer; font-size: 0.82rem; color: #6b7280; }

.dept-tabs {
  display: flex; gap: 6px; padding: 16px 24px 0; flex-wrap: wrap;
}
.dept-tab {
  background: none; border: 2px solid transparent; border-radius: 8px;
  padding: 4px 8px; cursor: pointer; transition: border-color 0.15s;
}
.dept-tab--active { border-color: #2563eb; }

.form-card {
  margin: 16px 24px 24px; padding: 20px;
  background: #fff; border: 1px solid #e5e7eb; border-radius: 12px;
}

.form-group { margin-top: 16px; }
.form-group:first-child { margin-top: 0; }
.form-group label {
  display: flex; align-items: center; gap: 8px;
  font-size: 0.85rem; font-weight: 600; color: #374151; margin-bottom: 6px;
}

.input {
  width: 100%; padding: 8px 12px; border: 1px solid #d1d5db;
  border-radius: 8px; font-size: 0.9rem; box-sizing: border-box;
}
.input:focus { outline: none; border-color: #2563eb; box-shadow: 0 0 0 2px rgba(37, 99, 235, 0.15); }
.input:disabled { background: #f3f4f6; cursor: not-allowed; }

.lock-badge {
  font-size: 0.72rem; background: #fef3c7; color: #92400e;
  padding: 1px 7px; border-radius: 4px; font-weight: 500;
}
.saved-badge {
  font-size: 0.72rem; background: #d1fae5; color: #065f46;
  padding: 1px 7px; border-radius: 4px; font-weight: 500;
}

.editor-toolbar {
  display: flex; gap: 2px; padding: 6px 8px;
  background: #f9fafb; border: 1px solid #e5e7eb;
  border-radius: 8px 8px 0 0; border-bottom: none;
}
.editor-toolbar button {
  background: none; border: 1px solid transparent; border-radius: 4px;
  padding: 4px 8px; cursor: pointer; font-size: 0.82rem; color: #374151;
}
.editor-toolbar button:hover { background: #e5e7eb; }
.toolbar-sep { width: 1px; background: #d1d5db; margin: 0 4px; }

.rich-editor {
  min-height: 200px; padding: 12px 14px;
  border: 1px solid #d1d5db; border-radius: 0 0 8px 8px;
  font-size: 0.9rem; line-height: 1.6; outline: none;
}
.rich-editor:focus { border-color: #2563eb; box-shadow: 0 0 0 2px rgba(37, 99, 235, 0.15); }
.editor-locked { background: #f3f4f6; pointer-events: none; opacity: 0.7; }

@media (max-width: 599px) {
  .header { padding: 20px 16px 0; }
  .header h1 { font-size: 1.3rem; }
  .dept-tabs { padding: 12px 16px 0; }
  .form-card { margin: 12px 16px 16px; padding: 14px; }
  .draft-banner { margin: 8px 16px 0; flex-wrap: wrap; }
}
</style>
