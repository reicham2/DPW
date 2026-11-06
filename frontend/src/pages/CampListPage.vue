<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useCamps } from '../composables/useCamps'
import { usePermissions } from '../composables/usePermissions'
import { user } from '../composables/useAuth'
import ErrorAlert from '../components/ErrorAlert.vue'
import { Tent, Plus, X, MapPin, Trash2 } from 'lucide-vue-next'
import type { CampInput } from '../types'

const router = useRouter()
const { camps, loading, error, fetchCamps, createCamp, deleteCamp } = useCamps()
const { writableDepts } = usePermissions()

const canCreate = ref(false)

const showCreate = ref(false)
const saving = ref(false)
const form = ref<CampInput>({
  title: '',
  motto: '',
  kind: 'Zeltlager',
  organizer: '',
  address_city: '',
  color: '#0080ff',
  department: null,
})

const KINDS = ['Zeltlager', 'Hauslager', 'Wanderlager', 'Kurs', 'Weekend']

function openCreate() {
  form.value = {
    title: '',
    motto: '',
    kind: 'Zeltlager',
    organizer: '',
    address_city: '',
    color: '#0080ff',
    department: user.value?.department ?? null,
  }
  showCreate.value = true
}

async function submitCreate() {
  if (!form.value.title.trim() || saving.value) return
  saving.value = true
  const camp = await createCamp(form.value)
  saving.value = false
  if (camp) {
    showCreate.value = false
    router.push(`/camps/${camp.id}`)
  }
}

async function confirmDelete(id: string, title: string) {
  if (!window.confirm(`Lager "${title}" wirklich löschen?`)) return
  await deleteCamp(id)
}

function openCamp(id: string) {
  router.push(`/camps/${id}`)
}

onMounted(() => {
  fetchCamps()
  canCreate.value = writableDepts(user.value?.department).length > 0
})
</script>

<template>
  <header class="header">
    <h1>Lager</h1>
    <div class="header-right">
      <button class="btn-primary" :disabled="!canCreate" @click="openCreate">
        <Plus :size="16" aria-hidden="true" /> Neues Lager
      </button>
    </div>
  </header>

  <main class="main">
    <ErrorAlert :error="error" />
    <p v-if="loading" class="loading">Laden...</p>

    <p v-if="!loading && camps.length === 0" class="filter-empty">
      Noch keine Lager. Erstelle dein erstes Lager.
    </p>

    <div class="camp-grid">
      <article
        v-for="camp in camps"
        :key="camp.id"
        class="camp-card"
        :style="{ '--camp-accent': camp.color }"
        @click="openCamp(camp.id)"
      >
        <div class="camp-card-stripe" />
        <div class="camp-card-body">
          <div class="camp-card-top">
            <span class="camp-card-icon"><Tent :size="20" aria-hidden="true" /></span>
            <h2 class="camp-card-title">{{ camp.title }}</h2>
            <button
              class="camp-card-del"
              title="Lager löschen"
              @click.stop="confirmDelete(camp.id, camp.title)"
            ><Trash2 :size="15" aria-hidden="true" /></button>
          </div>
          <p v-if="camp.motto" class="camp-card-motto">«{{ camp.motto }}»</p>
          <div class="camp-card-meta">
            <span v-if="camp.kind" class="camp-chip">{{ camp.kind }}</span>
            <span v-if="camp.address_city" class="camp-card-loc">
              <MapPin :size="13" aria-hidden="true" /> {{ camp.address_city }}
            </span>
            <span v-if="camp.department" class="camp-chip camp-chip--dept">{{ camp.department }}</span>
          </div>
        </div>
      </article>
    </div>

    <!-- Create modal -->
    <div v-if="showCreate" class="modal-backdrop" @click.self="showCreate = false">
      <div class="modal">
        <div class="modal-head">
          <h2>Neues Lager</h2>
          <button class="modal-close" @click="showCreate = false"><X :size="18" /></button>
        </div>
        <div class="modal-body">
          <label class="field">
            <span class="field-label">Titel *</span>
            <input v-model="form.title" class="field-input" placeholder="z.B. Sommerlager 2026" @keyup.enter="submitCreate" />
          </label>
          <label class="field">
            <span class="field-label">Motto</span>
            <input v-model="form.motto" class="field-input" placeholder="z.B. Piraten der sieben Meere" />
          </label>
          <div class="field-row">
            <label class="field">
              <span class="field-label">Lagerart</span>
              <select v-model="form.kind" class="field-input">
                <option v-for="k in KINDS" :key="k" :value="k">{{ k }}</option>
              </select>
            </label>
            <label class="field">
              <span class="field-label">Ort</span>
              <input v-model="form.address_city" class="field-input" placeholder="Ort" />
            </label>
          </div>
          <div class="field-row">
            <label class="field">
              <span class="field-label">Organisator</span>
              <input v-model="form.organizer" class="field-input" placeholder="z.B. Pfadi Hue" />
            </label>
            <label class="field field--color">
              <span class="field-label">Farbe</span>
              <input v-model="form.color" type="color" class="field-color" />
            </label>
          </div>
        </div>
        <div class="modal-foot">
          <button class="btn-ghost" @click="showCreate = false">Abbrechen</button>
          <button class="btn-primary" :disabled="!form.title.trim() || saving" @click="submitCreate">
            {{ saving ? 'Speichern…' : 'Erstellen' }}
          </button>
        </div>
      </div>
    </div>
  </main>
</template>

<style scoped>
.camp-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 16px;
  margin-top: 16px;
}
.camp-card {
  position: relative;
  display: flex;
  background: var(--bg-surface);
  border: 1px solid var(--border);
  border-radius: 12px;
  overflow: hidden;
  cursor: pointer;
  transition: box-shadow 0.15s, transform 0.15s, border-color 0.15s;
}
.camp-card:hover {
  box-shadow: 0 6px 20px rgba(0, 0, 0, 0.08);
  transform: translateY(-2px);
  border-color: var(--camp-accent, var(--accent));
}
.camp-card-stripe {
  width: 6px;
  background: var(--camp-accent, var(--accent));
  flex-shrink: 0;
}
.camp-card-body {
  padding: 16px;
  flex: 1;
  min-width: 0;
}
.camp-card-top {
  display: flex;
  align-items: center;
  gap: 8px;
}
.camp-card-icon {
  display: inline-flex;
  color: var(--camp-accent, var(--accent));
}
.camp-card-title {
  font-size: 1.05rem;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0;
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.camp-card-del {
  background: transparent;
  border: none;
  color: var(--text-subtle);
  cursor: pointer;
  padding: 4px;
  border-radius: 6px;
  display: inline-flex;
  transition: color 0.15s, background 0.15s;
}
.camp-card-del:hover {
  color: var(--btn-danger-color);
  background: var(--btn-danger-bg);
}
.camp-card-motto {
  font-style: italic;
  color: var(--text-muted);
  margin: 8px 0 0;
  font-size: 0.9rem;
}
.camp-card-meta {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
  margin-top: 12px;
}
.camp-chip {
  font-size: 0.75rem;
  font-weight: 600;
  padding: 3px 9px;
  border-radius: 999px;
  background: var(--accent-bg);
  color: var(--accent);
}
.camp-chip--dept {
  background: var(--bg-hover);
  color: var(--text-secondary);
}
.camp-card-loc {
  display: inline-flex;
  align-items: center;
  gap: 3px;
  font-size: 0.8rem;
  color: var(--text-muted);
}

/* Modal */
.modal-backdrop {
  position: fixed;
  inset: 0;
  background: rgba(15, 23, 42, 0.4);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 200;
  padding: 20px;
}
.modal {
  background: var(--bg-surface);
  border-radius: 14px;
  width: 100%;
  max-width: 440px;
  box-shadow: 0 12px 40px rgba(0, 0, 0, 0.2);
  display: flex;
  flex-direction: column;
  max-height: 90vh;
}
.modal-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 18px 20px;
  border-bottom: 1px solid var(--border);
}
.modal-head h2 {
  margin: 0;
  font-size: 1.15rem;
  font-weight: 700;
  color: var(--text-primary);
}
.modal-close {
  background: transparent;
  border: none;
  cursor: pointer;
  color: var(--text-muted);
  display: inline-flex;
  padding: 4px;
  border-radius: 6px;
}
.modal-close:hover { background: var(--bg-hover); }
.modal-body {
  padding: 20px;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 14px;
}
.modal-foot {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  padding: 16px 20px;
  border-top: 1px solid var(--border);
}
.field { display: flex; flex-direction: column; gap: 5px; flex: 1; }
.field-row { display: flex; gap: 12px; }
.field-label {
  font-size: 0.8rem;
  font-weight: 600;
  color: var(--text-secondary);
}
.field-input {
  padding: 9px 11px;
  border: 1px solid var(--input-border, var(--border-strong));
  border-radius: 8px;
  font-size: 0.9rem;
  background: var(--input-bg, var(--bg-surface));
  color: var(--input-color, var(--text-primary));
}
.field-input:focus {
  outline: none;
  border-color: var(--accent);
}
.field--color { flex: 0 0 70px; }
.field-color {
  width: 100%;
  height: 38px;
  border: 1px solid var(--border-strong);
  border-radius: 8px;
  background: var(--bg-surface);
  cursor: pointer;
  padding: 2px;
}
.btn-ghost {
  background: transparent;
  border: 1px solid var(--border-strong);
  color: var(--text-secondary);
  padding: 9px 16px;
  border-radius: 8px;
  font-weight: 600;
  font-size: 0.9rem;
  cursor: pointer;
}
.btn-ghost:hover { background: var(--bg-hover); }
</style>
