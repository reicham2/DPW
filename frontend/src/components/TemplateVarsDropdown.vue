<template>
	<div class="tpl-vars">
		<button type="button" class="tpl-vars-toggle" @click="open = !open">
			{{ open ? '▾' : '▸' }} Verfügbare Variablen
		</button>
		<p v-if="hint" class="tpl-vars-hint">{{ hint }}</p>
		<div v-if="open" class="tpl-vars-list">
			<div v-for="v in allVars" :key="v.var" class="tpl-var-row">
				<code class="tpl-var-code">{{ v.var }}</code>
				<span class="tpl-var-desc">{{ v.desc }}</span>
			</div>
		</div>
	</div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue';

interface TplVar {
	var: string;
	desc: string;
}

const BASE_VARIABLES: TplVar[] = [
	{ var: '{{titel}}',          desc: 'Titel der Aktivität' },
	{ var: '{{datum}}',          desc: 'Datum lang (z.B. Samstag, 12. April 2026)' },
	{ var: '{{datum_kurz}}',     desc: 'Datum kurz (z.B. 12.04.2026)' },
	{ var: '{{startzeit}}',      desc: 'Startzeit (HH:MM)' },
	{ var: '{{endzeit}}',        desc: 'Endzeit (HH:MM)' },
	{ var: '{{ort}}',            desc: 'Veranstaltungsort' },
	{ var: '{{verantwortlich}}', desc: 'Verantwortliche Person' },
	{ var: '{{abteilung}}',      desc: 'Stufe' },
	{ var: '{{ziel}}',           desc: 'Ziel der Aktivität' },
	{ var: '{{material}}',       desc: 'Materialliste (kommagetrennt)' },
	{ var: '{{schlechtwetter}}', desc: 'Schlechtwetter-Info' },
	{ var: '{{programm}}',       desc: 'Programmpunkte (formatiert)' },
];

const props = defineProps<{
	extraVars?: TplVar[];
	hint?: string;
}>();

const open = ref(false);
const allVars = computed(() =>
	props.extraVars?.length ? [...BASE_VARIABLES, ...props.extraVars] : BASE_VARIABLES
);
</script>

<style scoped>
.tpl-vars-hint {
	margin: 0.25rem 0 0;
	padding: 0 14px;
	font-size: 0.75rem;
	color: #9ca3af;
}
</style>
