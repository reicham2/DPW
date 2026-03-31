import { ref } from 'vue'
import type { Activity, WsEvent } from '../types'
import { useWebSocket } from './useWebSocket'

const BASE = '/api'

export interface ActivityInput {
  text?: string
  title: string
  description: string
  responsible: string
}

export function useActivities() {
  const activities = ref<Activity[]>([])
  const loading = ref(false)
  const error = ref<string | null>(null)

  // Exposes the most recently received WS update — detail page watches this
  const lastUpdatedActivity = ref<Activity | null>(null)

  function handleWsEvent(event: WsEvent) {
    if (event.event === 'created') {
      if (!activities.value.find(a => a.id === event.activity.id)) {
        activities.value.unshift(event.activity)
      }
    } else if (event.event === 'updated') {
      lastUpdatedActivity.value = event.activity
      const idx = activities.value.findIndex(a => a.id === event.activity.id)
      if (idx !== -1) activities.value[idx] = event.activity
    } else if (event.event === 'deleted') {
      activities.value = activities.value.filter(a => a.id !== event.id)
    }
  }

  const { connected } = useWebSocket(handleWsEvent)

  // ---- REST ----------------------------------------------------------------

  async function fetchActivities() {
    loading.value = true
    error.value = null
    try {
      const res = await fetch(`${BASE}/activities`)
      if (!res.ok) throw new Error(await res.text())
      activities.value = await res.json() as Activity[]
    } catch (e) {
      error.value = String(e)
    } finally {
      loading.value = false
    }
  }

  async function fetchActivity(id: string): Promise<Activity | null> {
    error.value = null
    try {
      const res = await fetch(`${BASE}/activities/${id}`)
      if (res.status === 404) return null
      if (!res.ok) throw new Error(await res.text())
      return await res.json() as Activity
    } catch (e) {
      error.value = String(e)
      return null
    }
  }

  async function createActivity(input: ActivityInput): Promise<void> {
    error.value = null
    try {
      const res = await fetch(`${BASE}/activities`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          text: input.text ?? '',
          title: input.title,
          description: input.description,
          responsible: input.responsible
        })
      })
      if (!res.ok) throw new Error(await res.text())
      // WS broadcast handles local state insertion
    } catch (e) {
      error.value = String(e)
    }
  }

  async function updateActivity(id: string, input: ActivityInput): Promise<void> {
    error.value = null
    try {
      const res = await fetch(`${BASE}/activities/${id}`, {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          text: input.text ?? '',
          title: input.title,
          description: input.description,
          responsible: input.responsible
        })
      })
      if (!res.ok) throw new Error(await res.text())
      // WS broadcast handles local state update
    } catch (e) {
      error.value = String(e)
    }
  }

  async function deleteActivity(id: string): Promise<void> {
    // Optimistic removal from list
    activities.value = activities.value.filter(a => a.id !== id)
    error.value = null
    try {
      const res = await fetch(`${BASE}/activities/${id}`, { method: 'DELETE' })
      if (!res.ok) throw new Error(await res.text())
    } catch (e) {
      error.value = String(e)
      await fetchActivities() // rollback on failure
    }
  }

  return {
    activities,
    loading,
    error,
    connected,
    lastUpdatedActivity,
    fetchActivities,
    fetchActivity,
    createActivity,
    updateActivity,
    deleteActivity
  }
}
