<template>
	<div class="form-builder">
		<!-- Template selector (always shown when department known, even in edit mode for adding/removing blocks) -->
		<div v-if="department && availableTemplates.length" class="builder-section template-loader">
			<label class="field-label">Vorlage</label>
			<select
				class="field-input"
				:value="selectedTemplateId"
				@change="onTemplateChangeRequested(($event.target as HTMLSelectElement).value)"
			>
				<option value="">— keine Vorlage —</option>
				<option v-for="t in availableTemplates" :key="t.id" :value="t.id">
					{{ t.name }} ({{ t.form_type === 'registration' ? 'Anmeldung' : 'Abmeldung' }})
					{{ t.is_default ? '⭐' : '' }}
				</option>
			</select>
			<p class="template-hint">
				Übernimmt Fragen &amp; Modus aus der gewählten Stufenvorlage.
			</p>
		</div>

		<!-- Form Header -->
		<div class="builder-section">
			<label class="field-label">{{ titleLabel }}</label>
			<div class="input-save-wrap">
				<input
					ref="titleInputRef"
					v-model="localTitle"
					type="text"
					class="field-input"
					:class="{ 'field-input--invalid': titleError }"
					:placeholder="titlePlaceholder"
					@input="markDirty('title')"
				/>
				<span v-if="savedFields['title']" class="field-saved-icon" :key="savedFields['title']"><Save :size="12" aria-hidden="true" /></span>
			</div>
			<p v-if="titleError" class="validation-hint">{{ titleError }}</p>
			<label class="field-label mt-3">Modus</label>
			<div class="mode-toggle">
				<button
					:class="['mode-btn', localFormType === 'registration' ? 'active' : '']"
					type="button"
					@click="localFormType = 'registration'; markDirty('mode')"
				>
					Anmeldung
				</button>
				<button
					:class="['mode-btn', localFormType === 'deregistration' ? 'active' : '']"
					type="button"
					@click="localFormType = 'deregistration'; markDirty('mode')"
				>
					Abmeldung
				</button>
				<span v-if="savedFields['mode']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['mode']"><Save :size="12" aria-hidden="true" /></span>
			</div>
		</div>

		<!-- Questions List -->
		<div class="builder-section">
			<div class="section-header">
				<span class="field-label">Fragen</span>
				<span class="question-count">{{ localQuestions.length }} Frage(n)</span>
			</div>

			<div v-if="localQuestions.length === 0" class="empty-questions">
				Noch keine Fragen. Füge eine Frage hinzu.
			</div>

			<div
				v-for="(q, idx) in localQuestions"
				:key="idx"
				:ref="el => setQuestionCardRef(el, idx)"
				:class="['question-card', isFromTemplate(idx) ? 'question-card--template' : '', choiceErrorIndex === idx ? 'question-card--invalid' : '']"
			>
				<div class="question-header">
					<div class="question-badges">
						<span class="question-type-badge" :data-type="q.question_type">
							{{ typeLabel(q.question_type) }}
						</span>
						<span v-if="isFromTemplate(idx)" class="template-badge">Vorlage</span>
					</div>
					<div class="question-actions">
						<button
							v-if="idx > 0"
							type="button"
							class="icon-btn"
							title="Nach oben"
							@click="moveQuestion(idx, -1)"
						>
							<ChevronUp :size="14" aria-hidden="true" />
						</button>
						<button
							v-if="idx < localQuestions.length - 1"
							type="button"
							class="icon-btn"
							title="Nach unten"
							@click="moveQuestion(idx, 1)"
						>
							<ChevronDown :size="14" aria-hidden="true" />
						</button>
						<button
							type="button"
							class="icon-btn danger"
							title="Entfernen"
							@click="removeQuestion(idx)"
						>
							<X :size="14" aria-hidden="true" />
						</button>
					</div>
				</div>

				<!-- Section type -->
				<template v-if="q.question_type === 'section'">
					<div class="input-save-wrap">
						<input
							v-model="q.question_text"
							class="field-input"
							placeholder="Abschnitts-Titel"
							@input="markDirty(`q${idx}`)"
						/>
						<span v-if="savedFields[`q${idx}`]" class="field-saved-icon" :key="savedFields[`q${idx}`]"><Save :size="12" aria-hidden="true" /></span>
					</div>
					<input
						v-model="q.metadata.subtitle"
						class="field-input mt-1"
						placeholder="Untertitel (optional)"
						@input="markDirty(`q${idx}`)"
					/>
				</template>

				<!-- All other types -->
				<template v-else>
					<div class="input-save-wrap">
						<input
							v-model="q.question_text"
							class="field-input"
							placeholder="Frage"
							@input="markDirty(`q${idx}`)"
						/>
						<span v-if="savedFields[`q${idx}`]" class="field-saved-icon" :key="savedFields[`q${idx}`]"><Save :size="12" aria-hidden="true" /></span>
					</div>
					<div class="question-options-row">
						<label class="checkbox-label">
							<input v-model="q.is_required" type="checkbox" @change="markDirty(`q${idx}`)" />
							Pflichtfeld
						</label>

						<!-- text_input extras -->
						<template v-if="q.question_type === 'text_input'">
							<label class="checkbox-label ml-3">
								<input v-model="q.metadata.multiline" type="checkbox" @change="markDirty(`q${idx}`)" />
								Mehrzeilig
							</label>
						</template>
					</div>

					<!-- Choice options (required for choice-type questions) -->
					<template v-if="['single_choice', 'multiple_choice', 'dropdown'].includes(q.question_type)">
						<div class="choices-list">
							<div
								v-for="(opt, oi) in (q.metadata.choices ?? [])"
								:key="oi"
								class="choice-row"
							>
								<input
									v-model="opt.label"
									class="field-input choice-input"
									:placeholder="`Option ${oi + 1}`"
										@input="markDirty(`q${idx}`)"
								/>
								<button
									type="button"
									class="icon-btn danger"
									@click="removeChoice(q, oi)"
									:disabled="(q.metadata.choices?.length ?? 0) <= 1"
								>
									<X :size="12" aria-hidden="true" />
								</button>
							</div>
							<button
								type="button"
								class="add-choice-btn"
								@click="addChoice(q)"
							>
								+ Option hinzufügen
							</button>
							<p v-if="!q.metadata.choices?.length" class="choices-warning">
								Mindestens eine Option ist erforderlich.
							</p>
						</div>
					</template>
				</template>
			</div>

			<!-- Add question buttons -->
			<div class="add-question-row">
				<span class="add-label">Hinzufügen:</span>
				<button type="button" class="add-btn" @click="addQuestion('text_input')">Freitext</button>
				<button type="button" class="add-btn" @click="addQuestion('single_choice')">Single Choice</button>
				<button type="button" class="add-btn" @click="addQuestion('multiple_choice')">Multiple Choice</button>
				<button type="button" class="add-btn" @click="addQuestion('dropdown')">Dropdown</button>
				<button type="button" class="add-btn" @click="addQuestion('section')">Abschnitt</button>
			</div>
		</div>

		<!-- Actions -->
		<div class="builder-actions">
			<button type="button" class="btn-secondary" @click="revertAndCancel">Abbrechen</button>
			<button
				type="button"
				class="btn-primary"
				@click="save"
			>
				{{ isEdit ? 'Speichern' : 'Erstellen' }}
			</button>
		</div>
		<p v-if="saveError" class="validation-hint validation-hint--global">{{ saveError }}</p>

		<!-- Template change warning modal -->
		<div v-if="showTemplateWarning" class="modal-backdrop" @click.self="cancelTemplateChange">
			<div class="modal modal--danger">
				<h2 class="modal-title modal-title--danger"><TriangleAlert :size="16" aria-hidden="true" /> Vorlage wechseln?</h2>
				<p class="modal-desc">
					Du hast bereits Änderungen am Formular vorgenommen. Beim Wechsel der Vorlage
					werden Fragen, die aus der bisherigen Vorlage stammen, entfernt und durch die
					neue Vorlage ersetzt.
				</p>
				<p class="modal-detail">
					<strong>Betroffene Fragen:</strong> {{ pendingWarningRemovedCount }} Frage(n) aus der aktuellen Vorlage
					werden entfernt.
				</p>
				<div class="modal-actions">
					<button class="btn-cancel" @click="cancelTemplateChange">Abbrechen</button>
					<button class="btn-danger" @click="confirmTemplateChange">Vorlage wechseln</button>
				</div>
			</div>
		</div>
	</div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import type { ComponentPublicInstance } from 'vue';
import { Save, ChevronUp, ChevronDown, X, TriangleAlert } from 'lucide-vue-next';
import type { SignupForm, SignupFormInput, FormQuestionInput, FormTemplate, QuestionType, FormType } from '../types';
import { useFormTemplates } from '../composables/useForms';
import { useAutosave } from '../composables/useAutosave';

interface TrackedQuestion extends FormQuestionInput {
	_fromTemplate?: boolean; // marks this question as originating from a template
}

const props = withDefaults(
	defineProps<{
		initial?: SignupForm | null;
		isEdit?: boolean;
		department?: string;
		titleLabel?: string;
		titlePlaceholder?: string;
	}>(),
	{
		titleLabel: 'Formular-Titel',
		titlePlaceholder: 'z. B. Sommerlager Anmeldung 2026',
	},
);

const emit = defineEmits<{
	(e: 'save', payload: SignupFormInput): void;
	(e: 'autosave', payload: SignupFormInput): void;
	(e: 'cancel'): void;
}>();

const savedFields = ref<Record<string, number>>({});
let savedTimer: ReturnType<typeof setTimeout> | null = null;
const dirtyFieldSet = new Set<string>();

const titleInputRef = ref<HTMLInputElement | null>(null);
const titleError = ref<string | null>(null);
const saveError = ref<string | null>(null);
const choiceErrorIndex = ref<number | null>(null);
const questionCardRefs = ref<Array<HTMLElement | null>>([]);

function setQuestionCardRef(el: Element | ComponentPublicInstance | null, idx: number) {
	questionCardRefs.value[idx] = (el as HTMLElement | null);
}

function showSavedIndicator() {
	if (savedTimer) clearTimeout(savedTimer);
	const snap: Record<string, number> = {};
	for (const f of dirtyFieldSet) snap[f] = Date.now();
	dirtyFieldSet.clear();
	savedFields.value = snap;
	savedTimer = setTimeout(() => { savedFields.value = {}; }, 2000);
}

// Form field state
const localTitle = ref(props.initial?.title ?? '');
const localFormType = ref<FormType>(props.initial?.form_type ?? 'registration');
const initialTitle = ref(localTitle.value);
const initialFormType = ref<FormType>(localFormType.value);
const initialQuestionsJson = ref('[]');

// Template picker
const { templates: availableTemplates, fetchTemplates } = useFormTemplates();
const selectedTemplateId = ref('');
const isDirty = ref(false);

// Warning modal state
const showTemplateWarning = ref(false);
const pendingTemplateId = ref('');
const pendingWarningRemovedCount = ref(0);

onMounted(async () => {
	if (props.department) {
		await fetchTemplates(props.department).catch(() => {});
		// Auto-select default template when creating (no initial form)
		if (!props.isEdit && !props.initial?.id) {
			const defaultTpl = availableTemplates.value.find((t) => t.is_default);
			if (defaultTpl) {
				applyTemplate(defaultTpl.id, false);
			}
		}
	}
	resetInitialSnapshot();
});

watch(
	() => props.department,
	async (d) => {
		if (d) {
			await fetchTemplates(d).catch(() => {});
			if (!props.isEdit && !props.initial?.id) {
				const defaultTpl = availableTemplates.value.find((t) => t.is_default);
				if (defaultTpl && !selectedTemplateId.value) {
					applyTemplate(defaultTpl.id, false);
				}
			}
			resetInitialSnapshot();
		}
	},
);

function markDirty(field?: string) {
	isDirty.value = true;
	titleError.value = null;
	saveError.value = null;
	choiceErrorIndex.value = null;
	if (field) dirtyFieldSet.add(field);
	if (props.isEdit) scheduleAutoSave();
}

/** Called when the user changes the template dropdown */
function onTemplateChangeRequested(id: string) {
	// If not dirty or no current template → apply directly
	if (!isDirty.value || !selectedTemplateId.value) {
		applyTemplate(id, true);
		return;
	}

	// Dirty + changing template → check if data loss would occur
	const templateQuestionCount = localQuestions.value.filter((q) => (q as TrackedQuestion)._fromTemplate).length;
	if (templateQuestionCount > 0) {
		pendingTemplateId.value = id;
		pendingWarningRemovedCount.value = templateQuestionCount;
		showTemplateWarning.value = true;
	} else {
		// No template questions to lose, just apply
		applyTemplate(id, false);
	}
}

function confirmTemplateChange() {
	showTemplateWarning.value = false;
	applyTemplate(pendingTemplateId.value, true);
}

function cancelTemplateChange() {
	showTemplateWarning.value = false;
	// Reset the select to current value
	pendingTemplateId.value = '';
}

/**
 * Apply a template:
 * - If not dirty (cleanSwap=true with no changes): replace everything
 * - If dirty: only remove old template questions and insert new template questions,
 *   keeping user-added questions in place
 */
function applyTemplate(id: string, hadPriorTemplate: boolean) {
	selectedTemplateId.value = id;

	if (!id) {
		// "keine Vorlage" selected
		if (!isDirty.value) {
			// Clean state: clear everything
			localQuestions.value = [];
			localFormType.value = 'registration';
		} else {
			// Dirty: only remove template-originated questions
			localQuestions.value = localQuestions.value.filter((q) => !(q as TrackedQuestion)._fromTemplate);
			reorderPositions();
		}
		return;
	}

	const tpl = availableTemplates.value.find((t) => t.id === id);
	if (!tpl) return;

	const templateQuestions: TrackedQuestion[] = (tpl.template_config ?? []).map((q, i) => ({
		question_text: q.question_text,
		question_type: q.question_type,
		position: i,
		is_required: q.is_required,
		metadata: {
			...(q.metadata ?? {}),
			choices: q.metadata?.choices ? q.metadata.choices.map((c) => ({ ...c })) : undefined,
		},
		_fromTemplate: true,
	}));

	if (!isDirty.value) {
		// Clean swap: replace everything
		localFormType.value = tpl.form_type;
		localQuestions.value = templateQuestions;
	} else {
		// Dirty: remove old template questions, prepend new template questions, keep user questions
		const userQuestions = localQuestions.value.filter((q) => !(q as TrackedQuestion)._fromTemplate);
		localQuestions.value = [...templateQuestions, ...userQuestions];
		localFormType.value = tpl.form_type;
	}
	reorderPositions();
}

function cloneTrackedQuestions(questions: TrackedQuestion[]): TrackedQuestion[] {
	return questions.map((q) => ({
		...q,
		metadata: {
			...(q.metadata ?? {}),
			choices: q.metadata?.choices ? q.metadata.choices.map((c) => ({ ...c })) : undefined,
		},
	}));
}

function cloneQuestions(): TrackedQuestion[] {
	if (!props.initial?.questions) return [];
	return props.initial.questions.map((q) => ({
		question_text: q.question_text,
		question_type: q.question_type,
		position: q.position,
		is_required: q.is_required,
		metadata: { ...q.metadata, choices: q.metadata.choices ? q.metadata.choices.map((c) => ({ ...c })) : undefined },
		_fromTemplate: false,
	}));
}

const localQuestions = ref<TrackedQuestion[]>(cloneQuestions());

function resetInitialSnapshot() {
	initialTitle.value = localTitle.value;
	initialFormType.value = localFormType.value;
	initialQuestionsJson.value = JSON.stringify(cloneTrackedQuestions(localQuestions.value));
}

resetInitialSnapshot();

watch(
	() => props.initial,
	() => {
		localTitle.value = props.initial?.title ?? '';
		localFormType.value = props.initial?.form_type ?? 'registration';
		localQuestions.value = cloneQuestions();
		isDirty.value = false;
		resetInitialSnapshot();
	},
);

function isFromTemplate(idx: number): boolean {
	return !!(localQuestions.value[idx] as TrackedQuestion)?._fromTemplate;
}

function typeLabel(type: QuestionType): string {
	const labels: Record<QuestionType, string> = {
		section: 'Abschnitt',
		text_input: 'Freitext',
		single_choice: 'Single Choice',
		multiple_choice: 'Multiple Choice',
		dropdown: 'Dropdown',
	};
	return labels[type] ?? type;
}

function addQuestion(type: QuestionType) {
	const q: TrackedQuestion = {
		question_text: '',
		question_type: type,
		position: localQuestions.value.length,
		is_required: type !== 'section',
		metadata: type === 'section'
			? { subtitle: '' }
			: ['single_choice', 'multiple_choice', 'dropdown'].includes(type)
				? { choices: [{ id: `opt_${Date.now()}`, label: '' }] }
				: {},
		_fromTemplate: false,
	};
	localQuestions.value.push(q);
	markDirty();
}

function removeQuestion(idx: number) {
	localQuestions.value.splice(idx, 1);
	reorderPositions();
	markDirty();
}

function moveQuestion(idx: number, dir: -1 | 1) {
	const arr = localQuestions.value;
	const target = idx + dir;
	if (target < 0 || target >= arr.length) return;
	[arr[idx], arr[target]] = [arr[target], arr[idx]];
	reorderPositions();
	markDirty();
}

function reorderPositions() {
	localQuestions.value.forEach((q, i) => (q.position = i));
}

function addChoice(q: FormQuestionInput) {
	if (!q.metadata.choices) q.metadata.choices = [];
	q.metadata.choices.push({ id: `opt_${Date.now()}`, label: '' });
	markDirty();
}

function removeChoice(q: FormQuestionInput, idx: number) {
	if ((q.metadata.choices?.length ?? 0) <= 1) return;
	q.metadata.choices?.splice(idx, 1);
	markDirty();
}

/** Validate: all choice-type questions must have at least one option with a label */
const hasChoiceErrors = computed(() =>
	localQuestions.value.some(
		(q) =>
			['single_choice', 'multiple_choice', 'dropdown'].includes(q.question_type) &&
			(!q.metadata.choices?.length || q.metadata.choices.every((c) => !c.label.trim())),
	),
);

const canSave = computed(() => localTitle.value.trim() && !hasChoiceErrors.value);

function firstChoiceErrorIndex(): number {
	return localQuestions.value.findIndex(
		(q) =>
			['single_choice', 'multiple_choice', 'dropdown'].includes(q.question_type) &&
			(!q.metadata.choices?.length || q.metadata.choices.every((c) => !c.label.trim())),
	);
}

function buildPayload(): SignupFormInput {
	reorderPositions();
	const cleanQuestions: FormQuestionInput[] = localQuestions.value.map(({ _fromTemplate, ...rest }) => rest);
	return {
		form_type: localFormType.value,
		title: localTitle.value.trim(),
		questions: cleanQuestions,
	};
}

const { scheduleAutoSave, flushAutoSave, cancelAutoSave } = useAutosave(async () => {
	if (canSave.value) {
		emit('autosave', buildPayload());
		showSavedIndicator();
	}
});

function revertAndCancel() {
	cancelAutoSave();
	localTitle.value = initialTitle.value;
	localFormType.value = initialFormType.value;
	localQuestions.value = JSON.parse(initialQuestionsJson.value);
	isDirty.value = false;
	dirtyFieldSet.clear();
	savedFields.value = {};
	if (props.isEdit) {
		emit('autosave', buildPayload());
	}
	emit('cancel');
}

function save() {
	titleError.value = null;
	saveError.value = null;
	choiceErrorIndex.value = null;

	if (!localTitle.value.trim()) {
		titleError.value = 'Bitte einen Titel eingeben.';
		saveError.value = 'Bitte fülle den Titel aus.';
		titleInputRef.value?.focus();
		titleInputRef.value?.scrollIntoView({ behavior: 'smooth', block: 'center' });
		return;
	}

	const badChoiceIdx = firstChoiceErrorIndex();
	if (badChoiceIdx >= 0) {
		choiceErrorIndex.value = badChoiceIdx;
		saveError.value = 'Mindestens eine Auswahlfrage hat keine gültige Option.';
		questionCardRefs.value[badChoiceIdx]?.scrollIntoView({ behavior: 'smooth', block: 'center' });
		return;
	}

	emit('save', buildPayload());
}

defineExpose({ flushAutoSave });
</script>

<style scoped>
.form-builder {
	display: flex;
	flex-direction: column;
	gap: 1.25rem;
}

.builder-section {
	background: var(--card-bg);
	border: 1px solid var(--border);
	border-radius: 0.5rem;
	padding: 1rem 1.25rem;
}

.section-header {
	display: flex;
	justify-content: space-between;
	align-items: center;
	margin-bottom: 0.75rem;
}

.question-count {
	font-size: 0.75rem;
	color: var(--text-muted);
}

.field-label {
	display: block;
	font-size: 0.8rem;
	font-weight: 600;
	color: var(--text-secondary);
	margin-bottom: 0.35rem;
	text-transform: uppercase;
	letter-spacing: 0.04em;
}

.mt-3 { margin-top: 0.75rem; }
.mt-1 { margin-top: 0.25rem; }
.ml-3 { margin-left: 0.75rem; }

.field-input {
	width: 100%;
	padding: 0.45rem 0.65rem;
	border: 1px solid var(--input-border);
	border-radius: 0.375rem;
	font-size: 0.9rem;
	outline: none;
	background: var(--input-bg);
	color: var(--input-color);
	box-sizing: border-box;
}
.field-input:focus { border-color: #6366f1; box-shadow: 0 0 0 2px rgba(99,102,241,0.15); }
.field-input--invalid { border-color: #dc2626; box-shadow: 0 0 0 2px rgba(220,38,38,0.15); }
.validation-hint {
	margin: 0.35rem 0 0;
	font-size: 0.8rem;
	color: #b91c1c;
}
.validation-hint--global {
	margin-top: -0.35rem;
	text-align: right;
}

.mode-toggle {
	display: flex;
	gap: 0.5rem;
}

.mode-btn {
	padding: 0.4rem 1rem;
	border: 1.5px solid var(--border-strong);
	border-radius: 0.375rem;
	background: var(--bg-surface);
	font-size: 0.85rem;
	cursor: pointer;
	transition: all 0.15s;
	color: var(--text-secondary);
}
.mode-btn.active {
	background: #6366f1;
	color: #fff;
	border-color: #6366f1;
}

.empty-questions {
	text-align: center;
	color: var(--text-subtle);
	font-size: 0.875rem;
	padding: 1.5rem;
}

.question-card {
	border: 1px solid var(--border);
	border-radius: 0.375rem;
	padding: 0.75rem 1rem;
	margin-bottom: 0.625rem;
	background: var(--bg-elevated);
}

.question-card--invalid {
	border-color: #dc2626;
	box-shadow: 0 0 0 2px rgba(220, 38, 38, 0.12);
}

.question-card--template {
	border-color: #c7d2fe;
	background: #f5f3ff;
}

.question-header {
	display: flex;
	justify-content: space-between;
	align-items: center;
	margin-bottom: 0.5rem;
}

.question-badges {
	display: flex;
	gap: 0.35rem;
	align-items: center;
}

.question-type-badge {
	font-size: 0.7rem;
	font-weight: 600;
	text-transform: uppercase;
	letter-spacing: 0.05em;
	padding: 0.2rem 0.5rem;
	border-radius: 9999px;
	background: #e0e7ff;
	color: #4338ca;
}
.question-type-badge[data-type="section"] { background: #fef3c7; color: #92400e; }
.question-type-badge[data-type="text_input"] { background: #d1fae5; color: #065f46; }

.template-badge {
	font-size: 0.65rem;
	font-weight: 600;
	text-transform: uppercase;
	letter-spacing: 0.04em;
	padding: 0.15rem 0.45rem;
	border-radius: 9999px;
	background: #ede9fe;
	color: #7c3aed;
	border: 1px solid #c4b5fd;
}

.question-actions {
	display: flex;
	gap: 0.25rem;
}

.icon-btn {
	padding: 0.2rem 0.45rem;
	border: 1px solid var(--border-strong);
	border-radius: 0.25rem;
	background: var(--bg-surface);
	font-size: 0.75rem;
	cursor: pointer;
	color: var(--text-secondary);
}
.icon-btn:hover { background: var(--bg-hover); }
.icon-btn.danger { border-color: var(--error-border); color: var(--error-color); }
.icon-btn.danger:hover { background: var(--error-bg); }
.icon-btn:disabled { opacity: 0.3; cursor: not-allowed; }

.question-options-row {
	display: flex;
	align-items: center;
	margin-top: 0.4rem;
}

.checkbox-label {
	display: flex;
	align-items: center;
	gap: 0.35rem;
	font-size: 0.82rem;
	color: var(--text-secondary);
	cursor: pointer;
}

.choices-list {
	margin-top: 0.5rem;
	display: flex;
	flex-direction: column;
	gap: 0.3rem;
}

.choice-row {
	display: flex;
	gap: 0.4rem;
	align-items: center;
}

.choice-input {
	flex: 1;
}

.choices-warning {
	margin: 0.25rem 0 0;
	font-size: 0.75rem;
	color: #dc2626;
	font-weight: 500;
}

.add-choice-btn {
	font-size: 0.8rem;
	color: #6366f1;
	background: none;
	border: none;
	cursor: pointer;
	padding: 0.25rem 0;
	text-align: left;
}
.add-choice-btn:hover { text-decoration: underline; }

.add-question-row {
	display: flex;
	align-items: center;
	gap: 0.4rem;
	flex-wrap: wrap;
	margin-top: 0.75rem;
}

.add-label {
	font-size: 0.78rem;
	color: var(--text-muted);
	font-weight: 600;
}

.add-btn {
	padding: 0.3rem 0.75rem;
	border: 1px dashed #a5b4fc;
	border-radius: 9999px;
	background: #eef2ff;
	color: #4338ca;
	font-size: 0.78rem;
	cursor: pointer;
	transition: all 0.15s;
}
.add-btn:hover { background: #e0e7ff; }

.builder-actions {
	display: flex;
	justify-content: flex-end;
	gap: 0.75rem;
}

.btn-primary {
	padding: 0.5rem 1.25rem;
	background: #6366f1;
	color: #fff;
	border: none;
	border-radius: 0.375rem;
	font-size: 0.875rem;
	font-weight: 600;
	cursor: pointer;
}
.btn-primary:hover:not(:disabled) { background: #4f46e5; }
.btn-primary:disabled { opacity: 0.5; cursor: not-allowed; }

.btn-secondary {
	padding: 0.5rem 1.25rem;
	background: var(--btn-secondary-bg);
	color: var(--text-secondary);
	border: 1px solid var(--border-strong);
	border-radius: 0.375rem;
	font-size: 0.875rem;
	cursor: pointer;
}
.btn-secondary:hover { background: var(--bg-elevated); }

.template-loader { background: #eef2ff; border-color: #c7d2fe; }
.template-hint {
	margin: 0.4rem 0 0;
	font-size: 0.75rem;
	color: #4338ca;
}

/* Warning modal */
.modal-backdrop {
	position: fixed;
	inset: 0;
	background: rgba(0,0,0,0.4);
	display: flex;
	align-items: center;
	justify-content: center;
	z-index: 100;
}

.modal {
	background: var(--modal-bg);
	border-radius: 16px;
	padding: 28px 32px;
	width: 100%;
	max-width: 500px;
	box-shadow: 0 8px 40px rgba(0,0,0,0.2);
}

.modal--danger { border: 2px solid var(--error-border); }

.modal-title {
	font-size: 1.1rem;
	font-weight: 700;
	color: var(--text-primary);
	margin: 0 0 6px;
}

.modal-title--danger { color: var(--error-color); }

.modal-desc {
	font-size: 0.88rem;
	color: var(--text-muted);
	margin: 0 0 12px;
}

.modal-detail {
	font-size: 0.85rem;
	color: var(--text-secondary);
	margin: 0 0 18px;
	padding: 0.5rem 0.75rem;
	background: #fef2f2;
	border-radius: 0.375rem;
	border: 1px solid #fecaca;
}

.modal-actions {
	display: flex;
	justify-content: flex-end;
	gap: 10px;
}

.btn-cancel {
	padding: 8px 18px;
	border-radius: 8px;
	border: 1px solid var(--border-strong);
	background: var(--btn-secondary-bg);
	color: var(--text-secondary);
	font-size: 0.88rem;
	cursor: pointer;
}
.btn-cancel:hover { background: var(--bg-hover); }

.btn-danger {
	padding: 8px 18px;
	border-radius: 8px;
	border: none;
	background: var(--btn-danger-bg);
	color: var(--btn-danger-color);
	font-size: 0.88rem;
	font-weight: 600;
	cursor: pointer;
}
.btn-danger:hover { background: var(--btn-danger-bg-hover); }
</style>
