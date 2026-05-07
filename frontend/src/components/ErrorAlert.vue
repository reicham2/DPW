<template>
	<div v-if="error" class="error-alert">
		<div class="error-alert-icon">
			<CircleAlert :size="18" aria-hidden="true" />
		</div>
		<span class="error-alert-text" v-html="renderedMessage"></span>
		<button v-if="config.GITHUB_BUG_REPORT_ENABLED" class="error-alert-report" @click="reportBug" title="Fehler melden">
			<Bug :size="15" aria-hidden="true" />
			Melden
		</button>
	</div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { Bug, CircleAlert } from 'lucide-vue-next'
import { config } from '../config'
import { openBugReport } from '../composables/useBugReport'

const props = defineProps<{ error: string | null }>()

function escapeHtml(text: string): string {
	return text
		.replace(/&/g, '&amp;')
		.replace(/</g, '&lt;')
		.replace(/>/g, '&gt;')
		.replace(/\"/g, '&quot;')
}

function renderMarkdown(text: string): string {
	return escapeHtml(text).replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
}

const friendlyMessage = computed(() => {
	if (!props.error) return ''
	let msg = props.error

	// Strip "Error: " prefix
	msg = msg.replace(/^Error:\s*/i, '')

	// Try to parse JSON error body
	try {
		const parsed = JSON.parse(msg)
		if (parsed.error) msg = parsed.error
		if (parsed.message) msg = parsed.message
	} catch { /* not JSON, keep as-is */ }

	// Fallback translations for common English patterns (e.g. from network layer)
	if (/^failed to fetch|networkerror|net::err/i.test(msg)) {
		return 'Verbindungsfehler – bitte prüfe deine Internetverbindung und versuche es erneut.'
	}
	if (/timeout/i.test(msg) && !/Zeit/i.test(msg)) {
		return 'Zeitüberschreitung – bitte versuche es erneut.'
	}

	return msg
})

const renderedMessage = computed(() => renderMarkdown(friendlyMessage.value))

function reportBug() {
	const page = window.location.pathname
	const prefill = `**Fehlermeldung:** ${props.error}\n**Seite:** ${page}\n**Zeitpunkt:** ${new Date().toLocaleString('de-CH')}\n\n**Beschreibung:**\nBeschreibe hier was du gemacht hast, als der Fehler aufgetreten ist…`
	openBugReport(prefill)
}
</script>

<style scoped>
.error-alert {
	display: flex;
	align-items: center;
	gap: 10px;
	background: var(--error-bg);
	border: 1px solid #fecaca;
	border-radius: 8px;
	padding: 12px 16px;
	color: var(--error-color);
	font-size: 0.9rem;
	line-height: 1.4;
}
.error-alert-icon {
	flex-shrink: 0;
	display: flex;
	color: #dc2626;
}
.error-alert-text {
	flex: 1;
	word-break: break-word;
}
.error-alert-report {
	flex-shrink: 0;
	display: inline-flex;
	align-items: center;
	gap: 5px;
	background: none;
	border: 1px solid #fca5a5;
	border-radius: 6px;
	padding: 5px 10px;
	font-size: 0.8rem;
	font-weight: 600;
	color: #b91c1c;
	cursor: pointer;
	transition: background 0.15s, border-color 0.15s;
	white-space: nowrap;
}
.error-alert-report:hover {
	background: #fee2e2;
	border-color: #f87171;
}
</style>
