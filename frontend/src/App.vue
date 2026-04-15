<template>
  <div>
    <nav class="global-nav">
      <div class="global-nav-inner">
        <router-link to="/" class="global-nav-logo">
          <img src="/logo.png" alt="DPWeb" class="global-nav-logo-img" />
          <span class="global-nav-title">DPWeb</span>
        </router-link>
        <div v-if="user" class="global-nav-links">
          <router-link to="/" class="global-nav-link" :class="{ 'global-nav-link--active': route.path === '/' || route.path.startsWith('/activities') }">Aktivitäten</router-link>
          <router-link v-if="showMailTemplates" to="/mail-templates" class="global-nav-link" :class="{ 'global-nav-link--active': route.path === '/mail-templates' }">Mail-Vorlagen</router-link>
          <router-link v-if="showAdmin" to="/admin" class="global-nav-link" :class="{ 'global-nav-link--active': route.path === '/admin' }">Admin</router-link>
        </div>
        <div class="global-nav-right">
          <a href="https://github.com/reicham2/DPW/wiki" target="_blank" rel="noopener noreferrer" class="global-nav-help" title="Hilfe">
            <svg width="20" height="20" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
              <circle cx="10" cy="10" r="8.5" />
              <path d="M7.5 7.5a2.5 2.5 0 0 1 4.85.83c0 1.67-2.5 2.5-2.5 2.5" />
              <circle cx="10" cy="14" r="0.5" fill="currentColor" stroke="none" />
            </svg>
          </a>
          <UserAvatar />
        </div>
      </div>
    </nav>

    <!-- Auth loading splash -->
    <div v-if="authLoading" class="auth-loading">
      <div class="auth-loading-spinner" />
    </div>

    <!-- Login required overlay -->
    <div v-else-if="!user" class="auth-overlay">
      <div class="auth-box">
        <h1 class="auth-title">DPWeb</h1>
        <p class="auth-subtitle">Melde dich mit deinem Microsoft-Konto an.</p>
        <button class="auth-btn" @click="handleLogin" :disabled="loggingIn">
          <svg class="auth-ms-icon" viewBox="0 0 21 21" width="21" height="21">
            <rect x="0" y="0" width="10" height="10" fill="#f25022"/>
            <rect x="11" y="0" width="10" height="10" fill="#7fba00"/>
            <rect x="0" y="11" width="10" height="10" fill="#00a4ef"/>
            <rect x="11" y="11" width="10" height="10" fill="#ffb900"/>
          </svg>
          {{ loggingIn ? 'Anmelden...' : 'Mit Microsoft anmelden' }}
        </button>

        <!-- Debug login -->
        <template v-if="isDebug">
          <div class="auth-divider"><span>Debug Login</span></div>
          <div v-if="debugUsersLoading" class="auth-debug-loading">Benutzer laden…</div>
          <template v-else>
            <select v-model="debugSelectedUser" class="auth-debug-select">
              <option value="" disabled>Benutzer wählen…</option>
              <option v-for="u in debugUsers" :key="u.id" :value="u.id">
                {{ u.display_name }} ({{ u.role }}{{ u.department ? ', ' + u.department : '' }})
              </option>
            </select>
            <button
              class="auth-btn auth-btn-debug"
              @click="handleDebugLogin"
              :disabled="!debugSelectedUser || loggingIn"
            >
              {{ loggingIn ? 'Anmelden...' : 'Als Benutzer anmelden' }}
            </button>
          </template>
        </template>

        <p v-if="loginError" class="auth-error">{{ loginError }}</p>
      </div>
    </div>

    <!-- App content -->
    <div v-else class="app">
      <router-view />
      <BugReportButton />
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useRoute } from 'vue-router'
import UserAvatar from './components/UserAvatar.vue'
import BugReportButton from './components/BugReportButton.vue'
import { user, authLoading, loginError, initAuth, login, isDebug, debugLogin } from './composables/useAuth'
import { usePermissions } from './composables/usePermissions'
import type { User } from './types'

const route = useRoute()
const { myPermissions, fetchMyPermissions } = usePermissions()

const showMailTemplates = computed(() => myPermissions.value?.mail_templates_scope && myPermissions.value.mail_templates_scope !== 'none')
const showAdmin = computed(() => {
  const p = myPermissions.value
  if (!p) return false
  return (p.user_dept_scope && p.user_dept_scope !== 'none') ||
         (p.user_role_scope && p.user_role_scope !== 'none')
})

const loggingIn = ref(false)
const debugUsers = ref<User[]>([])
const debugSelectedUser = ref('')
const debugUsersLoading = ref(false)

async function handleLogin() {
  loggingIn.value = true
  try { await login() } finally { loggingIn.value = false }
}

async function handleDebugLogin() {
  if (!debugSelectedUser.value) return
  loggingIn.value = true
  try { await debugLogin(debugSelectedUser.value) } finally { loggingIn.value = false }
}

async function loadDebugUsers() {
  if (!isDebug) return
  debugUsersLoading.value = true
  try {
    const res = await fetch('/api/debug/users')
    if (res.ok) debugUsers.value = await res.json()
  } finally {
    debugUsersLoading.value = false
  }
}

onMounted(() => {
  initAuth()
  loadDebugUsers()
})

watch(user, (u) => {
  if (u) fetchMyPermissions().catch(() => {})
}, { immediate: true })
</script>

<style scoped>
.auth-loading {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: calc(100vh - 56px);
}
.auth-loading-spinner {
  width: 36px;
  height: 36px;
  border: 3px solid #e2e8f0;
  border-top-color: #1a56db;
  border-radius: 50%;
  animation: spin 0.7s linear infinite;
}
@keyframes spin { to { transform: rotate(360deg); } }

.auth-overlay {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: calc(100vh - 56px);
  padding: 24px;
}
.auth-box {
  background: #fff;
  border-radius: 16px;
  box-shadow: 0 4px 32px rgba(0,0,0,0.1);
  padding: 48px 40px;
  max-width: 380px;
  width: 100%;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  text-align: center;
}
.auth-logo {
  width: 56px;
  height: 56px;
  margin-bottom: 4px;
}
.auth-title {
  font-size: 1.6rem;
  font-weight: 800;
  color: #1a202c;
  margin: 0;
}
.auth-subtitle {
  font-size: 0.95rem;
  color: #6b7280;
  margin: 0 0 8px;
}
.auth-btn {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 24px;
  background: #1a56db;
  color: #fff;
  border: none;
  border-radius: 8px;
  font-size: 0.95rem;
  font-weight: 600;
  cursor: pointer;
  width: 100%;
  justify-content: center;
  transition: background 0.15s;
  margin-top: 4px;
}
.auth-btn:hover:not(:disabled) { background: #1648c0; }
.auth-btn:disabled { opacity: 0.6; cursor: default; }
.auth-ms-icon { flex-shrink: 0; }
.auth-error {
  margin: 4px 0 0;
  font-size: 0.85rem;
  color: #dc2626;
  text-align: center;
}

.auth-divider {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 12px;
  margin: 8px 0 0;
  color: #9ca3af;
  font-size: 0.82rem;
}
.auth-divider::before,
.auth-divider::after {
  content: '';
  flex: 1;
  height: 1px;
  background: #e5e7eb;
}
.auth-debug-select {
  width: 100%;
  padding: 10px 12px;
  border: 1px solid #d1d5db;
  border-radius: 8px;
  font-size: 0.9rem;
  background: #fff;
  color: #1a202c;
  cursor: pointer;
}
.auth-btn-debug {
  background: #6b7280;
}
.auth-btn-debug:hover:not(:disabled) { background: #4b5563; }
.auth-debug-loading {
  font-size: 0.85rem;
  color: #9ca3af;
}

.global-nav-links {
  display: flex;
  align-items: center;
  gap: 4px;
  margin-left: 24px;
}

.global-nav-link {
  padding: 6px 14px;
  font-size: 0.88rem;
  font-weight: 600;
  color: #6b7280;
  text-decoration: none;
  border-radius: 6px;
  transition: color 0.15s, background 0.15s;
}
.global-nav-link:hover {
  color: #0080ff;
  background: #f0f7ff;
}
.global-nav-link--active {
  color: #0080ff;
  background: #e8f0fe;
}

.global-nav-right {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 12px;
}

.global-nav-help {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: #6b7280;
  transition: color 0.15s;
  text-decoration: none;
}
.global-nav-help:hover {
  color: #1a56db;
}

@media (max-width: 599px) {
  .global-nav-links {
    margin-left: 12px;
    gap: 2px;
  }
  .global-nav-link {
    padding: 6px 8px;
    font-size: 0.8rem;
  }
  .global-nav-title {
    display: none;
  }
}
</style>
