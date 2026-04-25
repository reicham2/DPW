<template>
  <section class="setup-shell">
    <div class="setup-card">
      <div class="setup-copy">
        <p class="setup-kicker">Initiales Setup</p>
        <h1>Setup abschliessen</h1>
        <p>
          Fehlende Werte fuer Azure und Web Push werden hier einmalig erfasst.
          Bereits gesetzte Felder bleiben unveraendert.
        </p>
      </div>

      <form class="setup-form" @submit.prevent="onSubmit">
        <label class="setup-field">
          <span>
            Azure Tenant ID
            <em v-if="setupFieldConfigured.tenant_id" class="setup-set">Bereits gesetzt</em>
          </span>
          <input
            v-model.trim="setupForm.tenant_id"
            type="text"
            autocomplete="off"
            :required="!setupFieldConfigured.tenant_id"
            :disabled="setupFieldConfigured.tenant_id"
            :placeholder="setupFieldConfigured.tenant_id ? 'Bereits gesetzt' : ''"
          />
        </label>

        <label class="setup-field">
          <span>
            Azure Client ID
            <em v-if="setupFieldConfigured.client_id" class="setup-set">Bereits gesetzt</em>
          </span>
          <input
            v-model.trim="setupForm.client_id"
            type="text"
            autocomplete="off"
            :required="!setupFieldConfigured.client_id"
            :disabled="setupFieldConfigured.client_id"
            :placeholder="setupFieldConfigured.client_id ? 'Bereits gesetzt' : ''"
          />
        </label>

        <label class="setup-field">
          <span>
            Azure Client Secret
            <em v-if="setupFieldConfigured.client_secret" class="setup-set">Bereits gesetzt</em>
          </span>
          <input
            v-model="setupForm.client_secret"
            type="password"
            autocomplete="new-password"
            :required="!setupFieldConfigured.client_secret"
            :disabled="setupFieldConfigured.client_secret"
            :placeholder="setupFieldConfigured.client_secret ? 'Bereits gesetzt' : ''"
          />
        </label>

        <label class="setup-field">
          <span>
            Kontakt E-Mail fuer Web Push
            <em v-if="setupFieldConfigured.contact_email" class="setup-set">Bereits gesetzt</em>
          </span>
          <input
            v-model.trim="setupForm.contact_email"
            type="email"
            autocomplete="email"
            :required="!setupFieldConfigured.contact_email"
            :disabled="setupFieldConfigured.contact_email"
            :placeholder="setupFieldConfigured.contact_email ? 'Bereits gesetzt' : ''"
          />
        </label>

        <button class="setup-submit" :disabled="setupSubmitting || !canSubmit" type="submit">
          {{ setupSubmitting ? 'Speichere...' : 'Speichern und anmelden' }}
        </button>

        <p v-if="setupError" class="setup-error">{{ setupError }}</p>
      </form>
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { login } from '../composables/useAuth'
import {
  ensureSetupStatus,
  setupError,
  setupFieldConfigured,
  setupForm,
  setupRequired,
  setupSubmitting,
  submitSetupConfig,
} from '../composables/useSetupAuthConfig'

const router = useRouter()

const canSubmit = computed(() => {
  const tenantOk = setupFieldConfigured.value.tenant_id || !!setupForm.value.tenant_id.trim()
  const clientOk = setupFieldConfigured.value.client_id || !!setupForm.value.client_id.trim()
  const secretOk = setupFieldConfigured.value.client_secret || !!setupForm.value.client_secret
  const emailOk = setupFieldConfigured.value.contact_email || !!setupForm.value.contact_email.trim()
  return tenantOk && clientOk && secretOk && emailOk
})

async function onSubmit() {
  await submitSetupConfig()
  await login()
  await router.replace('/')
}

onMounted(async () => {
  await ensureSetupStatus(true)
  if (!setupRequired.value) {
    await router.replace('/')
  }
})
</script>

<style scoped>
.setup-shell {
  min-height: 100vh;
  display: grid;
  place-items: center;
  padding: 24px;
  background:
    radial-gradient(circle at top left, rgba(16, 185, 129, 0.12), transparent 28%),
    radial-gradient(circle at bottom right, rgba(37, 99, 235, 0.12), transparent 34%),
    #f8fafc;
}

.setup-card {
  width: min(100%, 460px);
  border: 1px solid #dbe4ef;
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.94);
  box-shadow: 0 22px 48px rgba(15, 23, 42, 0.08);
  padding: 28px;
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.setup-kicker {
  margin: 0 0 8px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  font-size: 0.72rem;
  color: #0f766e;
  font-weight: 700;
}

.setup-copy h1 {
  margin: 0;
  font-size: 1.55rem;
  line-height: 1.15;
  color: #0f172a;
}

.setup-copy p {
  margin: 10px 0 0;
  color: #475569;
  line-height: 1.45;
}

.setup-form {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.setup-field {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.setup-field span {
  font-size: 0.84rem;
  font-weight: 600;
  color: #334155;
  display: flex;
  justify-content: space-between;
  gap: 10px;
  align-items: center;
}

.setup-set {
  font-style: normal;
  font-size: 0.74rem;
  font-weight: 700;
  color: #0f766e;
}

.setup-field input {
  width: 100%;
  border: 1px solid #cbd5e1;
  border-radius: 10px;
  padding: 11px 12px;
  font-size: 0.95rem;
  background: #fff;
  color: #0f172a;
}

.setup-field input:focus {
  outline: none;
  border-color: #0f766e;
  box-shadow: 0 0 0 3px rgba(15, 118, 110, 0.12);
}

.setup-submit {
  border: none;
  border-radius: 10px;
  padding: 12px 14px;
  background: #0f766e;
  color: white;
  font-weight: 700;
  cursor: pointer;
}

.setup-submit:disabled {
  opacity: 0.55;
  cursor: default;
}

.setup-error {
  margin: 0;
  color: #b91c1c;
  font-size: 0.84rem;
}
</style>