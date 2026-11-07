<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import { useRouter } from 'vue-router'
import { useMailTemplates } from '../composables/useMailTemplates'
import { useContactSearch } from '../composables/useContactSearch'
import { user } from '../composables/useAuth'
import { usePermissions } from '../composables/usePermissions'
import { wsSend, wsRegister, wsJoin, wsLeave, useWebSocket } from '../composables/useWebSocket'
import { Save, Check, Users, Lock, X } from 'lucide-vue-next'
import DepartmentBadge from '../components/DepartmentBadge.vue'
import TemplateVarsDropdown from '../components/TemplateVarsDropdown.vue'
import type { Department, EditSection, MailTemplate } from '../types'

const { departments: deptRecords, fetchDepartments, myPermissions, fetchMyPermissions } = usePermissions()

const ALL_DEPARTMENTS = computed<Department[]>(() => deptRecords.value.map(d => d.name))

const MAIL_EXTRA_VARS = [
  { var: '{{absender_email}}',   desc: 'E-Mail-Adresse des Absenders' },
  { var: '{{absender_name}}',    desc: 'Voller Name des Absenders (z.B. Leandro Klaus v/o Topo)' },
  { var: '{{formular_link}}',          desc: 'Link zum Formular (klickbar, Text: «Zum Formular»)' },
  { var: '{{formular_link|Anmelden}}', desc: 'Link zum Formular mit eigenem Linktext' },
]

const {
  fetchTemplates,
  createTemplate,
  updateTemplate,
  deleteTemplate,
  templates,
  loading,
  error,
} = useMailTemplates()
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

const selectedTemplateId = ref('')
const showTemplateEditor = ref(false)
const templateName = ref('')
const templateIsDefault = ref(false)
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
let linkSavedRange: Range | null = null
const showLinkDialog = ref(false)
const linkUrl = ref('')
const linkInputRef = ref<HTMLInputElement | null>(null)

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

const departmentTemplates = computed(() =>
  templates.value.filter((template) => template.department === activeDept.value),
)
const selectedTemplate = computed(() =>
  departmentTemplates.value.find((template) => template.id === selectedTemplateId.value) ?? null,
)

function tplId() {
  return selectedTemplateId.value
    ? `tpl_${activeDept.value}:${selectedTemplateId.value}`
    : `tpl_${activeDept.value}`
}

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
const autosaveInterval = () => config.AUTOSAVE_INTERVAL
const autosaveDebounce = () => config.AUTOSAVE_DEBOUNCE

let autoSaveTimer: ReturnType<typeof setTimeout> | null = null
let autoSaveInterval: ReturnType<typeof setInterval> | null = null
let hasPendingChanges = false

interface LocalMailTemplateDraft {
  version: 1;
  department: Department;
  templateId?: string;
  savedAt: number;
  name?: string;
  isDefault?: boolean;
  subject: string;
  body: string;
  recipients: string[];
  cc: string[];
}

const pendingLocalDraft = ref<LocalMailTemplateDraft | null>(null)
const localDraftRestoredAt = ref<number | null>(null)
let localDraftWriteTimer: ReturnType<typeof setTimeout> | null = null
const LOCAL_DRAFT_WRITE_DEBOUNCE_MS = 350

function localDraftKey(dept = activeDept.value, templateId = selectedTemplateId.value) {
  return templateId
    ? `dpw:mail-template-draft:${dept}:${templateId}`
    : `dpw:mail-template-draft:${dept}`
}

function buildLocalDraft(dept = activeDept.value): LocalMailTemplateDraft {
  return {
    version: 1,
    department: dept,
    templateId: selectedTemplateId.value || undefined,
    savedAt: Date.now(),
    name: templateName.value,
    isDefault: templateIsDefault.value,
    subject: subject.value,
    body: body.value,
    recipients: recipients.value.map(r => r.trim()).filter(Boolean),
    cc: cc.value.map(r => r.trim()).filter(Boolean),
  }
}

function clearLocalDraft(
  dept = activeDept.value,
  templateId = selectedTemplateId.value,
  includeLegacy = !templateId || templateIsDefault.value,
) {
  if (localDraftWriteTimer) {
    clearTimeout(localDraftWriteTimer)
    localDraftWriteTimer = null
  }
  try {
    if (templateId) window.localStorage.removeItem(localDraftKey(dept, templateId))
    if (includeLegacy) window.localStorage.removeItem(localDraftKey(dept, ''))
  } catch {
    /* ignore unavailable storage */
  }
}

function writeLocalDraftNow() {
  if (!activeDept.value || !selectedTemplateId.value) return
  try {
    window.localStorage.setItem(
      localDraftKey(activeDept.value, selectedTemplateId.value),
      JSON.stringify(buildLocalDraft()),
    )
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

function loadPendingLocalDraft(template: MailTemplate) {
  try {
    const raw = window.localStorage.getItem(localDraftKey(template.department, template.id))
      ?? (template.is_default
        ? window.localStorage.getItem(localDraftKey(template.department, ''))
        : null)
    if (!raw) {
      pendingLocalDraft.value = null
      return
    }
    const parsed = JSON.parse(raw) as Partial<LocalMailTemplateDraft>
    if (parsed.version !== 1 || parsed.department !== template.department ||
        (parsed.templateId && parsed.templateId !== template.id)) {
      pendingLocalDraft.value = null
      return
    }
    const draft = parsed as LocalMailTemplateDraft
    const currentSnapshot = JSON.stringify({
      name: templateName.value,
      isDefault: templateIsDefault.value,
      subject: subject.value,
      body: body.value,
      recipients: recipients.value.map(r => r.trim()).filter(Boolean),
      cc: cc.value.map(r => r.trim()).filter(Boolean),
    })
    const draftSnapshot = JSON.stringify({
      name: draft.name ?? template.name,
      isDefault: draft.isDefault ?? template.is_default,
      subject: draft.subject,
      body: draft.body,
      recipients: draft.recipients,
      cc: draft.cc,
    })
    if (currentSnapshot === draftSnapshot) {
      clearLocalDraft(template.department, template.id, template.is_default)
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
  templateName.value = pendingLocalDraft.value.name ?? templateName.value
  templateIsDefault.value = pendingLocalDraft.value.isDefault ?? templateIsDefault.value
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
  if (autosaveDebounce()) {
    if (autoSaveTimer) clearTimeout(autoSaveTimer)
    autoSaveTimer = setTimeout(doSave, autosaveInterval())
  } else {
    hasPendingChanges = true
  }
}

function startAutoSaveInterval() {
  if (!autosaveDebounce() && !autoSaveInterval) {
    autoSaveInterval = setInterval(() => {
      if (hasPendingChanges) {
        hasPendingChanges = false
        doSave()
      }
    }, autosaveInterval())
  }
}

function stopAutoSaveInterval() {
  if (autoSaveInterval) {
    clearInterval(autoSaveInterval)
    autoSaveInterval = null
  }
  hasPendingChanges = false
}

async function doSave(): Promise<boolean> {
  if (!selectedTemplateId.value) return false
  if (!templateName.value.trim()) {
    error.value = 'Name der Vorlage erforderlich.'
    return false
  }
  error.value = null
  writeLocalDraftNow()
  const validRecipients = recipients.value.map(r => r.trim()).filter(Boolean)
  const validCc = cc.value.map(r => r.trim()).filter(Boolean)
  const wasDefault = selectedTemplate.value?.is_default ?? false
  const result = await updateTemplate(
    selectedTemplateId.value,
    templateName.value.trim(),
    subject.value,
    body.value,
    validRecipients,
    validCc,
    templateIsDefault.value,
  )
  if (result) {
    showSavedIndicator()
    clearLocalDraft(result.department, result.id, result.is_default || wasDefault)
    pendingLocalDraft.value = null
    localDraftRestoredAt.value = null
    if (result.is_default || wasDefault) {
      templates.value.forEach((template) => {
        if (template.department === result.department && template.id !== result.id) {
          template.is_default = false
        }
      })
    }
    const idx = templates.value.findIndex((template) => template.id === result.id)
    if (idx >= 0) templates.value[idx] = result
    else templates.value.push(result)
    return true
  }
  return false
}

// Watchers for auto-save + dirty tracking
watch([templateName], () => { markDirty('name'); scheduleAutoSave() })
watch([templateIsDefault], () => { markDirty('default'); scheduleAutoSave() })
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
    const idx = templates.value.findIndex(template => template.id === tpl.id)
    const wasDefault = idx >= 0 && templates.value[idx].is_default
    if (idx >= 0) templates.value[idx] = tpl
    else templates.value.push(tpl)
    if (tpl.department !== activeDept.value || tpl.id !== selectedTemplateId.value) return
    if (tpl.is_default || wasDefault) {
      templates.value.forEach((template) => {
        if (template.department === tpl.department && template.id !== tpl.id) {
          template.is_default = false
        }
      })
    }
    // Merge remote changes into edit fields
    applyingRemote = true
    if (tpl.name !== templateName.value && !myLockedSection.value?.startsWith('tpl_name')) {
      templateName.value = tpl.name
    }
    if (tpl.is_default !== templateIsDefault.value && !myLockedSection.value?.startsWith('tpl_default')) {
      templateIsDefault.value = tpl.is_default
    }
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
  if (user.value) {
    wsRegister(user.value.display_name, user.value.microsoft_oid)
  }
  await loadDept(activeDept.value)
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

async function flushCurrentTemplate(): Promise<boolean> {
  if (!showTemplateEditor.value) return true
  const needsSave = dirtyFields.size > 0 || hasPendingChanges || autoSaveTimer !== null
  if (autoSaveTimer) {
    clearTimeout(autoSaveTimer)
    autoSaveTimer = null
  }
  hasPendingChanges = false
  writeLocalDraftNow()
  return needsSave ? doSave() : true
}

function activateTemplate(dept: Department, template: MailTemplate | null, edit = true) {
  wsLeave()
  myLockedSection.value = null
  sectionLocks.value = new Map()
  activeEditors.value = []
  localDraftRestoredAt.value = null

  suppressDirtyTracking = true
  activeDept.value = dept
  showTemplateEditor.value = edit
  selectedTemplateId.value = template?.id ?? ''
  templateName.value = template?.name ?? ''
  templateIsDefault.value = template?.is_default ?? false
  subject.value = template?.subject ?? ''
  body.value = template?.body ?? ''
  recipients.value = template?.recipients.length ? [...template.recipients] : ['']
  cc.value = template?.cc.length ? [...template.cc] : ['']
  pendingLocalDraft.value = null
  if (template) loadPendingLocalDraft(template)
  nextTick(() => {
    if (editorRef.value) editorRef.value.innerHTML = body.value
    suppressDirtyTracking = false
    dirtyFields.clear()
  })
  if (template && edit) wsJoin(tplId())
}

async function loadDept(dept: Department) {
  if (dept === activeDept.value && selectedTemplateId.value) return
  if (dept !== activeDept.value && showTemplateEditor.value && !(await flushCurrentTemplate())) return
  const templatesForDepartment = templates.value.filter(template => template.department === dept)
  const template = templatesForDepartment.find(item => item.is_default) ?? templatesForDepartment[0] ?? null
  activateTemplate(dept, template, false)
}

async function editTemplate(template: MailTemplate) {
  if (showTemplateEditor.value && !(await flushCurrentTemplate())) return
  activateTemplate(activeDept.value, template)
}

async function showTemplateList() {
  if (!(await flushCurrentTemplate())) return
  showTemplateEditor.value = false
  wsLeave()
  myLockedSection.value = null
  sectionLocks.value = new Map()
  activeEditors.value = []
}

async function saveTemplateNow() {
  if (autoSaveTimer) {
    clearTimeout(autoSaveTimer)
    autoSaveTimer = null
  }
  hasPendingChanges = false
  return doSave()
}

async function createNewTemplate() {
  const name = window.prompt('Name der neuen Mail-Vorlage:')
  if (!name?.trim() || !(await flushCurrentTemplate())) return
  const isDefault = departmentTemplates.value.length === 0
  const template = await createTemplate(name.trim(), activeDept.value, '', '', [], [], isDefault)
  if (!template) return
  templates.value.push(template)
  activateTemplate(activeDept.value, template)
}

async function deleteCurrentTemplate(template = selectedTemplate.value) {
  if (!template || !window.confirm(`Vorlage „${template.name}" wirklich löschen?`)) return
  const isEditingDeletedTemplate = showTemplateEditor.value && selectedTemplateId.value === template.id
  if (isEditingDeletedTemplate && !(await flushCurrentTemplate())) return
  clearLocalDraft(template.department, template.id, template.is_default)
  if (!(await deleteTemplate(template.id))) return
  if (!isEditingDeletedTemplate && selectedTemplateId.value !== template.id) return
  const remaining = departmentTemplates.value
  activateTemplate(
    activeDept.value,
    remaining.find(item => item.is_default) ?? remaining[0] ?? null,
    false,
  )
}

async function toggleDefault(template: MailTemplate) {
  const updated = await updateTemplate(
    template.id,
    template.name,
    template.subject,
    template.body,
    template.recipients,
    template.cc,
    !template.is_default,
  )
  if (!updated) return
  await fetchTemplates(activeDept.value)
}

function templatePreview(template: MailTemplate): string {
  return template.body.replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim()
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
  const varPattern = /\{\{\w+(?:\|[^}]*)?\}\}/g
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

    <div v-else :class="{ 'detail-form': showTemplateEditor }">
      <template v-if="!showTemplateEditor">
        <div class="template-overview-header">
          <h2 class="template-overview-title">Vorlagen für {{ activeDept || '—' }}</h2>
          <button v-if="activeDept" type="button" class="btn-primary template-overview-create" @click="createNewTemplate">
            + Neue Vorlage
          </button>
        </div>

        <div v-if="departmentTemplates.length === 0" class="mt-empty-state">
          Noch keine Mail-Vorlagen in dieser Stufe.
        </div>

        <div v-for="template in departmentTemplates" :key="template.id" class="mt-row template-overview-row">
          <div class="mt-row-main">
            <div class="mt-row-name">
              {{ template.name }}
              <span v-if="template.is_default" class="default-badge">Standard</span>
            </div>
            <div class="mt-row-meta">
              <span>{{ template.subject || 'Kein Betreff' }}</span>
              <span class="mt-meta-divider">·</span>
              <span>{{ template.recipients.length }} Empfänger</span>
              <span v-if="templatePreview(template)" class="mt-preview">
                {{ templatePreview(template) }}
              </span>
            </div>
          </div>
          <div class="mt-row-actions template-overview-row-actions">
            <button
              type="button"
              :class="['btn-sm', 'template-overview-row-action', template.is_default ? 'active-default' : '']"
              :title="template.is_default ? 'Standard entfernen' : 'Als Standard setzen'"
              @click="toggleDefault(template)"
            >
              {{ template.is_default ? '⭐ Standard' : '☆ Standard' }}
            </button>
            <button type="button" class="btn-sm template-overview-row-action" @click="editTemplate(template)">Bearbeiten</button>
            <button type="button" class="btn-sm template-overview-row-action danger" @click="deleteCurrentTemplate(template)">Löschen</button>
          </div>
        </div>
        <ErrorAlert :error="error" />
      </template>

      <template v-else>
      <div class="template-editor-header">
        <button type="button" class="btn-sm template-editor-back" @click="showTemplateList">← Zurück zu Vorlagen</button>
        <h2 class="template-overview-title">{{ templateName || 'Mail-Vorlage bearbeiten' }}</h2>
      </div>
      <div v-if="pendingLocalDraft" class="editors-banner" style="margin-bottom: 12px; gap: 10px; flex-wrap: wrap;">
        <span class="editors-banner-icon"><Save :size="16" aria-hidden="true" /></span>
        <span>Ungespeicherter Entwurf gefunden ({{ new Date(pendingLocalDraft.savedAt).toLocaleString('de-DE') }}).</span>
        <button type="button" class="btn-secondary" @click="applyLocalDraft">Wiederherstellen</button>
        <button type="button" class="btn-secondary" @click="discardLocalDraft">Verwerfen</button>
      </div>
      <div v-else-if="localDraftRestoredAt" class="editors-banner" style="margin-bottom: 12px;">
        <span class="editors-banner-icon"><Check :size="16" aria-hidden="true" /></span>
        <span>Lokaler Entwurf wurde wiederhergestellt.</span>
      </div>

      <!-- Active editors indicator -->
      <div v-if="activeEditors.length" class="editors-banner">
        <span class="editors-banner-icon"><Users :size="16" aria-hidden="true" /></span>
        <span>{{ activeEditors.join(', ') }} {{ activeEditors.length === 1 ? 'bearbeitet' : 'bearbeiten' }} ebenfalls</span>
      </div>

      <div class="form-group lock-wrapper" :class="{ 'is-locked': isLockedByOther('tpl_name') }"
        @focusin="lockSection('tpl_name')" @focusout="unlockSection('tpl_name', $event)">
        <div v-if="lockedBy('tpl_name')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('tpl_name') }}</div>
        <label for="mail-template-name">Name der Vorlage</label>
        <input id="mail-template-name" v-model="templateName" type="text" required :disabled="isLockedByOther('tpl_name')" />
      </div>

      <div class="default-template-control lock-wrapper" :class="{ 'is-locked': isLockedByOther('tpl_default') }"
        @focusin="lockSection('tpl_default')" @focusout="unlockSection('tpl_default', $event)">
        <div v-if="lockedBy('tpl_default')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('tpl_default') }}</div>
        <label class="checkbox-label">
          <input v-model="templateIsDefault" type="checkbox" :disabled="isLockedByOther('tpl_default')" />
          <span>Als Standard-Vorlage für diese Stufe setzen</span>
        </label>
        <p class="default-hint">Die Standard-Vorlage wird beim Mailversand automatisch ausgewählt.</p>
      </div>

      <!-- Recipients -->
      <div class="form-group lock-wrapper user-search-group" :class="{ 'is-locked': isLockedByOther('tpl_recipients') }"
        @focusin="lockSection('tpl_recipients')" @focusout="unlockSection('tpl_recipients', $event)">
        <div v-if="lockedBy('tpl_recipients')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('tpl_recipients') }}</div>
        <label>Empfänger
          <span v-if="savedFields['recipients']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['recipients']"><Save :size="12" aria-hidden="true" /></span>
        </label>
        <div class="user-chips" v-if="recipients.filter(r => r).length">
          <span v-for="(email, i) in recipients" :key="email || i" class="user-chip" v-show="email">
            {{ email }}
            <button type="button" class="user-chip-remove" @click="removeRecipient(i)" :disabled="isLockedByOther('tpl_recipients')" aria-label="Empfänger entfernen"><X :size="12" aria-hidden="true" /></button>
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
        <div v-if="lockedBy('tpl_cc')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('tpl_cc') }}</div>
        <label>CC
          <span v-if="savedFields['cc']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['cc']"><Save :size="12" aria-hidden="true" /></span>
        </label>
        <div class="user-chips" v-if="cc.filter(r => r).length">
          <span v-for="(email, i) in cc" :key="email || i" class="user-chip" v-show="email">
            {{ email }}
            <button type="button" class="user-chip-remove" @click="removeCc(i)" :disabled="isLockedByOther('tpl_cc')" aria-label="CC entfernen"><X :size="12" aria-hidden="true" /></button>
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
        <div v-if="lockedBy('tpl_subject')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('tpl_subject') }}</div>
        <label>Betreff-Vorlage</label>
        <div class="input-save-wrap">
          <input v-model="subject" type="text" placeholder="Betreff…" :disabled="isLockedByOther('tpl_subject')" />
          <span v-if="savedFields['subject']" class="field-saved-icon" :key="savedFields['subject']"><Save :size="12" aria-hidden="true" /></span>
        </div>
      </div>

      <div class="form-group lock-wrapper" :class="{ 'is-locked': isLockedByOther('tpl_body') }"
        @focusin="lockSection('tpl_body')" @focusout="unlockSection('tpl_body', $event)">
        <div v-if="lockedBy('tpl_body')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('tpl_body') }}</div>
        <label>Nachricht-Vorlage</label>
        <div class="input-save-wrap">
          <span v-if="savedFields['body']" class="field-saved-icon field-saved-icon--textarea" :key="savedFields['body']"><Save :size="12" aria-hidden="true" /></span>
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
          <button type="button" @mousedown.prevent @click="execCmd('removeFormat')" title="Formatierung entfernen"><X :size="12" aria-hidden="true" /> Format</button>
          <span class="toolbar-sep"></span>
          <button type="button" @mousedown="saveSelection" @click="openLinkDialog" title="Link einfügen">🔗 Link</button>
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
      <TemplateVarsDropdown :extra-vars="MAIL_EXTRA_VARS" hint="Variablen können im Betreff und im Inhalt verwendet werden. Im Betreff werden Links als URL eingefügt." />

      <div class="mt-editor-actions">
        <button type="button" class="btn-secondary" @click="showTemplateList">Zurück</button>
        <button type="button" class="btn-primary" @click="saveTemplateNow">Speichern</button>
      </div>

      <ErrorAlert :error="error" />
      </template>
    </div>
  </main>

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
</template>

<style scoped>
.mt-empty-state {
	text-align: center;
	color: var(--text-subtle);
	padding: 2rem;
	font-size: 0.875rem;
	border: 1px dashed var(--border);
	border-radius: 0.5rem;
}

.mt-row-main {
	flex: 1;
	min-width: 0;
}

.mt-row-name {
	font-weight: 600;
	color: var(--text-primary);
	font-size: 0.95rem;
}

.mt-row-meta {
	display: flex;
	gap: 0.5rem;
	margin-top: 0.25rem;
	font-size: 0.78rem;
	color: var(--text-muted);
	align-items: center;
	flex-wrap: wrap;
}

.mt-meta-divider {
	color: var(--text-subtle);
}

.mt-preview {
	overflow: hidden;
	text-overflow: ellipsis;
	white-space: nowrap;
	max-width: min(34rem, 55vw);
}

.btn-sm {
	padding: 0.25rem 0.65rem;
	background: var(--bg-surface);
	color: var(--text-secondary);
	border: 1px solid var(--border-strong);
	border-radius: 0.25rem;
	font-size: 0.78rem;
	cursor: pointer;
	white-space: nowrap;
}

.btn-sm:hover {
	background: var(--bg-hover);
}

.btn-sm.danger {
	color: var(--error-color);
	border-color: var(--error-border);
}

.btn-sm.danger:hover {
	background: var(--error-bg);
}

.default-badge {
	display: inline-block;
	font-size: 0.7rem;
	font-weight: 600;
	background: #fef3c7;
	color: #92400e;
	padding: 0.1rem 0.4rem;
	border-radius: 0.25rem;
	margin-left: 0.4rem;
	vertical-align: middle;
}

.active-default {
	background: #fef3c7 !important;
	border-color: #f59e0b !important;
	color: #92400e !important;
}

.default-template-control {
	margin-bottom: 1rem;
	padding: 0.75rem 1rem;
	border: 1px solid var(--border);
	border-radius: 0.5rem;
	background: var(--bg-elevated);
}

.checkbox-label {
	display: flex;
	align-items: center;
	gap: 0.5rem;
	font-size: 0.875rem;
	font-weight: 500;
	color: var(--text-secondary);
	cursor: pointer;
}

.checkbox-label input {
	width: 1rem;
	height: 1rem;
	accent-color: #6366f1;
}

.default-hint {
	margin: 0.25rem 0 0 1.5rem;
	font-size: 0.75rem;
	color: var(--text-subtle);
}

.mt-editor-actions {
	display: flex;
	justify-content: flex-end;
	gap: 0.75rem;
	margin-top: 1.5rem;
}

.mt-editor-actions .btn-secondary,
.mt-editor-actions .btn-primary {
	padding: 0.5rem 1.25rem;
	border-radius: 0.375rem;
	font-size: 0.875rem;
	cursor: pointer;
}

.mt-editor-actions .btn-secondary {
	background: var(--btn-secondary-bg);
	color: var(--text-secondary);
	border: 1px solid var(--border-strong);
}

.mt-editor-actions .btn-primary {
	background: var(--btn-primary-bg);
	color: var(--btn-primary-color, #fff);
	border: none;
	font-weight: 600;
}

.mt-editor-actions .btn-secondary:hover {
	background: var(--bg-elevated);
}

.mt-editor-actions .btn-primary:hover {
	background: var(--btn-primary-bg-hover);
}

@media (max-width: 599px) {
	.mt-preview {
		max-width: 100%;
	}

	.mt-editor-actions {
		justify-content: stretch;
	}

	.mt-editor-actions button {
		flex: 1;
		min-height: 42px;
	}
}
</style>
