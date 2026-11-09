<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import { useRouter } from 'vue-router'
import { useEventTemplates } from '../composables/useEventTemplates'
import { user } from '../composables/useAuth'
import { usePermissions } from '../composables/usePermissions'
import { wsSend, wsJoin, wsLeave, useWebSocket } from '../composables/useWebSocket'
import { CheckCircle2, Lock, Save, Users } from 'lucide-vue-next'
import DepartmentBadge from '../components/DepartmentBadge.vue'
import TemplateVarsDropdown from '../components/TemplateVarsDropdown.vue'
import ErrorAlert from '../components/ErrorAlert.vue'
import type { Department, EditSection, EventTemplate, EventTemplateChanges } from '../types'
import { formatApiError } from '../composables/useApi'
import { config } from '../config'

const router = useRouter()
const { departments: deptRecords, fetchDepartments, myPermissions, fetchMyPermissions } = usePermissions()
const ALL_DEPARTMENTS = computed<Department[]>(() => deptRecords.value.map(d => d.name))
const { fetchTemplates, createTemplate, updateTemplate, deleteTemplate, templates, loading, error } = useEventTemplates()

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
const showEditor = ref(false)
const selectedTemplateId = ref('')
const isCreatingTemplate = ref(false)
const templateName = ref('')
const templateIsDefault = ref(false)
const saving = ref(false)
const departmentTemplates = computed(() =>
  templates.value.filter(template => template.department === activeDept.value)
    .sort((a, b) => a.name.localeCompare(b.name, 'de', { sensitivity: 'base' }) || a.id.localeCompare(b.id))
)
const selectedTemplate = computed(() =>
  departmentTemplates.value.find(template => template.id === selectedTemplateId.value) ?? null
)

const title = ref('')
const body = ref('')
const editorRef = ref<HTMLElement | null>(null)
const curFont = ref('Arial')
const curSize = ref('12')
const curColor = ref('#000000')
const curBold = ref(false)
const curItalic = ref(false)
const curUnderline = ref(false)
const curUl = ref(false)
const curOl = ref(false)
const curBgColor = ref('#ffffff')
let savedSelection: Range | null = null
let linkSavedRange: Range | null = null
const showLinkDialog = ref(false)
const linkUrl = ref('')
const linkInputRef = ref<HTMLInputElement | null>(null)

// ---- Dirty tracking & save indicator ----------------------------------------
type TemplateField = keyof EventTemplateChanges
const dirtyFields = new Set<TemplateField>()
const savedFields = ref<Record<string, number>>({})
let savedTimer: ReturnType<typeof setTimeout> | null = null
let suppressDirtyTracking = true
let applyingRemote = false

function markDirty(...fields: TemplateField[]) {
  if (suppressDirtyTracking || applyingRemote) return
  for (const f of fields) dirtyFields.add(f)
}

function showSavedIndicator(fields: TemplateField[]) {
  if (savedTimer) clearTimeout(savedTimer)
  const snap: Record<string, number> = {}
  for (const f of fields) snap[f] = Date.now()
  savedFields.value = snap
  savedTimer = setTimeout(() => { savedFields.value = {} }, 2000)
}

// ---- Collaborative editing state -------------------------------------------
const activeEditors = ref<string[]>([])
const sectionLocks = ref<Map<EditSection, string>>(new Map())
const myLockedSection = ref<EditSection | null>(null)

function tplId() { return `evt_tpl_${activeDept.value}:${selectedTemplateId.value}` }

function isLockedByOther(section: EditSection): boolean {
  const locker = sectionLocks.value.get(section)
  return !!locker && locker !== user.value?.display_name
}
function lockedBy(section: EditSection): string | null {
  const locker = sectionLocks.value.get(section)
  return locker && locker !== user.value?.display_name ? locker : null
}

function lockSection(section: EditSection) {
  if (!selectedTemplateId.value || isCreatingTemplate.value) return
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
const autosaveInterval = () => config.AUTOSAVE_INTERVAL
const autosaveDebounce = () => config.AUTOSAVE_DEBOUNCE

let autoSaveTimer: ReturnType<typeof setTimeout> | null = null
let autoSaveInterval: ReturnType<typeof setInterval> | null = null
let saveQueue: Promise<boolean> = Promise.resolve(true)
let editorGeneration = 0

interface LocalEventTemplateDraft {
  version: 1;
  department: Department;
  templateId?: string;
  name?: string;
  isDefault?: boolean;
  savedAt: number;
  title: string;
  body: string;
}

const pendingLocalDraft = ref<LocalEventTemplateDraft | null>(null)
const localDraftRestoredAt = ref<number | null>(null)
let localDraftWriteTimer: ReturnType<typeof setTimeout> | null = null
const LOCAL_DRAFT_WRITE_DEBOUNCE_MS = 350

function localDraftKey(dept = activeDept.value, templateId = selectedTemplateId.value) {
  return templateId ? `dpw:event-template-draft:${dept}:${templateId}` : `dpw:event-template-draft:${dept}`
}

function newLocalDraftKey(dept = activeDept.value) {
  return `dpw:event-template-new-draft:${dept}`
}

function isLegacyTemplate(template: EventTemplate) {
  const first = templates.value.filter(t => t.department === template.department)
    .sort((a, b) => a.created_at.localeCompare(b.created_at) || a.id.localeCompare(b.id))[0]
  return first?.id === template.id
}

function buildLocalDraft(dept = activeDept.value): LocalEventTemplateDraft {
  return {
    version: 1,
    department: dept,
    templateId: selectedTemplateId.value || undefined,
    name: templateName.value,
    isDefault: templateIsDefault.value,
    savedAt: Date.now(),
    title: title.value,
    body: body.value,
  }
}

function clearLocalDraft(template = selectedTemplate.value, creating = isCreatingTemplate.value,
                         includeLegacy = template ? isLegacyTemplate(template) : false) {
  if (localDraftWriteTimer) { clearTimeout(localDraftWriteTimer); localDraftWriteTimer = null }
  try {
    if (creating) window.localStorage.removeItem(newLocalDraftKey())
    if (template) {
      window.localStorage.removeItem(localDraftKey(template.department, template.id))
      if (includeLegacy) window.localStorage.removeItem(localDraftKey(template.department, ''))
    }
  } catch (e) {
    error.value = `Lokaler Entwurf konnte nicht entfernt werden: ${formatApiError(e)}`
  }
}

function writeLocalDraftNow(): boolean {
  if (!showEditor.value || !activeDept.value || (!dirtyFields.size && !isCreatingTemplate.value)) return true
  if (isCreatingTemplate.value && !templateName.value && !title.value && !body.value) return true
  try {
    window.localStorage.setItem(
      isCreatingTemplate.value ? newLocalDraftKey() : localDraftKey(), JSON.stringify(buildLocalDraft())
    )
    return true
  } catch (e) {
    error.value = `Lokaler Entwurf konnte nicht gespeichert werden: ${formatApiError(e)}`
    return false
  }
}

function scheduleLocalDraftWrite() {
  if (!activeDept.value || suppressDirtyTracking || applyingRemote) return
  if (localDraftWriteTimer) clearTimeout(localDraftWriteTimer)
  localDraftWriteTimer = setTimeout(writeLocalDraftNow, LOCAL_DRAFT_WRITE_DEBOUNCE_MS)
}

function loadPendingLocalDraft(template: EventTemplate | null) {
  try {
    const raw = template
      ? window.localStorage.getItem(localDraftKey(template.department, template.id))
        ?? (isLegacyTemplate(template) ? window.localStorage.getItem(localDraftKey(template.department, '')) : null)
      : window.localStorage.getItem(newLocalDraftKey())
    if (!raw) { pendingLocalDraft.value = null; return }
    const d: LocalEventTemplateDraft = JSON.parse(raw)
    if (!d || d.version !== 1 || d.department !== activeDept.value ||
        (template && d.templateId && d.templateId !== template.id)) { pendingLocalDraft.value = null; return }
    if (template && d.title === template.title && d.body === template.body &&
        (d.name ?? template.name) === template.name && (d.isDefault ?? template.is_default) === template.is_default) {
      clearLocalDraft(template, false); pendingLocalDraft.value = null; return
    }
    pendingLocalDraft.value = d
  } catch (e) {
    pendingLocalDraft.value = null
    error.value = `Lokaler Entwurf konnte nicht gelesen werden: ${formatApiError(e)}`
  }
}

function restoreLocalDraft() {
  const d = pendingLocalDraft.value
  if (!d) return
  suppressDirtyTracking = true
  templateName.value = d.name ?? templateName.value
  templateIsDefault.value = d.isDefault ?? templateIsDefault.value
  title.value = d.title
  body.value = d.body
  if (editorRef.value) editorRef.value.innerHTML = d.body
  pendingLocalDraft.value = null
  localDraftRestoredAt.value = Date.now()
  suppressDirtyTracking = false
  markDirty('name', 'is_default', 'title', 'body')
  scheduleAutoSave()
}

function discardLocalDraft() {
  clearLocalDraft()
  pendingLocalDraft.value = null
  localDraftRestoredAt.value = null
}

function upsertTemplate(template: EventTemplate) {
  if (template.is_default) {
    for (const other of templates.value) {
      if (other.department === template.department && other.id !== template.id) other.is_default = false
    }
    if (selectedTemplateId.value !== template.id && template.department === activeDept.value &&
        !dirtyFields.has('is_default') && !isCreatingTemplate.value) {
      applyingRemote = true
      templateIsDefault.value = false
      applyingRemote = false
    }
  }
  const index = templates.value.findIndex(t => t.id === template.id)
  if (index >= 0) templates.value[index] = template
  else templates.value.push(template)
}

function currentChanges(): Required<EventTemplateChanges> {
  return { name: templateName.value, is_default: templateIsDefault.value, title: title.value, body: body.value }
}

function mergeTemplateIntoEditor(template: EventTemplate) {
  if (!showEditor.value || template.id !== selectedTemplateId.value || isCreatingTemplate.value) return
  applyingRemote = true
  if (!dirtyFields.has('name') && myLockedSection.value !== 'evt_tpl_name') templateName.value = template.name
  if (!dirtyFields.has('is_default') && myLockedSection.value !== 'evt_tpl_default') templateIsDefault.value = template.is_default
  if (!dirtyFields.has('title') && myLockedSection.value !== 'evt_tpl_title') title.value = template.title
  if (!dirtyFields.has('body') && myLockedSection.value !== 'evt_tpl_body') {
    body.value = template.body
    if (editorRef.value) editorRef.value.innerHTML = template.body
  }
  applyingRemote = false
}

async function persistTemplate(): Promise<boolean> {
  if (!showEditor.value || !activeDept.value) return false
  if (!templateName.value.trim()) {
    error.value = 'Name der Vorlage erforderlich.'
    return false
  }
  if (!isCreatingTemplate.value && !dirtyFields.size) return true
  const snapshot = currentChanges()
  const fields = [...dirtyFields]
  const changes: EventTemplateChanges = {}
  for (const field of fields) {
    if (field === 'is_default') changes.is_default = snapshot.is_default
    else changes[field] = snapshot[field]
  }
  writeLocalDraftNow()
  const creating = isCreatingTemplate.value
  const generation = editorGeneration
  saving.value = true
  const result = creating
    ? await createTemplate(snapshot.name.trim(), activeDept.value, snapshot.title, snapshot.body, snapshot.is_default)
    : await updateTemplate(selectedTemplateId.value, changes)
  saving.value = false
  if (generation !== editorGeneration) return false
  if (!result) return false
  upsertTemplate(result)
  if (creating) {
    clearLocalDraft(null, true)
    selectedTemplateId.value = result.id
    isCreatingTemplate.value = false
    wsJoin(tplId())
    startAutoSaveInterval()
  }
  const saved = fields.filter(field => currentChanges()[field] === snapshot[field])
  for (const field of saved) dirtyFields.delete(field)
  mergeTemplateIntoEditor(result)
  showSavedIndicator(saved)
  if (!dirtyFields.size) {
    clearLocalDraft(result, false)
    pendingLocalDraft.value = null
    localDraftRestoredAt.value = null
  } else {
    writeLocalDraftNow()
    scheduleAutoSave()
  }
  return true
}

function doSave(): Promise<boolean> {
  const generation = editorGeneration
  saveQueue = saveQueue.then(() => generation === editorGeneration ? persistTemplate() : false)
  return saveQueue
}

function scheduleAutoSave() {
  if (suppressDirtyTracking || applyingRemote) return
  scheduleLocalDraftWrite()
  if (isCreatingTemplate.value) return
  if (autosaveDebounce()) {
    if (autoSaveTimer) clearTimeout(autoSaveTimer)
    autoSaveTimer = setTimeout(() => { autoSaveTimer = null; void doSave() }, autosaveInterval())
  }
}

function startAutoSaveInterval() {
  if (!autosaveDebounce() && !autoSaveInterval) {
    autoSaveInterval = setInterval(() => {
      if (dirtyFields.size && !isCreatingTemplate.value) void doSave()
    }, autosaveInterval())
  }
}

function stopAutoSaveInterval() {
  if (autoSaveInterval) {
    clearInterval(autoSaveInterval)
    autoSaveInterval = null
  }
}

// ---- Watch fields -----------------------------------------------------------
watch(title, () => { markDirty('title'); scheduleAutoSave() }, { flush: 'sync' })
watch(templateName, () => { markDirty('name'); scheduleAutoSave() }, { flush: 'sync' })
watch(templateIsDefault, () => { markDirty('is_default'); scheduleAutoSave() }, { flush: 'sync' })
// body changes are triggered by onEditorInput which calls scheduleAutoSave directly

// ---- Load template for department -------------------------------------------
async function flushCurrentTemplate(): Promise<boolean> {
  if (autoSaveTimer) { clearTimeout(autoSaveTimer); autoSaveTimer = null }
  if (!showEditor.value) return true
  if (isCreatingTemplate.value) return writeLocalDraftNow()
  return doSave()
}

function activateTemplate(dept: Department, template: EventTemplate | null, edit: boolean) {
  editorGeneration++
  if (localDraftWriteTimer) { clearTimeout(localDraftWriteTimer); localDraftWriteTimer = null }
  if (unlockTimer) { clearTimeout(unlockTimer); unlockTimer = null }
  stopAutoSaveInterval()
  wsLeave()
  myLockedSection.value = null
  sectionLocks.value = new Map()
  activeEditors.value = []
  localDraftRestoredAt.value = null
  savedFields.value = {}
  suppressDirtyTracking = true
  activeDept.value = dept
  selectedTemplateId.value = template?.id ?? ''
  showEditor.value = edit
  isCreatingTemplate.value = edit && !template
  templateName.value = template?.name ?? ''
  templateIsDefault.value = template?.is_default ?? !departmentTemplates.value.length
  title.value = template?.title ?? ''
  body.value = template?.body ?? ''
  pendingLocalDraft.value = null
  if (edit) loadPendingLocalDraft(template)
  dirtyFields.clear()
  suppressDirtyTracking = false
  nextTick(() => { if (editorRef.value) editorRef.value.innerHTML = body.value })
  if (edit && template) {
    wsJoin(tplId())
    startAutoSaveInterval()
  }
}

async function loadDept(dept: Department) {
  if (dept === activeDept.value) return
  if (!(await flushCurrentTemplate())) return
  const available = templates.value.filter(t => t.department === dept)
    .sort((a, b) => a.name.localeCompare(b.name, 'de', { sensitivity: 'base' }) || a.id.localeCompare(b.id))
  activateTemplate(dept, available.find(t => t.is_default) ?? available[0] ?? null, showEditor.value)
}

async function openEditor(template: EventTemplate) {
  if (!(await flushCurrentTemplate())) return
  activateTemplate(template.department, template, true)
}

async function createNewTemplate() {
  if (!(await flushCurrentTemplate())) return
  activateTemplate(activeDept.value, null, true)
}

async function backToOverview() {
  if (!(await flushCurrentTemplate())) return
  wsLeave()
  myLockedSection.value = null
  sectionLocks.value = new Map()
  activeEditors.value = []
  stopAutoSaveInterval()
  showEditor.value = false
}

async function saveTemplateNow() {
  if (autoSaveTimer) { clearTimeout(autoSaveTimer); autoSaveTimer = null }
  return doSave()
}

async function toggleDefault(template: EventTemplate) {
  const updated = await updateTemplate(template.id, { is_default: !template.is_default })
  if (updated) upsertTemplate(updated)
}

async function removeTemplate(template: EventTemplate) {
  if (!window.confirm(`Vorlage „${template.name}" wirklich löschen?`)) return
  const legacy = isLegacyTemplate(template)
  if (!(await deleteTemplate(template.id))) return
  clearLocalDraft(template, false, legacy)
}

function templatePreview(template: EventTemplate) {
  return template.body.replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim()
}

// ---- WebSocket events -------------------------------------------------------
useWebSocket((e) => {
  const id = tplId()
  if (e.event === 'lock' && e.activity_id === id) {
    sectionLocks.value.set(e.section, e.user)
    sectionLocks.value = new Map(sectionLocks.value)
  } else if (e.event === 'unlock' && e.activity_id === id) {
    sectionLocks.value.delete(e.section)
    sectionLocks.value = new Map(sectionLocks.value)
  } else if (e.event === 'editors' && e.activity_id === id) {
    activeEditors.value = (e.users as string[]).filter((u: string) => u !== user.value?.display_name)
  } else if (e.event === 'locks_state' && e.activity_id === id) {
    const m = new Map<EditSection, string>()
    for (const lock of e.locks) m.set(lock.section, lock.user)
    sectionLocks.value = m
  } else if (e.event === 'event_template_updated') {
    const tpl = e.template
    upsertTemplate(tpl)
    mergeTemplateIntoEditor(tpl)
  } else if (e.event === 'event_template_deleted') {
    const deleted = templates.value.find(t => t.id === e.id)
    templates.value = templates.value.filter(t => t.id !== e.id)
    if (showEditor.value && e.id === selectedTemplateId.value) {
      editorGeneration++
      if (autoSaveTimer) { clearTimeout(autoSaveTimer); autoSaveTimer = null }
      stopAutoSaveInterval()
      writeLocalDraftNow()
      wsLeave()
      myLockedSection.value = null
      sectionLocks.value = new Map()
      activeEditors.value = []
      selectedTemplateId.value = ''
      isCreatingTemplate.value = true
      applyingRemote = true
      templateIsDefault.value = false
      applyingRemote = false
      pendingLocalDraft.value = null
      writeLocalDraftNow()
      if (deleted) error.value = 'Diese Vorlage wurde gelöscht. Dein Inhalt bleibt erhalten und kann als neue Vorlage gespeichert werden.'
    }
  }
})

// ---- Rich text helpers ------------------------------------------------------
function onEditorInput() {
  if (editorRef.value) body.value = editorRef.value.innerHTML
  updateToolbarState()
  markDirty('body')
  scheduleAutoSave()
}

function updateToolbarState() {
  const sel = window.getSelection()
  if (!sel || !sel.rangeCount || !editorRef.value) return
  const node = sel.anchorNode?.nodeType === 3 ? sel.anchorNode.parentElement : sel.anchorNode as HTMLElement
  if (!node || !editorRef.value.contains(node)) return
  const cs = window.getComputedStyle(node)
  curFont.value = cs.fontFamily.replace(/["']/g, '').split(',')[0].trim() || 'Arial'
  curSize.value = parseInt(cs.fontSize).toString() || '12'
  curColor.value = rgbToHex(cs.color) || '#000000'
  const bgRaw = cs.backgroundColor
  curBgColor.value = (bgRaw === 'rgba(0, 0, 0, 0)' || bgRaw === 'transparent') ? '#ffffff' : (rgbToHex(bgRaw) || '#ffffff')
  curBold.value = document.queryCommandState('bold')
  curItalic.value = document.queryCommandState('italic')
  curUnderline.value = document.queryCommandState('underline')
  curUl.value = document.queryCommandState('insertUnorderedList')
  curOl.value = document.queryCommandState('insertOrderedList')
}

function rgbToHex(rgb: string): string {
  const m = rgb.match(/^rgb\((\d+),\s*(\d+),\s*(\d+)\)$/)
  if (!m) return rgb
  return '#' + [m[1], m[2], m[3]].map(x => parseInt(x).toString(16).padStart(2, '0')).join('')
}

function saveEditorSelection() {
  const sel = window.getSelection()
  if (sel?.rangeCount && editorRef.value?.contains(sel.anchorNode)) {
    savedSelection = sel.getRangeAt(0).cloneRange()
  }
}

function execCmd(cmd: string, value?: string) {
  if (savedSelection) {
    const sel = window.getSelection()
    if (sel) { sel.removeAllRanges(); sel.addRange(savedSelection) }
    savedSelection = null
  }
  document.execCommand(cmd, false, value)
  editorRef.value?.focus()
  onEditorInput()
}

function setFontSize(size: string) {
  if (savedSelection) {
    const sel = window.getSelection()
    if (sel) { sel.removeAllRanges(); sel.addRange(savedSelection) }
    savedSelection = null
  }
  const sel = window.getSelection()
  if (sel && sel.rangeCount && sel.getRangeAt(0).collapsed) {
    const span = document.createElement('span')
    span.style.fontSize = size + 'px'
    span.textContent = '\u200B'
    const range = sel.getRangeAt(0)
    range.insertNode(span)
    const newRange = document.createRange()
    newRange.setStart(span.firstChild!, 1)
    newRange.collapse(true)
    sel.removeAllRanges()
    sel.addRange(newRange)
    editorRef.value?.focus()
    curSize.value = size
    onEditorInput()
    return
  }
  document.execCommand('fontSize', false, '7')
  const fontEls = editorRef.value?.querySelectorAll('font[size="7"]')
  fontEls?.forEach(el => {
    const span = document.createElement('span')
    span.style.fontSize = size + 'px'
    span.innerHTML = el.innerHTML
    el.replaceWith(span)
  })
  editorRef.value?.focus()
  onEditorInput()
}

function openLinkDialog() {
  linkSavedRange = savedSelection
  savedSelection = null
  const sel = window.getSelection()
  const anchor = sel?.focusNode?.parentElement?.closest('a') as HTMLAnchorElement | null
  linkUrl.value = anchor?.getAttribute('href') ?? ''
  showLinkDialog.value = true
  nextTick(() => linkInputRef.value?.focus())
}

function confirmLink() {
  showLinkDialog.value = false
  if (!linkSavedRange) return
  const sel = window.getSelection()
  if (sel) { sel.removeAllRanges(); sel.addRange(linkSavedRange) }
  if (linkUrl.value.trim()) {
    document.execCommand('createLink', false, linkUrl.value.trim())
  } else {
    document.execCommand('unlink')
  }
  editorRef.value?.focus()
  linkSavedRange = null
}

function cancelLink() {
  showLinkDialog.value = false
  linkSavedRange = null
}

function adjustFontSize(delta: number) {
  const current = parseInt(curSize.value) || 12
  const newSize = Math.max(8, Math.min(72, current + delta))
  setFontSize(String(newSize))
  curSize.value = String(newSize)
}

// ---- Lifecycle --------------------------------------------------------------
onMounted(async () => {
  await Promise.all([fetchMyPermissions(), fetchDepartments(), fetchTemplates()])
  if (!myPermissions.value || myPermissions.value.event_templates_scope === 'none') {
    router.replace('/')
    return
  }

  const firstVisibleDepartment = visibleDepartments.value[0]
  if (!visibleDepartments.value.includes(activeDept.value)) activeDept.value = firstVisibleDepartment ?? '' as Department
})

onUnmounted(() => {
  editorGeneration++
  if (autoSaveTimer) clearTimeout(autoSaveTimer)
  if (savedTimer) clearTimeout(savedTimer)
  if (localDraftWriteTimer) clearTimeout(localDraftWriteTimer)
  if (unlockTimer) clearTimeout(unlockTimer)
  writeLocalDraftNow()
  stopAutoSaveInterval()
  if (myLockedSection.value) {
    wsSend({ type: 'unlock', activity_id: tplId(), section: myLockedSection.value })
  }
  if (activeDept.value) wsLeave()
})
</script>

<template>
  <header class="header">
    <h1>Event-Vorlagen</h1>
  </header>

  <main class="main">
    <p v-if="loading" class="loading">Laden...</p>

    <template v-else>
    <div class="filter-tabs template-department-tabs">
      <button
        v-for="dept in visibleDepartments"
        :key="dept"
        class="filter-tab filter-tab--badge"
        @click="loadDept(dept)"
      >
        <DepartmentBadge :department="dept" :active="activeDept === dept" />
      </button>
    </div>

    <template v-if="showEditor">
      <div class="template-editor-header">
        <button type="button" class="btn-secondary template-editor-back" @click="backToOverview">← Zurück zu Vorlagen</button>
        <h2 class="template-overview-title">{{ isCreatingTemplate ? 'Neue Vorlage' : 'Vorlage bearbeiten' }}</h2>
        <button type="button" class="btn-primary" :disabled="saving" @click="saveTemplateNow">
          <Save :size="16" aria-hidden="true" /> {{ saving ? 'Speichern…' : 'Speichern' }}
        </button>
      </div>

      <div class="default-toggle-section event-template-default lock-wrapper"
        :class="{ 'is-locked': isLockedByOther('evt_tpl_default') }"
        @focusin="lockSection('evt_tpl_default')" @focusout="unlockSection('evt_tpl_default', $event)">
        <div v-if="lockedBy('evt_tpl_default')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('evt_tpl_default') }}</div>
        <label class="checkbox-label">
          <input v-model="templateIsDefault" type="checkbox" :disabled="isLockedByOther('evt_tpl_default')" />
          <span>Als Standard-Vorlage für diese Stufe setzen</span>
        </label>
        <p class="default-hint">Die Standard-Vorlage wird beim Erstellen eines Events automatisch ausgewählt.</p>
      </div>

      <!-- Local-draft recovery banner -->
      <div v-if="pendingLocalDraft" class="editors-banner" style="gap: 10px; flex-wrap: wrap;">
        <span class="editors-banner-icon"><Save :size="14" aria-hidden="true" /></span>
        <span>Ungespeicherter Entwurf gefunden ({{ new Date(pendingLocalDraft.savedAt).toLocaleString('de-DE') }}).</span>
        <button type="button" class="btn-secondary" @click="restoreLocalDraft">Wiederherstellen</button>
        <button type="button" class="btn-secondary" @click="discardLocalDraft">Verwerfen</button>
      </div>
      <div v-else-if="localDraftRestoredAt" class="editors-banner">
        <span class="editors-banner-icon"><CheckCircle2 :size="14" aria-hidden="true" /></span>
        <span>Lokaler Entwurf wurde wiederhergestellt.</span>
      </div>

      <!-- Active editors indicator -->
      <div v-if="activeEditors.length" class="editors-banner">
        <span class="editors-banner-icon"><Users :size="14" aria-hidden="true" /></span>
        <span>{{ activeEditors.join(', ') }} {{ activeEditors.length === 1 ? 'bearbeitet' : 'bearbeiten' }} ebenfalls</span>
      </div>

      <div class="template-editor-card event-template-title-card">
      <div class="form-group lock-wrapper" :class="{ 'is-locked': isLockedByOther('evt_tpl_name') }"
        @focusin="lockSection('evt_tpl_name')" @focusout="unlockSection('evt_tpl_name', $event)">
        <div v-if="lockedBy('evt_tpl_name')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('evt_tpl_name') }}</div>
        <label class="template-editor-field-label" for="event-template-name">Name der Vorlage</label>
        <input id="event-template-name" v-model="templateName" class="template-editor-field-input"
          placeholder="z. B. Standard-Event Pfadi" :disabled="isLockedByOther('evt_tpl_name')" />
      </div>
      <div class="form-group lock-wrapper" :class="{ 'is-locked': isLockedByOther('evt_tpl_title' as EditSection) }"
        @focusin="lockSection('evt_tpl_title' as EditSection)" @focusout="unlockSection('evt_tpl_title' as EditSection, $event)">
        <div v-if="lockedBy('evt_tpl_title' as EditSection)" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('evt_tpl_title' as EditSection) }}</div>
        <label class="template-editor-field-label" for="event-template-title">Titel-Vorlage</label>
        <div class="input-save-wrap">
          <input
            id="event-template-title"
            v-model="title"
            type="text"
            placeholder="z.B. {{abteilung}}: {{titel}}"
            class="template-editor-field-input"
            :disabled="isLockedByOther('evt_tpl_title' as EditSection)"
          />
          <span v-if="savedFields['title']" class="field-saved-icon" :key="'wrap-' + savedFields['title']"><Save :size="12" aria-hidden="true" /></span>
        </div>
      </div>
      </div>

      <div class="detail-form">

      <!-- Body (rich text editor) -->
      <div class="form-group lock-wrapper" :class="{ 'is-locked': isLockedByOther('evt_tpl_body' as EditSection) }"
        @focusin="lockSection('evt_tpl_body' as EditSection)" @focusout="unlockSection('evt_tpl_body' as EditSection, $event)">
        <div v-if="lockedBy('evt_tpl_body' as EditSection)" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('evt_tpl_body' as EditSection) }}</div>
        <label>Inhalt-Vorlage
          <span v-if="savedFields['body']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['body']"><Save :size="12" aria-hidden="true" /></span>
        </label>
        <div class="input-save-wrap">
          <span v-if="savedFields['body']" class="field-saved-icon field-saved-icon--textarea" :key="'wrap-' + savedFields['body']"><Save :size="12" aria-hidden="true" /></span>
        </div>
        <div class="rich-editor-toolbar" v-if="!isLockedByOther('evt_tpl_body' as EditSection)">
          <select class="toolbar-select" :value="curFont" @change="execCmd('fontName', ($event.target as HTMLSelectElement).value)" title="Schriftart">
            <option value="" disabled>Schriftart</option>
            <option value="Arial" style="font-family:Arial">Arial</option>
            <option value="Helvetica" style="font-family:Helvetica">Helvetica</option>
            <option value="Georgia" style="font-family:Georgia">Georgia</option>
            <option value="Times New Roman" style="font-family:'Times New Roman'">Times New Roman</option>
            <option value="Courier New" style="font-family:'Courier New'">Courier New</option>
            <option value="Verdana" style="font-family:Verdana">Verdana</option>
            <option value="Trebuchet MS" style="font-family:'Trebuchet MS'">Trebuchet MS</option>
          </select>
          <input type="text" class="toolbar-select toolbar-select--narrow" :value="curSize" @mousedown="saveEditorSelection" @change="setFontSize(($event.target as HTMLInputElement).value)" @keydown.enter.prevent="setFontSize(($event.target as HTMLInputElement).value)" title="Schriftgrösse" />
          <button type="button" class="toolbar-btn-sm" @mousedown.prevent @click="adjustFontSize(-2)" title="Kleiner">A−</button>
          <button type="button" class="toolbar-btn-sm" @mousedown.prevent @click="adjustFontSize(2)" title="Grösser">A+</button>
          <span class="toolbar-sep"></span>
          <button type="button" :class="{'toolbar-btn--active': curBold}" @mousedown.prevent @click="execCmd('bold')" title="Fett"><b>B</b></button>
          <button type="button" :class="{'toolbar-btn--active': curItalic}" @mousedown.prevent @click="execCmd('italic')" title="Kursiv"><i>I</i></button>
          <button type="button" :class="{'toolbar-btn--active': curUnderline}" @mousedown.prevent @click="execCmd('underline')" title="Unterstrichen"><u>U</u></button>
          <span class="toolbar-sep"></span>
          <label class="toolbar-color" title="Schriftfarbe" @mousedown="saveEditorSelection">
            A
            <input type="color" :value="curColor" @input="execCmd('foreColor', ($event.target as HTMLInputElement).value)" />
          </label>
          <label class="toolbar-color toolbar-color--bg" title="Hintergrundfarbe" @mousedown="saveEditorSelection">
            <span class="toolbar-color-icon">A</span>
            <input type="color" :value="curBgColor" @input="execCmd('hiliteColor', ($event.target as HTMLInputElement).value)" />
          </label>
          <span class="toolbar-sep"></span>
          <button type="button" :class="{'toolbar-btn--active': curUl}" @mousedown.prevent @click="execCmd('insertUnorderedList')" title="Aufzählung">• Liste</button>
          <button type="button" :class="{'toolbar-btn--active': curOl}" @mousedown.prevent @click="execCmd('insertOrderedList')" title="Nummerierte Liste">1. Liste</button>
          <span class="toolbar-sep"></span>
          <button type="button" @mousedown.prevent @click="execCmd('removeFormat')" title="Formatierung entfernen">✕ Format</button>
          <span class="toolbar-sep"></span>
          <button type="button" @mousedown="saveEditorSelection" @click="openLinkDialog" title="Link einfügen">🔗 Link</button>
        </div>
        <div
          ref="editorRef"
          class="rich-editor"
          :contenteditable="!isLockedByOther('evt_tpl_body' as EditSection)"
          @input="onEditorInput"
          @mouseup="updateToolbarState"
          @keyup="updateToolbarState"
          data-placeholder="Event-Inhalt…"
        ></div>
      </div>

      <!-- Variable reference -->
				<TemplateVarsDropdown hint="Variablen können im Titel und im Inhalt verwendet werden. Im Titel werden Links als URL eingefügt." :extra-vars="[
				{ var: '{{formular_link}}', desc: 'Link zum Anmeldeformular (klickbar, Text: «Zum Formular»)' },
				{ var: '{{formular_link|Anmelden}}', desc: 'Link zum Formular mit eigenem Linktext' },
			]" />

      <ErrorAlert :error="error" />
      </div>
    </template>

    <template v-else>
      <div class="template-overview-header">
        <h2 class="template-overview-title">Vorlagen für {{ activeDept || '—' }}</h2>
        <button v-if="activeDept" type="button" class="btn-primary template-overview-create" @click="createNewTemplate">+ Neue Vorlage</button>
      </div>

      <div v-if="activeDept && !departmentTemplates.length" class="empty-state">
        Noch keine Vorlagen in dieser Stufe.
      </div>
      <ErrorAlert :error="error" />

      <div
        v-for="template in departmentTemplates"
        :key="template.id"
        class="template-overview-row event-template-overview-row"
      >
        <div class="event-template-overview-main">
          <div class="event-template-overview-name">
            <span>{{ template.name }}</span>
            <span v-if="template.is_default" class="default-badge">Standard</span>
          </div>
          <div class="event-template-overview-meta">
            {{ template.title || 'Kein Titel' }}
          </div>
          <div v-if="templatePreview(template)" class="event-template-overview-meta">{{ templatePreview(template) }}</div>
        </div>
        <div class="template-overview-row-actions">
          <button type="button" :class="['btn-sm', 'template-overview-row-action', template.is_default ? 'active-default' : '']"
            :title="template.is_default ? 'Standard entfernen' : 'Als Standard setzen'"
            @click="toggleDefault(template)">{{ template.is_default ? '★' : '☆' }} Standard</button>
          <button type="button" class="btn-sm template-overview-row-action" @click="openEditor(template)">Bearbeiten</button>
          <button type="button" class="btn-sm btn-sm--danger template-overview-row-action" @click="removeTemplate(template)">Löschen</button>
        </div>
      </div>
    </template>
    </template>

    <div v-if="showLinkDialog" class="modal-backdrop" @click.self="cancelLink">
      <div class="modal link-modal">
        <h2 class="modal-title">Link einfügen</h2>
        <input
          ref="linkInputRef"
          v-model="linkUrl"
          type="url"
          class="link-modal-input"
          placeholder="https://"
          @keydown.enter.prevent="confirmLink"
          @keydown.escape.prevent="cancelLink"
        />
        <div class="modal-actions">
          <button class="btn-cancel" @mousedown.prevent @click="cancelLink">Abbrechen</button>
          <button v-if="linkUrl" class="btn-secondary" @mousedown.prevent @click="() => { linkUrl = ''; confirmLink() }">Link entfernen</button>
          <button class="btn-primary" @mousedown.prevent @click="confirmLink">{{ linkUrl ? 'Einfügen' : 'OK' }}</button>
        </div>
      </div>
    </div>
  </main>
</template>

<style scoped>
.active-default {
  background: #fef3c7 !important;
  border-color: #f59e0b !important;
  color: #92400e !important;
}

.event-template-default {
  margin-bottom: 1rem;
  padding: 0.75rem 1rem;
  border: 1px solid var(--border);
  border-radius: 0.5rem;
  background: var(--bg-elevated);
}

.event-template-default .default-hint {
  margin: 0.25rem 0 0 1.5rem;
  color: var(--text-subtle);
  font-size: 0.75rem;
}

.event-template-title-card {
  margin-bottom: 1.25rem;
}

.event-template-title-card .form-group {
  margin: 0;
}

.event-template-title-card .form-group + .form-group {
  margin-top: 1rem;
}

.event-template-overview-main {
  flex: 1;
  min-width: 0;
}

.event-template-overview-name {
  display: flex;
  align-items: center;
  gap: 0.6rem;
  color: var(--text-primary);
  font-size: 0.95rem;
  font-weight: 600;
}

.event-template-overview-meta {
  margin-top: 0.25rem;
  color: var(--text-muted);
  font-size: 0.78rem;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

@media (max-width: 599px) {
  .event-template-default {
    padding: 0.7rem 0.75rem;
  }

  .event-template-default .default-hint {
    margin-left: 1.35rem;
    font-size: 0.72rem;
  }

  .event-template-overview-name {
    flex-wrap: wrap;
    overflow-wrap: anywhere;
  }
}
</style>
