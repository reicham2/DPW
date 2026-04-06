import { ref } from 'vue'
import type { User } from '../types'
import { getIdToken } from './useAuth'

export function useUsers() {
  const users = ref<User[]>([])

  async function fetchUsers(): Promise<void> {
    try {
      const token = await getIdToken()
      const res = await fetch('/api/users', {
        headers: { Authorization: `Bearer ${token}` },
      })
      if (res.ok) users.value = await res.json() as User[]
    } catch { /* non-critical */ }
  }

  return { users, fetchUsers }
}
