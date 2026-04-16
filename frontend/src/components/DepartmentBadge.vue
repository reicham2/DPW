<template>
	<span
		v-if="department"
		class="dept-badge"
		:class="{ 'dept-badge--active': active }"
		:style="badgeStyle"
	>{{ department }}</span>
</template>

<script setup lang="ts">
import { computed, onMounted } from 'vue';
import { usePermissions } from '../composables/usePermissions';

const props = defineProps<{
	department: string | null | undefined;
	active?: boolean;
}>();

const { departments, fetchDepartments } = usePermissions();

const FALLBACK = '#6b7280';

const color = computed(() => {
	if (!props.department) return FALLBACK;
	const rec = departments.value.find((d) => d.name === props.department);
	return rec?.color ?? FALLBACK;
});

const badgeStyle = computed(() => {
	if (props.active) {
		return { background: color.value, color: '#fff' };
	}
	return { background: color.value + '22', color: color.value };
});

onMounted(() => {
	if (departments.value.length === 0) {
		fetchDepartments().catch(() => {
			/* silent — falls back to neutral color */
		});
	}
});
</script>

<style scoped>
.dept-badge {
	display: inline-block;
	padding: 2px 10px;
	border-radius: 999px;
	font-size: 0.78rem;
	font-weight: 600;
	line-height: 1.3;
	white-space: nowrap;
}
</style>
