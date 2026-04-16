<template>
	<div class="public-form-page">
		<!-- Loading -->
		<div v-if="loading" class="loading-state">
			<span class="spinner" />
			Formular wird geladen…
		</div>

		<!-- Not found -->
		<div v-else-if="notFound" class="error-state">
			<div class="error-icon">🔍</div>
			<h2>Formular nicht gefunden</h2>
			<p>Das gesuchte Formular existiert nicht oder wurde gelöscht.</p>
		</div>

		<!-- Error -->
		<div v-else-if="error" class="error-state">
			<div class="error-icon">⚠️</div>
			<h2>Fehler</h2>
			<p>{{ error }}</p>
		</div>

		<!-- Success confirmation -->
		<div v-else-if="submitted" class="success-state">
			<div class="success-icon">✅</div>
			<h2 class="success-title">
				{{ form?.form_type === 'registration' ? 'Anmeldung erfolgreich!' : 'Abmeldung erfolgreich!' }}
			</h2>
			<p class="success-desc">Deine Antworten wurden gespeichert.</p>
		</div>

		<!-- Form -->
		<template v-else-if="form">
			<div class="form-container">
				<header class="form-header">
					<h1 class="form-title">{{ form.title }}</h1>
					<p class="form-subtitle">
						{{ form.form_type === 'registration' ? 'Anmeldung' : 'Abmeldung' }}
					</p>
				</header>

				<form @submit.prevent="doSubmit">
					<FormQuestion
						v-for="q in form.questions"
						:key="q.id"
						:question="q"
						:model-value="answers[q.id] ?? ''"
						:validation-error="validationErrors[q.id]"
						@update="(qid, val) => setAnswer(qid, val)"
					/>

					<div v-if="submitError" class="submit-error">{{ submitError }}</div>

					<div class="form-footer">
						<button
							type="submit"
							class="btn-submit"
							:disabled="submitting"
						>
							<span v-if="submitting" class="spinner white" />
							{{ form.form_type === 'registration' ? 'Anmelden' : 'Abmelden' }}
						</button>
					</div>
				</form>
			</div>
		</template>
	</div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue';
import { useRoute } from 'vue-router';
import type { SignupForm } from '../types';
import { useForms } from '../composables/useForms';
import FormQuestion from '../components/FormQuestion.vue';

const route = useRoute();
const publicSlug = route.params.slug as string;

const { loading, error } = useForms();
const { fetchPublicForm, submitResponse } = useForms();

const form = ref<SignupForm | null>(null);
const notFound = ref(false);
const submitted = ref(false);
const submitting = ref(false);
const submitError = ref<string | null>(null);

const answers = reactive<Record<string, string>>({});
const validationErrors = reactive<Record<string, string>>({});

onMounted(async () => {
	const result = await fetchPublicForm(publicSlug);
	if (!result) {
		notFound.value = true;
		return;
	}
	form.value = result;
});

function setAnswer(questionId: string, value: string) {
	answers[questionId] = value;
	delete validationErrors[questionId];
}

function validate(): boolean {
	if (!form.value) return false;
	let valid = true;
	for (const q of form.value.questions) {
		if (q.question_type === 'section' || !q.is_required) continue;
		const val = answers[q.id];
		if (!val || val.trim() === '' || val === '[]') {
			validationErrors[q.id] = 'Dieses Feld ist erforderlich.';
			valid = false;
		}
	}
	return valid;
}

async function doSubmit() {
	if (!validate()) return;
	submitting.value = true;
	submitError.value = null;
	try {
		const answerList = Object.entries(answers).map(([question_id, answer_value]) => ({
			question_id,
			answer_value,
		}));
		const result = await submitResponse(publicSlug, { answers: answerList });
		if (!result) {
			submitError.value = error.value ?? 'Ein Fehler ist aufgetreten.';
			return;
		}
		submitted.value = true;
	} finally {
		submitting.value = false;
	}
}
</script>

<style scoped>
.public-form-page {
	min-height: 100vh;
	background: #f9fafb;
	display: flex;
	justify-content: center;
	padding: 2rem 1rem;
}

.loading-state, .error-state, .success-state {
	text-align: center;
	padding: 3rem 1rem;
	flex-direction: column;
	display: flex;
	align-items: center;
	gap: 0.75rem;
}

.loading-state {
	color: #6b7280;
	flex-direction: row;
	gap: 0.5rem;
}

.error-icon, .success-icon {
	font-size: 3rem;
	margin-bottom: 0.5rem;
}

.error-state h2, .success-state h2 {
	font-size: 1.25rem;
	font-weight: 700;
	color: #111827;
	margin: 0;
}

.error-state p, .success-state p {
	font-size: 0.875rem;
	color: #6b7280;
	margin: 0;
}

.success-title { color: #065f46 !important; }
.success-desc { color: #374151 !important; }

.form-container {
	background: #fff;
	border-radius: 0.75rem;
	box-shadow: 0 1px 6px rgba(0,0,0,0.08);
	padding: 2rem;
	width: 100%;
	max-width: 560px;
	height: fit-content;
}

.form-header {
	margin-bottom: 1.75rem;
}

.form-title {
	font-size: 1.35rem;
	font-weight: 800;
	color: #111827;
	margin: 0 0 0.3rem;
}

.form-subtitle {
	font-size: 0.85rem;
	color: #6b7280;
	margin: 0;
	font-weight: 500;
}

.submit-error {
	background: #fee2e2;
	color: #991b1b;
	border-radius: 0.375rem;
	padding: 0.65rem 0.9rem;
	font-size: 0.875rem;
	margin-bottom: 1rem;
}

.form-footer {
	display: flex;
	justify-content: flex-end;
	margin-top: 1.5rem;
}

.btn-submit {
	display: flex;
	align-items: center;
	gap: 0.5rem;
	padding: 0.6rem 1.75rem;
	background: #6366f1;
	color: #fff;
	border: none;
	border-radius: 0.5rem;
	font-size: 1rem;
	font-weight: 700;
	cursor: pointer;
	transition: background 0.15s;
}
.btn-submit:hover:not(:disabled) { background: #4f46e5; }
.btn-submit:disabled { opacity: 0.6; cursor: not-allowed; }

.spinner {
	width: 1rem;
	height: 1rem;
	border: 2px solid #e5e7eb;
	border-top-color: #6366f1;
	border-radius: 50%;
	animation: spin 0.7s linear infinite;
	display: inline-block;
}
.spinner.white {
	border-color: rgba(255,255,255,0.4);
	border-top-color: #fff;
}

@keyframes spin { to { transform: rotate(360deg); } }
</style>
