<template>
	<ColorBadge :label="role" kind="role" :active="active" />
</template>

<script setup lang="ts">
import ColorBadge from './ColorBadge.vue';

defineProps<{
	role: string;
	active?: boolean;
}>();
</script>
