<template>
  <Teleport to="body">
    <!-- Floating Action Button -->
    <button class="bug-fab" @click="openModal" title="Problem melden">
      <svg width="20" height="20" viewBox="0 0 20 20" fill="none" xmlns="http://www.w3.org/2000/svg">
        <path d="M10 2a8 8 0 100 16A8 8 0 0010 2zm.75 11.5h-1.5v-1.5h1.5v1.5zm0-3h-1.5V6h1.5v4.5z" fill="currentColor"/>
      </svg>
    </button>

    <!-- Modal Overlay -->
    <div v-if="isOpen" class="bug-overlay" @click.self="closeModal">
      <div class="bug-modal">
        <div class="bug-modal-header">
          <h2 class="bug-modal-title">Problem melden</h2>
          <button class="bug-close-btn" @click="closeModal" aria-label="Schliessen">×</button>
        </div>

        <!-- Form -->
        <div v-if="!success" class="bug-modal-body">
          <div class="bug-screenshot-row">
            <button class="bug-btn-secondary" @click="captureScreenshot" :disabled="isCapturing">
              <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
                <rect x="1" y="3.5" width="14" height="10" rx="1.5" stroke="currentColor" stroke-width="1.5"/>
                <circle cx="8" cy="8.5" r="2" stroke="currentColor" stroke-width="1.5"/>
                <path d="M5.5 3.5L6.5 2h3l1 1.5" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
              </svg>
              {{ isCapturing ? 'Aufnehmen...' : 'Screenshot erstellen' }}
            </button>
            <span v-if="screenshotDataUrl" class="bug-screenshot-badge">✓ Screenshot bereit</span>
          </div>

          <div v-if="screenshotDataUrl" class="bug-screenshot-preview-wrapper">
            <img :src="screenshotDataUrl" class="bug-screenshot-preview" alt="Screenshot" />
          </div>

          <label class="bug-label" for="bug-description">Beschreibung *</label>
          <textarea
            id="bug-description"
            v-model="description"
            class="bug-textarea"
            placeholder="Beschreibe das Problem so genau wie möglich..."
            rows="5"
          />

          <p v-if="error" class="bug-error">{{ error }}</p>

          <div class="bug-actions">
            <button class="bug-btn-secondary" @click="closeModal">Abbrechen</button>
            <button
              class="bug-btn-primary"
              @click="submitReport"
              :disabled="!description.trim() || isSubmitting"
            >
              {{ isSubmitting ? 'Wird gesendet...' : 'Melden' }}
            </button>
          </div>
        </div>

        <!-- Success -->
        <div v-else class="bug-success-body">
          <div class="bug-success-icon">✓</div>
          <p class="bug-success-text">Issue wurde erfolgreich erstellt!</p>
          <a :href="issueUrl" target="_blank" rel="noopener noreferrer" class="bug-issue-link">
            Auf GitHub ansehen →
          </a>
          <div class="bug-actions">
            <button class="bug-btn-primary" @click="closeAndReset">Schliessen</button>
          </div>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<script setup lang="ts">
import { ref, nextTick } from 'vue'
import html2canvas from 'html2canvas'
import { getIdToken } from '../composables/useAuth'

const isOpen = ref(false)
const isCapturing = ref(false)
const isSubmitting = ref(false)
const description = ref('')
const screenshotDataUrl = ref<string | null>(null)
const screenshotBase64 = ref<string | null>(null)
const error = ref('')
const success = ref(false)
const issueUrl = ref('')

function openModal() {
  isOpen.value = true
  error.value = ''
}

function closeModal() {
  if (isSubmitting.value) return
  isOpen.value = false
}

function closeAndReset() {
  isOpen.value = false
  description.value = ''
  screenshotDataUrl.value = null
  screenshotBase64.value = null
  error.value = ''
  success.value = false
  issueUrl.value = ''
}

async function captureScreenshot() {
  isCapturing.value = true
  isOpen.value = false
  await nextTick()
  // Short pause so the modal fully disappears before capture
  await new Promise(r => setTimeout(r, 120))

  try {
    const canvas = await html2canvas(document.body, { useCORS: true, logging: false })

    // Resize to max 800px width to keep base64 size manageable
    const maxWidth = 800
    const ratio = Math.min(maxWidth / canvas.width, 1)
    const tmpCanvas = document.createElement('canvas')
    tmpCanvas.width = Math.round(canvas.width * ratio)
    tmpCanvas.height = Math.round(canvas.height * ratio)
    const ctx = tmpCanvas.getContext('2d')!
    ctx.drawImage(canvas, 0, 0, tmpCanvas.width, tmpCanvas.height)

    const dataUrl = tmpCanvas.toDataURL('image/jpeg', 0.5)
    screenshotDataUrl.value = dataUrl
    screenshotBase64.value = dataUrl.split(',')[1]
  } catch (e) {
    console.error('Screenshot failed:', e)
  } finally {
    isCapturing.value = false
    isOpen.value = true
  }
}

async function submitReport() {
  if (!description.value.trim()) return
  isSubmitting.value = true
  error.value = ''

  try {
    const token = await getIdToken()
    const response = await fetch('/api/bug-report', {
      method: 'POST',
      headers: {
        Authorization: `Bearer ${token}`,
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        description: description.value,
        url: window.location.href,
        userAgent: navigator.userAgent,
        screenshot: screenshotBase64.value ?? '',
      }),
    })

    if (response.ok) {
      const data = await response.json()
      issueUrl.value = data.issue_url
      success.value = true
    } else {
      const data = await response.json().catch(() => ({ error: 'Unbekannter Fehler' }))
      error.value = data.error || `Fehler ${response.status}`
    }
  } catch {
    error.value = 'Netzwerkfehler. Bitte versuche es erneut.'
  } finally {
    isSubmitting.value = false
  }
}
</script>

<style scoped>
/* ─── Floating Action Button ─── */
.bug-fab {
  position: fixed;
  bottom: 24px;
  right: 24px;
  z-index: 200;
  width: 48px;
  height: 48px;
  border-radius: 50%;
  background: #0080ff;
  color: #fff;
  border: none;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 4px 14px rgba(0, 128, 255, 0.45);
  transition: background 0.15s, transform 0.1s;
}
.bug-fab:hover { background: #006ee0; transform: scale(1.06); }
.bug-fab:active { transform: scale(0.96); }

/* ─── Overlay ─── */
.bug-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.45);
  z-index: 199;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 16px;
}

/* ─── Modal ─── */
.bug-modal {
  background: #fff;
  border-radius: 16px;
  box-shadow: 0 8px 40px rgba(0, 0, 0, 0.18);
  width: 100%;
  max-width: 520px;
  max-height: 90vh;
  overflow-y: auto;
}

.bug-modal-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 20px 24px 0;
}

.bug-modal-title {
  font-size: 1.1rem;
  font-weight: 700;
  color: #1a202c;
  margin: 0;
}

.bug-close-btn {
  background: none;
  border: none;
  font-size: 1.6rem;
  line-height: 1;
  color: #9ca3af;
  cursor: pointer;
  padding: 2px 8px;
  border-radius: 6px;
  transition: background 0.15s, color 0.15s;
}
.bug-close-btn:hover { background: #f3f4f6; color: #374151; }

.bug-modal-body {
  padding: 16px 24px 24px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

/* ─── Screenshot ─── */
.bug-screenshot-row {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.bug-screenshot-badge {
  font-size: 0.82rem;
  color: #16a34a;
  font-weight: 600;
}

.bug-screenshot-preview-wrapper {
  border-radius: 8px;
  overflow: hidden;
  border: 1px solid #e5e7eb;
}

.bug-screenshot-preview {
  display: block;
  width: 100%;
  max-height: 180px;
  object-fit: cover;
  object-position: top;
}

/* ─── Form ─── */
.bug-label {
  font-size: 0.875rem;
  font-weight: 600;
  color: #374151;
}

.bug-textarea {
  width: 100%;
  padding: 10px 12px;
  border: 1px solid #d1d5db;
  border-radius: 8px;
  font-size: 0.9rem;
  font-family: inherit;
  resize: vertical;
  min-height: 100px;
  color: #1a202c;
  box-sizing: border-box;
  transition: border-color 0.15s;
}
.bug-textarea:focus {
  outline: none;
  border-color: #0080ff;
  box-shadow: 0 0 0 3px rgba(0, 128, 255, 0.12);
}

.bug-error {
  margin: 0;
  font-size: 0.85rem;
  color: #dc2626;
}

/* ─── Buttons ─── */
.bug-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 4px;
}

.bug-btn-primary {
  padding: 9px 20px;
  background: #0080ff;
  color: #fff;
  border: none;
  border-radius: 8px;
  font-size: 0.9rem;
  font-weight: 600;
  cursor: pointer;
  transition: background 0.15s;
}
.bug-btn-primary:hover:not(:disabled) { background: #006ee0; }
.bug-btn-primary:disabled { opacity: 0.55; cursor: default; }

.bug-btn-secondary {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 9px 16px;
  background: #f3f4f6;
  color: #374151;
  border: none;
  border-radius: 8px;
  font-size: 0.9rem;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.15s;
}
.bug-btn-secondary:hover:not(:disabled) { background: #e5e7eb; }
.bug-btn-secondary:disabled { opacity: 0.55; cursor: default; }

/* ─── Success ─── */
.bug-success-body {
  padding: 32px 24px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  text-align: center;
}

.bug-success-icon {
  width: 52px;
  height: 52px;
  border-radius: 50%;
  background: #dcfce7;
  color: #16a34a;
  font-size: 1.6rem;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 700;
}

.bug-success-text {
  margin: 0;
  font-size: 1rem;
  font-weight: 600;
  color: #1a202c;
}

.bug-issue-link {
  color: #0080ff;
  text-decoration: none;
  font-size: 0.9rem;
  font-weight: 500;
}
.bug-issue-link:hover { text-decoration: underline; }
</style>
