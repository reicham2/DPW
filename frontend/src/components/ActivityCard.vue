<script setup lang="ts">
import type { Activity } from '../types'
import { MapPin } from 'lucide-vue-next'
import DepartmentBadge from './DepartmentBadge.vue'
import ResponsibleAvatars from './ResponsibleAvatars.vue'

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
      <DepartmentBadge :department="activity.department" :active="false" />
    </div>
    <div class="card-meta">
      <span class="card-date">
        {{ formatDate(activity.date) }} &middot; {{ activity.start_time }}–{{ activity.end_time }}
      </span>
      <ResponsibleAvatars v-if="activity.responsible.length" :names="activity.responsible" />
    </div>
    <p v-if="activity.location" class="card-location"><MapPin :size="14" aria-hidden="true" /> {{ activity.location }}</p>
  </div>
</template>
