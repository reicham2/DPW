import { ref } from 'vue'
import type { PublicClientApplication, AccountInfo } from '@azure/msal-browser'
import type { User } from '../types'

const msalConfig = {
  auth: {
    clientId: import.meta.env.VITE_MSAL_CLIENT_ID as string,
    authority: `https://login.microsoftonline.com/${import.meta.env.VITE_MSAL_TENANT_ID as string}`,
    redirectUri: window.location.origin,
  },
  cache: {
    cacheLocation: 'localStorage' as const,
    storeAuthStateInCookie: false,
  },
}

const loginRequest = {
  scopes: ['openid', 'profile', 'email'],
}

let msalInstance: PublicClientApplication | null = null

export const user = ref<User | null>(null)
export const authLoading = ref(true)
export const loginError = ref<string | null>(null)

async function getMsal(): Promise<PublicClientApplication> {
  if (!msalInstance) {
    const { PublicClientApplication } = await import('@azure/msal-browser')
    msalInstance = new PublicClientApplication(msalConfig)
    await msalInstance.initialize()
    await msalInstance.handleRedirectPromise()
  }
  return msalInstance
}

export async function getIdToken(): Promise<string> {
  const msal = await getMsal()
  const account = msal.getActiveAccount()
  if (!account) throw new Error('Nicht angemeldet')

  try {
    const result = await msal.acquireTokenSilent({ ...loginRequest, account })
    return result.idToken
  } catch {
    // InteractionRequiredAuthError or similar — fall back to popup
    const result = await msal.acquireTokenPopup({ ...loginRequest, account })
    return result.idToken
  }
}

async function syncUser(idToken: string): Promise<void> {
  const res = await fetch('/api/auth/me', {
    method: 'POST',
    headers: { Authorization: `Bearer ${idToken}` },
  })
  if (res.ok) {
    user.value = (await res.json()) as User
  } else {
    const text = await res.text()
    throw new Error(text || `Server-Fehler: ${res.status}`)
  }
}

export async function login(): Promise<void> {
  loginError.value = null
  const msal = await getMsal()
  try {
    const result = await msal.loginPopup(loginRequest)
    msal.setActiveAccount(result.account)
    await syncUser(result.idToken)
  } catch (err) {
    loginError.value = err instanceof Error ? err.message : 'Anmeldung fehlgeschlagen.'
    throw err
  }
}

export async function logout(): Promise<void> {
  const msal = await getMsal()
  await msal.logoutPopup({ account: msal.getActiveAccount() ?? undefined })
  user.value = null
}

export async function initAuth(): Promise<void> {
  try {
    const msal = await getMsal()
    const accounts = msal.getAllAccounts()
    if (accounts.length > 0) {
      msal.setActiveAccount(accounts[0])
      const token = await getIdToken()
      await syncUser(token)
    }
  } catch {
    user.value = null
  } finally {
    authLoading.value = false
  }
}
