<script setup lang="ts">
import { onMounted } from 'vue'
import { useActivities } from '../composables/useActivities'
import ActivityList from '../components/ActivityList.vue'

const { activities, loading, error, connected, fetchActivities } = useActivities()

onMounted(fetchActivities)
</script>

<template>
  <header class="header">
    <h1>Aktivitäten</h1>
    <div class="header-right">
      <span class="status" :class="connected ? 'status--live' : 'status--off'">
        {{ connected ? 'Live' : 'Verbinde...' }}
      </span>
      <router-link to="/activities/new" class="btn-primary">+ Neue Aktivität</router-link>
    </div>
  </header>

  <main class="main">
    <p v-if="error" class="error">{{ error }}</p>
    <p v-if="loading" class="loading">Laden...</p>
    <ActivityList :activities="activities" />
  </main>
</template>
