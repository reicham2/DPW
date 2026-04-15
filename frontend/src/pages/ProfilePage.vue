<template>
  <div class="page-header">
    <h1 class="page-title">Mein Profil</h1>
  </div>

  <main class="page-content">
    <div class="profile-card">
      <div class="profile-avatar-large">{{ initials }}</div>

      <form class="profile-form" @submit.prevent="save">
        <div class="form-group">
          <label class="form-label" for="display_name">Anzeigename</label>
          <input
            id="display_name"
            v-model="form.display_name"
            class="form-input"
            type="text"
            required
            placeholder="Dein Name"
          />
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
import { ref, computed, onMounted } from 'vue'
import { user, getIdToken } from '../composables/useAuth'
import { usePermissions } from '../composables/usePermissions'
import ErrorAlert from '../components/ErrorAlert.vue'
import BadgeSelect from '../components/BadgeSelect.vue'
import DepartmentBadge from '../components/DepartmentBadge.vue'
import type { User } from '../types'

const { myPermissions, fetchMyPermissions, departments: deptRecords, fetchDepartments } = usePermissions()

const canChangeDepartment = computed(() => {
  const scope = myPermissions.value?.user_dept_scope
  return scope === 'own' || scope === 'own_dept' || scope === 'all'
})

const deptItems = computed(() => deptRecords.value.map(d => ({ value: d.name })))

const form = ref({
  display_name: user.value?.display_name ?? '',
  department:   user.value?.department   ?? '',
})
const saving = ref(false)
const saved  = ref(false)
const error  = ref<string | null>(null)

const initials = computed(() => {
  const name = form.value.display_name || user.value?.display_name || ''
  return name
    .split(' ')
    .filter(Boolean)
    .slice(0, 2)
    .map(w => w[0].toUpperCase())
    .join('')
})

onMounted(async () => {
  await Promise.all([fetchMyPermissions(), fetchDepartments()])
  if (user.value) {
    form.value.display_name = user.value.display_name
    form.value.department   = user.value.department ?? ''
  }
})

async function save() {
  saving.value = true
  saved.value  = false
  error.value  = null
  try {
    const token = await getIdToken()
    const res = await fetch('/api/me', {
      method: 'PATCH',
      headers: {
        'Content-Type':  'application/json',
        'Authorization': `Bearer ${token}`,
      },
      body: JSON.stringify({
        display_name: form.value.display_name,
        department:   form.value.department || null,
      }),
    })
    if (!res.ok) throw new Error(await res.text())
    const updated: User = await res.json()
    // Update global user state
    if (user.value) Object.assign(user.value, updated)
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
  color: #1a202c;
  margin: 0;
}
.page-content {
  padding: 24px;
  max-width: 560px;
  margin: 0 auto;
}
.profile-card {
  background: #fff;
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
  background: #1a56db;
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
  color: #374151;
}
.form-input {
  padding: 10px 12px;
  border: 1.5px solid #d1d5db;
  border-radius: 8px;
  font-size: 0.95rem;
  color: #1a202c;
  background: #fff;
  outline: none;
  transition: border-color 0.15s;
  width: 100%;
  box-sizing: border-box;
}
.form-input:focus {
  border-color: #1a56db;
}
.form-input--readonly {
  background: #f9fafb;
  color: #6b7280;
  cursor: default;
}
.btn-primary {
  padding: 11px 24px;
  background: #1a56db;
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
  background: #1648c0;
}
.btn-primary:disabled {
  opacity: 0.6;
  cursor: default;
}
.form-hint {
  font-size: 0.78rem;
  color: #9ca3af;
  margin: 2px 0 0;
}
.profile-dept-readonly {
  display: flex;
  align-items: center;
  min-height: 40px;
  padding: 8px 12px;
  border: 1.5px solid #d1d5db;
  border-radius: 8px;
  background: #f9fafb;
}
.profile-dept-empty {
  color: #6b7280;
  font-size: 0.88rem;
}
.profile-error {
  color: #dc2626;
  font-size: 0.875rem;
  background: #fff5f5;
  border-radius: 6px;
  padding: 8px 12px;
}
.profile-success {
  color: #15803d;
  font-size: 0.875rem;
  background: #f0fdf4;
  border-radius: 6px;
  padding: 8px 12px;
}
</style>
