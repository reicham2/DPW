import { ref } from 'vue'
import type { User } from '../types'
import { apiFetch } from './useApi'

const users = ref<User[]>([])
let usersRequest: Promise<void> | null = null
let usersFetchedAt = 0
const USERS_TTL_MS = 5 * 60 * 1000

export function useUsers() {
  async function fetchUsers(force = false): Promise<void> {
    const now = Date.now()
    const hasFreshCache = users.value.length > 0 && (now - usersFetchedAt) < USERS_TTL_MS
    if (!force && hasFreshCache) return
    if (!force && usersRequest) return usersRequest

    usersRequest = (async () => {
      try {
        const res = await apiFetch('/api/users')
        if (res.ok) {
          users.value = await res.json() as User[]
          usersFetchedAt = Date.now()
        }
      } catch {
        /* non-critical */
      } finally {
        usersRequest = null
      }
    })()

    return usersRequest
  }

  return { users, fetchUsers }
}
