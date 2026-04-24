<template>
	<div class="badge-select" ref="rootRef" :class="{ 'badge-select--disabled': disabled }">
		<button
			type="button"
			class="badge-select-button"
			:disabled="disabled"
			@click="toggle"
			@keydown="onButtonKey"
		>
			<RoleBadge v-if="kind === 'role' && modelValue" :role="modelValue" />
			<DepartmentBadge v-else-if="kind === 'department' && modelValue" :department="modelValue" />
			<span v-else class="badge-select-placeholder">{{ placeholderText }}</span>
			<ChevronDown class="badge-select-caret" :size="12" aria-hidden="true" />
		</button>

		<ul v-if="open" class="badge-select-menu" role="listbox">
			<li
				v-if="allowEmpty"
				class="badge-select-option"
				:class="{ 'badge-select-option--focused': focusedIndex === 0 }"
				role="option"
				:aria-selected="modelValue === null || modelValue === ''"
				@click="select(null)"
				@mouseenter="focusedIndex = 0"
			>
				<span class="badge-select-placeholder">{{ placeholderText }}</span>
			</li>
			<li
				v-for="(item, idx) in items"
				:key="item.value"
				class="badge-select-option"
				:class="{ 'badge-select-option--focused': focusedIndex === idx + (allowEmpty ? 1 : 0) }"
				role="option"
				:aria-selected="modelValue === item.value"
				@click="select(item.value)"
				@mouseenter="focusedIndex = idx + (allowEmpty ? 1 : 0)"
			>
				<RoleBadge v-if="kind === 'role'" :role="item.value" />
				<DepartmentBadge v-else :department="item.value" />
			</li>
		</ul>
	</div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick } from 'vue';
import { ChevronDown } from 'lucide-vue-next';
import RoleBadge from './RoleBadge.vue';
import DepartmentBadge from './DepartmentBadge.vue';

interface BadgeItem {
	value: string;
}

const props = withDefaults(
	defineProps<{
		modelValue: string | null | undefined;
		items: BadgeItem[];
		kind: 'role' | 'department';
		disabled?: boolean;
		allowEmpty?: boolean;
		placeholder?: string;
	}>(),
	{
		disabled: false,
		allowEmpty: false,
		placeholder: '',
	},
);

const emit = defineEmits<{
	(e: 'update:modelValue', value: string | null): void;
}>();

const open = ref(false);
const focusedIndex = ref(0);
const rootRef = ref<HTMLElement | null>(null);

const placeholderText = computed(() => {
	if (props.placeholder) return props.placeholder;
	return props.kind === 'role' ? 'Rolle wählen…' : 'Keine Angabe';
});

const optionCount = computed(() => props.items.length + (props.allowEmpty ? 1 : 0));

function valueAt(i: number): string | null {
	if (props.allowEmpty && i === 0) return null;
	const idx = i - (props.allowEmpty ? 1 : 0);
	return props.items[idx]?.value ?? null;
}

function currentIndex(): number {
	if (props.modelValue == null || props.modelValue === '') {
		return props.allowEmpty ? 0 : 0;
	}
	const i = props.items.findIndex((it) => it.value === props.modelValue);
	if (i < 0) return 0;
	return i + (props.allowEmpty ? 1 : 0);
}

function toggle() {
	if (props.disabled) return;
	open.value = !open.value;
	if (open.value) {
		focusedIndex.value = currentIndex();
	}
}

function close() {
	open.value = false;
}

function select(value: string | null) {
	emit('update:modelValue', value);
	close();
}

function onButtonKey(e: KeyboardEvent) {
	if (props.disabled) return;
	if (e.key === 'Enter' || e.key === ' ' || e.key === 'ArrowDown') {
		e.preventDefault();
		if (!open.value) {
			open.value = true;
			focusedIndex.value = currentIndex();
		}
	} else if (e.key === 'Escape') {
		close();
	}
}

function onDocumentKey(e: KeyboardEvent) {
	if (!open.value) return;
	if (e.key === 'Escape') {
		e.preventDefault();
		close();
	} else if (e.key === 'ArrowDown') {
		e.preventDefault();
		if (optionCount.value === 0) return;
		focusedIndex.value = (focusedIndex.value + 1) % optionCount.value;
	} else if (e.key === 'ArrowUp') {
		e.preventDefault();
		if (optionCount.value === 0) return;
		focusedIndex.value = (focusedIndex.value - 1 + optionCount.value) % optionCount.value;
	} else if (e.key === 'Enter') {
		e.preventDefault();
		select(valueAt(focusedIndex.value));
	}
}

function onClickOutside(e: MouseEvent) {
	if (rootRef.value && !rootRef.value.contains(e.target as Node)) {
		close();
	}
}

onMounted(() => {
	document.addEventListener('mousedown', onClickOutside);
	document.addEventListener('keydown', onDocumentKey);
	nextTick();
});
onUnmounted(() => {
	document.removeEventListener('mousedown', onClickOutside);
	document.removeEventListener('keydown', onDocumentKey);
});
</script>

<style scoped>
.badge-select {
	position: relative;
	display: inline-block;
	width: 100%;
}
.badge-select-button {
	width: 100%;
	display: flex;
	align-items: center;
	justify-content: space-between;
	gap: 8px;
	padding: 8px 12px;
	border: 1.5px solid #d1d5db;
	border-radius: 8px;
	background: #fff;
	font-size: 0.95rem;
	color: #1a202c;
	cursor: pointer;
	transition: border-color 0.15s;
}
.badge-select-button:hover:not(:disabled) {
	border-color: #9ca3af;
}
.badge-select-button:focus {
	outline: none;
	border-color: #1a56db;
}
.badge-select-button:disabled {
	opacity: 0.6;
	cursor: default;
	background: #f9fafb;
}
.badge-select-placeholder {
	color: #6b7280;
	font-size: 0.88rem;
}
.badge-select-caret {
	color: #6b7280;
	flex-shrink: 0;
}
.badge-select-menu {
	position: absolute;
	top: calc(100% + 4px);
	left: 0;
	right: 0;
	max-height: 260px;
	overflow-y: auto;
	background: #fff;
	border: 1px solid #e5e7eb;
	border-radius: 8px;
	box-shadow: 0 4px 16px rgba(0, 0, 0, 0.08);
	z-index: 100;
	padding: 4px;
	margin: 0;
	list-style: none;
}
.badge-select-option {
	padding: 6px 8px;
	border-radius: 6px;
	cursor: pointer;
	display: flex;
	align-items: center;
}
.badge-select-option--focused {
	background: #f3f4f6;
}
</style>
