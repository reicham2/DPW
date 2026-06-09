<template>
	<span v-if="names.length" class="responsible-avatars">
		<template v-for="(name, i) in names" :key="name">
			<!-- User (UUID) -->
			<span
				v-if="isUUID(name)"
				class="responsible-avatar-wrapper"
				:style="i > 0 ? { marginLeft: '-6px' } : {}"
			>
				<span
					class="responsible-avatar"
					:style="{ backgroundColor: resolveResponsibleColor(name) }"
				>{{ getInitials(resolveResponsibleName(name)) }}</span>
				<span class="responsible-avatar-name"><span>{{ resolveResponsibleName(name) }}</span></span>
			</span>
			<!-- Predefined / Freitext (nicht-UUID) -->
			<span
				v-else
				class="responsible-tag"
				:style="i > 0 ? { marginLeft: '4px' } : {}"
			>{{ name }}</span>
		</template>
	</span>
	<span v-else class="responsible-avatars-empty">—</span>
</template>

<script setup lang="ts">
import { useUserResolver } from '../composables/useUserResolver';

defineProps<{ names: string[] }>();

const { resolveResponsibleName, resolveResponsibleColor, isUUID } = useUserResolver();

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
	height: 28px;
	flex-wrap: wrap;
	gap: 2px 0;
}

.responsible-avatar-wrapper {
	display: inline-flex;
	align-items: center;
	height: 28px;
	position: relative;
	z-index: 1;
	cursor: default;
	transition: margin-left 0.25s ease;
}

.responsible-avatar-wrapper:hover {
	z-index: 10;
	margin-left: 0 !important;
}

.responsible-avatar {
	width: 28px;
	height: 28px;
	min-width: 28px;
	border-radius: 50%;
	color: #fff;
	border: 2px solid var(--bg-surface);
	font-size: 0.68rem;
	font-weight: 700;
	display: inline-flex;
	align-items: center;
	justify-content: center;
	letter-spacing: 0.3px;
	transition: width 0.25s ease, height 0.25s ease, min-width 0.25s ease,
		font-size 0.25s ease, border-radius 0.25s ease, border 0.25s ease;
}

.responsible-avatar-wrapper:hover .responsible-avatar {
	width: 10px;
	height: 10px;
	min-width: 10px;
	font-size: 0;
	border: none;
}

.responsible-avatar-name {
	display: grid;
	grid-template-columns: 0fr;
	opacity: 0;
	overflow: hidden;
	white-space: nowrap;
	font-size: 0.78rem;
	font-weight: 600;
	color: var(--text-primary);
	padding-left: 0;
	transition: grid-template-columns 0.3s ease, opacity 0.2s ease, padding-left 0.25s ease;
}

.responsible-avatar-name > * {
	overflow: hidden;
}

.responsible-avatar-wrapper:hover .responsible-avatar-name {
	grid-template-columns: 1fr;
	opacity: 1;
	padding-left: 5px;
	padding-right: 6px;
}

.responsible-tag {
	display: inline-flex;
	align-items: center;
	height: 22px;
	padding: 0 8px;
	border-radius: 11px;
	background: var(--bg-hover);
	color: var(--text-secondary);
	font-size: 0.72rem;
	font-weight: 600;
	white-space: nowrap;
	border: 1px solid var(--border-subtle);
}

.responsible-avatars-empty {
	color: var(--text-subtle);
}
</style>
