<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { CalendarDays, ClipboardList, Mail } from 'lucide-vue-next'
import { useRouter } from 'vue-router'
import { usePermissions } from '../composables/usePermissions'
import MailTemplatePage from './MailTemplatePage.vue'
import FormTemplatePage from './FormTemplatePage.vue'
import EventTemplatePage from './EventTemplatePage.vue'

const router = useRouter()
const { myPermissions, fetchMyPermissions } = usePermissions()

const canSeeMailTab = computed(() =>
  myPermissions.value?.mail_templates_scope && myPermissions.value.mail_templates_scope !== 'none'
)
const canSeeFormTab = computed(() =>
  myPermissions.value?.form_templates_scope && myPermissions.value.form_templates_scope !== 'none'
)
const canSeeEventTab = computed(() =>
  myPermissions.value?.event_templates_scope && myPermissions.value.event_templates_scope !== 'none'
)

const activeTab = ref<'mail' | 'form' | 'event'>('mail')

onMounted(async () => {
  await fetchMyPermissions()
  if (!canSeeMailTab.value && !canSeeFormTab.value && !canSeeEventTab.value) {
    router.replace('/')
    return
  }
  if (!canSeeMailTab.value) {
    if (canSeeFormTab.value) activeTab.value = 'form'
    else if (canSeeEventTab.value) activeTab.value = 'event'
  }
})
</script>

<template>
  <header class="header">
    <h1>Vorlagen</h1>
  </header>

  <nav class="tab-bar">
    <button
      v-if="canSeeMailTab"
      class="tab-btn"
      :class="{ 'tab-btn--active': activeTab === 'mail' }"
      @click="activeTab = 'mail'"
    >
      <Mail :size="16" aria-hidden="true" />
      Mail-Vorlagen
    </button>
    <button
      v-if="canSeeFormTab"
      class="tab-btn"
      :class="{ 'tab-btn--active': activeTab === 'form' }"
      @click="activeTab = 'form'"
    >
      <ClipboardList :size="16" aria-hidden="true" />
      Formular-Vorlagen
    </button>
    <button
      v-if="canSeeEventTab"
      class="tab-btn"
      :class="{ 'tab-btn--active': activeTab === 'event' }"
      @click="activeTab = 'event'"
    >
      <CalendarDays :size="16" aria-hidden="true" />
      Event-Vorlagen
    </button>
  </nav>

  <div class="tab-content">
    <MailTemplatePage v-if="activeTab === 'mail' && canSeeMailTab" />
    <FormTemplatePage v-if="activeTab === 'form' && canSeeFormTab" />
    <EventTemplatePage v-if="activeTab === 'event' && canSeeEventTab" />
  </div>
</template>

<style scoped>
.header {
  padding: 28px 24px 0;
  display: flex;
  align-items: baseline;
  gap: 12px;
}
.header h1 {
  font-size: 1.5rem;
  font-weight: 700;
  color: #1a202c;
  margin: 0;
}

.tab-bar {
  display: flex;
  gap: 4px;
  padding: 16px 24px 0;
  border-bottom: 1px solid #e5e7eb;
}
.tab-btn {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 10px 18px;
  font-size: 0.88rem;
  font-weight: 600;
  color: #6b7280;
  background: none;
  border: none;
  border-bottom: 2px solid transparent;
  cursor: pointer;
  transition: color 0.15s, border-color 0.15s;
  margin-bottom: -1px;
}
.tab-btn:hover {
  color: #374151;
}
.tab-btn--active {
  color: #1a56db;
  border-bottom-color: #1a56db;
}

.tab-content :deep(.header) {
  display: none;
}

@media (max-width: 599px) {
  .header {
    padding: 20px 16px 0;
  }
  .header h1 {
    font-size: 1.3rem;
  }
  .tab-bar {
    padding: 12px 8px 0;
    gap: 2px;
    overflow-x: auto;
    -webkit-overflow-scrolling: touch;
    scrollbar-width: none;
    flex-wrap: nowrap;
  }
  .tab-bar::-webkit-scrollbar { display: none; }
  .tab-btn {
    padding: 10px 12px;
    font-size: 0.82rem;
    white-space: nowrap;
    flex-shrink: 0;
  }
}
</style>
