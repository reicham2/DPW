<template>
	<span
		v-if="label"
		class="color-badge"
		:class="{ 'color-badge--active': active }"
		:style="badgeStyle"
	>{{ label }}</span>
</template>

<script setup lang="ts">
import { computed, onMounted } from 'vue';
import { usePermissions } from '../composables/usePermissions';

const props = defineProps<{
	label: string | null | undefined;
	kind: 'department' | 'role';
	active?: boolean;
}>();

const { departments, roles, fetchDepartments, fetchRoles } = usePermissions();

const FALLBACK = '#6b7280';

const color = computed(() => {
	if (!props.label) return FALLBACK;
	if (props.kind === 'department') {
		const rec = departments.value.find((d) => d.name === props.label);
		return rec?.color ?? FALLBACK;
	}
	const rec = roles.value.find((r) => r.name === props.label);
	return rec?.color ?? FALLBACK;
});

const badgeStyle = computed(() => {
	if (props.active) {
		return { background: color.value, color: '#fff' };
	}
	return { background: color.value + '22', color: color.value };
});

onMounted(() => {
	const items = props.kind === 'department' ? departments : roles;
	const fetchFn = props.kind === 'department' ? fetchDepartments : fetchRoles;
	if (items.value.length === 0) {
		fetchFn().catch(() => { /* silent — falls back to neutral color */ });
	}
});
</script>

<style scoped>
.color-badge {
	display: inline-block;
	padding: 2px 10px;
	border-radius: 999px;
	font-size: 0.78rem;
	font-weight: 600;
	line-height: 1.3;
	white-space: nowrap;
}
</style>
