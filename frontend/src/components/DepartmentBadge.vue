<template>
	<ColorBadge :label="department" kind="department" :active="active" />
</template>

<script setup lang="ts">
import ColorBadge from './ColorBadge.vue';

withDefaults(defineProps<{
	department: string | null | undefined;
	active?: boolean;
}>(), { active: true });
</script>
