<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { Lock } from 'lucide-vue-next'
import { apiFetch, formatApiError } from '../composables/useApi'
import { applyRuntimeConfig } from '../config'
import { setRuntimeAuthConfig } from '../composables/useAuth'
import ErrorAlert from './ErrorAlert.vue'
import type { AppSettingItem } from '../types'
import { fetchAdminMaintenance, saveAdminMaintenance, type MaintenanceWindow } from '../composables/useMaintenance'
import { useWebSocket } from '../composables/useWebSocket'

useWebSocket((event) => {
  if (event.event !== 'maintenance_status') return
  maintenanceActive.value = event.active
  maintenanceScheduled.value = event.scheduled_now
  maintenanceForm.value.enabled = event.enabled
  if (typeof event.message === 'string') maintenanceForm.value.message = event.message
})

const props = withDefaults(defineProps<{ mode?: 'settings' | 'maintenance' }>(), {
  mode: 'settings',
})

type SettingFieldType = 'text' | 'password' | 'url' | 'number' | 'boolean'

interface SettingMeta {
  title: string
  hint: string
  category: string
  categoryOrder: number
  fieldType: SettingFieldType
  order: number
  min?: number
  max?: number
  step?: number
}

const settings = ref<AppSettingItem[]>([])
const loading = ref(false)
const error = ref<string | null>(null)
const saveError = ref<string | null>(null)
const savingKey = ref<string | null>(null)
const editValues = ref<Record<string, string>>({})
const resettingAzure = ref(false)
const hiddenSettingsKeys = new Set([
  'push.vapid_public_key',
  'push.vapid_private_key',
  'maintenance.enabled',
  'maintenance.message',
  'maintenance.scheduled_start',
  'maintenance.scheduled_end',
  'maintenance.windows_json',
])

const settingMeta: Record<string, SettingMeta> = {
  'azure.tenant_id': {
    title: 'Azure Tenant ID',
    hint: 'Tenant-ID aus Azure AD',
    category: 'Microsoft Anmeldung',
    categoryOrder: 10,
    fieldType: 'text',
    order: 10,
  },
  'azure.client_id': {
    title: 'Azure Client ID',
    hint: 'Application (Client) ID',
    category: 'Microsoft Anmeldung',
    categoryOrder: 10,
    fieldType: 'text',
    order: 11,
  },
  'azure.client_secret': {
    title: 'Azure Client Secret',
    hint: 'Wird verschluesselt gespeichert',
    category: 'Microsoft Anmeldung',
    categoryOrder: 10,
    fieldType: 'password',
    order: 12,
  },
  'frontend.public_base_url': {
    title: 'Öffentliche Basis-URL',
    hint: 'z.B. https://dpw.example.org',
    category: 'Allgemein',
    categoryOrder: 20,
    fieldType: 'url',
    order: 20,
  },
  'frontend.autosave_interval_ms': {
    title: 'Autosave Intervall (ms)',
    hint: 'Intervall fuer Auto-Save in Millisekunden',
    category: 'Allgemein',
    categoryOrder: 20,
    fieldType: 'number',
    order: 21,
    min: 250,
    max: 600000,
    step: 50,
  },
  'frontend.autosave_debounce': {
    title: 'Autosave Debounce',
    hint: 'Bei Ja wird nach Tipp-Pause gespeichert',
    category: 'Allgemein',
    categoryOrder: 20,
    fieldType: 'boolean',
    order: 22,
  },
  'midata.weather_refresh_interval_ms': {
    title: 'MiData Refresh Intervall (ms)',
    hint: 'Auto-Refresh fuer MiData/Wetter',
    category: 'Teilnehmende Anzahl',
    categoryOrder: 30,
    fieldType: 'number',
    order: 30,
    min: 10000,
    max: 86400000,
    step: 1000,
  },
  'midata.api_key': {
    title: 'MiData API Key',
    hint: 'Wird verschluesselt gespeichert',
    category: 'Teilnehmende Anzahl',
    categoryOrder: 30,
    fieldType: 'password',
    order: 31,
  },
  'midata.api_url_template': {
    title: 'MiData API URL Template',
    hint: 'Muss {group_id} enthalten',
    category: 'Teilnehmende Anzahl',
    categoryOrder: 30,
    fieldType: 'url',
    order: 32,
  },
  'midata.api_timeout_ms': {
    title: 'MiData API Timeout (ms)',
    hint: 'HTTP Timeout fuer MiData Requests',
    category: 'Teilnehmende Anzahl',
    categoryOrder: 30,
    fieldType: 'number',
    order: 33,
    min: 500,
    max: 120000,
    step: 100,
  },
  'wp.url': {
    title: 'WordPress URL',
    hint: 'Basis-URL der WordPress Instanz',
    category: 'Wordpress Veroeffentlichung',
    categoryOrder: 40,
    fieldType: 'url',
    order: 40,
  },
  'wp.user': {
    title: 'WordPress User',
    hint: 'Technischer User fuer Event Publishing',
    category: 'Wordpress Veroeffentlichung',
    categoryOrder: 40,
    fieldType: 'text',
    order: 41,
  },
  'wp.app_password': {
    title: 'WordPress App Password',
    hint: 'Wird verschluesselt gespeichert',
    category: 'Wordpress Veroeffentlichung',
    categoryOrder: 40,
    fieldType: 'password',
    order: 42,
  },
  'push.vapid_subject': {
    title: 'Web Push Kontakt',
    hint: 'E-Mail wird als mailto:-Kontakt fuer VAPID verwendet',
    category: 'Web Push',
    categoryOrder: 45,
    fieldType: 'text',
    order: 45,
  },
  'push.vapid_public_key': {
    title: 'VAPID Public Key',
    hint: 'Wird automatisch erzeugt, wenn leer',
    category: 'Web Push',
    categoryOrder: 45,
    fieldType: 'password',
    order: 46,
  },
  'push.vapid_private_key': {
    title: 'VAPID Private Key',
    hint: 'Wird automatisch erzeugt, wenn leer',
    category: 'Web Push',
    categoryOrder: 45,
    fieldType: 'password',
    order: 47,
  },
  'github.token': {
    title: 'GitHub Token',
    hint: 'Token fuer Bug-Reports als Issues',
    category: 'Bug-Reports',
    categoryOrder: 50,
    fieldType: 'password',
    order: 50,
  },
  'github.repo': {
    title: 'GitHub Repository',
    hint: 'Format: owner/repo',
    category: 'Bug-Reports',
    categoryOrder: 50,
    fieldType: 'text',
    order: 51,
  },
}

function metaFor(item: AppSettingItem): SettingMeta {
  const known = settingMeta[item.key]
  if (known) return known
  return {
    title: item.key,
    hint: item.env_name,
    category: 'Weitere',
    categoryOrder: 999,
    fieldType: item.is_secret ? 'password' : 'text',
    order: 999,
  }
}

const sortedSettings = computed(() => {
  return [...settings.value]
    .filter((item) => !hiddenSettingsKeys.has(item.key))
    .sort((a, b) => {
    const ma = metaFor(a)
    const mb = metaFor(b)
    if (ma.categoryOrder !== mb.categoryOrder) return ma.categoryOrder - mb.categoryOrder
    if (ma.category !== mb.category) return ma.category.localeCompare(mb.category, 'de')
    if (ma.order !== mb.order) return ma.order - mb.order
    return ma.title.localeCompare(mb.title, 'de')
  })
})

const azureSettings = computed(() => sortedSettings.value.filter((item) => isAzureSetting(item)))

const nonAzureSettings = computed(() => sortedSettings.value.filter((item) => !isAzureSetting(item)))

const invalidByKey = computed(() => {
  const out: Record<string, string | null> = {}
  for (const item of sortedSettings.value) {
    out[item.key] = validateValue(item, editValues.value[item.key] ?? '')
  }
  return out
})

function placeholderFor(item: AppSettingItem) {
  if (item.key === 'push.vapid_subject') {
    return item.value?.replace(/^mailto:/, '') ?? ''
  }
  if (item.is_secret) {
    if (item.source === 'env') return 'Gesperrt'
    if (item.configured) return 'Gespeichert (verdeckt)'
    return 'Nicht gesetzt'
  }
  return item.value ?? ''
}

function fieldTypeFor(item: AppSettingItem): SettingFieldType {
  const fieldType = metaFor(item).fieldType
  if (item.is_secret && fieldType !== 'number' && fieldType !== 'boolean') return 'password'
  return fieldType
}

function groupedCategoryLabel(item: AppSettingItem, index: number, list = sortedSettings.value): string | null {
  const current = metaFor(item).category
  if (index === 0) return current
  const previous = metaFor(list[index - 1]).category
  return previous === current ? null : current
}

function setBooleanValue(item: AppSettingItem, next: boolean) {
  editValues.value[item.key] = next ? 'true' : 'false'
}

function onBooleanToggle(item: AppSettingItem, event: Event) {
  const target = event.target as HTMLInputElement | null
  setBooleanValue(item, !!target?.checked)
}

function booleanValue(item: AppSettingItem): boolean {
  const value = (editValues.value[item.key] ?? '').trim().toLowerCase()
  return value === 'true' || value === '1' || value === 'yes' || value === 'on'
}

function validateValue(item: AppSettingItem, raw: string): string | null {
  if (item.locked_by_env || isAzureSetting(item)) return null
  const value = raw.trim()
  if (item.key === 'push.vapid_subject' && !value) {
    return 'Kontakt darf nicht leer sein.'
  }
  if (!value) return null

  const meta = metaFor(item)
  if (meta.fieldType === 'url') {
    if (!/^https?:\/\/\S+$/i.test(value)) {
      return 'Bitte mit http:// oder https:// beginnen.'
    }
  }

  if (meta.fieldType === 'number') {
    const parsed = Number(value)
    if (!Number.isInteger(parsed)) return 'Bitte ganze Zahl eingeben.'
    if (meta.min != null && parsed < meta.min) return `Minimum: ${meta.min}`
    if (meta.max != null && parsed > meta.max) return `Maximum: ${meta.max}`
  }

  if (item.key === 'github.repo' && !/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(value)) {
    return 'Format muss owner/repo sein.'
  }

  if (item.key === 'push.vapid_subject' && !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value)) {
    return 'Bitte gueltige E-Mail angeben.'
  }

  if (item.key === 'midata.api_url_template' && !value.includes('{group_id}')) {
    return 'Template sollte {group_id} enthalten.'
  }

  return null
}

function isClearEnabled(item: AppSettingItem) {
  if (item.key === 'push.vapid_subject') return false
  return !item.locked_by_env && !isAzureSetting(item)
}

function isAzureSetting(item: AppSettingItem): boolean {
  return (
    item.key === 'azure.tenant_id' ||
    item.key === 'azure.client_id' ||
    item.key === 'azure.client_secret'
  )
}

function itemValue(settingsList: AppSettingItem[], key: string): string | undefined {
  return settingsList.find((item) => item.key === key)?.value
}

function isConfigured(settingsList: AppSettingItem[], key: string): boolean {
  const item = settingsList.find((entry) => entry.key === key)
  return !!item?.configured
}

function applyRuntimeOverrides(settingsList: AppSettingItem[]) {
  const autosaveIntervalRaw = itemValue(settingsList, 'frontend.autosave_interval_ms')
  const autosaveDebounceRaw = itemValue(settingsList, 'frontend.autosave_debounce')
  const weatherRefreshRaw = itemValue(settingsList, 'midata.weather_refresh_interval_ms')
  const wpUrl = itemValue(settingsList, 'wp.url')
  const publicBaseUrl = itemValue(settingsList, 'frontend.public_base_url')
  const azureTenantId = itemValue(settingsList, 'azure.tenant_id') ?? ''
  const azureClientId = itemValue(settingsList, 'azure.client_id') ?? ''
  const midataEnabled = isConfigured(settingsList, 'midata.api_key')
  const wpPublishingEnabled =
    isConfigured(settingsList, 'wp.url') &&
    isConfigured(settingsList, 'wp.user') &&
    isConfigured(settingsList, 'wp.app_password')
  const githubBugReportEnabled = isConfigured(settingsList, 'github.token')

  applyRuntimeConfig({
    autosaveIntervalMs: autosaveIntervalRaw ? Number(autosaveIntervalRaw) : undefined,
    autosaveDebounce: autosaveDebounceRaw ? autosaveDebounceRaw === 'true' : undefined,
    midataWeatherRefreshIntervalMs: weatherRefreshRaw ? Number(weatherRefreshRaw) : undefined,
    midataEnabled,
    wpPublishingEnabled,
    githubBugReportEnabled,
    wpUrl,
    publicBaseUrl,
  })

  setRuntimeAuthConfig(azureTenantId, azureClientId)
}

async function fetchSettings() {
  loading.value = true
  error.value = null
  try {
    const res = await apiFetch('/api/admin/app-settings')
    if (!res.ok) throw new Error(await res.text())
    settings.value = (await res.json()) as AppSettingItem[]
    applyRuntimeOverrides(settings.value)

    const next: Record<string, string> = {}
    for (const item of settings.value) {
      if (item.key === 'push.vapid_subject') {
        next[item.key] = (item.value ?? '').replace(/^mailto:/, '')
        continue
      }
      next[item.key] = item.is_secret ? '' : (item.value ?? '')
    }
    editValues.value = next
  } catch (e) {
    error.value = formatApiError(e)
  } finally {
    loading.value = false
  }
}

async function saveSetting(item: AppSettingItem) {
  saveError.value = null
  savingKey.value = item.key
  try {
    const validationError = validateValue(item, editValues.value[item.key] ?? '')
    if (validationError) {
      saveError.value = validationError
      return
    }

    const res = await apiFetch(`/api/admin/app-settings/${encodeURIComponent(item.key)}`, {
      method: 'PUT',
      body: JSON.stringify({ value: editValues.value[item.key] ?? '' }),
    })
    if (!res.ok) throw new Error(await res.text())
    await fetchSettings()
  } catch (e) {
    saveError.value = formatApiError(e)
  } finally {
    savingKey.value = null
  }
}

async function clearSetting(item: AppSettingItem) {
  saveError.value = null
  savingKey.value = item.key
  try {
    const res = await apiFetch(`/api/admin/app-settings/${encodeURIComponent(item.key)}`, {
      method: 'PUT',
      body: JSON.stringify({ value: null }),
    })
    if (!res.ok) throw new Error(await res.text())
    editValues.value[item.key] = ''
    await fetchSettings()
  } catch (e) {
    saveError.value = formatApiError(e)
  } finally {
    savingKey.value = null
  }
}

async function resetAzureAuth() {
  if (resettingAzure.value) return
  const confirmed = window.confirm(
    'Azure Login zuruecksetzen? Danach wirst du zum Setup weitergeleitet und musst die Microsoft-Anmeldung neu einrichten.'
  )
  if (!confirmed) return

  saveError.value = null
  resettingAzure.value = true
  try {
    const res = await apiFetch('/api/admin/reset-azure-auth', {
      method: 'POST',
    })
    if (!res.ok) throw new Error(await res.text())

    setRuntimeAuthConfig('', '')
    window.location.assign('/setup')
  } catch (e) {
    saveError.value = formatApiError(e)
  } finally {
    resettingAzure.value = false
  }
}

onMounted(() => {
  if (props.mode === 'maintenance') {
    void loadMaintenance()
    return
  }
  void fetchSettings()
})

// ---------- Wartungsmodus ----------
interface MaintenanceForm {
  enabled: boolean
  message: string
}

const maintenanceForm = ref<MaintenanceForm>({
  enabled: false,
  message: '',
})
const maintenanceActive = ref(false)
const maintenanceScheduled = ref(false)
const loadingMaintenance = ref(false)
const savingMaintenance = ref(false)
const maintenanceError = ref<string | null>(null)
const maintenanceWindows = ref<MaintenanceWindow[]>([])

const newWindowStart = ref('')
const newWindowEnd = ref('')
const newWindowRecurrence = ref<'none' | 'daily' | 'weekly' | 'monthly'>('none')
const newWindowInterval = ref(1)
const newWindowUntil = ref('')

function formatDateTimeLocal(iso: string): string {
  if (!iso) return '—'
  try {
    return new Date(iso).toLocaleString('de-CH', {
      day: '2-digit', month: '2-digit', year: 'numeric',
      hour: '2-digit', minute: '2-digit',
    })
  } catch {
    return iso
  }
}

function formatDateLocal(iso: string): string {
  if (!iso) return '—'
  try {
    return new Date(iso).toLocaleDateString('de-CH', {
      day: '2-digit', month: '2-digit', year: 'numeric',
    })
  } catch {
    return iso
  }
}

function formatTimeLocal(iso: string): string {
  if (!iso) return '—'
  try {
    return new Date(iso).toLocaleTimeString('de-CH', {
      hour: '2-digit', minute: '2-digit',
    })
  } catch {
    return iso
  }
}

function formatWindowRange(startIso: string, endIso: string): string {
  const start = new Date(startIso)
  const end = new Date(endIso)
  if (Number.isNaN(start.getTime()) || Number.isNaN(end.getTime())) {
    return `${formatDateTimeLocal(startIso)} bis ${formatDateTimeLocal(endIso)}`
  }
  const sameDay =
    start.getFullYear() === end.getFullYear() &&
    start.getMonth() === end.getMonth() &&
    start.getDate() === end.getDate()
  if (sameDay) {
    return `${formatDateLocal(startIso)} von ${formatTimeLocal(startIso)} bis ${formatTimeLocal(endIso)}`
  }
  return `${formatDateTimeLocal(startIso)} bis ${formatDateTimeLocal(endIso)}`
}

function windowStatus(window: MaintenanceWindow): 'planned' | 'active' | 'ended' {
  if (window.status === 'planned' || window.status === 'active' || window.status === 'ended') {
    return window.status
  }
  const now = Date.now()
  const startTs = new Date(window.start).getTime()
  const endTs = new Date(window.end).getTime()
  if (!Number.isNaN(startTs) && !Number.isNaN(endTs)) {
    if (now >= startTs && now < endTs) return 'active'
    if (now >= endTs) return 'ended'
  }
  return 'planned'
}

function windowStatusLabel(window: MaintenanceWindow): string {
  const status = windowStatus(window)
  if (status === 'active') return 'Aktiv'
  if (status === 'ended') return 'Beendet'
  return 'Geplant'
}

function recurrenceLabel(freq: string, interval = 1): string {
  if (freq === 'daily') return interval > 1 ? `alle ${interval} Tage` : 'täglich'
  if (freq === 'weekly') return interval > 1 ? `alle ${interval} Wochen` : 'wöchentlich'
  if (freq === 'monthly') return interval > 1 ? `alle ${interval} Monate` : 'monatlich'
  return 'einmalig'
}

function resetWindowDraft() {
  newWindowStart.value = ''
  newWindowEnd.value = ''
  newWindowRecurrence.value = 'none'
  newWindowInterval.value = 1
  newWindowUntil.value = ''
}

function windowsWithGlobalMessage(windows: MaintenanceWindow[]): MaintenanceWindow[] {
  const globalMessage = maintenanceForm.value.message.trim()
  return windows.map((w) => ({ ...w, message: globalMessage }))
}

async function persistWindows(): Promise<void> {
  await saveAdminMaintenance({
    windows: windowsWithGlobalMessage(maintenanceWindows.value),
    message: maintenanceForm.value.message,
  })
}

async function saveMaintenanceMessage() {
  savingMaintenance.value = true
  maintenanceError.value = null
  try {
    await saveAdminMaintenance({
      message: maintenanceForm.value.message,
      windows: windowsWithGlobalMessage(maintenanceWindows.value),
    })
    await loadMaintenance()
  } catch (e) {
    maintenanceError.value = formatApiError(e)
  } finally {
    savingMaintenance.value = false
  }
}

async function addWindow() {
  maintenanceError.value = null
  const start = newWindowStart.value.trim()
  const end = newWindowEnd.value.trim()
  if (!start || !end) {
    maintenanceError.value = 'Bitte Start und Ende ausfüllen.'
    return
  }
  if (new Date(end).getTime() <= new Date(start).getTime()) {
    maintenanceError.value = 'Das Ende muss nach dem Start liegen.'
    return
  }
  const interval = Math.max(1, Math.floor(newWindowInterval.value || 1))
  const nextWindow: MaintenanceWindow = {
    id: `w-${Date.now()}-${maintenanceWindows.value.length + 1}`,
    start,
    end,
    message: maintenanceForm.value.message.trim(),
    recurrence: newWindowRecurrence.value,
    interval,
    until: newWindowRecurrence.value === 'none' ? '' : newWindowUntil.value.trim(),
  }
  maintenanceWindows.value.push(nextWindow)

  savingMaintenance.value = true
  try {
    await persistWindows()
    await loadMaintenance()
    resetWindowDraft()
  } catch (e) {
    maintenanceWindows.value = maintenanceWindows.value.filter((w) => w.id !== nextWindow.id)
    maintenanceError.value = formatApiError(e)
  } finally {
    savingMaintenance.value = false
  }
}

async function removeWindow(id: string) {
  maintenanceError.value = null
  const previous = [...maintenanceWindows.value]
  maintenanceWindows.value = maintenanceWindows.value.filter((w) => w.id !== id)

  savingMaintenance.value = true
  try {
    await persistWindows()
    await loadMaintenance()
  } catch (e) {
    maintenanceWindows.value = previous
    maintenanceError.value = formatApiError(e)
  } finally {
    savingMaintenance.value = false
  }
}

async function endWindowNow(id: string) {
  savingMaintenance.value = true
  maintenanceError.value = null
  try {
    await saveAdminMaintenance({ end_window_id: id })
    await loadMaintenance()
  } catch (e) {
    maintenanceError.value = formatApiError(e)
  } finally {
    savingMaintenance.value = false
  }
}

async function loadMaintenance() {
  loadingMaintenance.value = true
  maintenanceError.value = null
  try {
    const data = await fetchAdminMaintenance()
    maintenanceActive.value = data.active
    maintenanceScheduled.value = data.scheduled_now ?? false
    maintenanceForm.value = {
      enabled: data.enabled,
      message: data.message ?? '',
    }
    maintenanceWindows.value = [...(data.windows ?? [])]
  } catch (e) {
    maintenanceError.value = formatApiError(e)
  } finally {
    loadingMaintenance.value = false
  }
}

async function disableMaintenanceNow() {
  savingMaintenance.value = true
  maintenanceError.value = null
  try {
    await saveAdminMaintenance({
      enabled: false,
    })
    await loadMaintenance()
  } catch (e) {
    maintenanceError.value = formatApiError(e)
  } finally {
    savingMaintenance.value = false
  }
}

async function toggleMaintenanceNow() {
  if (maintenanceActive.value) {
    await disableMaintenanceNow()
    return
  }
  savingMaintenance.value = true
  maintenanceError.value = null
  try {
    await saveAdminMaintenance({ enabled: true })
    await loadMaintenance()
  } catch (e) {
    maintenanceError.value = formatApiError(e)
  } finally {
    savingMaintenance.value = false
  }
}

watch(
  () => props.mode,
  (mode) => {
    if (mode === 'maintenance') {
      void loadMaintenance()
      return
    }
    void fetchSettings()
  },
)
</script>

<template>
  <section class="settings-wrap">
    <header v-if="props.mode === 'settings'" class="settings-header">
      <div>
        <h2>System-Konfiguration</h2>
      </div>
      <button class="btn-refresh" :disabled="loading" @click="fetchSettings">
        {{ loading ? 'Lädt...' : 'Aktualisieren' }}
      </button>
    </header>

    <header v-else class="settings-header">
      <div>
        <h2>Wartungsfenster</h2>
      </div>
    </header>

    <ErrorAlert v-if="props.mode === 'settings'" :error="error" />
    <ErrorAlert v-if="props.mode === 'settings'" :error="saveError" />

    <div v-if="props.mode === 'settings'" class="settings-list">
      <div v-if="loading" class="maint-loading">Lade Einstellungen...</div>

      <div v-else-if="!sortedSettings.length" class="maint-loading">Keine Einstellungen gefunden.</div>

      <template v-else>
      <section v-if="azureSettings.length" class="settings-box settings-box--azure">
        <div class="group-header group-header--box">
          <div>
            <h3 class="group-title">Microsoft Anmeldung</h3>
            <p class="group-copy">Tenant, Client ID und Secret werden gemeinsam verwaltet und können hier nur zurückgesetzt werden.</p>
          </div>
          <button
            class="btn-reset-azure"
            :disabled="resettingAzure"
            @click="resetAzureAuth"
          >
            {{ resettingAzure ? 'Setze zurück...' : 'Alles zurücksetzen' }}
          </button>
        </div>
        <article v-for="item in azureSettings" :key="item.key" class="setting-row setting-row--boxed">
          <div class="setting-main">
            <div class="setting-headline">
              <strong>{{ metaFor(item).title }}</strong>
            </div>
            <p class="setting-hint">{{ metaFor(item).hint }}</p>
          </div>

          <div class="setting-edit">
            <div class="setting-control" :class="{ 'setting-control--locked': item.locked_by_env }">
              <span
                v-if="item.locked_by_env"
                class="setting-lock"
                :title="`Dieses Feld ist durch die ENV-Datei gesperrt (${item.env_name}).`"
                aria-label="Durch ENV-Datei gesperrt"
              >
                <Lock :size="13" />
              </span>

              <input
                v-if="fieldTypeFor(item) === 'text' || fieldTypeFor(item) === 'password' || fieldTypeFor(item) === 'url'"
                v-model="editValues[item.key]"
                class="setting-input"
                :type="fieldTypeFor(item)"
                :disabled="true"
                :placeholder="placeholderFor(item)"
                :autocomplete="fieldTypeFor(item) === 'password' ? 'new-password' : 'off'"
                :inputmode="fieldTypeFor(item) === 'url' ? 'url' : 'text'"
              />
            </div>
          </div>

          <div class="setting-actions">
            <button class="btn-save" disabled>Gesperrt</button>
          </div>
        </article>
      </section>

      <template v-for="(item, index) in nonAzureSettings" :key="item.key">
        <div v-if="groupedCategoryLabel(item, index, nonAzureSettings)" class="group-header">
          <h3 class="group-title">{{ groupedCategoryLabel(item, index, nonAzureSettings) }}</h3>
        </div>
        <article class="setting-row">
          <div class="setting-main">
            <div class="setting-headline">
              <strong>{{ metaFor(item).title }}</strong>
            </div>
            <p class="setting-hint">{{ metaFor(item).hint }}</p>
          </div>

          <div class="setting-edit">
            <div class="setting-control" :class="{ 'setting-control--locked': item.locked_by_env }">
              <span
                v-if="item.locked_by_env"
                class="setting-lock"
                :title="`Dieses Feld ist durch die ENV-Datei gesperrt (${item.env_name}).`"
                aria-label="Durch ENV-Datei gesperrt"
              >
                <Lock :size="13" />
              </span>

              <input
                v-if="fieldTypeFor(item) === 'text' || fieldTypeFor(item) === 'password' || fieldTypeFor(item) === 'url'"
                v-model="editValues[item.key]"
                class="setting-input"
                :type="fieldTypeFor(item)"
                :disabled="item.locked_by_env || isAzureSetting(item) || savingKey === item.key"
                :placeholder="placeholderFor(item)"
                :autocomplete="fieldTypeFor(item) === 'password' ? 'new-password' : 'off'"
                :inputmode="fieldTypeFor(item) === 'url' ? 'url' : 'text'"
              />

              <input
                v-else-if="fieldTypeFor(item) === 'number'"
                v-model="editValues[item.key]"
                class="setting-input"
                type="number"
                :min="metaFor(item).min"
                :max="metaFor(item).max"
                :step="metaFor(item).step ?? 1"
                :disabled="item.locked_by_env || isAzureSetting(item) || savingKey === item.key"
                :placeholder="placeholderFor(item)"
                inputmode="numeric"
              />

              <label v-else class="bool-toggle">
                <input
                  type="checkbox"
                  :checked="booleanValue(item)"
                  :disabled="item.locked_by_env || isAzureSetting(item) || savingKey === item.key"
                  @change="onBooleanToggle(item, $event)"
                />
              </label>
            </div>

            <p v-if="invalidByKey[item.key]" class="setting-invalid">{{ invalidByKey[item.key] }}</p>
          </div>

          <div class="setting-actions">
            <button
              class="btn-save"
              :disabled="item.locked_by_env || isAzureSetting(item) || savingKey === item.key || !!invalidByKey[item.key]"
              @click="saveSetting(item)"
            >
              {{ savingKey === item.key ? '...' : 'Speichern' }}
            </button>
            <button class="btn-clear" :disabled="!isClearEnabled(item) || savingKey === item.key" @click="clearSetting(item)">
              Leeren
            </button>
          </div>
        </article>
      </template>
      </template>
    </div>

    <section v-if="props.mode === 'maintenance'" class="maint-section">
      <div class="maint-header">
        <div>
          <h3 class="maint-title">Wartungsmodus</h3>
          <p class="maint-copy">Nur Admins koennen sich anmelden, wenn der Wartungsmodus aktiv ist.</p>
        </div>
        <span
          class="maint-status-badge"
          :class="maintenanceActive ? 'maint-status-badge--active' : (maintenanceScheduled ? 'maint-status-badge--scheduled' : 'maint-status-badge--off')"
        >
          {{ maintenanceActive ? 'Aktiv' : (maintenanceScheduled ? 'Geplant' : 'Inaktiv') }}
        </span>
      </div>
      <div class="maint-toggle-row">
        <button class="btn-save" :disabled="savingMaintenance || loadingMaintenance" @click="toggleMaintenanceNow">
          {{ maintenanceActive ? 'Wartungsmodus ausschalten' : 'Wartungsmodus aktivieren' }}
        </button>
        <button class="btn-refresh" :disabled="loadingMaintenance" @click="loadMaintenance">
          Aktualisieren
        </button>
      </div>

      <ErrorAlert :error="maintenanceError" />

      <div v-if="loadingMaintenance" class="maint-loading">Ladet...</div>
      <div v-else class="maint-form">
        <label class="maint-field">
          <span class="maint-label">Meldung</span>
          <input
            class="maint-input"
            type="text"
            v-model="maintenanceForm.message"
            placeholder="z.B. Update laeuft, bitte spaeter erneut versuchen."
            :disabled="savingMaintenance || loadingMaintenance"
            @blur="saveMaintenanceMessage"
          />
        </label>

        <div class="maint-add-grid">
          <label class="maint-field">
            <span class="maint-label">Start</span>
            <input class="maint-input" type="datetime-local" v-model="newWindowStart" />
          </label>

          <label class="maint-field">
            <span class="maint-label">Ende</span>
            <input class="maint-input" type="datetime-local" v-model="newWindowEnd" />
          </label>

          <label class="maint-field">
            <span class="maint-label">Wiederholung</span>
            <select class="maint-input" v-model="newWindowRecurrence">
              <option value="none">einmalig</option>
              <option value="daily">täglich</option>
              <option value="weekly">wöchentlich</option>
              <option value="monthly">monatlich</option>
            </select>
          </label>

          <label class="maint-field" v-if="newWindowRecurrence !== 'none'">
            <span class="maint-label">Intervall</span>
            <input class="maint-input" type="number" min="1" max="365" v-model.number="newWindowInterval" />
          </label>

          <label class="maint-field" v-if="newWindowRecurrence !== 'none'">
            <span class="maint-label">Wiederholen bis</span>
            <input class="maint-input" type="datetime-local" v-model="newWindowUntil" />
          </label>

          <div class="maint-add-actions">
            <button class="btn-save" type="button" @click="addWindow">Hinzufügen</button>
          </div>
        </div>

        <div class="maint-windows">
          <h4 class="maint-subtitle">Geplante Wartungsfenster</h4>
          <div v-if="!maintenanceWindows.length" class="maint-loading">Noch keine Wartungsfenster erstellt.</div>
          <div v-else class="maint-list">
            <article v-for="w in maintenanceWindows" :key="w.id" class="maint-list-item">
              <div>
                <strong>{{ formatWindowRange(w.start, w.end) }}</strong>
                <p class="maint-list-meta">{{ recurrenceLabel(w.recurrence, w.interval) }}<span v-if="maintenanceForm.message"> | {{ maintenanceForm.message }}</span></p>
                <p class="maint-list-meta">
                  <span class="window-status" :class="`window-status--${windowStatus(w)}`">{{ windowStatusLabel(w) }}</span>
                </p>
              </div>
              <div class="maint-item-actions">
                <button
                  v-if="windowStatus(w) === 'active'"
                  class="btn-save"
                  type="button"
                  :disabled="savingMaintenance || loadingMaintenance"
                  @click="endWindowNow(w.id)"
                >
                  Beenden
                </button>
                <button class="btn-clear" type="button" :disabled="savingMaintenance || loadingMaintenance" @click="removeWindow(w.id)">Entfernen</button>
              </div>
            </article>
          </div>
        </div>

      </div>
    </section>
  </section>
</template>

<style scoped>
.settings-wrap {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.settings-header {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  align-items: center;
}
.settings-header h2 {
  margin: 0;
  font-size: 0.98rem;
  color: var(--text-primary);
}
.btn-refresh {
  border: 1px solid var(--border-strong);
  background: var(--bg-surface);
  border-radius: 8px;
  padding: 6px 10px;
  cursor: pointer;
  color: var(--text-secondary);
}
.settings-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.settings-box {
  border: 1px solid var(--border);
  border-radius: 12px;
  background: var(--bg-elevated);
  padding: 12px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.settings-box--azure {
  margin-top: 8px;
}
.group-header {
  margin-top: 8px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}
.group-header--box {
  margin-top: 0;
}
.group-title {
  margin: 0;
  font-size: 0.77rem;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  color: var(--text-muted);
}
.group-copy {
  margin: 4px 0 0;
  font-size: 0.78rem;
  color: var(--text-muted);
}
.btn-reset-azure {
  border: 1px solid var(--error-border);
  background: var(--bg-surface);
  color: var(--error-color);
  border-radius: 7px;
  padding: 4px 8px;
  font-size: 0.75rem;
  cursor: pointer;
  white-space: nowrap;
}
.btn-reset-azure:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}
.setting-row {
  display: grid;
  grid-template-columns: minmax(160px, 1.25fr) minmax(170px, 1fr) auto;
  gap: 8px;
  align-items: center;
  border: 1px solid var(--border);
  border-radius: 9px;
  background: var(--card-bg);
  padding: 7px 9px;
}
.setting-row--boxed {
  background: var(--bg-surface);
}
.setting-main {
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.setting-headline {
  display: flex;
  align-items: center;
  gap: 6px;
}
.setting-headline strong {
  margin: 0;
  font-size: 0.86rem;
  color: var(--text-primary);
}
.setting-hint {
  margin: 0;
  color: var(--text-muted);
  font-size: 0.72rem;
}
.setting-edit {
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.setting-control {
  position: relative;
}
.setting-control--locked .setting-input,
.setting-control--locked .bool-toggle {
  opacity: 0.72;
}
.setting-lock {
  position: absolute;
  top: 50%;
  right: 8px;
  transform: translateY(-50%);
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: var(--text-muted);
  z-index: 1;
  pointer-events: auto;
}
.setting-input {
  border: 1px solid var(--input-border);
  border-radius: 7px;
  padding: 5px 28px 5px 8px;
  width: 100%;
  background: var(--input-bg);
  color: var(--input-color);
}
.setting-invalid {
  margin: 0;
  color: var(--error-color);
  font-size: 0.71rem;
}
.bool-toggle {
  display: flex;
  align-items: center;
  min-height: 32px;
}
.bool-toggle input {
  width: 16px;
  height: 16px;
  margin: 0;
}
.setting-actions {
  display: flex;
  align-items: center;
  gap: 6px;
}
.btn-save,
.btn-clear {
  border: 0;
  border-radius: 7px;
  padding: 5px 8px;
  cursor: pointer;
  white-space: nowrap;
  font-size: 0.8rem;
}
.btn-save {
  background: var(--btn-primary-bg);
  color: var(--btn-primary-color);
}
.btn-clear {
  background: var(--border);
  color: var(--text-primary);
}

.maint-section {
  border: 1px solid var(--border);
  border-radius: 12px;
  background: var(--bg-elevated);
  padding: 12px;
  margin-top: 8px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.maint-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 8px;
}
.maint-title {
  margin: 0;
  font-size: 0.9rem;
  color: var(--text-primary);
}
.maint-copy {
  margin: 4px 0 0;
  font-size: 0.78rem;
  color: var(--text-muted);
}
.maint-status-badge {
  border: 1px solid var(--border);
  border-radius: 999px;
  padding: 3px 9px;
  font-size: 0.75rem;
  font-weight: 700;
  white-space: nowrap;
}
.maint-status-badge--active {
  color: var(--warning-color, #92400e);
  background: var(--warning-bg, #fef3c7);
  border-color: var(--warning-border, #f59e0b);
}
.maint-status-badge--scheduled {
  color: var(--accent);
  background: var(--nav-link-active-bg);
  border-color: var(--accent);
}
.maint-status-badge--off {
  color: var(--text-muted);
  background: var(--bg-surface);
}
.maint-loading {
  font-size: 0.82rem;
  color: var(--text-muted);
}
.maint-toggle-row {
  display: flex;
  align-items: center;
  gap: 8px;
}
.maint-form {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.maint-add-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
}
.maint-field {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.maint-label {
  font-size: 0.76rem;
  color: var(--text-muted);
}
.maint-input {
  border: 1px solid var(--input-border);
  border-radius: 7px;
  padding: 6px 8px;
  width: 100%;
  background: var(--input-bg);
  color: var(--input-color);
}
.maint-add-actions {
  grid-column: 1 / -1;
  display: flex;
  align-items: center;
  justify-content: flex-end;
}
.maint-windows {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.maint-subtitle {
  margin: 0;
  font-size: 0.8rem;
  color: var(--text-secondary);
}
.maint-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.maint-list-item {
  border: 1px solid var(--border);
  border-radius: 8px;
  background: var(--card-bg);
  padding: 8px 10px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}
.maint-list-meta {
  margin: 2px 0 0;
  font-size: 0.76rem;
  color: var(--text-muted);
}
.maint-item-actions {
  display: flex;
  align-items: center;
  gap: 6px;
}
.window-status {
  display: inline-flex;
  align-items: center;
  border-radius: 999px;
  padding: 2px 8px;
  font-size: 0.72rem;
  font-weight: 700;
  border: 1px solid var(--border);
  background: var(--bg-surface);
  color: var(--text-muted);
}
.window-status--active {
  color: var(--warning-color, #92400e);
  background: var(--warning-bg, #fef3c7);
  border-color: var(--warning-border, #f59e0b);
}
.window-status--planned {
  color: var(--accent);
  border-color: var(--accent);
  background: var(--nav-link-active-bg);
}
.window-status--ended {
  color: var(--text-muted);
  background: var(--bg-surface);
}
@media (max-width: 980px) {
  .group-header {
    align-items: flex-start;
    flex-direction: column;
  }
  .setting-row {
    grid-template-columns: 1fr;
    gap: 8px;
  }
  .setting-actions {
    justify-content: flex-start;
  }
  .maint-form {
    grid-template-columns: 1fr;
  }
  .maint-add-grid {
    grid-template-columns: 1fr;
  }
  .maint-list-item {
    flex-direction: column;
    align-items: flex-start;
  }
  .maint-item-actions {
    width: 100%;
  }
}
</style>
