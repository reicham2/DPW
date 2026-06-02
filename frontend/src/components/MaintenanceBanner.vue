<template>
  <div v-if="showMaintenanceNotice" class="banner-wrap">
    <div class="maintenance-banner" role="alert">
      <span class="maintenance-banner-icon">🔧</span>
      <span class="maintenance-banner-text">
        <strong>Geplante Wartung:</strong>
        {{ formattedStart }} – {{ formattedEnd }}
        <template v-if="message"> – {{ message }}</template>
      </span>
      <button class="maintenance-banner-close" @click="dismissedMaintenance = true" aria-label="Schliessen">✕</button>
    </div>
  </div>

  <div v-if="showReloadPrompt" class="banner-wrap">
    <div class="maintenance-banner maintenance-banner--reload" role="alert">
      <span class="maintenance-banner-icon">↻</span>
      <span class="maintenance-banner-text">
        <strong>Wartung beendet:</strong>
        Bitte lade die Seite neu, damit alle Änderungen aktiv sind.
      </span>
      <button class="maintenance-banner-action" @click="hardRefreshNow">Hardrefresh</button>
      <button class="maintenance-banner-close" @click="dismissedReload = true" aria-label="Schliessen">✕</button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { maintenanceAnnounced, maintenanceScheduledStart, maintenanceScheduledEnd, maintenanceMessage } from '../composables/useMaintenance'
import { maintenanceActive } from '../composables/useMaintenance'

const dismissedMaintenance = ref(false)
const dismissedReload = ref(false)
const sawMaintenanceActive = ref(false)

watch(
  () => maintenanceActive.value,
  (active, prev) => {
    if (active) {
      sawMaintenanceActive.value = true
      dismissedReload.value = false
      return
    }
    if (prev && !active && sawMaintenanceActive.value) {
      dismissedReload.value = false
    }
  },
)

const startsWithin24Hours = computed(() => {
  const startIso = maintenanceScheduledStart.value
  if (!startIso) return false
  const startTs = new Date(startIso).getTime()
  if (Number.isNaN(startTs)) return false
  const now = Date.now()
  const diff = startTs - now
  return diff > 0 && diff <= 24 * 60 * 60 * 1000
})

const showMaintenanceNotice = computed(
  () => maintenanceAnnounced.value && startsWithin24Hours.value && !dismissedMaintenance.value,
)

const showReloadPrompt = computed(
  () => sawMaintenanceActive.value && !maintenanceActive.value && !dismissedReload.value,
)

const message = computed(() => maintenanceMessage.value)

async function hardRefreshNow() {
  const currentUrl = new URL(window.location.href)
  currentUrl.searchParams.set('hardRefresh', Date.now().toString())

  if ('caches' in window) {
    try {
      const keys = await caches.keys()
      await Promise.all(keys.map((key) => caches.delete(key)))
    } catch {
      // ignore cache clear failures
    }
  }

  window.location.replace(currentUrl.toString())
}

function fmt(iso: string): string {
  if (!iso) return ''
  try {
    return new Date(iso).toLocaleString('de-CH', {
      day: '2-digit', month: '2-digit', year: 'numeric',
      hour: '2-digit', minute: '2-digit',
    })
  } catch {
    return iso
  }
}

const formattedStart = computed(() => fmt(maintenanceScheduledStart.value))
const formattedEnd = computed(() => fmt(maintenanceScheduledEnd.value))
</script>

<style scoped>
.banner-wrap {
  padding: 0.6rem 0.9rem 0;
}
.maintenance-banner {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.7rem 0.9rem;
  border-radius: 12px;
  border: 1px solid #f59e0b;
  background: #fef3c7;
  font-size: 0.875rem;
  color: #78350f;
  box-shadow: 0 1px 2px rgba(16, 24, 40, 0.06);
}
.maintenance-banner--reload {
  border-color: #2f6e9d;
  background: #e7f3fb;
  color: #0f3e60;
}
.maintenance-banner-icon {
  font-size: 1rem;
  flex-shrink: 0;
}
.maintenance-banner-text {
  flex: 1;
  min-width: 0;
}
.maintenance-banner-close {
  background: none;
  border: none;
  cursor: pointer;
  color: inherit;
  font-size: 1rem;
  padding: 0 0.25rem;
  flex-shrink: 0;
  line-height: 1;
}
.maintenance-banner-action {
  border: 1px solid currentColor;
  background: transparent;
  border-radius: 8px;
  padding: 0.32rem 0.55rem;
  color: inherit;
  font-size: 0.8rem;
  font-weight: 700;
  cursor: pointer;
}
.maintenance-banner-close:hover {
  opacity: 0.7;
}
@media (max-width: 700px) {
  .maintenance-banner {
    align-items: flex-start;
    flex-wrap: wrap;
  }
}
</style>
