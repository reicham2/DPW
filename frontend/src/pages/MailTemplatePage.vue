<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useMailTemplates } from '../composables/useMailTemplates'
import type { Department } from '../types'

const DEPARTMENTS: Department[] = ['Leiter', 'Pio', 'Pfadi', 'Wölfe', 'Biber']

const TEMPLATE_VARIABLES = [
  { var: '{{titel}}',            desc: 'Titel der Aktivität' },
  { var: '{{datum}}',            desc: 'Datum (z.B. Samstag, 12. April 2026)' },
  { var: '{{startzeit}}',        desc: 'Startzeit (HH:MM)' },
  { var: '{{endzeit}}',          desc: 'Endzeit (HH:MM)' },
  { var: '{{ort}}',              desc: 'Veranstaltungsort' },
  { var: '{{verantwortlich}}',   desc: 'Verantwortliche Person' },
  { var: '{{abteilung}}',        desc: 'Abteilung / Stufe' },
  { var: '{{ziel}}',             desc: 'Ziel der Aktivität' },
  { var: '{{material}}',         desc: 'Materialliste (kommagetrennt)' },
  { var: '{{schlechtwetter}}',   desc: 'Schlechtwetter-Info' },
  { var: '{{programm}}',         desc: 'Programmpunkte (formatiert)' },
]

const { fetchTemplates, saveTemplate, templates, loading, error } = useMailTemplates()

const activeDept = ref<Department>('Pfadi')
const subject = ref('')
const body = ref('')
const saving = ref(false)
const saved = ref(false)
const showVars = ref(false)

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
}

async function handleSave() {
  saving.value = true
  saved.value  = false
  const result = await saveTemplate(activeDept.value, subject.value, body.value)
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
  </nav>

  <header class="header">
    <h1>Mail-Vorlagen</h1>
  </header>

  <main class="main">
    <!-- Department tabs -->
    <div class="filter-tabs" style="margin-bottom: 24px">
      <button
        v-for="dept in DEPARTMENTS"
        :key="dept"
        class="filter-tab"
        :class="{ 'filter-tab--active': activeDept === dept }"
        @click="loadDept(dept)"
      >{{ dept }}</button>
    </div>

    <p v-if="loading" class="loading">Laden...</p>

    <form v-else class="detail-form" @submit.prevent="handleSave">
      <div class="form-group">
        <label>Betreff-Vorlage</label>
        <input v-model="subject" type="text" placeholder="Betreff…" />
      </div>

      <div class="form-group">
        <label>Nachricht-Vorlage</label>
        <textarea v-model="body" rows="14" placeholder="Mail-Text…"></textarea>
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
