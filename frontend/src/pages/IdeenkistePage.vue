<script setup lang="ts">
import { ref, computed, onMounted, nextTick } from 'vue'
import { Plus, Pencil, Trash2, X, Check, Search } from 'lucide-vue-next'
import { useRouter } from 'vue-router'
import { user } from '../composables/useAuth'
import { usePermissions } from '../composables/usePermissions'
import { useIdeenkiste } from '../composables/useIdeenkiste'
import DepartmentBadge from '../components/DepartmentBadge.vue'
import BadgeSelect from '../components/BadgeSelect.vue'
import ErrorAlert from '../components/ErrorAlert.vue'
import type { IdeenkisteItem } from '../types'

const router = useRouter()
const { myPermissions, fetchMyPermissions, departments, fetchDepartments, canIdeenkisteAdd, canIdeenkisteDelete } = usePermissions()
const { items, loading, error, fetchItems, createItem, updateItem, deleteItem } = useIdeenkiste()

const isOwnDeptOnly = computed(() => myPermissions.value?.ideenkiste_scope === 'own_dept')
const canAll = computed(() => myPermissions.value?.ideenkiste_scope === 'all')
const canEdit = computed(() => canIdeenkisteAdd())
const canDelete = computed(() => canIdeenkisteDelete())

const searchQuery = ref('')
const filterDept = ref<string | null>(null)
const deptItems = computed(() => departments.value.map(d => ({ value: d.name })))

const filtered = computed(() => {
  let list = items.value
  if (filterDept.value)
    list = list.filter(i => i.department === filterDept.value)
  if (searchQuery.value.trim()) {
    const q = searchQuery.value.toLowerCase()
    list = list.filter(i => i.title.toLowerCase().includes(q) || i.description.toLowerCase().includes(q))
  }
  return list
})

// New item form
const showNewForm = ref(false)
const newTitle = ref('')
const newDuration = ref(0)
const newDescription = ref('')
const newDepartment = ref<string | null>(null)
const saving = ref(false)
const saveError = ref<string | null>(null)

// Edit inline
const editingId = ref<string | null>(null)
const editTitle = ref('')
const editDuration = ref(0)
const editDescription = ref('')
const editDepartment = ref<string | null>(null)
const editSaving = ref(false)
const editError = ref<string | null>(null)
const editDescEditorRef = ref<HTMLElement | null>(null)
const editDescToolbar = ref<{
  bold: boolean
  italic: boolean
  underline: boolean
  ul: boolean
  ol: boolean
  font: string
  size: string
  color: string
  bgColor: string
} | null>(null)
let editDescSavedSelection: Range | null = null
let editDescLinkSavedRange: Range | null = null
const showEditDescLinkDialog = ref(false)
const editDescLinkUrl = ref('')
const editDescLinkInputRef = ref<HTMLInputElement | null>(null)

function setEditDescEditorRef(refEl: unknown) {
  const el = refEl instanceof Element
    ? refEl
    : (refEl && typeof refEl === 'object' && '$el' in refEl && (refEl as { $el?: unknown }).$el instanceof Element
      ? (refEl as { $el: Element }).$el
      : null)
  editDescEditorRef.value = el as HTMLElement | null
  if (editDescEditorRef.value) {
    editDescEditorRef.value.innerHTML = editDescription.value
  }
}

// Delete confirm
const deleteTarget = ref<IdeenkisteItem | null>(null)
const deleteError = ref<string | null>(null)

function openEdit(item: IdeenkisteItem) {
  if (!canEditItem(item)) return
  editingId.value = item.id
  editTitle.value = item.title
  editDuration.value = item.duration_minutes
  editDescription.value = item.description
  editDepartment.value = item.department
  editError.value = null
  nextTick(() => {
    syncEditDescEditor()
  })
}

function cancelEdit() {
  editingId.value = null
  showEditDescLinkDialog.value = false
}

async function saveEdit() {
  if (!editingId.value || !canEdit.value) return
  onEditDescInput()
  editSaving.value = true
  editError.value = null
  const result = await updateItem(editingId.value, {
    title: editTitle.value.trim(),
    duration_minutes: editDuration.value,
    description: editDescription.value,
    department: editDepartment.value,
  })
  editSaving.value = false
  if (result) editingId.value = null
  else editError.value = 'Fehler beim Speichern.'
}

async function submitNew() {
  saving.value = true
  saveError.value = null
  const result = await createItem({
    title: newTitle.value.trim(),
    duration_minutes: newDuration.value,
    description: newDescription.value,
    department: newDepartment.value,
  })
  saving.value = false
  if (result) {
    showNewForm.value = false
    newTitle.value = ''
    newDuration.value = 0
    newDescription.value = ''
    newDepartment.value = null
  } else {
    saveError.value = 'Fehler beim Erstellen.'
  }
}

async function confirmDelete() {
  if (!deleteTarget.value || !canDeleteItem(deleteTarget.value)) return
  deleteError.value = null
  const ok = await deleteItem(deleteTarget.value.id)
  if (ok) deleteTarget.value = null
  else deleteError.value = 'Fehler beim Löschen.'
}

function canEditItem(item: IdeenkisteItem): boolean {
  const scope = myPermissions.value?.ideenkiste_add_scope
  if (!scope || scope === 'none') return false
  if (scope === 'all') return true
  if (scope === 'own_dept') {
    return !!user.value?.department && item.department === user.value.department
  }
  return false
}

function canDeleteItem(item: IdeenkisteItem): boolean {
  const scope = myPermissions.value?.ideenkiste_delete_scope
  if (!scope || scope === 'none') return false
  if (scope === 'all') return true
  if (scope === 'own_dept') {
    return !!user.value?.department && item.department === user.value.department
  }
  return false
}

function rgbToHex(rgb: string): string {
  const m = rgb.match(/^rgb\((\d+),\s*(\d+),\s*(\d+)\)$/)
  if (!m) return rgb
  return '#' + [m[1], m[2], m[3]].map(x => parseInt(x, 10).toString(16).padStart(2, '0')).join('')
}

function syncEditDescEditor() {
  const el = editDescEditorRef.value
  if (!el) return
  if (el.innerHTML !== editDescription.value) {
    el.innerHTML = editDescription.value
  }
}

function onEditDescInput() {
  const el = editDescEditorRef.value
  if (el) editDescription.value = el.innerHTML
  updateEditDescToolbar()
}

function updateEditDescToolbar() {
  const el = editDescEditorRef.value
  const sel = window.getSelection()
  if (!sel || !sel.rangeCount || !el) return
  const node = sel.anchorNode?.nodeType === 3 ? sel.anchorNode.parentElement : sel.anchorNode as HTMLElement
  if (!node || !el.contains(node)) return
  const cs = window.getComputedStyle(node)
  editDescToolbar.value = {
    bold: document.queryCommandState('bold'),
    italic: document.queryCommandState('italic'),
    underline: document.queryCommandState('underline'),
    ul: document.queryCommandState('insertUnorderedList'),
    ol: document.queryCommandState('insertOrderedList'),
    font: cs.fontFamily.replace(/["']/g, '').split(',')[0].trim() || 'Arial',
    size: parseInt(cs.fontSize, 10).toString() || '12',
    color: rgbToHex(cs.color) || '#000000',
    bgColor: (cs.backgroundColor === 'rgba(0, 0, 0, 0)' || cs.backgroundColor === 'transparent') ? '#ffffff' : (rgbToHex(cs.backgroundColor) || '#ffffff'),
  }
}

function onEditDescFocus() {
  editDescToolbar.value = {
    bold: false,
    italic: false,
    underline: false,
    ul: false,
    ol: false,
    font: 'Arial',
    size: '12',
    color: '#000000',
    bgColor: '#ffffff',
  }
  updateEditDescToolbar()
}

function editDescSaveSelection() {
  const sel = window.getSelection()
  const el = editDescEditorRef.value
  if (sel?.rangeCount && el?.contains(sel.anchorNode)) {
    editDescSavedSelection = sel.getRangeAt(0).cloneRange()
  }
}

function editDescExecCmd(cmd: string, value?: string) {
  if (editDescSavedSelection) {
    const sel = window.getSelection()
    if (sel) {
      sel.removeAllRanges()
      sel.addRange(editDescSavedSelection)
    }
    editDescSavedSelection = null
  }
  document.execCommand(cmd, false, value)
  editDescEditorRef.value?.focus()
  onEditDescInput()
}

function editDescSetFontSize(size: string) {
  if (editDescSavedSelection) {
    const sel = window.getSelection()
    if (sel) {
      sel.removeAllRanges()
      sel.addRange(editDescSavedSelection)
    }
    editDescSavedSelection = null
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
    editDescEditorRef.value?.focus()
    if (editDescToolbar.value) editDescToolbar.value.size = size
    onEditDescInput()
    return
  }
  document.execCommand('fontSize', false, '7')
  const fontEls = editDescEditorRef.value?.querySelectorAll('font[size="7"]')
  fontEls?.forEach(fe => {
    const span = document.createElement('span')
    span.style.fontSize = size + 'px'
    span.innerHTML = fe.innerHTML
    fe.replaceWith(span)
  })
  editDescEditorRef.value?.focus()
  onEditDescInput()
}

function editDescAdjustFontSize(delta: number) {
  const current = parseInt(editDescToolbar.value?.size ?? '12', 10) || 12
  const newSize = Math.max(8, Math.min(72, current + delta))
  editDescSetFontSize(String(newSize))
  if (editDescToolbar.value) editDescToolbar.value.size = String(newSize)
}

function openEditDescLinkDialog() {
  editDescLinkSavedRange = editDescSavedSelection
  editDescSavedSelection = null
  const sel = window.getSelection()
  const anchor = sel?.focusNode?.parentElement?.closest('a') as HTMLAnchorElement | null
  editDescLinkUrl.value = anchor?.getAttribute('href') ?? ''
  showEditDescLinkDialog.value = true
  nextTick(() => editDescLinkInputRef.value?.focus())
}

function confirmEditDescLink() {
  showEditDescLinkDialog.value = false
  if (!editDescLinkSavedRange) return
  const sel = window.getSelection()
  if (sel) {
    sel.removeAllRanges()
    sel.addRange(editDescLinkSavedRange)
  }
  if (editDescLinkUrl.value.trim()) {
    document.execCommand('createLink', false, editDescLinkUrl.value.trim())
  } else {
    document.execCommand('unlink')
  }
  editDescEditorRef.value?.focus()
  onEditDescInput()
  editDescLinkSavedRange = null
}

function cancelEditDescLink() {
  showEditDescLinkDialog.value = false
  editDescLinkSavedRange = null
}

function formatDuration(minutes: number): string {
  if (minutes === 0) return '–'
  if (minutes < 60) return `${minutes} min`
  const h = Math.floor(minutes / 60)
  const m = minutes % 60
  return m === 0 ? `${h} h` : `${h} h ${m} min`
}

onMounted(async () => {
  await fetchMyPermissions()
  if (!myPermissions.value || myPermissions.value.ideenkiste_scope === 'none') {
    router.replace('/')
    return
  }
  await Promise.all([fetchItems(), fetchDepartments()])
  if (isOwnDeptOnly.value && myPermissions.value) {
    // pre-set filter/department to own dept
  }
})
</script>

<template>
  <header class="header">
    <h1>Ideenkiste</h1>
  </header>

  <main class="main">
    <div class="filter-bar">
      <div class="filter-search">
        <span class="filter-search-icon"><Search :size="16" aria-hidden="true" /></span>
        <input
          v-model="searchQuery"
          type="search"
          class="filter-search-input"
          placeholder="Suchen nach Titel, Beschreibung…"
        />
      </div>

      <div v-if="canAll" class="filter-tabs">
        <button
          class="filter-tab"
          :class="{ 'filter-tab--active': filterDept === null }"
          @click="filterDept = null"
        >Alle</button>
        <button
          v-for="dep in departments"
          :key="dep.name"
          class="filter-tab filter-tab--badge"
          @click="filterDept = dep.name"
        >
          <DepartmentBadge :department="dep.name" :active="filterDept === dep.name" />
        </button>
      </div>

      <div class="ideenkiste-filter-actions">
        <button v-if="canEdit" class="btn-primary ideenkiste-add-btn" @click="showNewForm = true">
          <Plus :size="16" aria-hidden="true" />
          Neuer Eintrag
        </button>
      </div>
    </div>

    <!-- New item form -->
    <div v-if="showNewForm" class="ideenkiste-form-card">
      <h3 class="ideenkiste-form-title">Neuer Eintrag</h3>
      <form @submit.prevent="submitNew" class="ideenkiste-form">
        <div class="form-group">
          <label class="form-label">Titel *</label>
          <input v-model="newTitle" class="form-input" required placeholder="Titel" />
        </div>
        <div class="form-group">
          <label class="form-label">Dauer (Minuten)</label>
          <input v-model.number="newDuration" type="number" min="0" class="form-input" />
        </div>
        <div v-if="canAll" class="form-group">
          <label class="form-label">Stufe</label>
          <BadgeSelect
            kind="department"
            :items="deptItems"
            allow-empty
            placeholder="Keine Angabe"
            :model-value="newDepartment"
            @update:model-value="(v) => newDepartment = v"
          />
        </div>
        <div class="form-group">
          <label class="form-label">Beschreibung</label>
          <textarea v-model="newDescription" class="form-input ideenkiste-textarea" rows="4" placeholder="Beschreibung…" />
        </div>
        <div v-if="saveError" class="error-msg"><ErrorAlert :error="saveError" /></div>
        <div class="ideenkiste-form-actions">
          <button type="button" class="btn-cancel" @click="showNewForm = false">Abbrechen</button>
          <button type="submit" class="btn-primary" :disabled="saving || !newTitle.trim()">
            {{ saving ? 'Speichern…' : 'Erstellen' }}
          </button>
        </div>
      </form>
    </div>

    <!-- Loading / error -->
    <div v-if="loading" class="loading">Lade Ideenkiste…</div>
    <div v-else-if="error" class="error-msg"><ErrorAlert :error="error" /></div>

    <!-- Item list -->
    <template v-else>
      <p v-if="filtered.length === 0" class="ideenkiste-empty">Keine Einträge gefunden.</p>
      <div v-else class="ideenkiste-list">
        <div v-for="item in filtered" :key="item.id" class="ideenkiste-list-item">
          <!-- Edit mode for this item: program-card (same as Activity edit) -->
          <div v-if="editingId === item.id" class="program-card">
            <form @submit.prevent="saveEdit">
              <div class="program-card__fields">
                <div class="form-group">
                  <label>Dauer (Minuten)</label>
                  <input v-model.number="editDuration" type="number" min="0" step="5" placeholder="z.B. 30" />
                </div>
                <div class="form-group">
                  <label>Titel</label>
                  <input v-model="editTitle" type="text" required placeholder="Titel" />
                </div>
                <div v-if="canAll" class="form-group">
                  <label>Stufe</label>
                  <BadgeSelect
                    kind="department"
                    :items="deptItems"
                    allow-empty
                    placeholder="Keine"
                    :model-value="editDepartment"
                    @update:model-value="(v) => editDepartment = v"
                  />
                </div>
                <div class="form-group program-card__full">
                  <label>Beschreibung</label>
                  <div class="input-save-wrap">
                    <div class="rich-editor-toolbar rich-editor-toolbar--compact" v-if="editDescToolbar">
                      <select class="toolbar-select" :value="editDescToolbar.font" @change="editDescExecCmd('fontName', ($event.target as HTMLSelectElement).value)" title="Schriftart">
                        <option value="Arial" style="font-family:Arial">Arial</option>
                        <option value="Helvetica" style="font-family:Helvetica">Helvetica</option>
                        <option value="Georgia" style="font-family:Georgia">Georgia</option>
                        <option value="Times New Roman" style="font-family:'Times New Roman'">Times New Roman</option>
                        <option value="Courier New" style="font-family:'Courier New'">Courier New</option>
                        <option value="Verdana" style="font-family:Verdana">Verdana</option>
                      </select>
                      <input type="text" class="toolbar-select toolbar-select--narrow" :value="editDescToolbar.size" @mousedown="editDescSaveSelection" @change="editDescSetFontSize(($event.target as HTMLInputElement).value)" @keydown.enter.prevent="editDescSetFontSize(($event.target as HTMLInputElement).value)" title="Schriftgrösse" />
                      <button type="button" class="toolbar-btn-sm" @mousedown.prevent @click="editDescAdjustFontSize(-2)" title="Kleiner">A−</button>
                      <button type="button" class="toolbar-btn-sm" @mousedown.prevent @click="editDescAdjustFontSize(2)" title="Grösser">A+</button>
                      <span class="toolbar-sep"></span>
                      <button type="button" :class="{ 'toolbar-btn--active': editDescToolbar.bold }" @mousedown.prevent @click="editDescExecCmd('bold')" title="Fett"><b>B</b></button>
                      <button type="button" :class="{ 'toolbar-btn--active': editDescToolbar.italic }" @mousedown.prevent @click="editDescExecCmd('italic')" title="Kursiv"><i>I</i></button>
                      <button type="button" :class="{ 'toolbar-btn--active': editDescToolbar.underline }" @mousedown.prevent @click="editDescExecCmd('underline')" title="Unterstrichen"><u>U</u></button>
                      <span class="toolbar-sep"></span>
                      <label class="toolbar-color" title="Schriftfarbe" @mousedown="editDescSaveSelection">
                        A
                        <input type="color" :value="editDescToolbar.color" @input="editDescExecCmd('foreColor', ($event.target as HTMLInputElement).value)" />
                      </label>
                      <label class="toolbar-color toolbar-color--bg" title="Hintergrundfarbe" @mousedown="editDescSaveSelection">
                        <span class="toolbar-color-icon">A</span>
                        <input type="color" :value="editDescToolbar.bgColor" @input="editDescExecCmd('hiliteColor', ($event.target as HTMLInputElement).value)" />
                      </label>
                      <span class="toolbar-sep"></span>
                      <button type="button" :class="{ 'toolbar-btn--active': editDescToolbar.ul }" @mousedown.prevent @click="editDescExecCmd('insertUnorderedList')" title="Aufzählung">• Liste</button>
                      <button type="button" :class="{ 'toolbar-btn--active': editDescToolbar.ol }" @mousedown.prevent @click="editDescExecCmd('insertOrderedList')" title="Nummerierte Liste">1. Liste</button>
                      <span class="toolbar-sep"></span>
                      <button type="button" @mousedown.prevent @click="editDescExecCmd('removeFormat')" title="Formatierung entfernen"><X :size="12" aria-hidden="true" /> Format</button>
                      <span class="toolbar-sep"></span>
                      <button type="button" @mousedown="editDescSaveSelection" @click="openEditDescLinkDialog" title="Link einfügen">🔗 Link</button>
                    </div>
                    <div
                      :ref="setEditDescEditorRef"
                      class="rich-editor rich-editor--compact"
                      contenteditable="true"
                      @input="onEditDescInput"
                      @focus="onEditDescFocus"
                      @mouseup="updateEditDescToolbar"
                      @keyup="updateEditDescToolbar"
                      data-placeholder="Beschreibung…"
                    ></div>
                  </div>
                </div>
              </div>
              <div v-if="editError" class="error-msg"><ErrorAlert :error="editError" /></div>
              <div class="ideenkiste-edit-actions">
                <button type="button" class="btn-cancel" @click="cancelEdit">
                  <X :size="14" aria-hidden="true" /> Abbrechen
                </button>
                <button type="submit" class="btn-primary" :disabled="editSaving">
                  <Check :size="14" aria-hidden="true" /> {{ editSaving ? 'Speichern…' : 'Speichern' }}
                </button>
              </div>
            </form>
          </div>

          <!-- View mode: program-body (same as Activity view) -->
          <div v-else class="program-body ideenkiste-program-body">
            <div v-if="canEditItem(item) || canDeleteItem(item)" class="program-card__top-actions">
              <button
                v-if="canEditItem(item)"
                type="button"
                class="program-card__save-idee"
                @click="openEdit(item)"
                title="Bearbeiten"
              >
                <Pencil :size="14" aria-hidden="true" />
              </button>
              <button
                v-if="canDeleteItem(item)"
                type="button"
                class="program-card__remove"
                @click="deleteTarget = item"
                title="Löschen"
              >
                <Trash2 :size="14" aria-hidden="true" />
              </button>
            </div>
            <div class="program-meta">
              <span v-if="item.duration_minutes" class="program-time">{{ formatDuration(item.duration_minutes) }}</span>
              <DepartmentBadge v-if="item.department" :department="item.department" />
            </div>
            <div class="program-header">
              <p class="program-title">{{ item.title }}</p>
            </div>
            <div v-if="item.description" class="program-desc" v-html="item.description" />
          </div>
        </div>
      </div>
    </template>
  </main>

  <!-- Delete confirm modal -->
  <div v-if="deleteTarget" class="modal-backdrop" @click.self="deleteTarget = null">
    <div class="modal">
      <h2 class="modal-title">Eintrag löschen?</h2>
      <p>«{{ deleteTarget.title }}» wird unwiderruflich gelöscht.</p>
      <div v-if="deleteError" class="error-msg"><ErrorAlert :error="deleteError" /></div>
      <div class="modal-actions">
        <button class="btn-cancel" @click="deleteTarget = null">Abbrechen</button>
        <button class="btn-danger" @click="confirmDelete">Löschen</button>
      </div>
    </div>
  </div>

  <div v-if="showEditDescLinkDialog" class="modal-backdrop" @click.self="cancelEditDescLink">
    <div class="modal">
      <h2 class="modal-title">Link einfügen</h2>
      <div class="form-group">
        <label class="form-label">URL</label>
        <input
          ref="editDescLinkInputRef"
          v-model="editDescLinkUrl"
          class="form-input"
          type="url"
          placeholder="https://..."
          @keydown.enter.prevent="confirmEditDescLink"
        />
      </div>
      <div class="modal-actions">
        <button class="btn-cancel" @click="cancelEditDescLink">Abbrechen</button>
        <button class="btn-primary" @click="confirmEditDescLink">Übernehmen</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.ideenkiste-filter-actions {
  display: flex;
  justify-content: flex-end;
}

.ideenkiste-add-btn {
  display: flex;
  align-items: center;
  gap: 0.4rem;
}

.ideenkiste-form-card {
  background: var(--color-surface);
  border: 1px solid var(--color-border);
  border-radius: 8px;
  padding: 1.25rem;
  margin-bottom: 1.5rem;
}

.ideenkiste-form-title {
  margin: 0 0 1rem;
  font-size: 1rem;
  font-weight: 600;
}

.ideenkiste-form {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.ideenkiste-form-actions {
  display: flex;
  gap: 0.5rem;
  justify-content: flex-end;
  margin-top: 0.25rem;
}

.ideenkiste-empty {
  color: var(--color-text-muted);
  text-align: center;
  padding: 2rem 0;
}

.ideenkiste-list {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.ideenkiste-textarea {
  resize: vertical;
  font-family: inherit;
  width: 100%;
}

.ideenkiste-program-body {
  position: relative;
}

.ideenkiste-program-body .program-meta {
  padding-right: 68px;
}

.ideenkiste-edit-actions {
  display: flex;
  gap: 0.5rem;
  justify-content: flex-end;
  margin-top: 0.75rem;
}
</style>
