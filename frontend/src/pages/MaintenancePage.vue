<template>
  <main class="maintenance-page">
    <section class="maintenance-card" role="status" aria-live="polite">
      <div class="maintenance-header">
        <div class="maintenance-badge">Wartungsmodus</div>
        <h1>DPWeb ist aktuell in Wartung</h1>
        <p class="maintenance-message">
          {{ maintenanceMessage || 'Die Anwendung wird aktualisiert. Vielen Dank für deine Geduld.' }}
        </p>
      </div>

      <div class="maintenance-meta">
        <p v-if="maintenanceScheduledEnd" class="maintenance-end">
          Voraussichtliches Ende: <strong>{{ formattedEnd }}</strong>
        </p>
        <p class="maintenance-note">Admins können sich weiterhin anmelden.</p>
      </div>

      <section class="maintenance-joke">
        <h2>Witz des Tages</h2>
        <p>{{ jokeOfTheDay }}</p>
      </section>

      <button class="maintenance-login" @click="goToLoginPage">
        Zur Anmeldung
      </button>
    </section>
  </main>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import { useRouter } from 'vue-router'
import {
  fetchMaintenanceStatus,
  maintenanceActive,
  maintenanceMessage,
  maintenanceScheduledEnd,
} from '../composables/useMaintenance'

const router = useRouter()
let statusTimer: ReturnType<typeof setInterval> | null = null
const jokeOfTheDay = ref('Warum sind Wartungsfenster wie Zelte? Danach steht wieder alles stabil.')

const formattedEnd = computed(() => {
  const iso = maintenanceScheduledEnd.value
  if (!iso) return ''
  try {
    return new Date(iso).toLocaleString('de-CH', {
      day: '2-digit', month: '2-digit', year: 'numeric',
      hour: '2-digit', minute: '2-digit',
    })
  } catch {
    return iso
  }
})

function goToLoginPage() {
  void router.replace('/')
}

async function fetchJokeOfTheDay() {
  try {
    const response = await fetch('https://witzapi.de/api/joke/')
    if (!response.ok) return
    const jokes = await response.json() as Array<{ text?: string }>
    const jokeText = Array.isArray(jokes) ? jokes[0]?.text?.trim() : ''
    if (jokeText) {
      jokeOfTheDay.value = jokeText
    }
  } catch {
    // Keep fallback joke when API is unavailable.
  }
}

async function refreshMaintenanceState() {
  await fetchMaintenanceStatus()
  if (!maintenanceActive.value) {
    void router.replace('/')
  }
}

watch(
  () => maintenanceActive.value,
  (active) => {
    if (!active) {
      void router.replace('/')
    }
  },
)

onMounted(() => {
  void fetchJokeOfTheDay()
  void refreshMaintenanceState()
  statusTimer = setInterval(() => {
    void refreshMaintenanceState()
  }, 5000)
})

onUnmounted(() => {
  if (!statusTimer) return
  clearInterval(statusTimer)
  statusTimer = null
})
</script>

<style scoped>
.maintenance-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 28px;
  background:
    radial-gradient(circle at 15% 20%, #fff7d6 0, #fff1bd 35%, transparent 55%),
    radial-gradient(circle at 85% 80%, #ffd9d1 0, #ffe6de 30%, transparent 55%),
    linear-gradient(165deg, #f5f8fc 0, #edf2f8 100%);
}
.maintenance-card {
  width: min(680px, 100%);
  background: #fff;
  border: 2px solid #f0b429;
  border-radius: 20px;
  padding: 30px;
  box-shadow: 0 18px 36px rgba(20, 32, 52, 0.12);
}
.maintenance-header {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.maintenance-badge {
  align-self: flex-start;
  background: #fff2cf;
  border: 1px solid #f0b429;
  border-radius: 999px;
  padding: 4px 10px;
  font-size: 0.76rem;
  font-weight: 700;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  color: #8a5600;
}
h1 {
  margin: 0;
  color: #202f44;
  font-size: clamp(1.5rem, 3.4vw, 2rem);
  line-height: 1.15;
}
.maintenance-message {
  margin: 0;
  color: #30465d;
}
.maintenance-meta {
  margin-top: 12px;
  padding-top: 12px;
  border-top: 1px dashed #d7dde5;
}
.maintenance-end {
  margin: 0;
  color: #24435a;
}
.maintenance-note {
  margin: 10px 0 0;
  color: #5a7288;
}
.maintenance-joke {
  margin-top: 16px;
  padding: 14px;
  border-radius: 14px;
  background: #f4f9ff;
  border: 1px solid #c9deef;
}
.maintenance-joke h2 {
  margin: 0;
  font-size: 0.95rem;
  color: #214766;
}
.maintenance-joke p {
  margin: 8px 0 0;
  color: #2f5676;
}
.maintenance-login {
  margin-top: 18px;
  border: 0;
  border-radius: 12px;
  padding: 11px 18px;
  cursor: pointer;
  background: linear-gradient(135deg, #1f5d8f 0, #2e79b3 100%);
  color: #fff;
  font-weight: 700;
}
.maintenance-login:disabled {
  opacity: 0.65;
  cursor: not-allowed;
}
@media (max-width: 640px) {
  .maintenance-page {
    padding: 16px;
  }
  .maintenance-card {
    padding: 20px;
    border-radius: 16px;
  }
}
</style>
