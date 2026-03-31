<script setup lang="ts">
import type { Activity } from '../types'
import ActivityItem from './ActivityItem.vue'

defineProps<{ activities: Activity[] }>()
const emit = defineEmits<{
  update: [id: string, text: string]
  delete: [id: string]
}>()
</script>

<template>
  <div class="activity-list">
    <p v-if="activities.length === 0" class="empty">Noch keine Aktivitäten.</p>
    <ActivityItem
      v-for="activity in activities"
      :key="activity.id"
      :activity="activity"
      @update="(id, text) => emit('update', id, text)"
      @delete="(id) => emit('delete', id)"
    />
  </div>
</template>
