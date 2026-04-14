<script setup lang="ts">
import type { Activity, MaterialItem } from '../types'
import ActivityCard from './ActivityCard.vue'

export interface ListEntry {
  activity: Activity
  dimmed: boolean
  myMaterials: MaterialItem[]
}

defineProps<{ entries: ListEntry[] }>()
</script>

<template>
  <div class="activity-list">
    <p v-if="entries.length === 0" class="empty">Noch keine Aktivitäten.</p>
    <div
      v-for="(entry, eIdx) in entries"
      :key="entry.activity.id"
      class="activity-list-entry"
    >
      <router-link
        :to="`/activities/${entry.activity.id}`"
        class="activity-card-link"
      >
        <ActivityCard
          :activity="entry.activity"
          :dimmed="entry.dimmed"
        />
      </router-link>
      <!-- Materials assigned to me -->
      <div v-if="entry.myMaterials.length" class="my-materials">
        <router-link
          v-for="(mat, mIdx) in entry.myMaterials"
          :key="mIdx"
          :to="`/activities/${entry.activity.id}?focus=material_${entry.activity.material.findIndex(m => m.name === mat.name)}`"
          class="my-material-chip"
        >
          🧰 {{ mat.name }}
        </router-link>
      </div>
    </div>
  </div>
</template>
