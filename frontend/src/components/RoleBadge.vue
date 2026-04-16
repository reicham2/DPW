<template>
	<span
		class="role-badge"
		:class="{ 'role-badge--active': active }"
		:style="badgeStyle"
	>{{ role }}</span>
</template>

<script setup lang="ts">
import { computed, onMounted } from 'vue';
import { usePermissions } from '../composables/usePermissions';

const props = defineProps<{
	role: string;
	active?: boolean;
}>();

const { roles, fetchRoles } = usePermissions();

const FALLBACK = '#6b7280';

const color = computed(() => {
	const rec = roles.value.find((r) => r.name === props.role);
	return rec?.color ?? FALLBACK;
});

const badgeStyle = computed(() => {
	if (props.active) {
		return { background: color.value, color: '#fff' };
	}
	return { background: color.value + '22', color: color.value };
});

onMounted(() => {
	if (roles.value.length === 0) {
		fetchRoles().catch(() => {
			/* silent — falls back to neutral color */
		});
	}
});
</script>

<style scoped>
.role-badge {
	display: inline-block;
	padding: 2px 10px;
	border-radius: 999px;
	font-size: 0.78rem;
	font-weight: 600;
	line-height: 1.3;
	white-space: nowrap;
}
</style>
