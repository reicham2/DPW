<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { useEventTemplates } from '../composables/useEventTemplates'
import { user } from '../composables/useAuth'
import { usePermissions } from '../composables/usePermissions'
import { wsSend, wsJoin, wsLeave, useWebSocket } from '../composables/useWebSocket'
import DepartmentBadge from '../components/DepartmentBadge.vue'
import TemplateVarsDropdown from '../components/TemplateVarsDropdown.vue'
import ErrorAlert from '../components/ErrorAlert.vue'
import type { Department, EditSection, EventTemplate } from '../types'
import { config } from '../config'

const router = useRouter()
const { departments: deptRecords, fetchDepartments, myPermissions, fetchMyPermissions } = usePermissions()
const ALL_DEPARTMENTS = computed<Department[]>(() => deptRecords.value.map(d => d.name))
const { fetchTemplates, saveTemplate, templates, loading, error } = useEventTemplates()

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
  if (suppressDirtyTracking || applyingRemote) return
  scheduleLocalDraftWrite()
  if (AUTOSAVE_DEBOUNCE) {
    if (autoSaveTimer) clearTimeout(autoSaveTimer)
    autoSaveTimer = setTimeout(doSave, AUTOSAVE_INTERVAL)
  } else {
    hasPendingChanges = true
  }
}

function startAutoSaveInterval() {
  if (!AUTOSAVE_DEBOUNCE && !autoSaveInterval) {
    autoSaveInterval = setInterval(() => {
      if (hasPendingChanges) {
        hasPendingChanges = false
        doSave()
      }
    }, AUTOSAVE_INTERVAL)
  }
}

function stopAutoSaveInterval() {
  if (autoSaveInterval) {
    clearInterval(autoSaveInterval)
    autoSaveInterval = null
  }
  hasPendingChanges = false
}

// ---- Watch fields -----------------------------------------------------------
watch(title, () => { markDirty('title'); scheduleAutoSave() })
// body changes are triggered by onEditorInput which calls scheduleAutoSave directly

// ---- Load template for department -------------------------------------------
function loadDept(dept: Department) {
  wsLeave()
  myLockedSection.value = null
  sectionLocks.value = new Map()
  activeEditors.value = []
  localDraftRestoredAt.value = null

  suppressDirtyTracking = true
  activeDept.value = dept
  const tpl = templates.value.find(t => t.department === dept)
  title.value = tpl?.title ?? ''
  body.value = tpl?.body ?? ''
  loadPendingLocalDraft(dept)

  if (editorRef.value) editorRef.value.innerHTML = body.value
  suppressDirtyTracking = false
  dirtyFields.clear()
  hasPendingChanges = false

  wsJoin(tplId())
}

// ---- WebSocket events -------------------------------------------------------
useWebSocket((e: any) => {
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
    if (e.locks && typeof e.locks === 'object') {
      for (const [k, v] of Object.entries(e.locks)) m.set(k as EditSection, v as string)
    }
    sectionLocks.value = m
  } else if (e.event === 'event_template_updated') {
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
        if (editorRef.value) editorRef.value.innerHTML = tpl.body
      }
      applyingRemote = false
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

  if (dept) wsJoin(tplId())
  startAutoSaveInterval()
})

onUnmounted(() => {
  if (autoSaveTimer) clearTimeout(autoSaveTimer)
  if (savedTimer) clearTimeout(savedTimer)
  if (localDraftWriteTimer) clearTimeout(localDraftWriteTimer)
  stopAutoSaveInterval()
  writeLocalDraftNow()
  if (hasPendingChanges && activeDept.value) doSave()
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
    <!-- Department tabs -->
    <div class="filter-tabs" style="margin-bottom: 24px">
      <button
        v-for="dept in visibleDepartments"
        :key="dept"
        class="filter-tab filter-tab--badge"
        @click="loadDept(dept)"
      >
        <DepartmentBadge :department="dept" :active="activeDept === dept" />
      </button>
    </div>

    <p v-if="loading" class="loading">Laden...</p>

    <div v-else class="detail-form">
      <!-- Local-draft recovery banner -->
      <div v-if="pendingLocalDraft" class="editors-banner" style="gap: 10px; flex-wrap: wrap;">
        <span class="editors-banner-icon">💾</span>
        <span>Ungespeicherter Entwurf gefunden ({{ new Date(pendingLocalDraft.savedAt).toLocaleString('de-DE') }}).</span>
        <button type="button" class="btn-secondary" @click="restoreLocalDraft">Wiederherstellen</button>
        <button type="button" class="btn-secondary" @click="discardLocalDraft">Verwerfen</button>
      </div>
      <div v-else-if="localDraftRestoredAt" class="editors-banner">
        <span class="editors-banner-icon">✅</span>
        <span>Lokaler Entwurf wurde wiederhergestellt.</span>
      </div>

      <!-- Active editors indicator -->
      <div v-if="activeEditors.length" class="editors-banner">
        <span class="editors-banner-icon">👥</span>
        <span>{{ activeEditors.join(', ') }} {{ activeEditors.length === 1 ? 'bearbeitet' : 'bearbeiten' }} ebenfalls</span>
      </div>

      <!-- Title -->
      <div class="form-group lock-wrapper" :class="{ 'is-locked': isLockedByOther('evt_tpl_title' as EditSection) }"
        @focusin="lockSection('evt_tpl_title' as EditSection)" @focusout="unlockSection('evt_tpl_title' as EditSection, $event)">
        <div v-if="lockedBy('evt_tpl_title' as EditSection)" class="lock-badge">🔒 {{ lockedBy('evt_tpl_title' as EditSection) }}</div>
        <label>Titel-Vorlage</label>
        <div class="input-save-wrap">
          <input
            v-model="title"
            type="text"
            placeholder="z.B. {{abteilung}}: {{titel}}"
            :disabled="isLockedByOther('evt_tpl_title' as EditSection)"
          />
          <span v-if="savedFields['title']" class="field-saved-icon" :key="'wrap-' + savedFields['title']">💾</span>
        </div>
      </div>

      <!-- Body (rich text editor) -->
      <div class="form-group lock-wrapper" :class="{ 'is-locked': isLockedByOther('evt_tpl_body' as EditSection) }"
        @focusin="lockSection('evt_tpl_body' as EditSection)" @focusout="unlockSection('evt_tpl_body' as EditSection, $event)">
        <div v-if="lockedBy('evt_tpl_body' as EditSection)" class="lock-badge">🔒 {{ lockedBy('evt_tpl_body' as EditSection) }}</div>
        <label>Inhalt-Vorlage
          <span v-if="savedFields['body']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['body']">💾</span>
        </label>
        <div class="input-save-wrap">
          <span v-if="savedFields['body']" class="field-saved-icon field-saved-icon--textarea" :key="'wrap-' + savedFields['body']">💾</span>
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
				<TemplateVarsDropdown />

      <ErrorAlert :error="error" />
    </div>
  </main>
</template>
