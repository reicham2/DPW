<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { usePermissions } from '../composables/usePermissions'
import MailTemplatePage from './MailTemplatePage.vue'
import FormTemplatePage from './FormTemplatePage.vue'

const router = useRouter()
const { myPermissions, fetchMyPermissions } = usePermissions()

const canSeeMailTab = computed(() =>
  myPermissions.value?.mail_templates_scope && myPermissions.value.mail_templates_scope !== 'none'
)
const canSeeFormTab = computed(() =>
  myPermissions.value?.form_templates_scope && myPermissions.value.form_templates_scope !== 'none'
)

const activeTab = ref<'mail' | 'form'>('mail')

onMounted(async () => {
  await fetchMyPermissions()
  if (!canSeeMailTab.value && !canSeeFormTab.value) {
    router.replace('/')
    return
  }
  if (!canSeeMailTab.value && canSeeFormTab.value) {
    activeTab.value = 'form'
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
      <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
        <rect x="1.5" y="3.5" width="13" height="9" rx="1.5" />
        <path d="M1.5 5.5l6.5 4 6.5-4" />
      </svg>
      Mail-Vorlagen
    </button>
    <button
      v-if="canSeeFormTab"
      class="tab-btn"
      :class="{ 'tab-btn--active': activeTab === 'form' }"
      @click="activeTab = 'form'"
    >
      <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
        <rect x="2" y="1.5" width="12" height="13" rx="1.5" />
        <path d="M5 5.5h6M5 8h6M5 10.5h4" />
      </svg>
      Formular-Vorlagen
    </button>
  </nav>

  <div class="tab-content">
    <MailTemplatePage v-if="activeTab === 'mail' && canSeeMailTab" />
    <FormTemplatePage v-if="activeTab === 'form' && canSeeFormTab" />
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
</style>
