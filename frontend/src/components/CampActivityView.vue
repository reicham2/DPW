<script setup lang="ts">
import { computed } from 'vue'
import { Pencil, X, MapPin, Clock, Tag } from 'lucide-vue-next'
import ResponsibleAvatars from './ResponsibleAvatars.vue'
import { formatMinuteOfDay } from '../utils/campTime'
import type { CampActivity, CampCategory } from '../types'

const props = defineProps<{
  activity: CampActivity
  categories: CampCategory[]
  numberLabel?: string
}>()

defineEmits<{
  (e: 'edit'): void
  (e: 'close'): void
}>()

const category = computed(() =>
  props.activity.category_id ? props.categories.find((c) => c.id === props.activity.category_id) ?? null : null,
)
const color = computed(() => category.value?.color ?? '#9ca3af')

// First schedule entry → start time / length for the header.
const firstEntry = computed(() => props.activity.schedule_entries[0] ?? null)
const startMinuteOfDay = computed(() =>
  firstEntry.value ? firstEntry.value.period_offset - Math.floor(firstEntry.value.period_offset / 1440) * 1440 : 0,
)
const timeRange = computed(() => {
  if (!firstEntry.value) return ''
  const s = startMinuteOfDay.value
  const e = s + firstEntry.value.length
  return `${formatMinuteOfDay(((s % 1440) + 1440) % 1440)}–${formatMinuteOfDay(((e % 1440) + 1440) % 1440)}`
})

function programClock(i: number): string {
  let acc = 0
  for (let k = 0; k < i; k++) acc += Number(props.activity.programs[k].duration_minutes) || 0
  const s = startMinuteOfDay.value + acc
  const e = s + (Number(props.activity.programs[i].duration_minutes) || 0)
  return `${formatMinuteOfDay(((s % 1440) + 1440) % 1440)}–${formatMinuteOfDay(((e % 1440) + 1440) % 1440)}`
}

// Content nodes by type for a simple read-only render.
function nodeData(type: string) {
  return props.activity.content_nodes.filter((n) => n.content_type === type)
}
const storyNodes = computed(() => nodeData('Storyboard'))
const textNodes = computed(() => nodeData('SingleText'))

function nodeHtml(data: Record<string, unknown>): string {
  return typeof data.html === 'string' ? data.html : ''
}
function nodeSections(data: Record<string, unknown>): { column1: string; column2: string }[] {
  return Array.isArray(data.sections) ? (data.sections as { column1: string; column2: string }[]) : []
}
</script>

<template>
  <div class="av">
    <div class="av-head">
      <div class="av-head-main">
        <span v-if="numberLabel" class="av-cat" :style="{ background: color }">{{ numberLabel }}</span>
        <h2 class="av-title">{{ activity.title || 'Aktivität' }}</h2>
      </div>
      <div class="av-head-actions">
        <button class="btn-primary" @click="$emit('edit')"><Pencil :size="15" /> Bearbeiten</button>
        <button class="btn-icon" @click="$emit('close')" title="Schliessen"><X :size="16" /></button>
      </div>
    </div>

    <div class="av-meta">
      <span v-if="timeRange" class="av-chip"><Clock :size="13" /> {{ timeRange }}</span>
      <span v-if="activity.location" class="av-chip"><MapPin :size="13" /> {{ activity.location }}</span>
      <span v-if="category" class="av-chip"><Tag :size="13" /> {{ category.name }}</span>
    </div>

    <div v-if="activity.responsible?.length" class="av-section">
      <div class="av-section-label">Verantwortlich</div>
      <ResponsibleAvatars :names="activity.responsible" />
    </div>

    <div v-if="activity.programs?.length" class="av-section">
      <div class="av-section-label">Programmpunkte</div>
      <div class="av-prog-timeline">
        <div v-for="(prog, i) in activity.programs" :key="prog.id || i" class="av-prog">
          <span class="av-prog-dot" :style="{ background: color }" />
          <div class="av-prog-body">
            <div class="av-prog-head">
              <span class="av-prog-time">{{ programClock(i) }}</span>
              <span class="av-prog-title">{{ prog.title || '—' }}</span>
              <ResponsibleAvatars v-if="prog.responsible?.length" :names="prog.responsible" />
            </div>
            <div v-if="prog.description" class="av-prog-desc" v-html="prog.description" />
          </div>
        </div>
      </div>
    </div>

    <div v-for="n in textNodes" :key="n.id" class="av-section">
      <div class="av-section-label">{{ n.instance_name || 'Text' }}</div>
      <div class="av-text" v-html="nodeHtml(n.data)" />
    </div>

    <div v-for="n in storyNodes" :key="n.id" class="av-section">
      <div class="av-section-label">{{ n.instance_name || 'Geschichte' }}</div>
      <div class="av-story">
        <div v-for="(s, si) in nodeSections(n.data)" :key="si" class="av-story-row">
          <span class="av-story-time">{{ s.column1 }}</span>
          <span class="av-story-text" v-html="s.column2" />
        </div>
      </div>
    </div>

    <p v-if="!activity.programs?.length && !activity.responsible?.length && !textNodes.length && !storyNodes.length" class="av-empty">
      Keine Details erfasst. Klicke «Bearbeiten», um Inhalte hinzuzufügen.
    </p>
  </div>
</template>

<style scoped>
.av {
  background: var(--bg-surface);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 18px 20px;
  height: 100%;
  overflow-y: auto;
}
.av-head { display: flex; align-items: flex-start; justify-content: space-between; gap: 12px; }
.av-head-main { display: flex; align-items: center; gap: 10px; min-width: 0; }
.av-cat { color: #fff; font-weight: 800; font-size: 0.78rem; padding: 3px 9px; border-radius: 6px; flex-shrink: 0; }
.av-title { margin: 0; font-size: 1.3rem; font-weight: 800; color: var(--text-primary); }
.av-head-actions { display: flex; gap: 8px; flex-shrink: 0; }
.av-meta { display: flex; flex-wrap: wrap; gap: 8px; margin: 14px 0 4px; }
.av-chip {
  display: inline-flex; align-items: center; gap: 5px;
  font-size: 0.82rem; color: var(--text-secondary);
  background: var(--bg-hover); padding: 4px 11px; border-radius: 999px;
}
.av-section { margin-top: 18px; }
.av-section-label {
  font-size: 0.78rem; font-weight: 700; text-transform: uppercase;
  letter-spacing: 0.03em; color: var(--text-secondary); margin-bottom: 8px;
}
.av-prog-timeline { display: flex; flex-direction: column; gap: 8px; padding-left: 14px; border-left: 2px solid var(--border); }
.av-prog { display: flex; align-items: flex-start; gap: 8px; }
.av-prog-dot { width: 9px; height: 9px; border-radius: 50%; margin: 4px 0 0 -19px; flex-shrink: 0; box-shadow: 0 0 0 2px var(--bg-surface); }
.av-prog-body { flex: 1; min-width: 0; }
.av-prog-head { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.av-prog-time { font-size: 0.74rem; font-weight: 700; color: var(--program-time-color, var(--accent)); background: var(--program-time-bg, var(--accent-bg)); padding: 1px 7px; border-radius: 999px; }
.av-prog-title { font-size: 0.88rem; font-weight: 600; color: var(--text-primary); }
.av-prog-desc { font-size: 0.82rem; color: var(--text-muted); margin-top: 2px; }
.av-text { font-size: 0.9rem; color: var(--text-secondary); line-height: 1.5; }
.av-story { display: flex; flex-direction: column; gap: 6px; }
.av-story-row { display: grid; grid-template-columns: 80px 1fr; gap: 10px; padding: 5px 0; border-top: 1px solid var(--border); }
.av-story-time { font-weight: 700; font-size: 0.82rem; color: var(--text-muted); }
.av-story-text { font-size: 0.88rem; color: var(--text-secondary); }
.av-empty { color: var(--text-subtle); font-size: 0.88rem; margin-top: 20px; }
.btn-primary { display: inline-flex; align-items: center; gap: 6px; }
.btn-icon {
  display: inline-flex; align-items: center; justify-content: center;
  width: 34px; height: 34px; border-radius: 8px; cursor: pointer;
  background: transparent; border: 1px solid var(--border-strong); color: var(--text-muted);
}
.btn-icon:hover { background: var(--bg-hover); color: var(--text-secondary); }
</style>
