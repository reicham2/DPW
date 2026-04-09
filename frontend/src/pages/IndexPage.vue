<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useActivities } from '../composables/useActivities'
import { user } from '../composables/useAuth'
import ActivityList from '../components/ActivityList.vue'
import type { Department } from '../types'

const DEPARTMENTS: Department[] = ['Leiter', 'Pio', 'Pfadi', 'Wölfe', 'Biber']

const { activities, loading, error, connected, fetchActivities } = useActivities()

const search     = ref('')
const activedept = ref<Department | 'Alle'>(user.value?.department ?? 'Alle')

onMounted(fetchActivities)

const filtered = computed(() => {
  let list = activities.value

  if (activedept.value !== 'Alle') {
    list = list.filter(a => a.department === activedept.value)
  }

  const q = search.value.trim().toLowerCase()
  if (q) {
    list = list.filter(a =>
      a.title.toLowerCase().includes(q) ||
      a.responsible.toLowerCase().includes(q) ||
      a.location.toLowerCase().includes(q) ||
      (a.goal ?? '').toLowerCase().includes(q)
    )
  }

  return list
})
</script>

<template>
  <nav class="page-tabs">
    <router-link to="/" class="page-tab page-tab--active">Aktivitäten</router-link>
    <router-link to="/mail-templates" class="page-tab">Mail-Vorlagen</router-link>
  </nav>

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
    <!-- Filter bar -->
    <div class="filter-bar">
      <div class="filter-search">
        <span class="filter-search-icon">🔍</span>
        <input
          v-model="search"
          type="search"
          class="filter-search-input"
          placeholder="Suchen nach Titel, Ort, Verantwortlichen…"
        />
      </div>
      <div class="filter-tabs">
        <button
          class="filter-tab"
          :class="{ 'filter-tab--active': activedept === 'Alle' }"
          @click="activedept = 'Alle'"
        >Alle</button>
        <button
          v-for="dep in DEPARTMENTS"
          :key="dep"
          class="filter-tab"
          :class="{ 'filter-tab--active': activedept === dep }"
          @click="activedept = dep"
        >{{ dep }}</button>
      </div>
    </div>

    <p v-if="error" class="error">{{ error }}</p>
    <p v-if="loading" class="loading">Laden...</p>

    <template v-if="!loading">
      <ActivityList :activities="filtered" />
      <p v-if="filtered.length === 0 && activities.length > 0" class="filter-empty">
        Keine Aktivitäten für diese Filter.
      </p>
    </template>
  </main>
</template>
