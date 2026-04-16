<script setup lang="ts">
import type { Activity } from '../types'
import DepartmentBadge from './DepartmentBadge.vue'

const props = defineProps<{
  activity: Activity
  dimmed?: boolean
}>()

function formatDate(d: string): string {
  // Append time to avoid timezone-shift on date-only strings
  return new Date(d + 'T00:00:00').toLocaleDateString('de-DE', {
    weekday: 'short',
    day: 'numeric',
    month: 'long'
  })
}
</script>

<template>
  <div class="activity-card" :class="{ 'activity-card--dimmed': dimmed }">
    <div class="card-header">
      <span class="card-title">{{ activity.title }}</span>
      <DepartmentBadge :department="activity.department" />
    </div>
    <div class="card-meta">
      <span class="card-date">
        {{ formatDate(activity.date) }} &middot; {{ activity.start_time }}–{{ activity.end_time }}
      </span>
      <span v-if="activity.responsible.length" class="card-responsible">
        {{ activity.responsible.join(', ') }}
      </span>
    </div>
    <p v-if="activity.location" class="card-location">📍 {{ activity.location }}</p>
  </div>
</template>
