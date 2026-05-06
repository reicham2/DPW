<template>
	<div class="form-question">
		<!-- Section -->
		<template v-if="question.question_type === 'section'">
			<div class="section-divider">
				<span class="section-title">{{ question.question_text }}</span>
				<p v-if="question.metadata.subtitle" class="section-subtitle">
					{{ question.metadata.subtitle }}
				</p>
			</div>
		</template>

		<!-- Text input -->
		<template v-else-if="question.question_type === 'text_input'">
			<label class="question-label">
				{{ question.question_text }}
				<span v-if="question.is_required" class="required-star">*</span>
			</label>
			<textarea
				v-if="question.metadata.multiline"
				v-model="currentValue"
				class="field-input"
				rows="3"
				:maxlength="question.metadata.max_length"
				:placeholder="question.question_text"
				@input="emit('update', question.id, currentValue)"
			/>
			<input
				v-else
				v-model="currentValue"
				type="text"
				class="field-input"
				:maxlength="question.metadata.max_length"
				:placeholder="question.question_text"
				@input="emit('update', question.id, currentValue)"
			/>
			<span v-if="validationError" class="validation-error">{{ validationError }}</span>
		</template>

		<!-- Single choice (radio) -->
		<template v-else-if="question.question_type === 'single_choice'">
			<label class="question-label">
				{{ question.question_text }}
				<span v-if="question.is_required" class="required-star">*</span>
			</label>
			<div class="choices-group">
				<label
					v-for="opt in question.metadata.choices ?? []"
					:key="opt.id"
					class="choice-option"
				>
					<input
						type="radio"
						:name="question.id"
						:value="opt.id"
						:checked="currentValue === opt.id"
						@change="onSingleChange(opt.id)"
					/>
					<span>{{ opt.label }}</span>
				</label>
			</div>
			<span v-if="validationError" class="validation-error">{{ validationError }}</span>
		</template>

		<!-- Multiple choice (checkboxes) -->
		<template v-else-if="question.question_type === 'multiple_choice'">
			<label class="question-label">
				{{ question.question_text }}
				<span v-if="question.is_required" class="required-star">*</span>
			</label>
			<div class="choices-group">
				<label
					v-for="opt in question.metadata.choices ?? []"
					:key="opt.id"
					class="choice-option"
				>
					<input
						type="checkbox"
						:value="opt.id"
						:checked="multiValues.includes(opt.id)"
						@change="onMultiChange(opt.id)"
					/>
					<span>{{ opt.label }}</span>
				</label>
			</div>
			<span v-if="validationError" class="validation-error">{{ validationError }}</span>
		</template>

		<!-- Dropdown -->
		<template v-else-if="question.question_type === 'dropdown'">
			<label class="question-label">
				{{ question.question_text }}
				<span v-if="question.is_required" class="required-star">*</span>
			</label>
			<select
				v-model="currentValue"
				class="field-input"
				@change="emit('update', question.id, currentValue)"
			>
				<option value="">— Bitte wählen —</option>
				<option
					v-for="opt in question.metadata.choices ?? []"
					:key="opt.id"
					:value="opt.id"
				>
					{{ opt.label }}
				</option>
			</select>
			<span v-if="validationError" class="validation-error">{{ validationError }}</span>
		</template>
	</div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue';
import type { FormQuestion } from '../types';

const props = defineProps<{
	question: FormQuestion;
	modelValue?: string;
	validationError?: string;
}>();

const emit = defineEmits<{
	(e: 'update', questionId: string, value: string): void;
}>();

const currentValue = ref(props.modelValue ?? '');

watch(
	() => props.modelValue,
	(v) => { currentValue.value = v ?? ''; },
);

// For multiple choice: value is a JSON array string e.g. '["opt1","opt2"]'
const multiValues = computed<string[]>(() => {
	if (!currentValue.value) return [];
	try { return JSON.parse(currentValue.value); }
	catch { return []; }
});

function onSingleChange(id: string) {
	currentValue.value = id;
	emit('update', props.question.id, id);
}

function onMultiChange(id: string) {
	const arr = [...multiValues.value];
	const idx = arr.indexOf(id);
	if (idx === -1) arr.push(id);
	else arr.splice(idx, 1);
	const val = JSON.stringify(arr);
	currentValue.value = val;
	emit('update', props.question.id, val);
}
</script>

<style scoped>
.form-question {
	margin-bottom: 1.25rem;
}

.section-divider {
	border-top: 2px solid var(--border);
	margin: 0.5rem 0 1.5rem;
	padding-top: 1rem;
}

.section-title {
	font-size: 1rem;
	font-weight: 700;
	color: var(--text-primary);
	text-transform: uppercase;
	letter-spacing: 0.05em;
}

.section-subtitle {
	font-size: 0.85rem;
	color: var(--text-muted);
	margin-top: 0.25rem;
	margin-bottom: 0;
}

.question-label {
	display: block;
	font-size: 0.9rem;
	font-weight: 600;
	color: var(--text-secondary);
	margin-bottom: 0.4rem;
}

.required-star {
	color: #dc2626;
	margin-left: 0.2rem;
}

.field-input {
	width: 100%;
	padding: 0.5rem 0.75rem;
	border: 1px solid var(--input-border);
	border-radius: 0.375rem;
	font-size: 0.9rem;
	background: var(--input-bg);
	color: var(--input-color);
	box-sizing: border-box;
	outline: none;
	transition: border-color 0.15s;
}
.field-input:focus { border-color: #6366f1; box-shadow: 0 0 0 2px rgba(99,102,241,0.12); }

textarea.field-input { resize: vertical; }

.choices-group {
	display: flex;
	flex-direction: column;
	gap: 0.4rem;
}

.choice-option {
	display: flex;
	align-items: center;
	gap: 0.5rem;
	font-size: 0.9rem;
	color: var(--text-secondary);
	cursor: pointer;
}

.choice-option input { cursor: pointer; }

.validation-error {
	display: block;
	font-size: 0.78rem;
	color: #dc2626;
	margin-top: 0.25rem;
}
</style>
