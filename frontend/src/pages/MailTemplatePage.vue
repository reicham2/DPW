<script setup lang="ts">
import { ref, computed, onMounted, nextTick } from 'vue'
import { useMailTemplates } from '../composables/useMailTemplates'
import { user } from '../composables/useAuth'
import type { Department } from '../types'

const ALL_DEPARTMENTS: Department[] = ['Leiter', 'Pio', 'Pfadi', 'Wölfe', 'Biber']

const TEMPLATE_VARIABLES = [
  { var: '{{titel}}',            desc: 'Titel der Aktivität' },
  { var: '{{datum}}',            desc: 'Datum lang (z.B. Samstag, 12. April 2026)' },
  { var: '{{datum_kurz}}',       desc: 'Datum kurz (z.B. 12.04.2026)' },
  { var: '{{startzeit}}',        desc: 'Startzeit (HH:MM)' },
  { var: '{{endzeit}}',          desc: 'Endzeit (HH:MM)' },
  { var: '{{ort}}',              desc: 'Veranstaltungsort' },
  { var: '{{verantwortlich}}',   desc: 'Verantwortliche Person' },
  { var: '{{abteilung}}',        desc: 'Abteilung / Stufe' },
  { var: '{{ziel}}',             desc: 'Ziel der Aktivität' },
  { var: '{{material}}',         desc: 'Materialliste (kommagetrennt)' },
  { var: '{{schlechtwetter}}',   desc: 'Schlechtwetter-Info' },
  { var: '{{programm}}',         desc: 'Programmpunkte (formatiert)' },
  { var: '{{absender_email}}',   desc: 'E-Mail-Adresse des Absenders' },
  { var: '{{absender_name}}',    desc: 'Voller Name des Absenders (z.B. Leandro Klaus v/o Topo)' },
]

const { fetchTemplates, saveTemplate, templates, loading, error } = useMailTemplates()

const isAdmin        = computed(() => user.value?.role === 'admin')
const isStufenleiter = computed(() => user.value?.role === 'Stufenleiter')

const visibleDepartments = computed<Department[]>(() => {
  if (isStufenleiter.value) {
    const own = user.value?.department as Department | undefined
    return own ? ALL_DEPARTMENTS.filter(d => d === own) : []
  }
  return ALL_DEPARTMENTS
})

const activeDept = ref<Department>(
  isStufenleiter.value && user.value?.department
    ? user.value.department as Department
    : 'Pfadi'
)

const subject = ref('')
const body = ref('')
const recipients = ref<string[]>([''])
const saving = ref(false)
const saved = ref(false)
const showVars = ref(false)
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

onMounted(async () => {
  await fetchTemplates()
  loadDept(activeDept.value)
})

function loadDept(dept: Department) {
  activeDept.value = dept
  saved.value = false
  const tpl = templates.value.find(t => t.department === dept)
  subject.value = tpl?.subject ?? ''
  body.value    = tpl?.body ?? ''
  recipients.value = tpl?.recipients?.length ? [...tpl.recipients] : ['']
  nextTick(() => {
    if (editorRef.value) editorRef.value.innerHTML = body.value
  })
}

function onEditorInput() {
  if (editorRef.value) body.value = editorRef.value.innerHTML
  updateToolbarState()
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
  const container = editorRef.value
  const fullText = container.textContent ?? ''
  // Map range start/end to text offsets, then expand if inside a {{...}}
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
  // Expand start backwards if inside a variable
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
    const sel = window.getSelection()
    if (sel) {
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
        sel.removeAllRanges()
        sel.addRange(range)
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

function addRecipient() {
  recipients.value.push('')
}

function removeRecipient(index: number) {
  recipients.value.splice(index, 1)
}

async function handleSave() {
  saving.value = true
  saved.value  = false
  const validRecipients = recipients.value.map(r => r.trim()).filter(Boolean)
  const result = await saveTemplate(activeDept.value, subject.value, body.value, validRecipients)
  saving.value = false
  if (result) {
    saved.value = true
    const idx = templates.value.findIndex(t => t.department === activeDept.value)
    if (idx >= 0) templates.value[idx] = result
    else templates.value.push(result)
  }
}
</script>

<template>
  <nav class="page-tabs">
    <router-link to="/" class="page-tab">Aktivitäten</router-link>
    <router-link to="/mail-templates" class="page-tab page-tab--active">Mail-Vorlagen</router-link>
    <router-link v-if="isAdmin || isStufenleiter" to="/admin" class="page-tab">Admin</router-link>
  </nav>

  <header class="header">
    <h1>Mail-Vorlagen</h1>
  </header>

  <main class="main">
    <!-- Department tabs -->
    <div class="filter-tabs" style="margin-bottom: 24px">
      <button
        v-for="dept in visibleDepartments"
        :key="dept"
        class="filter-tab"
        :class="{ 'filter-tab--active': activeDept === dept }"
        @click="loadDept(dept)"
      >{{ dept }}</button>
    </div>

    <p v-if="loading" class="loading">Laden...</p>

    <form v-else class="detail-form" @submit.prevent="handleSave">
      <!-- Recipients -->
      <div class="form-group">
        <label>Empfänger</label>
        <div class="mail-recipients">
          <div v-for="(_, i) in recipients" :key="i" class="mail-recipient-row">
            <input
              v-model="recipients[i]"
              type="email"
              placeholder="email@example.com"
            />
            <button
              v-if="recipients.length > 1"
              type="button"
              class="btn-remove-sm"
              @click="removeRecipient(i)"
            >×</button>
          </div>
          <button type="button" class="btn-add" @click="addRecipient">+ Empfänger</button>
        </div>
      </div>

      <div class="form-group">
        <label>Betreff-Vorlage</label>
        <input v-model="subject" type="text" placeholder="Betreff…" />
      </div>

      <div class="form-group">
        <label>Nachricht-Vorlage</label>
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
          <input type="text" class="toolbar-select toolbar-select--narrow" :value="curSize" @change="setFontSize(($event.target as HTMLInputElement).value)" @keydown.enter.prevent="setFontSize(($event.target as HTMLInputElement).value)" title="Schriftgrösse" />
          <button type="button" class="toolbar-btn-sm" @click="adjustFontSize(-2)" title="Kleiner">A−</button>
          <button type="button" class="toolbar-btn-sm" @click="adjustFontSize(2)" title="Grösser">A+</button>
          <span class="toolbar-sep"></span>
          <button type="button" :class="{'toolbar-btn--active': curBold}" @click="execCmd('bold')" title="Fett"><b>B</b></button>
          <button type="button" :class="{'toolbar-btn--active': curItalic}" @click="execCmd('italic')" title="Kursiv"><i>I</i></button>
          <button type="button" :class="{'toolbar-btn--active': curUnderline}" @click="execCmd('underline')" title="Unterstrichen"><u>U</u></button>
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
          <button type="button" :class="{'toolbar-btn--active': curUl}" @click="execCmd('insertUnorderedList')" title="Aufzählung">• Liste</button>
          <button type="button" :class="{'toolbar-btn--active': curOl}" @click="execCmd('insertOrderedList')" title="Nummerierte Liste">1. Liste</button>
          <span class="toolbar-sep"></span>
          <button type="button" @click="execCmd('removeFormat')" title="Formatierung entfernen">✕ Format</button>
        </div>
        <div
          ref="editorRef"
          class="rich-editor"
          contenteditable="true"
          @input="onEditorInput"
          @mouseup="updateToolbarState"
          @keyup="updateToolbarState"
          data-placeholder="Mail-Text…"
        ></div>
      </div>

      <!-- Variable reference -->
      <div class="tpl-vars">
        <button type="button" class="tpl-vars-toggle" @click="showVars = !showVars">
          {{ showVars ? '▾' : '▸' }} Verfügbare Variablen
        </button>
        <div v-if="showVars" class="tpl-vars-list">
          <div v-for="v in TEMPLATE_VARIABLES" :key="v.var" class="tpl-var-row">
            <code class="tpl-var-code">{{ v.var }}</code>
            <span class="tpl-var-desc">{{ v.desc }}</span>
          </div>
        </div>
      </div>

      <p v-if="error" class="error">{{ error }}</p>

      <div class="form-actions">
        <span v-if="saved" class="mail-saved-badge">✅ Gespeichert</span>
        <div class="form-actions-right">
          <button type="submit" class="btn-primary" :disabled="saving">
            {{ saving ? 'Speichern...' : 'Vorlage speichern' }}
          </button>
        </div>
      </div>
    </form>
  </main>
</template>
