<script setup lang="ts">
import { onMounted } from 'vue'
import { useActivities } from './composables/useActivities'
import CreateActivityForm from './components/CreateActivityForm.vue'
import ActivityList from './components/ActivityList.vue'

const {
  activities,
  loading,
  error,
  connected,
  fetchActivities,
  createActivity,
  updateActivity,
  deleteActivity
} = useActivities()

onMounted(fetchActivities)
</script>

<template>
  <div class="app">
    <header class="header">
      <h1>Aktivitäten</h1>
      <span class="status" :class="connected ? 'status--live' : 'status--off'">
        {{ connected ? 'Live' : 'Verbinde...' }}
      </span>
    </header>

    <main class="main">
      <CreateActivityForm @create="createActivity" />

      <p v-if="error" class="error">{{ error }}</p>
      <p v-if="loading" class="loading">Laden...</p>

      <ActivityList
        :activities="activities"
        @update="updateActivity"
        @delete="deleteActivity"
      />
    </main>
  </div>
</template>
