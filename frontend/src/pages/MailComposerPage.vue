<script setup lang="ts">
import { ref, onMounted, onUnmounted, nextTick } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useActivities } from '../composables/useActivities';
import { useMailTemplates } from '../composables/useMailTemplates';
import { useContactSearch } from '../composables/useContactSearch';
import { user } from '../composables/useAuth';
import { usePermissions } from '../composables/usePermissions';
import { useForms } from '../composables/useForms';
import ErrorAlert from '../components/ErrorAlert.vue';
import DepartmentBadge from '../components/DepartmentBadge.vue';
import { ArrowLeft, Save, Check, X, Clipboard, Send } from 'lucide-vue-next';
import type { Activity, Department, SentMail } from '../types';
import { useAutosave } from '../composables/useAutosave';
import { config } from '../config';

const route = useRoute()
const router = useRouter()
const activityId = route.params.id as string

const { fetchActivity } = useActivities()
const { fetchTemplate, sendMail, sending, error, fetchSentMails, fetchDraft, saveDraft, deleteDraft } = useMailTemplates()
const { myPermissions, fetchMyPermissions } = usePermissions()
const { fetchForm: fetchActivityForm } = useForms()

const activity = ref<Activity | null>(null)
const subject = ref('')
const body = ref('')
const recipients = ref<string[]>([''])
const cc = ref<string[]>([''])
const sent = ref(false)
const loadError = ref<string | null>(null)
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

// Sent mail state
const sentMails = ref<SentMail[]>([])
const showWarning = ref(false)
const warningConfirmed = ref(false)
const viewingMail = ref<SentMail | null>(null)

// Form link state
const hasForm = ref(false)
const formUrl = ref('')
const linkCopied = ref(false)

function configuredPublicBaseUrl(): string | null {
	const normalized = (config.PUBLIC_BASE_URL || '').trim().replace(/\/+$/, '')
	return normalized || null
}

// Autosave / per-field saved indicators
const savedFields = ref<Record<string, number>>({})
let savedTimer: ReturnType<typeof setTimeout> | null = null
const dirtyFieldSet = new Set<string>()

function showSavedIndicator() {
	if (savedTimer) clearTimeout(savedTimer)
	const snap: Record<string, number> = {}
	for (const f of dirtyFieldSet) snap[f] = Date.now()
	dirtyFieldSet.clear()
	savedFields.value = snap
	savedTimer = setTimeout(() => { savedFields.value = {} }, 2000)
}

function markDirty(field: string) {
	dirtyFieldSet.add(field)
	scheduleLocalDraftWrite()
	scheduleAutoSave()
}

const { scheduleAutoSave, flushAutoSave, cancelAutoSave } = useAutosave(async () => {
	writeLocalDraftNow()
	const saved = await saveDraft(activityId, recipients.value.filter(r => r.trim()), subject.value, body.value, cc.value.filter(r => r.trim()))
	if (saved) {
		showSavedIndicator()
		clearLocalDraft()
		pendingLocalDraft.value = null
		localDraftRestoredAt.value = null
	}
})

// Snapshot for cancel/revert
let initialRecipients: string[] = []
let initialCc: string[] = []
let initialSubject = ''
let initialBody = ''
let hadDraftOnEntry = false

interface LocalMailDraft {
	version: 1;
	activityId: string;
	savedAt: number;
	recipients: string[];
	cc: string[];
	subject: string;
	body: string;
}

const pendingLocalDraft = ref<LocalMailDraft | null>(null)
const localDraftRestoredAt = ref<number | null>(null)
let localDraftWriteTimer: ReturnType<typeof setTimeout> | null = null
const LOCAL_DRAFT_WRITE_DEBOUNCE_MS = 350

function localDraftKey() {
	return `dpw:mail-composer-draft:${activityId}`
}

function buildLocalDraft(): LocalMailDraft {
	return {
		version: 1,
		activityId,
		savedAt: Date.now(),
		recipients: recipients.value.filter(r => r.trim()),
		cc: cc.value.filter(r => r.trim()),
		subject: subject.value,
		body: body.value,
	}
}

function clearLocalDraft() {
	if (localDraftWriteTimer) {
		clearTimeout(localDraftWriteTimer)
		localDraftWriteTimer = null
	}
	try {
		window.localStorage.removeItem(localDraftKey())
	} catch {
		/* ignore unavailable storage */
	}
}

function writeLocalDraftNow() {
	try {
		window.localStorage.setItem(localDraftKey(), JSON.stringify(buildLocalDraft()))
	} catch {
		/* ignore unavailable storage */
	}
}

function scheduleLocalDraftWrite() {
	if (localDraftWriteTimer) clearTimeout(localDraftWriteTimer)
	localDraftWriteTimer = setTimeout(() => {
		writeLocalDraftNow()
	}, LOCAL_DRAFT_WRITE_DEBOUNCE_MS)
}

function loadPendingLocalDraft() {
	try {
		const raw = window.localStorage.getItem(localDraftKey())
		if (!raw) {
			pendingLocalDraft.value = null
			return
		}
		const parsed = JSON.parse(raw) as Partial<LocalMailDraft>
		if (parsed.version !== 1 || parsed.activityId !== activityId) {
			pendingLocalDraft.value = null
			return
		}
		const draft = parsed as LocalMailDraft
		const sameAsCurrent = JSON.stringify({
			recipients: recipients.value.filter(r => r.trim()),
			cc: cc.value.filter(r => r.trim()),
			subject: subject.value,
			body: body.value,
		}) === JSON.stringify({
			recipients: draft.recipients,
			cc: draft.cc,
			subject: draft.subject,
			body: draft.body,
		})
		if (sameAsCurrent) {
			clearLocalDraft()
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
	recipients.value = pendingLocalDraft.value.recipients.length ? [...pendingLocalDraft.value.recipients] : ['']
	cc.value = pendingLocalDraft.value.cc.length ? [...pendingLocalDraft.value.cc] : ['']
	subject.value = pendingLocalDraft.value.subject
	body.value = pendingLocalDraft.value.body
	pendingLocalDraft.value = null
	localDraftRestoredAt.value = Date.now()
	nextTick(() => {
		if (editorRef.value) editorRef.value.innerHTML = body.value
		scheduleLocalDraftWrite()
		scheduleAutoSave()
	})
}

function discardLocalDraft() {
	clearLocalDraft()
	pendingLocalDraft.value = null
	localDraftRestoredAt.value = null
}

function formatDateLong(d: string): string {
  return new Date(d + 'T00:00:00').toLocaleDateString('de-DE', {
    weekday: 'long', day: 'numeric', month: 'long', year: 'numeric'
  })
}

function formatDateShort(d: string): string {
  return new Date(d + 'T00:00:00').toLocaleDateString('de-DE', {
    day: '2-digit', month: '2-digit', year: 'numeric'
  })
}

function formatPrograms(act: Activity): string {
  if (!act.programs.length) return '—'
  return act.programs.map(p => {
    const dur = p.duration_minutes ? `${p.duration_minutes} min` : '—'
    const resp = p.responsible && p.responsible.length ? ' (' + p.responsible.join(', ') + ')' : ''
    const desc = p.description ? ': ' + p.description : ''
    return `${dur} – ${p.title}${resp}${desc}`
  }).join('\n')
}

function replaceTemplateVars(text: string, act: Activity): string {
  // Use a temporary DOM element to do replacements while preserving formatting.
  // Walking text nodes means <font face="Arial">{{titel}}</font> keeps its font.
  const container = document.createElement('div')
  container.innerHTML = text
  const senderEmail = user.value?.email ?? ''
  const senderName = user.value?.display_name ?? ''
  const vars: Record<string, string> = {
    titel: act.title,
    datum: formatDateLong(act.date),
    datum_kurz: formatDateShort(act.date),
    startzeit: act.start_time,
    endzeit: act.end_time,
    ort: act.location,
    verantwortlich: act.responsible.join(', '),
    abteilung: act.department ?? '—',
    ziel: act.goal,
    material: act.material.map(m => m.name).join(', ') || '—',
    schlechtwetter: act.bad_weather_info ?? '—',
    programm: formatPrograms(act),
    absender_email: senderEmail,
    absender_name: senderName,
  }
  const formLink = formUrl.value || '#'
  const walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT)
  const textNodes: Text[] = []
  while (walker.nextNode()) textNodes.push(walker.currentNode as Text)
  for (const node of textNodes) {
    const original = node.nodeValue ?? ''
    // Handle {{formular_link}} or {{formular_link|Eigener Text}} – replace with an <a> element
    if (/\{\{formular_link(?:\|[^}]*)?\}\}/i.test(original)) {
      const parts = original.split(/\{\{formular_link(?:\|([^}]*))?\}\}/i)
      const parent = node.parentNode!
      for (let i = 0; i < parts.length; i++) {
        if (i % 2 === 0) {
          const partText = parts[i].replace(/\{\{(\w+)\}\}/gi, (m, key) => {
            const lk = key.toLowerCase()
            return lk in vars ? vars[lk] : m
          })
          if (partText) parent.insertBefore(document.createTextNode(partText), node)
        } else {
          const a = document.createElement('a')
          a.href = formLink
          a.textContent = parts[i] || 'Zum Formular'
          parent.insertBefore(a, node)
        }
      }
      parent.removeChild(node)
      continue
    }
    const replaced = original.replace(/\{\{(\w+)\}\}/gi, (m, key) => {
      const lk = key.toLowerCase()
      return lk in vars ? vars[lk] : m
    })
    if (replaced !== original) node.nodeValue = replaced
  }
  return container.innerHTML
}

onMounted(async () => {
  await fetchMyPermissions()
  if (!myPermissions.value || myPermissions.value.mail_send_scope === 'none') {
    router.replace('/')
    return
  }

  const act = await fetchActivity(activityId)
  if (!act) {
    loadError.value = 'Aktivität nicht gefunden.'
    return
  }
  activity.value = act

  // Check if a form exists for this activity
  const existingForm = await fetchActivityForm(activityId)
  if (existingForm) {
    hasForm.value = true
		const baseUrl = configuredPublicBaseUrl()
		if (!baseUrl) {
			loadError.value = 'Öffentliche Basis-URL ist nicht konfiguriert.'
		} else {
			formUrl.value = `${baseUrl}/forms/${existingForm.public_slug}`
		}
  }

  // Load sent mails for this activity
  sentMails.value = await fetchSentMails(activityId)
  if (sentMails.value.length > 0) {
    showWarning.value = true
  }

  if (act.department) {
    const tpl = await fetchTemplate(act.department as Department)
    if (tpl) {
      subject.value = replaceTemplateVars(tpl.subject, act)
      body.value    = replaceTemplateVars(tpl.body, act)
      if (tpl.recipients?.length) {
        recipients.value = [...tpl.recipients]
      }
      if (tpl.cc?.length) {
        cc.value = [...tpl.cc]
      }
    }
  }

  // Load existing draft (overrides template if present)
  const draft = await fetchDraft(activityId)
  if (draft) {
    hadDraftOnEntry = true
    if (draft.recipients.length) recipients.value = [...draft.recipients]
    if (draft.cc?.length) cc.value = [...draft.cc]
    if (draft.subject) subject.value = draft.subject
    if (draft.body_html) body.value = draft.body_html
  }

	loadPendingLocalDraft()

  // Snapshot initial state for cancel/revert
  initialRecipients = [...recipients.value]
  initialCc = [...cc.value]
  initialSubject = subject.value
  initialBody = body.value

  nextTick(() => {
    if (editorRef.value) editorRef.value.innerHTML = body.value
  })
})

onUnmounted(() => {
	if (localDraftWriteTimer) {
		clearTimeout(localDraftWriteTimer)
		localDraftWriteTimer = null
	}
	if (!sent.value) writeLocalDraftNow()
})

function onEditorInput() {
  if (editorRef.value) body.value = editorRef.value.innerHTML
  updateToolbarState()
  markDirty('body')
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

function addRecipient(email: string) {
	if (!recipients.value.includes(email)) {
		if (recipients.value.length === 1 && recipients.value[0] === '') {
			recipients.value[0] = email;
		} else {
			recipients.value.push(email);
		}
	}
	recipientSearch.value = '';
	showRecipientDropdown.value = false;
	clearContactSearch();
	markDirty('recipients');
}

function removeRecipient(index: number) {
	recipients.value.splice(index, 1);
	if (recipients.value.length === 0) recipients.value = [''];
	markDirty('recipients');
}

// ---- Contact search ---------------------------------------------------------
const { results: contactResults, searching: contactSearching, search: searchContacts, clear: clearContactSearch } = useContactSearch()
const recipientSearch = ref('')
const showRecipientDropdown = ref(false)

function onRecipientSearchInput() {
	searchContacts(recipientSearch.value)
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
			cc.value[0] = email;
		} else {
			cc.value.push(email);
		}
	}
	ccSearch.value = '';
	showCcDropdown.value = false;
	clearCcSearch();
	markDirty('cc');
}

function removeCc(index: number) {
	cc.value.splice(index, 1);
	if (cc.value.length === 0) cc.value = [''];
	markDirty('cc');
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

async function handleSend() {
	const validTo = recipients.value.map((r) => r.trim()).filter(Boolean);
	if (validTo.length === 0 || !subject.value.trim() || !body.value.trim())
		return;

	cancelAutoSave();
	const fromEmail = user.value?.email ?? '';
	const bodyHtml = body.value;

	const validCc = cc.value.map((r) => r.trim()).filter(Boolean);
	const ok = await sendMail(validTo, subject.value, bodyHtml, fromEmail, activityId, validCc);
	if (ok) {
		await deleteDraft(activityId);
		clearLocalDraft();
		pendingLocalDraft.value = null;
		localDraftRestoredAt.value = null;
		sent.value = true;
	}
}

function confirmWarning() {
	warningConfirmed.value = true
	showWarning.value = false
}

function formatSentDate(iso: string): string {
	const d = new Date(iso)
	return d.toLocaleDateString('de-CH', { day: '2-digit', month: '2-digit', year: 'numeric', hour: '2-digit', minute: '2-digit' })
}

function openMailViewer(mail: SentMail) {
	viewingMail.value = mail
}

function closeMailViewer() {
	viewingMail.value = null
}

async function copyFormLink() {
	if (!formUrl.value) {
		loadError.value = 'Öffentliche Basis-URL ist nicht konfiguriert.'
		return
	}
	try {
		await navigator.clipboard.writeText(formUrl.value)
		linkCopied.value = true
		setTimeout(() => { linkCopied.value = false }, 2000)
	} catch {
		// fallback
		const ta = document.createElement('textarea')
		ta.value = formUrl.value
		document.body.appendChild(ta)
		ta.select()
		document.execCommand('copy')
		document.body.removeChild(ta)
		linkCopied.value = true
		setTimeout(() => { linkCopied.value = false }, 2000)
	}
}

async function cancelAndRevert() {
	cancelAutoSave()
	recipients.value = [...initialRecipients]
	cc.value = [...initialCc]
	subject.value = initialSubject
	body.value = initialBody
	if (hadDraftOnEntry) {
		await saveDraft(activityId, initialRecipients.filter(r => r.trim()), initialSubject, initialBody, initialCc.filter(r => r.trim()))
	} else {
		await deleteDraft(activityId)
	}
	router.back()
}
</script>

<template>
	<header class="header">
		<button class="btn-back" @click="flushAutoSave(); router.back()"><ArrowLeft class="btn-icon" :size="16" aria-hidden="true" /> Zurück</button>
		<h1>Mail senden</h1>
	</header>

	<main class="main">
		<ErrorAlert :error="loadError" />
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

		<!-- Warning dialog when mails already sent -->
		<div v-if="!loadError && showWarning && !warningConfirmed" class="modal-backdrop" @click.self="router.back()">
			<div class="modal modal--info">
				<h2 class="modal-title modal-title--info">Bereits versendete Mails</h2>
				<p class="modal-warning">
					Für diese Aktivität {{ sentMails.length === 1 ? 'wurde bereits 1 Mail' : `wurden bereits ${sentMails.length} Mails` }} versendet.
					Möchtest du trotzdem ein weiteres Mail senden?
				</p>
				<div class="modal-actions">
					<button class="btn-cancel" @click="router.back()">Abbrechen</button>
					<button class="btn-info" @click="confirmWarning">Trotzdem senden</button>
				</div>
			</div>
		</div>

		<div v-if="!loadError && sent" class="mail-sent">
			<p class="mail-sent-text"><Check class="btn-icon" :size="16" aria-hidden="true" /> Mail wurde erfolgreich versendet!</p>
			<button class="btn-primary" @click="router.back()">
				Zurück zur Aktivität
			</button>
		</div>

		<template v-if="!loadError && !sent && activity">
			<!-- Sent mails list -->
			<div v-if="sentMails.length > 0" class="sent-mails-section">
				<h2 class="sent-mails-title">Bereits versendete Mails</h2>
				<div class="sent-mails-list">
					<button
						v-for="mail in sentMails"
						:key="mail.id"
						class="sent-mail-card"
						@click="openMailViewer(mail)"
					>
						<div class="sent-mail-card-header">
							<span class="sent-mail-subject">{{ mail.subject }}</span>
							<span class="sent-mail-date">{{ formatSentDate(mail.sent_at) }}</span>
						</div>
						<div class="sent-mail-meta">
							<span class="sent-mail-from">Von: {{ mail.sender_email }}</span>
							<span class="sent-mail-to">An: {{ mail.to_emails.join(', ') }}</span>
						</div>
					</button>
				</div>
			</div>

			<!-- Mail viewer overlay -->
			<Teleport to="body">
				<div v-if="viewingMail" class="mail-viewer-overlay" @click.self="closeMailViewer">
					<div class="mail-viewer-modal">
						<div class="mail-viewer-header">
							<h2 class="mail-viewer-title">Versendetes Mail</h2>
							<button class="bug-close-btn" @click="closeMailViewer" aria-label="Schliessen"><X :size="14" aria-hidden="true" /></button>
						</div>
						<div class="mail-viewer-meta">
							<div><strong>Von:</strong> {{ viewingMail.sender_email }}</div>
							<div><strong>An:</strong> {{ viewingMail.to_emails.join(', ') }}</div>
							<div v-if="viewingMail.cc_emails?.length"><strong>CC:</strong> {{ viewingMail.cc_emails.join(', ') }}</div>
							<div><strong>Betreff:</strong> {{ viewingMail.subject }}</div>
							<div><strong>Gesendet:</strong> {{ formatSentDate(viewingMail.sent_at) }}</div>
						</div>
						<div class="mail-viewer-body" v-html="viewingMail.body_html"></div>
						<div class="mail-viewer-actions">
							<button class="btn-secondary" @click="closeMailViewer">Schliessen</button>
						</div>
					</div>
				</div>
			</Teleport>

			<!-- Compose form -->
			<form class="detail-form" @submit.prevent="handleSend">
			<!-- Activity info -->
			<div class="mail-activity-info">
				<DepartmentBadge :department="activity.department" />
				<strong>{{ activity.title }}</strong>
			</div>

			<!-- Form link -->
			<div v-if="hasForm" class="form-link-section">
				<label>Formular-Link</label>
				<div class="form-link-row">
					<input type="text" :value="formUrl" readonly class="form-link-input" />
					<button type="button" class="btn-secondary btn-copy" @click="copyFormLink">
						<Check v-if="linkCopied" class="btn-icon" :size="14" aria-hidden="true" />
						<Clipboard v-else class="btn-icon" :size="14" aria-hidden="true" />
						{{ linkCopied ? 'Kopiert' : 'Kopieren' }}
					</button>
				</div>
			</div>

			<!-- From (read-only) -->
			<div class="form-group">
				<label>Absender</label>
				<input
					type="email"
					:value="user?.email ?? ''"
					disabled
					class="mail-from"
				/>
			</div>

			<!-- Recipients -->
			<div class="form-group user-search-group">
				<label>Empfänger <span class="required">*</span> <span v-if="savedFields['recipients']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['recipients']"><Save :size="12" aria-hidden="true" /></span></label>
				<div class="user-chips" v-if="recipients.filter(r => r).length">
					<span v-for="(email, i) in recipients" :key="email || i" class="user-chip" v-show="email">
						{{ email }}
						<button type="button" class="user-chip-remove" @click="removeRecipient(i)" aria-label="Empfänger entfernen"><X :size="12" aria-hidden="true" /></button>
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
			<div class="form-group user-search-group">
				<label>CC <span v-if="savedFields['cc']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['cc']"><Save :size="12" aria-hidden="true" /></span></label>
				<div class="user-chips" v-if="cc.filter(r => r).length">
					<span v-for="(email, i) in cc" :key="email || i" class="user-chip" v-show="email">
						{{ email }}
						<button type="button" class="user-chip-remove" @click="removeCc(i)" aria-label="CC entfernen"><X :size="12" aria-hidden="true" /></button>
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

			<!-- Subject -->
			<div class="form-group">
				<label>Betreff <span class="required">*</span> <span v-if="savedFields['subject']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['subject']"><Save :size="12" aria-hidden="true" /></span></label>
				<input v-model="subject" type="text" required @input="markDirty('subject')" />
			</div>

			<!-- Body -->
			<div class="form-group">
				<label>Nachricht <span class="required">*</span> <span v-if="savedFields['body']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['body']"><Save :size="12" aria-hidden="true" /></span></label>
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
					contenteditable="true"
					@input="onEditorInput"
					@mouseup="updateToolbarState"
					@keyup="updateToolbarState"
					data-placeholder="Nachricht…"
				></div>
			</div>

			<ErrorAlert :error="error" />

			<div class="form-actions">
				<button type="button" class="btn-secondary" @click="cancelAndRevert">
					Abbrechen
				</button>
				<button type="submit" class="btn-primary" :disabled="sending">
					<Send v-if="!sending" class="btn-icon" :size="16" aria-hidden="true" />
					{{ sending ? 'Senden...' : 'Senden' }}
				</button>
			</div>
		</form>
		</template>

		<p v-else class="loading">Laden...</p>
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
