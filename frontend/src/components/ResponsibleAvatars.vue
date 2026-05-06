<template>
	<span v-if="names.length" class="responsible-avatars">
		<span
			v-for="(name, i) in names"
			:key="name"
			class="responsible-avatar"
			:style="i > 0 ? { marginLeft: '-8px' } : {}"
			:title="name"
		>{{ getInitials(name) }}</span>
	</span>
	<span v-else class="responsible-avatars-empty">—</span>
</template>

<script setup lang="ts">
defineProps<{ names: string[] }>();

function getInitials(name: string): string {
	const words = name.trim().split(/\s+/);
	if (words.length === 1) return name.trim().slice(0, 2).toUpperCase();
	return words.slice(0, 2).map(w => w[0].toUpperCase()).join('');
}
</script>

<style scoped>
.responsible-avatars {
	display: inline-flex;
	align-items: center;
}

.responsible-avatar {
	width: 28px;
	height: 28px;
	border-radius: 50%;
	background: #e8f0fe;
	color: #1a56db;
	border: 2px solid #fff;
	font-size: 0.68rem;
	font-weight: 700;
	display: inline-flex;
	align-items: center;
	justify-content: center;
	letter-spacing: 0.3px;
	cursor: default;
	position: relative;
	z-index: 1;
}

.responsible-avatar:hover {
	z-index: 10;
	background: #1a56db;
	color: #fff;
}

.responsible-avatars-empty {
	color: #9ca3af;
}
</style>
