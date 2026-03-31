<script setup lang="ts">
import type { Activity } from '../types'
import ActivityCard from './ActivityCard.vue'
import { useRouter } from 'vue-router'

defineProps<{ activities: Activity[] }>()
const router = useRouter()
</script>

<template>
  <div class="activity-list">
    <p v-if="activities.length === 0" class="empty">Noch keine Aktivitäten.</p>
    <ActivityCard
      v-for="activity in activities"
      :key="activity.id"
      :activity="activity"
      @click="router.push(`/activities/${activity.id}`)"
    />
  </div>
</template>
