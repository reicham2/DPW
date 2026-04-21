<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import { useRouter } from 'vue-router'
import { useMailTemplates } from '../composables/useMailTemplates'
import { useContactSearch } from '../composables/useContactSearch'
import { user } from '../composables/useAuth'
import { usePermissions } from '../composables/usePermissions'
import { wsSend, wsRegister, wsJoin, wsLeave, useWebSocket } from '../composables/useWebSocket'
import DepartmentBadge from '../components/DepartmentBadge.vue'
import TemplateVarsDropdown from '../components/TemplateVarsDropdown.vue'
import type { Department, EditSection } from '../types'

const { departments: deptRecords, fetchDepartments, myPermissions, fetchMyPermissions } = usePermissions()

const ALL_DEPARTMENTS = computed<Department[]>(() => deptRecords.value.map(d => d.name))

const MAIL_EXTRA_VARS = [
  { var: '{{absender_email}}',   desc: 'E-Mail-Adresse des Absenders' },
  { var: '{{absender_name}}',    desc: 'Voller Name des Absenders (z.B. Leandro Klaus v/o Topo)' },
  { var: '{{formular_link}}',    desc: 'Link zum Formular der Aktivität (als klickbarer Link)' },
]

const { fetchTemplates, saveTemplate, templates, loading, error } = useMailTemplates()
import ErrorAlert from '../components/ErrorAlert.vue'

const canEditAll = computed(() => myPermissions.value?.mail_templates_scope === 'all')
const canEditOwnDept = computed(() => {
  const scope = myPermissions.value?.mail_templates_scope
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

const subject = ref('')
const body = ref('')
const recipients = ref<string[]>([''])
const cc = ref<string[]>([''])
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

function tplId() { return `tpl_${activeDept.value}` }

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
import { config } from '../config'
const AUTOSAVE_INTERVAL = config.AUTOSAVE_INTERVAL
const AUTOSAVE_DEBOUNCE = config.AUTOSAVE_DEBOUNCE

let autoSaveTimer: ReturnType<typeof setTimeout> | null = null
let autoSaveInterval: ReturnType<typeof setInterval> | null = null
let hasPendingChanges = false

interface LocalMailTemplateDraft {
  version: 1;
  department: Department;
  savedAt: number;
  subject: string;
  body: string;
  recipients: string[];
  cc: string[];
}

const pendingLocalDraft = ref<LocalMailTemplateDraft | null>(null)
const localDraftRestoredAt = ref<number | null>(null)
let localDraftWriteTimer: ReturnType<typeof setTimeout> | null = null
const LOCAL_DRAFT_WRITE_DEBOUNCE_MS = 350

function localDraftKey(dept = activeDept.value) {
  return `dpw:mail-template-draft:${dept}`
}

function buildLocalDraft(dept = activeDept.value): LocalMailTemplateDraft {
  return {
    version: 1,
    department: dept,
    savedAt: Date.now(),
    subject: subject.value,
    body: body.value,
    recipients: recipients.value.map(r => r.trim()).filter(Boolean),
    cc: cc.value.map(r => r.trim()).filter(Boolean),
  }
}

function clearLocalDraft(dept = activeDept.value) {
  if (localDraftWriteTimer) {
    clearTimeout(localDraftWriteTimer)
    localDraftWriteTimer = null
  }
  try {
    window.localStorage.removeItem(localDraftKey(dept))
  } catch {
    /* ignore unavailable storage */
  }
}

function writeLocalDraftNow() {
  if (!activeDept.value) return
  try {
    window.localStorage.setItem(localDraftKey(), JSON.stringify(buildLocalDraft()))
  } catch {
    /* ignore unavailable storage */
  }
}

function scheduleLocalDraftWrite() {
  if (!activeDept.value || suppressDirtyTracking || applyingRemote) return
  if (localDraftWriteTimer) clearTimeout(localDraftWriteTimer)
  localDraftWriteTimer = setTimeout(() => {
    writeLocalDraftNow()
  }, LOCAL_DRAFT_WRITE_DEBOUNCE_MS)
}

function loadPendingLocalDraft(dept: Department) {
  try {
    const raw = window.localStorage.getItem(localDraftKey(dept))
    if (!raw) {
      pendingLocalDraft.value = null
      return
    }
    const parsed = JSON.parse(raw) as Partial<LocalMailTemplateDraft>
    if (parsed.version !== 1 || parsed.department !== dept) {
      pendingLocalDraft.value = null
      return
    }
    const draft = parsed as LocalMailTemplateDraft
    const currentSnapshot = JSON.stringify({
      subject: subject.value,
      body: body.value,
      recipients: recipients.value.map(r => r.trim()).filter(Boolean),
      cc: cc.value.map(r => r.trim()).filter(Boolean),
    })
    const draftSnapshot = JSON.stringify({
      subject: draft.subject,
      body: draft.body,
      recipients: draft.recipients,
      cc: draft.cc,
    })
    if (currentSnapshot === draftSnapshot) {
      clearLocalDraft(dept)
      pendingLocalDraft.value = null
      return
    }
    pendingLocalDraft.value = draft
  } catch {
    pendingLocalDraft.value = null
  }
}

function applyLocalDraft() {
  if (!pendingLocalDraft.value) return
  suppressDirtyTracking = true
  subject.value = pendingLocalDraft.value.subject
  body.value = pendingLocalDraft.value.body
  recipients.value = pendingLocalDraft.value.recipients.length ? [...pendingLocalDraft.value.recipients] : ['']
  cc.value = pendingLocalDraft.value.cc.length ? [...pendingLocalDraft.value.cc] : ['']
  nextTick(() => {
    if (editorRef.value) editorRef.value.innerHTML = body.value
    suppressDirtyTracking = false
    pendingLocalDraft.value = null
    localDraftRestoredAt.value = Date.now()
    scheduleLocalDraftWrite()
    scheduleAutoSave()
  })
}

function discardLocalDraft() {
  clearLocalDraft()
  pendingLocalDraft.value = null
  localDraftRestoredAt.value = null
}

function scheduleAutoSave() {
  if (suppressDirtyTracking) return
  if (applyingRemote) return
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

async function doSave() {
  error.value = null
  writeLocalDraftNow()
  const validRecipients = recipients.value.map(r => r.trim()).filter(Boolean)
  const validCc = cc.value.map(r => r.trim()).filter(Boolean)
  const result = await saveTemplate(activeDept.value, subject.value, body.value, validRecipients, validCc)
  if (result) {
    showSavedIndicator()
    clearLocalDraft()
    pendingLocalDraft.value = null
    localDraftRestoredAt.value = null
    const idx = templates.value.findIndex(t => t.department === activeDept.value)
    if (idx >= 0) templates.value[idx] = result
    else templates.value.push(result)
  }
}

// Watchers for auto-save + dirty tracking
watch([subject], () => { markDirty('subject'); scheduleAutoSave() })
watch([recipients], () => { markDirty('recipients'); scheduleAutoSave() }, { deep: true })
watch([cc], () => { markDirty('cc'); scheduleAutoSave() }, { deep: true })
// body changes are triggered by onEditorInput which calls scheduleAutoSave directly

// ---- WS: live sync of template updates from other editors ------------------
useWebSocket((e) => {
  const id = tplId()
  if (e.event === 'lock' && e.activity_id === id) {
    sectionLocks.value.set(e.section, e.user)
    sectionLocks.value = new Map(sectionLocks.value)
  } else if (e.event === 'unlock' && e.activity_id === id) {
    sectionLocks.value.delete(e.section)
    sectionLocks.value = new Map(sectionLocks.value)
  } else if (e.event === 'editors' && e.activity_id === id) {
    activeEditors.value = e.users.filter((u: string) => u !== user.value?.display_name)
  } else if (e.event === 'locks_state' && e.activity_id === id) {
    const m = new Map<EditSection, string>()
    for (const l of e.locks) {
      m.set(l.section, l.user)
    }
    sectionLocks.value = m
  } else if (e.event === 'template_updated') {
    const tpl = e.template
    if (tpl.department !== activeDept.value) return
    // Update local templates cache
    const idx = templates.value.findIndex(t => t.department === tpl.department)
    if (idx >= 0) templates.value[idx] = tpl
    else templates.value.push(tpl)
    // Merge remote changes into edit fields
    applyingRemote = true
    if (tpl.subject !== subject.value && !myLockedSection.value?.startsWith('tpl_subject')) {
      subject.value = tpl.subject
    }
    if (JSON.stringify(tpl.recipients) !== JSON.stringify(recipients.value.map(r => r.trim()).filter(Boolean))
        && !myLockedSection.value?.startsWith('tpl_recipients')) {
      recipients.value = tpl.recipients.length ? [...tpl.recipients] : ['']
    }
    const remoteCc = tpl.cc ?? []
    if (JSON.stringify(remoteCc) !== JSON.stringify(cc.value.map(r => r.trim()).filter(Boolean))
        && !myLockedSection.value?.startsWith('tpl_cc')) {
      cc.value = remoteCc.length ? [...remoteCc] : ['']
    }
    if (tpl.body !== body.value && !myLockedSection.value?.startsWith('tpl_body')) {
      body.value = tpl.body
      nextTick(() => {
        if (editorRef.value) editorRef.value.innerHTML = body.value
      })
    }
    nextTick(() => { applyingRemote = false })
  }
})

// ---- Load + mount -----------------------------------------------------------
const router = useRouter()
onMounted(async () => {
  await Promise.all([fetchDepartments(), fetchTemplates(), fetchMyPermissions()])
  // No access: redirect
  if (!myPermissions.value || myPermissions.value.mail_templates_scope === 'none') {
    router.replace('/')
    return
  }
  // Set activeDept to first visible department if current is not visible
  const visDepts = visibleDepartments.value
  if (visDepts.length && !visDepts.includes(activeDept.value)) {
    activeDept.value = visDepts[0]
  }
  loadDept(activeDept.value)
  if (user.value) {
    wsRegister(user.value.display_name, user.value.microsoft_oid)
    wsJoin(tplId())
  }
  startAutoSaveInterval()
})

onUnmounted(() => {
  if (localDraftWriteTimer) {
    clearTimeout(localDraftWriteTimer)
    localDraftWriteTimer = null
  }
  writeLocalDraftNow()
  if (autoSaveTimer) clearTimeout(autoSaveTimer)
  if (savedTimer) clearTimeout(savedTimer)
  stopAutoSaveInterval()
  wsLeave()
})

function loadDept(dept: Department) {
  // Leave previous template room
  wsLeave()
  myLockedSection.value = null
  sectionLocks.value = new Map()
  activeEditors.value = []
  localDraftRestoredAt.value = null

  suppressDirtyTracking = true
  activeDept.value = dept
  const tpl = templates.value.find(t => t.department === dept)
  subject.value = tpl?.subject ?? ''
  body.value    = tpl?.body ?? ''
  recipients.value = tpl?.recipients?.length ? [...tpl.recipients] : ['']
  cc.value = tpl?.cc?.length ? [...tpl.cc] : ['']
  loadPendingLocalDraft(dept)
  nextTick(() => {
    if (editorRef.value) editorRef.value.innerHTML = body.value
    suppressDirtyTracking = false
    dirtyFields.clear()
  })

  // Join new template room
  wsJoin(tplId())
}

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

function expandSelectionToVariables() {
  const sel = window.getSelection()
  if (!sel || !sel.rangeCount || !editorRef.value) return
  const range = sel.getRangeAt(0)
  if (range.collapsed) return
  const container = editorRef.value
  const fullText = container.textContent ?? ''
  function nodeOffset(node: Node, off: number): number {
    const walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT)
    let pos = 0
    while (walker.nextNode()) {
      if (walker.currentNode === node) return pos + off
      pos += (walker.currentNode as Text).length
    }
    return pos
  }
  function offsetToNodePos(target: number): { node: Text; offset: number } | null {
    const walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT)
    let pos = 0
    while (walker.nextNode()) {
      const t = walker.currentNode as Text
      if (pos + t.length >= target) return { node: t, offset: target - pos }
      pos += t.length
    }
    return null
  }
  let startOff = nodeOffset(range.startContainer, range.startOffset)
  let endOff = nodeOffset(range.endContainer, range.endOffset)
  const varPattern = /\{\{\w+\}\}/g
  let m: RegExpExecArray | null
  while ((m = varPattern.exec(fullText)) !== null) {
    const vs = m.index, ve = m.index + m[0].length
    if (startOff > vs && startOff < ve) startOff = vs
    if (endOff > vs && endOff < ve) endOff = ve
  }
  const newStart = offsetToNodePos(startOff)
  const newEnd = offsetToNodePos(endOff)
  if (newStart && newEnd) {
    range.setStart(newStart.node, newStart.offset)
    range.setEnd(newEnd.node, newEnd.offset)
    sel.removeAllRanges()
    sel.addRange(range)
  }
}

function execCmd(cmd: string, value?: string) {
  if (savedSelection) {
    const sel = window.getSelection()
    if (sel) { sel.removeAllRanges(); sel.addRange(savedSelection) }
    savedSelection = null
  }
  expandSelectionToVariables()
  document.execCommand(cmd, false, value)
  editorRef.value?.focus()
  onEditorInput()
}

function saveSelection() {
  const sel = window.getSelection()
  if (sel?.rangeCount && editorRef.value?.contains(sel.anchorNode)) {
    savedSelection = sel.getRangeAt(0).cloneRange()
  }
}

function setFontSize(size: string) {
  if (savedSelection) {
    const sel = window.getSelection()
    if (sel) { sel.removeAllRanges(); sel.addRange(savedSelection) }
    savedSelection = null
  }
  const sel = window.getSelection()
  if (sel && sel.rangeCount && sel.getRangeAt(0).collapsed) {
    // Collapsed cursor: insert a zero-width space inside a styled span
    const span = document.createElement('span')
    span.style.fontSize = size + 'px'
    span.textContent = '\u200B'
    const range = sel.getRangeAt(0)
    range.insertNode(span)
    // Place cursor after the ZWS inside the span
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
  expandSelectionToVariables()
  document.execCommand('fontSize', false, '7')
  const fontEls = editorRef.value?.querySelectorAll('font[size="7"]')
  const newSpans: HTMLElement[] = []
  fontEls?.forEach(el => {
    const span = document.createElement('span')
    span.style.fontSize = size + 'px'
    span.innerHTML = el.innerHTML
    el.replaceWith(span)
    newSpans.push(span)
  })
  if (newSpans.length) {
    const sel2 = window.getSelection()
    if (sel2) {
      const range = document.createRange()
      const tw1 = document.createTreeWalker(newSpans[0], NodeFilter.SHOW_TEXT)
      const firstText = tw1.nextNode()
      const lastSpan = newSpans[newSpans.length - 1]
      const tw2 = document.createTreeWalker(lastSpan, NodeFilter.SHOW_TEXT)
      let lastText: Node | null = null
      while (tw2.nextNode()) lastText = tw2.currentNode
      if (firstText && lastText) {
        range.setStart(firstText, 0)
        range.setEnd(lastText, (lastText as Text).length)
        sel2.removeAllRanges()
        sel2.addRange(range)
      }
    }
  }
  editorRef.value?.focus()
  onEditorInput()
}

function adjustFontSize(delta: number) {
  const sel = window.getSelection()
  if (!sel || !sel.rangeCount || !editorRef.value) return
  const current = parseInt(curSize.value) || 12
  const newSize = Math.max(8, Math.min(72, current + delta))
  setFontSize(String(newSize))
  curSize.value = String(newSize)
}

// ---- Recipient contact search -----------------------------------------------
const { results: contactResults, searching: contactSearching, search: searchContacts, clear: clearContactSearch } = useContactSearch()
const recipientSearch = ref('')
const showRecipientDropdown = ref(false)

function onRecipientSearchInput() {
  searchContacts(recipientSearch.value)
}

function addRecipient(email: string) {
  if (!recipients.value.includes(email)) {
    // Replace the empty sentinel if present
    if (recipients.value.length === 1 && recipients.value[0] === '') {
      recipients.value[0] = email
    } else {
      recipients.value.push(email)
    }
  }
  recipientSearch.value = ''
  showRecipientDropdown.value = false
  clearContactSearch()
}

function removeRecipient(index: number) {
  recipients.value.splice(index, 1)
  if (recipients.value.length === 0) recipients.value = ['']
}

function onRecipientBlur() {
  setTimeout(() => {
    showRecipientDropdown.value = false
  }, 200)
}

function onRecipientKeydown(e: KeyboardEvent) {
  if (e.key === 'Enter') {
    e.preventDefault()
    const val = recipientSearch.value.trim()
    if (val && val.includes('@')) {
      addRecipient(val)
    }
  }
}

// ---- CC contact search ------------------------------------------------------
const { results: ccResults, searching: ccSearching, search: searchCcContacts, clear: clearCcSearch } = useContactSearch()
const ccSearch = ref('')
const showCcDropdown = ref(false)

function onCcSearchInput() {
  searchCcContacts(ccSearch.value)
}

function addCc(email: string) {
  if (!cc.value.includes(email)) {
    if (cc.value.length === 1 && cc.value[0] === '') {
      cc.value[0] = email
    } else {
      cc.value.push(email)
    }
  }
  ccSearch.value = ''
  showCcDropdown.value = false
  clearCcSearch()
}

function removeCc(index: number) {
  cc.value.splice(index, 1)
  if (cc.value.length === 0) cc.value = ['']
}

function onCcBlur() {
  setTimeout(() => {
    showCcDropdown.value = false
  }, 200)
}

function onCcKeydown(e: KeyboardEvent) {
  if (e.key === 'Enter') {
    e.preventDefault()
    const val = ccSearch.value.trim()
    if (val && val.includes('@')) {
      addCc(val)
    }
  }
}
</script>

<template>
  <header class="header">
    <h1>Mail-Vorlagen</h1>
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
      <div v-if="pendingLocalDraft" class="editors-banner" style="margin-bottom: 12px; gap: 10px; flex-wrap: wrap;">
        <span class="editors-banner-icon">💾</span>
        <span>Ungespeicherter Entwurf gefunden ({{ new Date(pendingLocalDraft.savedAt).toLocaleString('de-DE') }}).</span>
        <button type="button" class="btn-secondary" @click="applyLocalDraft">Wiederherstellen</button>
        <button type="button" class="btn-secondary" @click="discardLocalDraft">Verwerfen</button>
      </div>
      <div v-else-if="localDraftRestoredAt" class="editors-banner" style="margin-bottom: 12px;">
        <span class="editors-banner-icon">✅</span>
        <span>Lokaler Entwurf wurde wiederhergestellt.</span>
      </div>

      <!-- Active editors indicator -->
      <div v-if="activeEditors.length" class="editors-banner">
        <span class="editors-banner-icon">👥</span>
        <span>{{ activeEditors.join(', ') }} {{ activeEditors.length === 1 ? 'bearbeitet' : 'bearbeiten' }} ebenfalls</span>
      </div>

      <!-- Recipients -->
      <div class="form-group lock-wrapper user-search-group" :class="{ 'is-locked': isLockedByOther('tpl_recipients') }"
        @focusin="lockSection('tpl_recipients')" @focusout="unlockSection('tpl_recipients', $event)">
        <div v-if="lockedBy('tpl_recipients')" class="lock-badge">🔒 {{ lockedBy('tpl_recipients') }}</div>
        <label>Empfänger
          <span v-if="savedFields['recipients']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['recipients']">💾</span>
        </label>
        <div class="user-chips" v-if="recipients.filter(r => r).length">
          <span v-for="(email, i) in recipients" :key="email || i" class="user-chip" v-show="email">
            {{ email }}
            <button type="button" class="user-chip-remove" @click="removeRecipient(i)" :disabled="isLockedByOther('tpl_recipients')">✕</button>
          </span>
        </div>
        <div class="user-search-wrapper">
          <input
            type="text"
            v-model="recipientSearch"
            placeholder="Kontakt suchen…"
            @input="onRecipientSearchInput"
            @focus="showRecipientDropdown = true"
            @blur="onRecipientBlur"
            @keydown="onRecipientKeydown"
            :disabled="isLockedByOther('tpl_recipients')"
          />
          <div v-if="showRecipientDropdown && (contactResults.length || contactSearching)" class="user-dropdown">
            <div v-if="contactSearching" class="user-dropdown-item user-dropdown-item--loading">Suchen…</div>
            <div
              v-for="c in contactResults"
              :key="c.email"
              class="user-dropdown-item"
              @mousedown.prevent="addRecipient(c.email)"
            >
              <span class="contact-name">{{ c.displayName }}</span>
              <span class="contact-email">{{ c.email }}</span>
            </div>
          </div>
        </div>
      </div>

      <!-- CC -->
      <div class="form-group lock-wrapper user-search-group" :class="{ 'is-locked': isLockedByOther('tpl_cc') }"
        @focusin="lockSection('tpl_cc')" @focusout="unlockSection('tpl_cc', $event)">
        <div v-if="lockedBy('tpl_cc')" class="lock-badge">🔒 {{ lockedBy('tpl_cc') }}</div>
        <label>CC
          <span v-if="savedFields['cc']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['cc']">💾</span>
        </label>
        <div class="user-chips" v-if="cc.filter(r => r).length">
          <span v-for="(email, i) in cc" :key="email || i" class="user-chip" v-show="email">
            {{ email }}
            <button type="button" class="user-chip-remove" @click="removeCc(i)" :disabled="isLockedByOther('tpl_cc')">✕</button>
          </span>
        </div>
        <div class="user-search-wrapper">
          <input
            type="text"
            v-model="ccSearch"
            placeholder="Kontakt suchen…"
            @input="onCcSearchInput"
            @focus="showCcDropdown = true"
            @blur="onCcBlur"
            @keydown="onCcKeydown"
            :disabled="isLockedByOther('tpl_cc')"
          />
          <div v-if="showCcDropdown && (ccResults.length || ccSearching)" class="user-dropdown">
            <div v-if="ccSearching" class="user-dropdown-item user-dropdown-item--loading">Suchen…</div>
            <div
              v-for="c in ccResults"
              :key="c.email"
              class="user-dropdown-item"
              @mousedown.prevent="addCc(c.email)"
            >
              <span class="contact-name">{{ c.displayName }}</span>
              <span class="contact-email">{{ c.email }}</span>
            </div>
          </div>
        </div>
      </div>

      <div class="form-group lock-wrapper" :class="{ 'is-locked': isLockedByOther('tpl_subject') }"
        @focusin="lockSection('tpl_subject')" @focusout="unlockSection('tpl_subject', $event)">
        <div v-if="lockedBy('tpl_subject')" class="lock-badge">🔒 {{ lockedBy('tpl_subject') }}</div>
        <label>Betreff-Vorlage</label>
        <div class="input-save-wrap">
          <input v-model="subject" type="text" placeholder="Betreff…" :disabled="isLockedByOther('tpl_subject')" />
          <span v-if="savedFields['subject']" class="field-saved-icon" :key="savedFields['subject']">💾</span>
        </div>
      </div>

      <div class="form-group lock-wrapper" :class="{ 'is-locked': isLockedByOther('tpl_body') }"
        @focusin="lockSection('tpl_body')" @focusout="unlockSection('tpl_body', $event)">
        <div v-if="lockedBy('tpl_body')" class="lock-badge">🔒 {{ lockedBy('tpl_body') }}</div>
        <label>Nachricht-Vorlage</label>
        <div class="input-save-wrap">
          <span v-if="savedFields['body']" class="field-saved-icon field-saved-icon--textarea" :key="savedFields['body']">💾</span>
        </div>
        <div class="rich-editor-toolbar">
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
          <input type="text" class="toolbar-select toolbar-select--narrow" :value="curSize" @mousedown="saveSelection" @change="setFontSize(($event.target as HTMLInputElement).value)" @keydown.enter.prevent="setFontSize(($event.target as HTMLInputElement).value)" title="Schriftgrösse" />
          <button type="button" class="toolbar-btn-sm" @mousedown.prevent @click="adjustFontSize(-2)" title="Kleiner">A−</button>
          <button type="button" class="toolbar-btn-sm" @mousedown.prevent @click="adjustFontSize(2)" title="Grösser">A+</button>
          <span class="toolbar-sep"></span>
          <button type="button" :class="{'toolbar-btn--active': curBold}" @mousedown.prevent @click="execCmd('bold')" title="Fett"><b>B</b></button>
          <button type="button" :class="{'toolbar-btn--active': curItalic}" @mousedown.prevent @click="execCmd('italic')" title="Kursiv"><i>I</i></button>
          <button type="button" :class="{'toolbar-btn--active': curUnderline}" @mousedown.prevent @click="execCmd('underline')" title="Unterstrichen"><u>U</u></button>
          <span class="toolbar-sep"></span>
          <label class="toolbar-color" title="Schriftfarbe" @mousedown="saveSelection">
            A
            <input type="color" :value="curColor" @input="execCmd('foreColor', ($event.target as HTMLInputElement).value)" />
          </label>
          <label class="toolbar-color toolbar-color--bg" title="Hintergrundfarbe" @mousedown="saveSelection">
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
          :contenteditable="!isLockedByOther('tpl_body')"
          @input="onEditorInput"
          @mouseup="updateToolbarState"
          @keyup="updateToolbarState"
          data-placeholder="Mail-Text…"
        ></div>
      </div>

      <!-- Variable reference -->
      <TemplateVarsDropdown :extra-vars="MAIL_EXTRA_VARS" />

      <ErrorAlert :error="error" />
    </div>
  </main>
</template>
