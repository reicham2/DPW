<template>
	<div v-if="error" class="error-alert">
		<div class="error-alert-icon">
			<svg width="18" height="18" viewBox="0 0 20 20" fill="currentColor">
				<path fill-rule="evenodd" d="M10 18a8 8 0 100-16 8 8 0 000 16zM9 7a1 1 0 112 0v4a1 1 0 11-2 0V7zm1 8a1 1 0 100-2 1 1 0 000 2z" clip-rule="evenodd"/>
			</svg>
		</div>
		<span class="error-alert-text" v-html="renderedMessage"></span>
		<button class="error-alert-report" @click="reportBug" title="Fehler melden">
			<svg width="15" height="15" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
				<path d="M3 7V5a2 2 0 012-2h2M17 7V5a2 2 0 00-2-2h-2M3 13v2a2 2 0 002 2h2M17 13v2a2 2 0 01-2 2h-2"/>
				<circle cx="10" cy="10" r="3"/>
			</svg>
			Melden
		</button>
	</div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
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
	background: #fef2f2;
	border: 1px solid #fecaca;
	border-radius: 8px;
	padding: 12px 16px;
	color: #991b1b;
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
