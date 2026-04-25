<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { Lock } from 'lucide-vue-next'
import { apiFetch, formatApiError } from '../composables/useApi'
import { applyRuntimeConfig } from '../config'
import { setRuntimeAuthConfig } from '../composables/useAuth'
import ErrorAlert from './ErrorAlert.vue'
import type { AppSettingItem } from '../types'

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
  return [...settings.value].sort((a, b) => {
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
  void fetchSettings()
})
</script>

<template>
  <section class="settings-wrap">
    <header class="settings-header">
      <div>
        <h2>System-Konfiguration</h2>
      </div>
      <button class="btn-refresh" :disabled="loading" @click="fetchSettings">
        {{ loading ? 'Lädt...' : 'Aktualisieren' }}
      </button>
    </header>

    <ErrorAlert :error="error" />
    <ErrorAlert :error="saveError" />

    <div class="settings-list" v-if="!loading && sortedSettings.length">
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
    </div>
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
}
.btn-refresh {
  border: 1px solid #d1d5db;
  background: white;
  border-radius: 8px;
  padding: 6px 10px;
  cursor: pointer;
}
.settings-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.settings-box {
  border: 1px solid #dbe4ef;
  border-radius: 12px;
  background: linear-gradient(180deg, #f8fbff 0%, #ffffff 100%);
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
  color: #64748b;
}
.group-copy {
  margin: 4px 0 0;
  font-size: 0.78rem;
  color: #475569;
}
.btn-reset-azure {
  border: 1px solid #b91c1c;
  background: white;
  color: #991b1b;
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
  border: 1px solid #e5e7eb;
  border-radius: 9px;
  background: #fff;
  padding: 7px 9px;
}
.setting-row--boxed {
  background: rgba(255, 255, 255, 0.86);
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
}
.setting-hint {
  margin: 0;
  color: #6b7280;
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
  color: #64748b;
  z-index: 1;
  pointer-events: auto;
}
.setting-input {
  border: 1px solid #d1d5db;
  border-radius: 7px;
  padding: 5px 28px 5px 8px;
  width: 100%;
}
.setting-invalid {
  margin: 0;
  color: #b91c1c;
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
  background: #0f766e;
  color: white;
}
.btn-clear {
  background: #e5e7eb;
  color: #111827;
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
}
</style>
