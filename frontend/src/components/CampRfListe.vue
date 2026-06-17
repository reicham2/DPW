<script setup lang="ts">
import { ref } from 'vue'
import { X, Plus, Trash2, Users, Tag, CalendarRange, Package } from 'lucide-vue-next'
import { useCamps } from '../composables/useCamps'
import type { CampDetail } from '../types'

const props = defineProps<{ camp: CampDetail }>()
const emit = defineEmits<{ (e: 'close'): void; (e: 'changed'): void }>()

const {
  createCollaboration, deleteCollaboration,
  createCategory, deleteCategory,
  createPeriod, deletePeriod,
  createMaterialList, deleteMaterialList, createMaterialItem, deleteMaterialItem,
} = useCamps()

type Tab = 'rf' | 'categories' | 'periods' | 'material'
const tab = ref<Tab>('rf')
const busy = ref(false)

async function run(fn: () => Promise<unknown>) {
  if (busy.value) return
  busy.value = true
  await fn()
  busy.value = false
  emit('changed')
}

// ── RF (collaborations) ──────────────────────────────────────────────────────
const rfName = ref('')
const rfRole = ref('')
const rfAbbr = ref('')
const rfColor = ref('#0080ff')
const rfStufenRole = ref<'member' | 'manager' | 'guest'>('member')

function addRf() {
  if (!rfName.value.trim()) return
  void run(async () => {
    await createCollaboration(props.camp.id, {
      display_name: rfName.value,
      camp_role: rfRole.value,
      abbreviation: rfAbbr.value,
      color: rfColor.value,
      role: rfStufenRole.value,
    })
    rfName.value = ''; rfRole.value = ''; rfAbbr.value = ''
  })
}

// ── Categories ────────────────────────────────────────────────────────────────
const catShort = ref('')
const catName = ref('')
const catColor = ref('#065f46')
function addCat() {
  if (!catShort.value.trim()) return
  void run(async () => {
    await createCategory(props.camp.id, {
      short_name: catShort.value, name: catName.value, color: catColor.value,
    })
    catShort.value = ''; catName.value = ''
  })
}

// ── Periods ───────────────────────────────────────────────────────────────────
const pDesc = ref('')
const pStart = ref('')
const pEnd = ref('')
function addPeriod() {
  if (!pStart.value || !pEnd.value) return
  void run(async () => {
    await createPeriod(props.camp.id, {
      description: pDesc.value, start_date: pStart.value, end_date: pEnd.value,
    })
    pDesc.value = ''; pStart.value = ''; pEnd.value = ''
  })
}


// ── Material ──────────────────────────────────────────────────────────────────
const listName = ref('')
function addList() {
  if (!listName.value.trim()) return
  void run(async () => {
    await createMaterialList(props.camp.id, { name: listName.value })
    listName.value = ''
  })
}
const newItem = ref<Record<string, { name: string; qty: string; unit: string }>>({})
function itemDraft(listId: string) {
  if (!newItem.value[listId]) newItem.value[listId] = { name: '', qty: '', unit: '' }
  return newItem.value[listId]
}
function addItem(listId: string) {
  const d = itemDraft(listId)
  if (!d.name.trim()) return
  void run(async () => {
    await createMaterialItem(props.camp.id, listId, {
      article_name: d.name,
      quantity: d.qty ? Number(d.qty) : null,
      unit: d.unit,
    })
    newItem.value[listId] = { name: '', qty: '', unit: '' }
  })
}
</script>

<template>
  <div class="modal-backdrop" @click.self="emit('close')">
    <div class="panel">
      <div class="panel-head">
        <h2>Lager-Verwaltung</h2>
        <button class="modal-close" @click="emit('close')"><X :size="18" /></button>
      </div>

      <div class="panel-tabs">
        <button class="ptab" :class="{ 'ptab--active': tab === 'rf' }" @click="tab = 'rf'"><Users :size="15" /> RF-Liste</button>
        <button class="ptab" :class="{ 'ptab--active': tab === 'categories' }" @click="tab = 'categories'"><Tag :size="15" /> Kategorien</button>
        <button class="ptab" :class="{ 'ptab--active': tab === 'periods' }" @click="tab = 'periods'"><CalendarRange :size="15" /> Perioden</button>
        <button class="ptab" :class="{ 'ptab--active': tab === 'material' }" @click="tab = 'material'"><Package :size="15" /> Material</button>
      </div>

      <div class="panel-body">
        <!-- RF-Liste -->
        <template v-if="tab === 'rf'">
          <p class="panel-hint">Verantwortlichkeiten &amp; Funktionen (RF) im Lager.</p>
          <div class="rf-list">
            <div v-for="c in camp.collaborations" :key="c.id" class="rf-row">
              <span class="rf-dot" :style="{ background: c.color }" />
              <span class="rf-abbr">{{ c.abbreviation || '–' }}</span>
              <span class="rf-name">{{ c.display_name }}</span>
              <span class="rf-role">{{ c.camp_role }}</span>
              <span class="rf-stufen">{{ c.role }}</span>
              <button class="row-del" @click="run(() => deleteCollaboration(camp.id, c.id))"><Trash2 :size="14" /></button>
            </div>
          </div>
          <div class="add-form">
            <input v-model="rfName" class="fi" placeholder="Name" />
            <input v-model="rfRole" class="fi" placeholder="Funktion (z.B. Küche)" />
            <input v-model="rfAbbr" class="fi fi--sm" placeholder="Kürzel" maxlength="4" />
            <select v-model="rfStufenRole" class="fi fi--sm">
              <option value="member">Mitglied</option>
              <option value="manager">Leitung</option>
              <option value="guest">Gast</option>
            </select>
            <input v-model="rfColor" type="color" class="fi-color" />
            <button class="btn-add" :disabled="busy" @click="addRf"><Plus :size="15" /></button>
          </div>
        </template>

        <!-- Categories -->
        <template v-else-if="tab === 'categories'">
          <p class="panel-hint">Programmtypen mit Farbe &amp; Kürzel.</p>
          <div class="rf-list">
            <div v-for="c in camp.categories" :key="c.id" class="rf-row">
              <span class="rf-dot" :style="{ background: c.color }" />
              <span class="rf-abbr">{{ c.short_name }}</span>
              <span class="rf-name">{{ c.name }}</span>
              <button class="row-del" @click="run(() => deleteCategory(camp.id, c.id))"><Trash2 :size="14" /></button>
            </div>
          </div>
          <div class="add-form">
            <input v-model="catShort" class="fi fi--sm" placeholder="Kürzel" maxlength="4" />
            <input v-model="catName" class="fi" placeholder="Name (z.B. Lagerprogramm)" />
            <input v-model="catColor" type="color" class="fi-color" />
            <button class="btn-add" :disabled="busy" @click="addCat"><Plus :size="15" /></button>
          </div>
        </template>

        <!-- Periods -->
        <template v-else-if="tab === 'periods'">
          <p class="panel-hint">Zeitblöcke des Lagers (dürfen sich nicht überlappen).</p>
          <div class="rf-list">
            <div v-for="p in camp.periods" :key="p.id" class="rf-row">
              <span class="rf-name">{{ p.description || 'Periode' }}</span>
              <span class="rf-role">{{ p.start_date }} → {{ p.end_date }}</span>
              <button class="row-del" @click="run(() => deletePeriod(camp.id, p.id))"><Trash2 :size="14" /></button>
            </div>
          </div>
          <div class="add-form">
            <input v-model="pDesc" class="fi" placeholder="Beschreibung" />
            <input v-model="pStart" type="date" class="fi fi--sm" />
            <input v-model="pEnd" type="date" class="fi fi--sm" />
            <button class="btn-add" :disabled="busy" @click="addPeriod"><Plus :size="15" /></button>
          </div>
        </template>

        <!-- Material -->
        <template v-else>
          <p class="panel-hint">Materiallisten des Lagers.</p>
          <div v-for="l in camp.material_lists" :key="l.id" class="mat-list">
            <div class="mat-list-head">
              <strong>{{ l.name }}</strong>
              <button class="row-del" @click="run(() => deleteMaterialList(camp.id, l.id))"><Trash2 :size="14" /></button>
            </div>
            <div v-for="it in l.items" :key="it.id" class="mat-item">
              <span class="mat-qty">{{ it.quantity ?? '' }} {{ it.unit }}</span>
              <span class="mat-name">{{ it.article_name }}</span>
              <button class="row-del" @click="run(() => deleteMaterialItem(camp.id, it.id))"><X :size="13" /></button>
            </div>
            <div class="add-form add-form--inline">
              <input v-model="itemDraft(l.id).name" class="fi" placeholder="Artikel" />
              <input v-model="itemDraft(l.id).qty" class="fi fi--xs" placeholder="Anz." />
              <input v-model="itemDraft(l.id).unit" class="fi fi--xs" placeholder="Einh." />
              <button class="btn-add" :disabled="busy" @click="addItem(l.id)"><Plus :size="14" /></button>
            </div>
          </div>
          <div class="add-form">
            <input v-model="listName" class="fi" placeholder="Neue Materialliste" />
            <button class="btn-add" :disabled="busy" @click="addList"><Plus :size="15" /></button>
          </div>
        </template>
      </div>
    </div>
  </div>
</template>

<style scoped>
.modal-backdrop {
  position: fixed; inset: 0;
  background: rgba(15, 23, 42, 0.45);
  display: flex; align-items: center; justify-content: center;
  z-index: 210; padding: 20px;
}
.panel {
  background: var(--bg-surface);
  border-radius: 14px;
  width: 100%; max-width: 600px; max-height: 90vh;
  display: flex; flex-direction: column;
  box-shadow: 0 16px 48px rgba(0,0,0,0.25);
}
.panel-head {
  display: flex; align-items: center; justify-content: space-between;
  padding: 16px 20px; border-bottom: 1px solid var(--border);
}
.panel-head h2 { margin: 0; font-size: 1.15rem; font-weight: 700; color: var(--text-primary); }
.modal-close { background: transparent; border: none; cursor: pointer; color: var(--text-muted); padding: 4px; border-radius: 6px; display: inline-flex; }
.modal-close:hover { background: var(--bg-hover); }
.panel-tabs { display: flex; gap: 4px; padding: 10px 16px 0; flex-wrap: wrap; }
.ptab {
  display: inline-flex; align-items: center; gap: 5px;
  padding: 7px 12px; border: none; background: transparent;
  border-radius: 8px 8px 0 0; font-size: 0.85rem; font-weight: 600;
  color: var(--text-muted); cursor: pointer; border-bottom: 2px solid transparent;
}
.ptab--active { color: var(--accent); border-bottom-color: var(--accent); }
.panel-body { padding: 16px 20px; overflow-y: auto; }
.panel-hint { font-size: 0.82rem; color: var(--text-subtle); margin: 0 0 12px; }
.rf-list { display: flex; flex-direction: column; gap: 6px; margin-bottom: 14px; }
.rf-row {
  display: flex; align-items: center; gap: 10px;
  padding: 8px 10px; border: 1px solid var(--border); border-radius: 8px;
  background: var(--bg-elevated);
}
.rf-dot { width: 12px; height: 12px; border-radius: 50%; flex-shrink: 0; }
.rf-abbr { font-weight: 800; font-size: 0.8rem; color: var(--text-secondary); min-width: 32px; }
.rf-name { font-weight: 600; color: var(--text-primary); flex: 1; min-width: 0; }
.rf-role { font-size: 0.82rem; color: var(--text-muted); }
.rf-stufen { font-size: 0.72rem; color: var(--text-subtle); text-transform: uppercase; }
.row-del {
  background: transparent; border: none; color: var(--text-subtle);
  cursor: pointer; padding: 4px; border-radius: 5px; display: inline-flex;
}
.row-del:hover { color: var(--btn-danger-color); background: var(--btn-danger-bg); }
.add-form { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
.add-form--inline { margin-top: 8px; }
.fi {
  flex: 1; min-width: 80px;
  padding: 8px 10px; border: 1px solid var(--border-strong); border-radius: 8px;
  font-size: 0.86rem; background: var(--input-bg, var(--bg-surface)); color: var(--text-primary);
}
.fi--sm { flex: 0 0 110px; }
.fi--xs { flex: 0 0 60px; min-width: 50px; }
.fi:focus { outline: none; border-color: var(--accent); }
.fi-color { width: 40px; height: 36px; border: 1px solid var(--border-strong); border-radius: 8px; cursor: pointer; padding: 2px; }
.btn-add {
  display: inline-flex; align-items: center; justify-content: center;
  background: var(--accent); color: #fff; border: none;
  border-radius: 8px; width: 38px; height: 36px; cursor: pointer;
}
.btn-add:disabled { opacity: 0.6; cursor: default; }
.mat-list { border: 1px solid var(--border); border-radius: 10px; padding: 12px; margin-bottom: 12px; }
.mat-list-head { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; color: var(--text-primary); }
.mat-item { display: flex; align-items: center; gap: 10px; padding: 4px 0; font-size: 0.86rem; }
.mat-qty { color: var(--text-muted); min-width: 60px; font-variant-numeric: tabular-nums; }
.mat-name { flex: 1; color: var(--text-primary); }
</style>
