<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { ProgramImageRenderer, sanitizeProgramDescription } from '../utils/programImages';

const props = defineProps<{ html: string; sharedToken?: string; guestToken?: string }>();
const content = ref<HTMLElement | null>(null);
const imageError = ref<string | null>(null);
const renderer = new ProgramImageRenderer(props.sharedToken, props.guestToken);

async function render() {
	const el = content.value;
	if (!el) return;
	el.innerHTML = sanitizeProgramDescription(props.html);
	imageError.value = null;
	try {
		await renderer.render(el);
	} catch (error) {
		imageError.value = error instanceof Error ? error.message : 'Bild konnte nicht geladen werden.';
	}
}

onMounted(render);
watch(() => props.html, render);
onBeforeUnmount(() => renderer.dispose());
</script>

<template>
	<div class="program-desc">
		<div ref="content" />
		<p v-if="imageError" class="field-warning" role="alert">{{ imageError }}</p>
	</div>
</template>
