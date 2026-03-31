<script setup lang="ts">
import type { Activity } from '../types'
defineProps<{ activity: Activity }>()
</script>

<template>
  <div class="activity-card">
    <div class="card-header">
      <span class="card-title">{{ activity.title || '(Kein Titel)' }}</span>
      <span v-if="activity.responsible" class="card-responsible">{{ activity.responsible }}</span>
    </div>
    <p v-if="activity.description" class="card-description">{{ activity.description }}</p>
  </div>
</template>
