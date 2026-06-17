<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import { X, Plus, Trash2, BookOpen, Users, Package, AlignLeft, ListChecks } from 'lucide-vue-next'
import { useUsers } from '../composables/useUsers'
import { useUserResolver } from '../composables/useUserResolver'
import type {
  CampActivity,
  CampActivityInput,
  CampCategory,
  CampCollaboration,
  CampPeriod,
  ContentNodeInput,
  ContentNodeType,
  ProgramInput,
} from '../types'

const { users, fetchUsers } = useUsers()
const { resolveResponsibleName } = useUserResolver()
onMounted(() => { fetchUsers() })

const props = defineProps<{
  activity: CampActivity | null // null = create mode
  categories: CampCategory[]
  collaborations: CampCollaboration[]
  periods: CampPeriod[]
  defaultPeriodId: string
  // When set (calendar drag-create), prefills period + time on a new activity.
  prefillSchedule?: { period_id: string; period_offset: number; length: number } | null
  // inline = render as a side panel (eCamp split view) instead of a modal overlay.
  inline?: boolean
}>()

const emit = defineEmits<{
  (e: 'save', input: CampActivityInput): void
  (e: 'delete', id: string): void
  (e: 'close'): void
}>()

// ── Form state ────────────────────────────────────────────────────────────
const title = ref('')
const location = ref('')
const categoryId = ref<string | null>(null)
const responsibleIds = ref<string[]>([])      // collaboration ids (RF-Liste)
const responsible = ref<string[]>([])          // user ids / free-text, like Activity
const programs = ref<ProgramInput[]>([])       // Programmpunkte, same as activity

// Schedule (first entry only in editor; calendar handles multi-placement)
const periodId = ref('')
const dayOffset = ref(0)        // which day of the period (0-based)
const startMinutes = ref(540)   // minute-of-day, 09:00
const length = ref(60)

// Content widgets (flat list under an implicit root column)
interface WidgetDraft {
  key: string
  id?: string
  content_type: ContentNodeType
  instance_name: string
  // SingleText
  html?: string
  // Storyboard
  sections?: { id: string; column1: string; column2: string }[]
  // MultiSelect / Checklist
  options?: { id: string; label: string; checked: boolean }[]
}

const widgets = ref<WidgetDraft[]>([])

let keySeq = 0
function nextKey() {
  keySeq += 1
  return `w${keySeq}`
}

function loadFromActivity(a: CampActivity | null) {
  if (!a) {
    title.value = ''
    location.value = ''
    categoryId.value = props.categories[0]?.id ?? null
    responsibleIds.value = []
    responsible.value = []
    programs.value = []
    if (props.prefillSchedule) {
      periodId.value = props.prefillSchedule.period_id
      dayOffset.value = Math.floor(props.prefillSchedule.period_offset / (24 * 60))
      startMinutes.value = props.prefillSchedule.period_offset - dayOffset.value * 24 * 60
      length.value = props.prefillSchedule.length
    } else {
      periodId.value = props.defaultPeriodId
      dayOffset.value = 0
      startMinutes.value = 540
      length.value = 60
    }
    widgets.value = []
    return
  }
  title.value = a.title
  location.value = a.location
  categoryId.value = a.category_id
  responsibleIds.value = [...a.responsible_collaboration_ids]
  responsible.value = [...(a.responsible ?? [])]
  programs.value = (a.programs ?? []).map((p) => ({
    duration_minutes: p.duration_minutes,
    title: p.title,
    description: p.description,
    responsible: [...p.responsible],
  }))
  const se = a.schedule_entries[0]
  periodId.value = se?.period_id ?? props.defaultPeriodId
  const off = se?.period_offset ?? 540
  dayOffset.value = Math.floor(off / (24 * 60))
  startMinutes.value = off - dayOffset.value * 24 * 60
  length.value = se?.length ?? 60

  // Flatten non-layout content nodes into widget drafts.
  widgets.value = a.content_nodes
    .filter((n) => !n.is_root && n.content_type !== 'ColumnLayout')
    .sort((x, y) => x.position - y.position)
    .map((n) => {
      const d = n.data as Record<string, unknown>
      return {
        key: nextKey(),
        id: n.id,
        content_type: n.content_type,
        instance_name: n.instance_name,
        html: typeof d.html === 'string' ? d.html : '',
        sections: Array.isArray(d.sections)
          ? (d.sections as WidgetDraft['sections'])
          : [],
        options: Array.isArray(d.options)
          ? (d.options as WidgetDraft['options'])
          : [],
      }
    })
}

watch(
  () => props.activity,
  (a) => loadFromActivity(a),
  { immediate: true },
)

// ── Time helpers ────────────────────────────────────────────────────────────
const startTime = computed({
  get: () => minutesToTime(startMinutes.value),
  set: (v: string) => { startMinutes.value = timeToMinutes(v) },
})
function minutesToTime(m: number): string {
  const h = Math.floor(m / 60)
  const mm = m % 60
  return `${String(h).padStart(2, '0')}:${String(mm).padStart(2, '0')}`
}
function timeToMinutes(t: string): number {
  const [h, m] = t.split(':').map(Number)
  return (h || 0) * 60 + (m || 0)
}

// Days available in the currently selected period (for the day picker).
const periodDayOptions = computed(() => {
  const p = props.periods.find((x) => x.id === periodId.value)
  if (!p) return [{ value: 0, label: 'Tag 1' }]
  const start = new Date(p.start_date + 'T00:00:00')
  const end = new Date(p.end_date + 'T00:00:00')
  const out: { value: number; label: string }[] = []
  let i = 0
  for (let d = new Date(start); d <= end; d.setDate(d.getDate() + 1)) {
    const wd = ['So', 'Mo', 'Di', 'Mi', 'Do', 'Fr', 'Sa'][d.getDay()]
    out.push({ value: i, label: `Tag ${i + 1} · ${wd} ${d.getDate()}.${d.getMonth() + 1}.` })
    i++
  }
  return out
})

// ── Widget management ────────────────────────────────────────────────────────
function addWidget(type: ContentNodeType) {
  const names: Record<string, string> = {
    SingleText: 'Beschreibung',
    Storyboard: 'Geschichte',
    MaterialNode: 'Material',
    MultiSelect: 'Auswahl',
    Checklist: 'Checkliste',
  }
  widgets.value.push({
    key: nextKey(),
    content_type: type,
    instance_name: names[type] ?? type,
    html: '',
    sections: type === 'Storyboard' ? [{ id: nextKey(), column1: '', column2: '' }] : [],
    options: [],
  })
}
function removeWidget(key: string) {
  widgets.value = widgets.value.filter((w) => w.key !== key)
}
function addSection(w: WidgetDraft) {
  w.sections = w.sections ?? []
  w.sections.push({ id: nextKey(), column1: '', column2: '' })
}
function removeSection(w: WidgetDraft, id: string) {
  w.sections = (w.sections ?? []).filter((s) => s.id !== id)
}
function addOption(w: WidgetDraft) {
  w.options = w.options ?? []
  w.options.push({ id: nextKey(), label: '', checked: false })
}
function removeOption(w: WidgetDraft, id: string) {
  w.options = (w.options ?? []).filter((o) => o.id !== id)
}

function toggleResponsible(id: string) {
  const idx = responsibleIds.value.indexOf(id)
  if (idx === -1) responsibleIds.value.push(id)
  else responsibleIds.value.splice(idx, 1)
}

// ── User responsible picker (same UX as activity DetailPage) ──────────────────
const responsibleSearch = ref('')
const showResponsibleDropdown = ref(false)
const filteredResponsibleUsers = computed(() => {
  const q = responsibleSearch.value.toLowerCase()
  return users.value.filter(
    (u) => !responsible.value.includes(u.id) &&
      (q === '' || u.display_name.toLowerCase().includes(q)),
  )
})
function addResponsible(id: string) {
  if (!responsible.value.includes(id)) responsible.value.push(id)
  responsibleSearch.value = ''
  showResponsibleDropdown.value = false
}
function removeResponsible(i: number) {
  responsible.value.splice(i, 1)
}
function onResponsibleBlur() {
  setTimeout(() => { showResponsibleDropdown.value = false; responsibleSearch.value = '' }, 200)
}

// ── Programmpunkte (same model/UX as activity programs) ───────────────────────
function addProgram() {
  programs.value.push({
    duration_minutes: 0,
    title: '',
    description: '',
    responsible: responsible.value.length ? [responsible.value[0]] : [],
  })
}
function removeProgram(i: number) {
  programs.value.splice(i, 1)
}
// Sequential clock label for a program, based on the activity start time and
// the cumulative duration of preceding programs (mirrors activity behaviour).
function pad2(n: number): string { return String(n).padStart(2, '0') }
function clockFromMinutes(total: number): string {
  const t = ((total % 1440) + 1440) % 1440
  return `${pad2(Math.floor(t / 60))}:${pad2(t % 60)}`
}
function programTimeLabel(i: number): string {
  let acc = 0
  for (let k = 0; k < i; k++) acc += Number(programs.value[k].duration_minutes) || 0
  const start = startMinutes.value + acc
  const end = start + (Number(programs.value[i].duration_minutes) || 0)
  return `${clockFromMinutes(start)}–${clockFromMinutes(end)}`
}
// Per-program responsible picker state.
const progRespSearch = ref<Record<number, string>>({})
const progRespDropdown = ref<number | null>(null)
function progRespFiltered(i: number) {
  const q = (progRespSearch.value[i] ?? '').toLowerCase()
  const cur = programs.value[i].responsible
  return users.value.filter((u) => !cur.includes(u.id) && (q === '' || u.display_name.toLowerCase().includes(q)))
}
function addProgResponsible(i: number, id: string) {
  if (!programs.value[i].responsible.includes(id)) programs.value[i].responsible.push(id)
  progRespSearch.value[i] = ''
  progRespDropdown.value = null
}
function addProgRespFreeText(i: number) {
  const txt = (progRespSearch.value[i] ?? '').trim()
  if (txt && !programs.value[i].responsible.includes(txt)) programs.value[i].responsible.push(txt)
  progRespSearch.value[i] = ''
  progRespDropdown.value = null
}
function removeProgResponsible(i: number, ri: number) {
  programs.value[i].responsible.splice(ri, 1)
}
function onProgRespBlur(i: number) {
  setTimeout(() => { if (progRespDropdown.value === i) progRespDropdown.value = null; progRespSearch.value[i] = '' }, 200)
}

// ── Build payload ────────────────────────────────────────────────────────────
function buildContentNodes(): ContentNodeInput[] {
  const nodes: ContentNodeInput[] = []
  // Root column layout.
  const rootKey = 'root'
  nodes.push({
    id: rootKey,
    parent_id: null,
    slot: '',
    position: 0,
    content_type: 'ColumnLayout',
    instance_name: '',
    is_root: true,
    data: { columns: [{ slot: '1', width: 12 }] },
  })
  widgets.value.forEach((w, i) => {
    let data: Record<string, unknown> = {}
    if (w.content_type === 'SingleText') data = { html: w.html ?? '' }
    else if (w.content_type === 'Storyboard') data = { sections: (w.sections ?? []).map((s, p) => ({ ...s, position: p })) }
    else if (w.content_type === 'MultiSelect' || w.content_type === 'Checklist') data = { options: w.options ?? [] }
    nodes.push({
      parent_id: rootKey,
      slot: '1',
      position: i,
      content_type: w.content_type,
      instance_name: w.instance_name,
      is_root: false,
      data,
    })
  })
  return nodes
}

function save() {
  if (!title.value.trim()) return
  const input: CampActivityInput = {
    category_id: categoryId.value,
    title: title.value,
    location: location.value,
    responsible: [...responsible.value],
    programs: programs.value.map((p) => ({
      duration_minutes: Number(p.duration_minutes) || 0,
      title: p.title,
      description: p.description,
      responsible: [...p.responsible],
    })),
    responsible_collaboration_ids: [...responsibleIds.value],
    schedule_entries: periodId.value
      ? [{
          period_id: periodId.value,
          period_offset: dayOffset.value * 24 * 60 + startMinutes.value,
          length: length.value,
          left_fraction: props.activity?.schedule_entries[0]?.left_fraction ?? 0,
          width_fraction: props.activity?.schedule_entries[0]?.width_fraction ?? 1,
        }]
      : [],
    content_nodes: buildContentNodes(),
  }
  emit('save', input)
}

const widgetIcon = (t: ContentNodeType) =>
  t === 'Storyboard' ? BookOpen
    : t === 'MaterialNode' ? Package
    : t === 'Checklist' ? ListChecks
    : t === 'MultiSelect' ? ListChecks
    : AlignLeft
</script>

<template>
  <div :class="inline ? 'editor-inline-host' : 'modal-backdrop'" @click.self="!inline && emit('close')">
    <div class="editor" :class="{ 'editor--inline': inline }">
      <div class="editor-head">
        <h2>{{ activity ? 'Aktivität bearbeiten' : 'Neue Aktivität' }}</h2>
        <button class="modal-close" @click="emit('close')"><X :size="18" /></button>
      </div>

      <div class="editor-body">
        <!-- Basics -->
        <div class="field-row">
          <label class="field" style="flex:2">
            <span class="field-label">Titel *</span>
            <input v-model="title" class="field-input" placeholder="z.B. Geländespiel" />
          </label>
          <label class="field">
            <span class="field-label">Kategorie</span>
            <select v-model="categoryId" class="field-input">
              <option :value="null">—</option>
              <option v-for="c in categories" :key="c.id" :value="c.id">
                {{ c.short_name }} · {{ c.name }}
              </option>
            </select>
          </label>
        </div>

        <div class="field-row">
          <label class="field">
            <span class="field-label">Ort</span>
            <input v-model="location" class="field-input" placeholder="z.B. Wald" />
          </label>
          <label class="field">
            <span class="field-label">Periode</span>
            <select v-model="periodId" class="field-input">
              <option v-for="p in periods" :key="p.id" :value="p.id">
                {{ p.description || p.start_date }}
              </option>
            </select>
          </label>
        </div>

        <div class="field-row">
          <label class="field">
            <span class="field-label">Tag</span>
            <select v-model.number="dayOffset" class="field-input">
              <option v-for="d in periodDayOptions" :key="d.value" :value="d.value">{{ d.label }}</option>
            </select>
          </label>
          <label class="field">
            <span class="field-label">Startzeit</span>
            <input v-model="startTime" type="time" class="field-input" />
          </label>
          <label class="field">
            <span class="field-label">Dauer (Min.)</span>
            <input v-model.number="length" type="number" min="0" step="5" class="field-input" />
          </label>
        </div>

        <!-- Verantwortlich — same user picker as activities -->
        <div class="section">
          <div class="section-label"><Users :size="15" /> Verantwortlich</div>
          <div class="user-search-wrapper">
            <input
              type="text"
              class="field-input"
              v-model="responsibleSearch"
              placeholder="Person suchen…"
              @focus="showResponsibleDropdown = true"
              @blur="onResponsibleBlur"
            />
            <div v-if="showResponsibleDropdown && filteredResponsibleUsers.length" class="user-dropdown">
              <div
                v-for="u in filteredResponsibleUsers"
                :key="u.id"
                class="user-dropdown-item"
                @mousedown.prevent="addResponsible(u.id)"
              >{{ u.display_name }}</div>
            </div>
          </div>
          <div class="user-chips" v-if="responsible.length">
            <span v-for="(entry, i) in responsible" :key="entry" class="user-chip">
              {{ resolveResponsibleName(entry) }}
              <button type="button" class="user-chip-remove" @click="removeResponsible(i)" aria-label="Entfernen"><X :size="12" /></button>
            </span>
          </div>
        </div>

        <!-- Funktionen / RF-Liste assignment (camp-specific collaborations) -->
        <div class="section" v-if="collaborations.length">
          <div class="section-label"><Users :size="15" /> Funktion (RF-Liste)</div>
          <div class="resp-chips">
            <button
              v-for="c in collaborations"
              :key="c.id"
              type="button"
              class="resp-chip"
              :class="{ 'resp-chip--on': responsibleIds.includes(c.id) }"
              :style="responsibleIds.includes(c.id) ? { background: c.color, borderColor: c.color, color: '#fff' } : {}"
              @click="toggleResponsible(c.id)"
            >
              {{ c.abbreviation || c.display_name }}
            </button>
          </div>
        </div>

        <!-- Programmpunkte — same fields as an activity -->
        <div class="section">
          <div class="section-label"><ListChecks :size="15" /> Programmpunkte</div>
          <div class="prog-list">
            <div v-for="(prog, i) in programs" :key="i" class="prog-card">
              <div class="prog-card-head">
                <span class="prog-time">{{ programTimeLabel(i) }}</span>
                <button class="widget-del" @click="removeProgram(i)" title="Programmpunkt entfernen"><Trash2 :size="14" /></button>
              </div>
              <div class="prog-fields">
                <label class="field prog-field-dur">
                  <span class="field-label">Dauer (Min.)</span>
                  <input type="number" min="0" step="5" class="field-input"
                    :value="prog.duration_minutes"
                    @input="prog.duration_minutes = Math.max(0, parseInt(($event.target as HTMLInputElement).value, 10) || 0)" />
                </label>
                <label class="field prog-field-title">
                  <span class="field-label">Titel</span>
                  <input v-model="prog.title" class="field-input" placeholder="Titel" />
                </label>
              </div>
              <div class="field">
                <span class="field-label">Verantwortlich</span>
                <div class="user-search-wrapper">
                  <input
                    type="text"
                    class="field-input"
                    :value="progRespSearch[i] ?? ''"
                    @input="progRespSearch[i] = ($event.target as HTMLInputElement).value"
                    placeholder="Person suchen oder eingeben…"
                    @focus="progRespDropdown = i"
                    @blur="onProgRespBlur(i)"
                    @keydown.enter.prevent="addProgRespFreeText(i)"
                  />
                  <div v-if="progRespDropdown === i && progRespFiltered(i).length" class="user-dropdown">
                    <div v-for="u in progRespFiltered(i)" :key="u.id" class="user-dropdown-item" @mousedown.prevent="addProgResponsible(i, u.id)">{{ u.display_name }}</div>
                  </div>
                </div>
                <div class="user-chips" v-if="prog.responsible.length">
                  <span v-for="(entry, ri) in prog.responsible" :key="entry" class="user-chip">
                    {{ resolveResponsibleName(entry) }}
                    <button type="button" class="user-chip-remove" @click="removeProgResponsible(i, ri)" aria-label="Entfernen"><X :size="12" /></button>
                  </span>
                </div>
              </div>
              <label class="field">
                <span class="field-label">Beschreibung</span>
                <textarea v-model="prog.description" class="field-input" rows="2" placeholder="Beschreibung…" />
              </label>
            </div>
          </div>
          <button class="btn-add" @click="addProgram"><Plus :size="14" /> Programmpunkt</button>
        </div>

        <!-- Content widgets -->
        <div class="section">
          <div class="section-label">Weitere Inhalte</div>
          <div class="widget-list">
            <div v-for="w in widgets" :key="w.key" class="widget">
              <div class="widget-head">
                <component :is="widgetIcon(w.content_type)" :size="15" />
                <input v-model="w.instance_name" class="widget-name" />
                <button class="widget-del" @click="removeWidget(w.key)"><Trash2 :size="14" /></button>
              </div>

              <!-- SingleText -->
              <textarea
                v-if="w.content_type === 'SingleText'"
                v-model="w.html"
                class="field-input widget-text"
                rows="3"
                placeholder="Text…"
              />

              <!-- Storyboard (Geschichte) -->
              <div v-else-if="w.content_type === 'Storyboard'" class="story">
                <div class="story-head">
                  <span>Zeit / Phase</span><span>Was passiert</span><span></span>
                </div>
                <div v-for="s in w.sections" :key="s.id" class="story-row">
                  <input v-model="s.column1" class="field-input" placeholder="00:00" />
                  <textarea v-model="s.column2" class="field-input" rows="2" placeholder="Beschreibung…" />
                  <button class="widget-del" @click="removeSection(w, s.id)"><X :size="14" /></button>
                </div>
                <button class="btn-add-sm" @click="addSection(w)"><Plus :size="14" /> Abschnitt</button>
              </div>

              <!-- MaterialNode -->
              <p v-else-if="w.content_type === 'MaterialNode'" class="hint">
                Material wird in der Materialliste des Lagers verwaltet und hier verknüpft.
              </p>

              <!-- MultiSelect / Checklist -->
              <div v-else class="opts">
                <div v-for="o in w.options" :key="o.id" class="opt-row">
                  <input v-model="o.label" class="field-input" placeholder="Option…" />
                  <button class="widget-del" @click="removeOption(w, o.id)"><X :size="14" /></button>
                </div>
                <button class="btn-add-sm" @click="addOption(w)"><Plus :size="14" /> Option</button>
              </div>
            </div>
          </div>

          <div class="widget-add-bar">
            <button class="btn-add" @click="addWidget('SingleText')"><AlignLeft :size="14" /> Text</button>
            <button class="btn-add" @click="addWidget('Storyboard')"><BookOpen :size="14" /> Geschichte</button>
            <button class="btn-add" @click="addWidget('MaterialNode')"><Package :size="14" /> Material</button>
            <button class="btn-add" @click="addWidget('Checklist')"><ListChecks :size="14" /> Checkliste</button>
          </div>
        </div>
      </div>

      <div class="editor-foot">
        <button v-if="activity" class="btn-danger" @click="emit('delete', activity.id)">
          <Trash2 :size="15" /> Löschen
        </button>
        <span style="flex:1" />
        <button class="btn-ghost" @click="emit('close')">Abbrechen</button>
        <button class="btn-primary" :disabled="!title.trim()" @click="save">Speichern</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.modal-backdrop {
  position: fixed;
  inset: 0;
  background: rgba(15, 23, 42, 0.45);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 220;
  padding: 20px;
}
.editor {
  background: var(--bg-surface);
  border-radius: 14px;
  width: 100%;
  max-width: 640px;
  max-height: 92vh;
  display: flex;
  flex-direction: column;
  box-shadow: 0 16px 48px rgba(0, 0, 0, 0.25);
}
/* Inline (eCamp split view): fill the right panel, scroll internally. */
.editor-inline-host { height: 100%; }
.editor--inline {
  max-width: none;
  max-height: 100%;
  height: 100%;
  border-radius: 12px;
  border: 1px solid var(--border);
  box-shadow: none;
}
.editor-head, .editor-foot {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 16px 20px;
}
.editor-head { border-bottom: 1px solid var(--border); }
.editor-foot { border-top: 1px solid var(--border); }
.editor-head h2 { margin: 0; font-size: 1.15rem; font-weight: 700; color: var(--text-primary); flex: 1; }
.editor-body {
  padding: 18px 20px;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.modal-close { background: transparent; border: none; cursor: pointer; color: var(--text-muted); padding: 4px; border-radius: 6px; display: inline-flex; }
.modal-close:hover { background: var(--bg-hover); }
.field { display: flex; flex-direction: column; gap: 5px; flex: 1; min-width: 0; }
.field-row { display: flex; gap: 12px; }
.field-label { font-size: 0.78rem; font-weight: 600; color: var(--text-secondary); }
.field-input {
  padding: 8px 10px;
  border: 1px solid var(--input-border, var(--border-strong));
  border-radius: 8px;
  font-size: 0.88rem;
  background: var(--input-bg, var(--bg-surface));
  color: var(--input-color, var(--text-primary));
  font-family: inherit;
}
.field-input:focus { outline: none; border-color: var(--accent); }
.section { display: flex; flex-direction: column; gap: 10px; }
.section-label {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 0.82rem;
  font-weight: 700;
  color: var(--text-secondary);
  text-transform: uppercase;
  letter-spacing: 0.03em;
}
.prog-list { display: flex; flex-direction: column; gap: 10px; margin-bottom: 10px; }
.prog-card { border: 1px solid var(--border); border-radius: 10px; padding: 12px; background: var(--bg-elevated); display: flex; flex-direction: column; gap: 10px; }
.prog-card-head { display: flex; align-items: center; justify-content: space-between; }
.prog-time { font-size: 0.78rem; font-weight: 700; color: var(--program-time-color, var(--accent)); background: var(--program-time-bg, var(--accent-bg)); padding: 3px 10px; border-radius: 999px; }
.prog-fields { display: flex; gap: 12px; }
.prog-field-dur { flex: 0 0 110px; }
.prog-field-title { flex: 1; }
.user-search-wrapper { position: relative; }
.user-dropdown {
  position: absolute; top: calc(100% + 4px); left: 0; right: 0; z-index: 50;
  background: var(--dropdown-bg, var(--bg-surface)); border: 1px solid var(--border);
  border-radius: 8px; box-shadow: 0 4px 16px rgba(0,0,0,0.1); max-height: 220px; overflow-y: auto; padding: 4px;
}
.user-dropdown-item { padding: 8px 10px; border-radius: 6px; cursor: pointer; font-size: 0.88rem; color: var(--text-secondary); }
.user-dropdown-item:hover { background: var(--dropdown-hover, var(--bg-hover)); color: var(--accent); }
.user-chips { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 8px; }
.user-chip {
  display: inline-flex; align-items: center; gap: 5px;
  padding: 4px 6px 4px 11px; border-radius: 999px;
  background: var(--accent-bg); color: var(--accent); font-size: 0.82rem; font-weight: 600;
}
.user-chip-remove {
  display: inline-flex; align-items: center; justify-content: center;
  background: rgba(0,0,0,0.08); border: none; color: inherit; cursor: pointer;
  width: 16px; height: 16px; border-radius: 50%; padding: 0;
}
.user-chip-remove:hover { background: rgba(0,0,0,0.18); }
.resp-chips { display: flex; flex-wrap: wrap; gap: 8px; }
.resp-chip {
  padding: 5px 12px;
  border-radius: 999px;
  border: 1px solid var(--border-strong);
  background: var(--bg-surface);
  color: var(--text-secondary);
  font-size: 0.82rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.12s;
}
.resp-chip:hover { border-color: var(--accent); }
.hint { font-size: 0.82rem; color: var(--text-subtle); }
.widget-list { display: flex; flex-direction: column; gap: 12px; }
.widget {
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 12px;
  background: var(--bg-elevated);
}
.widget-head { display: flex; align-items: center; gap: 8px; margin-bottom: 8px; color: var(--text-muted); }
.widget-name {
  flex: 1;
  border: none;
  background: transparent;
  font-size: 0.92rem;
  font-weight: 700;
  color: var(--text-primary);
  padding: 2px 0;
}
.widget-name:focus { outline: none; border-bottom: 1px solid var(--accent); }
.widget-del {
  background: transparent;
  border: none;
  color: var(--text-subtle);
  cursor: pointer;
  padding: 3px;
  border-radius: 5px;
  display: inline-flex;
}
.widget-del:hover { color: var(--btn-danger-color); background: var(--btn-danger-bg); }
.widget-text { width: 100%; resize: vertical; }
.story { display: flex; flex-direction: column; gap: 6px; }
.story-head, .story-row {
  display: grid;
  grid-template-columns: 90px 1fr 26px;
  gap: 8px;
  align-items: start;
}
.story-head { font-size: 0.72rem; font-weight: 600; color: var(--text-subtle); }
.opts { display: flex; flex-direction: column; gap: 6px; }
.opt-row { display: grid; grid-template-columns: 1fr 26px; gap: 8px; align-items: center; }
.btn-add-sm {
  align-self: flex-start;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  background: transparent;
  border: 1px dashed var(--border-strong);
  border-radius: 6px;
  padding: 4px 10px;
  font-size: 0.8rem;
  color: var(--text-muted);
  cursor: pointer;
}
.btn-add-sm:hover { border-color: var(--accent); color: var(--accent); }
.widget-add-bar { display: flex; flex-wrap: wrap; gap: 8px; }
.btn-add {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  background: var(--accent-bg);
  border: none;
  border-radius: 8px;
  padding: 7px 12px;
  font-size: 0.82rem;
  font-weight: 600;
  color: var(--accent);
  cursor: pointer;
}
.btn-add:hover { background: var(--accent-bg-hover); }
.btn-ghost {
  background: transparent;
  border: 1px solid var(--border-strong);
  color: var(--text-secondary);
  padding: 8px 16px;
  border-radius: 8px;
  font-weight: 600;
  font-size: 0.88rem;
  cursor: pointer;
}
.btn-ghost:hover { background: var(--bg-hover); }
.btn-danger {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  background: var(--btn-danger-bg);
  color: var(--btn-danger-color);
  border: none;
  padding: 8px 14px;
  border-radius: 8px;
  font-weight: 600;
  font-size: 0.88rem;
  cursor: pointer;
}
</style>
