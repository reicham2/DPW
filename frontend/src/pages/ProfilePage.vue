<template>
  <div class="page-header">
    <h1 class="page-title">Mein Profil</h1>
  </div>

  <main class="page-content">
    <div class="profile-card">
      <div class="profile-avatar-large">{{ initials }}</div>

      <form class="profile-form" @submit.prevent="save">
        <div class="form-group">
          <label class="form-label">Anzeigename</label>
          <input class="form-input form-input--readonly" type="text" :value="user?.display_name" readonly />
          <p class="form-hint">Wird automatisch aus deinem Microsoft-Konto übernommen.</p>
        </div>

        <div class="form-group">
          <label class="form-label" for="department">Stufe</label>
          <BadgeSelect
            v-if="canChangeDepartment"
            kind="department"
            :items="deptItems"
            allow-empty
            placeholder="Keine Angabe"
            :model-value="form.department || null"
            @update:model-value="(v) => form.department = (v ?? '')"
          />
          <div v-else class="profile-dept-readonly">
            <DepartmentBadge v-if="form.department" :department="form.department" />
            <span v-else class="profile-dept-empty">Keine Angabe</span>
          </div>
          <p v-if="!canChangeDepartment" class="form-hint">
            Stufe kann nur von einem Admin geändert werden.
          </p>
        </div>

        <div class="form-group">
          <label class="form-label">E-Mail</label>
          <input class="form-input form-input--readonly" type="text" :value="user?.email" readonly />
        </div>

        <div class="form-group">
          <label class="form-label">Zeitanzeige der Programmpunkte</label>
          <div class="time-mode-toggle" role="tablist">
            <button
              type="button"
              role="tab"
              :aria-selected="form.time_display_mode === 'minutes'"
              class="time-mode-pill"
              :class="{ 'time-mode-pill--active': form.time_display_mode === 'minutes' }"
              @click="form.time_display_mode = 'minutes'"
            >
              Minuten
            </button>
            <button
              type="button"
              role="tab"
              :aria-selected="form.time_display_mode === 'clock'"
              class="time-mode-pill"
              :class="{ 'time-mode-pill--active': form.time_display_mode === 'clock' }"
              @click="form.time_display_mode = 'clock'"
            >
              Uhrzeit
            </button>
          </div>
        </div>

        <div class="form-group">
          <label class="form-label form-label--with-info">
            Statistik-Anzeige auf Aktivität
            <span
              class="info-tip"
              tabindex="0"
              role="button"
              aria-label="Mehr Informationen"
              @click="statsInfoOpen = !statsInfoOpen"
              @blur="statsInfoOpen = false"
            >
              ?
              <span v-if="statsInfoOpen" class="info-tip__popover">
                <strong>Seitlich:</strong> Die Statistik erscheint rechts neben der Aktivität, wenn der Bildschirm breit genug ist. Auf schmalen Bildschirmen wird sie automatisch aufklappbar.<br /><br />
                <strong>Klappbar:</strong> Die Statistik ist immer über einen Knopf am rechten Rand zum Öffnen verfügbar – auch auf breiten Bildschirmen.
              </span>
            </span>
          </label>
          <div class="time-mode-toggle" role="tablist">
            <button
              type="button"
              role="tab"
              :aria-selected="statsDisplayMode === 'auto'"
              class="time-mode-pill"
              :class="{ 'time-mode-pill--active': statsDisplayMode === 'auto' }"
              @click="statsDisplayMode = 'auto'"
            >
              Seitlich
            </button>
            <button
              type="button"
              role="tab"
              :aria-selected="statsDisplayMode === 'always-drawer'"
              class="time-mode-pill"
              :class="{ 'time-mode-pill--active': statsDisplayMode === 'always-drawer' }"
              @click="statsDisplayMode = 'always-drawer'"
            >
              Klappbar
            </button>
          </div>
        </div>

        <div class="form-group">
          <label class="form-label">Erscheinungsbild</label>
          <div class="time-mode-toggle" role="tablist">
            <button
              type="button"
              role="tab"
              :aria-selected="themeMode === 'light'"
              class="time-mode-pill"
              :class="{ 'time-mode-pill--active': themeMode === 'light' }"
              @click="themeMode = 'light'"
            >
              Hell
            </button>
            <button
              type="button"
              role="tab"
              :aria-selected="themeMode === 'dark'"
              class="time-mode-pill"
              :class="{ 'time-mode-pill--active': themeMode === 'dark' }"
              @click="themeMode = 'dark'"
            >
              Dunkel
            </button>
          </div>
        </div>

        <div class="form-group">
          <label class="form-label">Benachrichtigungen abonnieren</label>
          <label class="notify-option">
            <input v-model="form.notify_activity_assigned" type="checkbox" />
            <span>Aktivität dir zugewiesen</span>
          </label>
          <label class="notify-option">
            <input v-model="form.notify_program_assigned" type="checkbox" />
            <span>Programmblock dir zugewiesen</span>
          </label>
          <label class="notify-option">
            <input v-model="form.notify_material_assigned" type="checkbox" />
            <span>Material an dich zugewiesen</span>
          </label>
          <label class="notify-option">
            <input v-model="form.notify_mail_own_activity" type="checkbox" />
            <span>Mail versendet für deine Aktivität</span>
          </label>
          <label class="notify-option">
            <input v-model="form.notify_mail_department" type="checkbox" />
            <span>Mail versendet in deiner Stufe</span>
          </label>
        </div>

        <div class="form-group">
          <label class="form-label">Benachrichtigungskanal</label>
          <label class="notify-option">
            <input v-model="form.notify_channel_websocket" type="checkbox" />
            <span>Browser / App</span>
          </label>
          <label class="notify-option">
            <input v-model="form.notify_channel_email" type="checkbox" />
            <span>E-Mail</span>
          </label>
          <p class="form-hint">
            Browser / App zeigt Benachrichtigungen im Browser und in der installierten PWA-App.
          </p>
        </div>

        <ErrorAlert :error="error" />
        <div v-if="saved" class="profile-success">Gespeichert!</div>

        <button type="submit" class="btn-primary" :disabled="saving">
          {{ saving ? 'Speichern...' : 'Speichern' }}
        </button>
      </form>
    </div>
  </main>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { user } from '../composables/useAuth'
import { apiFetch } from '../composables/useApi'
import { usePermissions } from '../composables/usePermissions'
import { requestBrowserNotificationPermission, syncPushSubscription } from '../composables/useNotifications'
import { statsDisplayMode, themeMode } from '../composables/useUserPrefs'
import ErrorAlert from '../components/ErrorAlert.vue'
import BadgeSelect from '../components/BadgeSelect.vue'
import DepartmentBadge from '../components/DepartmentBadge.vue'
import type { User, TimeDisplayMode } from '../types'

const { myPermissions, fetchMyPermissions, departments: deptRecords, fetchDepartments } = usePermissions()

const canChangeDepartment = computed(() => {
  const scope = myPermissions.value?.user_dept_scope
  return scope === 'own' || scope === 'own_dept' || scope === 'all'
})

const deptItems = computed(() => deptRecords.value.map(d => ({ value: d.name })))

const form = ref({
  department:   user.value?.department   ?? '',
  time_display_mode: (user.value?.time_display_mode ?? 'minutes') as TimeDisplayMode,
  notify_material_assigned: user.value?.notify_material_assigned ?? true,
  notify_activity_assigned: user.value?.notify_activity_assigned ?? true,
  notify_program_assigned: user.value?.notify_program_assigned ?? true,
  notify_mail_own_activity: user.value?.notify_mail_own_activity ?? true,
  notify_mail_department: user.value?.notify_mail_department ?? true,
  notify_channel_websocket: user.value?.notify_channel_websocket ?? true,
  notify_channel_email: user.value?.notify_channel_email ?? false,
})
const saving = ref(false)
const saved  = ref(false)
const error  = ref<string | null>(null)
const statsInfoOpen = ref(false)

const initials = computed(() => {
  const name = user.value?.display_name || ''
  const words = name.split(' ').filter(Boolean)
  if (words.length === 1) return name.slice(0, 2).toUpperCase()
  return words.slice(0, 2).map(w => w[0].toUpperCase()).join('')
})

let initialLoaded = false

onMounted(async () => {
  await Promise.all([fetchMyPermissions(), fetchDepartments()])
  if (user.value) {
    form.value.department   = user.value.department ?? ''
    form.value.time_display_mode = user.value.time_display_mode ?? 'minutes'
    form.value.notify_material_assigned = user.value.notify_material_assigned ?? true
    form.value.notify_activity_assigned = user.value.notify_activity_assigned ?? true
    form.value.notify_program_assigned = user.value.notify_program_assigned ?? true
    form.value.notify_mail_own_activity = user.value.notify_mail_own_activity ?? true
    form.value.notify_mail_department = user.value.notify_mail_department ?? true
    form.value.notify_channel_websocket = user.value.notify_channel_websocket ?? true
    form.value.notify_channel_email = user.value.notify_channel_email ?? false
  }
  initialLoaded = true
})

async function save() {
  saving.value = true
  saved.value  = false
  error.value  = null
  try {
    const res = await apiFetch('/api/me', {
      method: 'PATCH',
      body: JSON.stringify({
        department:   form.value.department || null,
        time_display_mode: form.value.time_display_mode,
        notify_material_assigned: form.value.notify_material_assigned,
        notify_activity_assigned: form.value.notify_activity_assigned,
        notify_program_assigned: form.value.notify_program_assigned,
        notify_mail_own_activity: form.value.notify_mail_own_activity,
        notify_mail_department: form.value.notify_mail_department,
        notify_channel_websocket: form.value.notify_channel_websocket,
        notify_channel_email: form.value.notify_channel_email,
      }),
    })
    if (!res.ok) throw new Error(await res.text())
    const updated: User = await res.json()
    // Update global user state
    if (user.value) Object.assign(user.value, updated)

    if (form.value.notify_channel_websocket) {
      await requestBrowserNotificationPermission()
    }
    await syncPushSubscription(form.value.notify_channel_websocket)
    saved.value = true
    setTimeout(() => { saved.value = false }, 3000)
  } catch (e) {
    error.value = String(e)
  } finally {
    saving.value = false
  }
}
</script>

<style scoped>
.page-header {
  padding: 32px 24px 0;
  max-width: 560px;
  margin: 0 auto;
}
.page-title {
  font-size: 1.5rem;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0;
}
.page-content {
  padding: 24px;
  max-width: 560px;
  margin: 0 auto;
}
.profile-card {
  background: var(--card-bg);
  border-radius: 14px;
  padding: 32px;
  box-shadow: 0 2px 12px rgba(0,0,0,0.07);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 28px;
}
.profile-avatar-large {
  width: 72px;
  height: 72px;
  border-radius: 50%;
  background: var(--accent);
  color: #fff;
  font-size: 1.6rem;
  font-weight: 700;
  display: flex;
  align-items: center;
  justify-content: center;
  letter-spacing: 1px;
}
.profile-form {
  width: 100%;
  display: flex;
  flex-direction: column;
  gap: 18px;
}
.form-group {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.form-label {
  font-size: 0.85rem;
  font-weight: 600;
  color: var(--text-secondary);
}
.form-input {
  padding: 10px 12px;
  border: 1.5px solid var(--input-border);
  border-radius: 8px;
  font-size: 0.95rem;
  color: var(--input-color);
  background: var(--input-bg);
  outline: none;
  transition: border-color 0.15s;
  width: 100%;
  box-sizing: border-box;
}
.form-input:focus {
  border-color: var(--accent);
}
.form-input--readonly {
  background: var(--input-readonly-bg);
  color: var(--input-readonly-color);
  cursor: default;
}
.btn-primary {
  padding: 11px 24px;
  background: var(--btn-primary-bg);
  color: #fff;
  border: none;
  border-radius: 8px;
  font-size: 0.95rem;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.15s;
  align-self: flex-start;
}
.btn-primary:hover:not(:disabled) {
  background: var(--btn-primary-bg-hover);
}
.btn-primary:disabled {
  opacity: 0.6;
  cursor: default;
}
.form-hint {
  font-size: 0.78rem;
  color: var(--text-subtle);
  margin: 2px 0 0;
}
.profile-dept-readonly {
  display: flex;
  align-items: center;
  min-height: 40px;
  padding: 8px 12px;
  border: 1.5px solid var(--input-border);
  border-radius: 8px;
  background: var(--input-readonly-bg);
}
.profile-dept-empty {
  color: var(--text-muted);
  font-size: 0.88rem;
}
.profile-error {
  color: var(--error-color);
  font-size: 0.875rem;
  background: var(--error-bg);
  border-radius: 6px;
  padding: 8px 12px;
}
.time-mode-toggle {
  display: inline-flex;
  align-items: center;
  gap: 0;
  padding: 4px;
  background: var(--bg-hover);
  border-radius: 999px;
  align-self: flex-start;
}
.time-mode-pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 8px 18px;
  border: none;
  background: transparent;
  color: var(--text-secondary);
  font-size: 0.9rem;
  font-weight: 500;
  border-radius: 999px;
  cursor: pointer;
  transition: background 0.15s, color 0.15s, box-shadow 0.15s;
}
.time-mode-pill:hover:not(.time-mode-pill--active) {
  color: var(--text-primary);
}
.time-mode-pill--active {
  background: var(--accent-bg);
  color: var(--text-primary);
  box-shadow: var(--shadow-sm);
}
.time-mode-check {
  color: var(--accent);
  font-weight: 700;
}
.profile-success {
  color: var(--success-color);
  font-size: 0.875rem;
  background: var(--success-bg);
  border-radius: 6px;
  padding: 8px 12px;
}

.notify-option {
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--text-secondary);
  font-size: 0.9rem;
}

.notify-option input {
  width: 16px;
  height: 16px;
}

.form-label--with-info {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}
.info-tip {
  position: relative;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 18px;
  height: 18px;
  font-size: 0.72rem;
  font-weight: 700;
  color: var(--text-secondary);
  background: var(--border);
  border-radius: 50%;
  cursor: pointer;
  user-select: none;
  transition: background 0.15s, color 0.15s;
  outline: none;
}
.info-tip:hover,
.info-tip:focus {
  background: var(--accent);
  color: var(--btn-primary-color);
}
.info-tip__popover {
  position: absolute;
  top: calc(100% + 6px);
  left: 0;
  z-index: 20;
  width: min(280px, 78vw);
  padding: 10px 12px;
  background: #1f2937;
  color: #f9fafb;
  font-size: 0.8rem;
  font-weight: 400;
  line-height: 1.45;
  border-radius: 8px;
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.18);
  text-align: left;
  cursor: default;
}
.info-tip__popover strong {
  color: #fff;
  font-weight: 700;
}
.info-tip__popover::before {
  content: '';
  position: absolute;
  top: -5px;
  left: 6px;
  width: 10px;
  height: 10px;
  background: #1f2937;
  transform: rotate(45deg);
}
</style>
