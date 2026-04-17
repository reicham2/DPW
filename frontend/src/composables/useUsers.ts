import { ref } from 'vue'
import type { User } from '../types'
import { apiFetch } from './useApi'

export function useUsers() {
  const users = ref<User[]>([])

  async function fetchUsers(): Promise<void> {
    try {
      const res = await apiFetch('/api/users')
      if (res.ok) users.value = await res.json() as User[]
    } catch { /* non-critical */ }
  }

  return { users, fetchUsers }
}
