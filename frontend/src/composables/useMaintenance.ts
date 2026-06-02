import { ref, computed } from 'vue'
import { apiFetch } from './useApi'

export interface MaintenanceState {
  active: boolean
  message: string
  enabled?: boolean
  scheduled_now?: boolean
  scheduled_start?: string
  scheduled_end?: string
}

export interface MaintenanceWindow {
  id: string
  start: string
  end: string
  message: string
  recurrence: 'none' | 'daily' | 'weekly' | 'monthly'
  interval: number
  until?: string
  status?: 'planned' | 'active' | 'ended'
}

export interface UpcomingMaintenanceWindow {
  window_id: string
  start: string
  end: string
  message: string
  recurrence: 'none' | 'daily' | 'weekly' | 'monthly'
}

export const maintenanceActive = ref(false)
export const maintenanceMessage = ref('')
export const maintenanceScheduledStart = ref('')
export const maintenanceScheduledEnd = ref('')

/** True when a maintenance window is announced but not yet active */
export const maintenanceAnnounced = computed(
  () =>
    !maintenanceActive.value &&
    !!maintenanceScheduledStart.value &&
    !!maintenanceScheduledEnd.value,
)

let pollTimer: ReturnType<typeof setInterval> | null = null

export function applyMaintenanceData(data: {
  maintenance_active?: boolean
  maintenance_message?: string
  maintenance_scheduled_start?: string
  maintenance_scheduled_end?: string
}) {
  if (typeof data.maintenance_active === 'boolean')
    maintenanceActive.value = data.maintenance_active
  if (typeof data.maintenance_message === 'string')
    maintenanceMessage.value = data.maintenance_message
  if (typeof data.maintenance_scheduled_start === 'string')
    maintenanceScheduledStart.value = data.maintenance_scheduled_start
  if (typeof data.maintenance_scheduled_end === 'string')
    maintenanceScheduledEnd.value = data.maintenance_scheduled_end
}

export function applyMaintenanceStatus(data: {
  active?: boolean
  message?: string
  scheduled_start?: string
  scheduled_end?: string
}): void {
  if (typeof data.active === 'boolean')
    maintenanceActive.value = data.active
  if (typeof data.message === 'string')
    maintenanceMessage.value = data.message
  if (typeof data.scheduled_start === 'string')
    maintenanceScheduledStart.value = data.scheduled_start
  if (typeof data.scheduled_end === 'string')
    maintenanceScheduledEnd.value = data.scheduled_end
}

export async function fetchMaintenanceStatus(): Promise<void> {
  try {
    const res = await fetch('/api/maintenance')
    if (!res.ok) return
    const data = (await res.json()) as {
      active: boolean
      message: string
      scheduled_start?: string
      scheduled_end?: string
    }
    maintenanceActive.value = data.active
    maintenanceMessage.value = data.message ?? ''
    maintenanceScheduledStart.value = data.scheduled_start ?? ''
    maintenanceScheduledEnd.value = data.scheduled_end ?? ''
  } catch {
    // network error — keep current state
  }
}

export function startMaintenancePoll(intervalMs = 30_000): void {
  stopMaintenancePoll()
  void fetchMaintenanceStatus()
  pollTimer = setInterval(fetchMaintenanceStatus, intervalMs)
}

export function stopMaintenancePoll(): void {
  if (pollTimer !== null) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

// ── Admin helpers ────────────────────────────────────────────────────────────

export interface AdminMaintenanceConfig extends MaintenanceState {
  enabled: boolean
  scheduled_now: boolean
  windows: MaintenanceWindow[]
  upcoming_windows: UpcomingMaintenanceWindow[]
}

export async function fetchAdminMaintenance(): Promise<AdminMaintenanceConfig> {
  const res = await apiFetch('/api/admin/maintenance')
  if (!res.ok) throw new Error(await res.text())
  return (await res.json()) as AdminMaintenanceConfig
}

export async function saveAdminMaintenance(
  patch: Partial<{
    enabled: boolean
    message: string
    scheduled_start: string
    scheduled_end: string
    windows: MaintenanceWindow[]
    end_window_id: string
  }>,
): Promise<AdminMaintenanceConfig> {
  const res = await apiFetch('/api/admin/maintenance', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(patch),
  })
  if (!res.ok) throw new Error(await res.text())
  const result = (await res.json()) as AdminMaintenanceConfig
  // Sync public state
  maintenanceActive.value = result.active
  maintenanceMessage.value = result.message ?? ''
  maintenanceScheduledStart.value = result.scheduled_start ?? ''
  maintenanceScheduledEnd.value = result.scheduled_end ?? ''
  return result
}
