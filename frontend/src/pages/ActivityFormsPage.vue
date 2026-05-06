<template>
	<header class="header">
		<button class="btn-back" @click="goBack"><ArrowLeft class="btn-icon" :size="16" aria-hidden="true" /> Zurück</button>
		<h1>Formular</h1>
	</header>

	<main class="main">
		<div class="forms-page">
			<!-- Loading -->
			<div v-if="loading && !form" class="loading-state">
				<span class="spinner" />
				Formular wird geladen…
			</div>

			<!-- Error -->
			<div v-else-if="error" class="error-banner">{{ error }}</div>

			<!-- No form yet -->
			<template v-else-if="!form && !showBuilder">
				<div class="empty-form-state">
					<div class="empty-icon"><ClipboardList :size="30" aria-hidden="true" /></div>
					<p class="empty-title">Kein Formular vorhanden</p>
					<p class="empty-desc">Diese Aktivität hat noch kein Formular.</p>
					<button class="btn-primary" @click="openCreate">Formular erstellen</button>
				</div>
			</template>

			<!-- Form Builder (create or edit) -->
			<template v-else-if="showBuilder">
				<div class="page-header">
					<h2 class="page-title">{{ editingForm ? 'Formular bearbeiten' : 'Formular erstellen' }}</h2>
				</div>
				<FormBuilder
					ref="formBuilderRef"
					:initial="editingForm ?? templateInitial"
					:is-edit="!!editingForm"
					:department="activityDepartment"
					@save="onSave"
					@autosave="onAutoSave"
					@cancel="cancelBuilder"
				/>
			</template>

			<!-- Existing form view -->
			<template v-else-if="form">
				<div class="page-header">
					<div class="form-info">
						<h2 class="page-title">{{ form.title }}</h2>
						<span class="mode-badge" :data-type="form.form_type">
							{{ form.form_type === 'registration' ? 'Anmeldung' : 'Abmeldung' }}
						</span>
					</div>
					<div class="header-actions">
						<button
							v-if="canOpenPublicFormLink(form?.public_slug)"
							class="btn-mail"
							:class="{ 'btn-mail--active': linkCopied }"
							@click="copyPublicFormLink(form?.public_slug)"
						>
							<Check v-if="linkCopied" class="btn-icon" :size="16" aria-hidden="true" />
							<Share2 v-else class="btn-icon" :size="16" aria-hidden="true" />
							{{ linkCopied ? 'Kopiert' : 'Link kopieren' }}
						</button>
						<button
							v-else
							class="btn-mail"
							disabled
							title="Öffentliche Basis-URL ist nicht konfiguriert"
						>
							<Share2 class="btn-icon" :size="16" aria-hidden="true" />
							Link kopieren
						</button>
						<button class="btn-secondary" @click="openEdit">Bearbeiten</button>
						<button class="btn-danger" @click="confirmDelete">Löschen</button>
					</div>
				</div>

				<!-- Tabs: Fragen / Antworten / Statistik -->
				<div class="tabs">
					<button
						v-for="tab in tabs"
						:key="tab.id"
						:class="['tab-btn', activeTab === tab.id ? 'active' : '']"
						@click="activeTab = tab.id"
					>
						{{ tab.label }}
					</button>
				</div>

				<!-- Tab: Fragen -->
				<div v-if="activeTab === 'questions'" class="tab-content">
					<div v-if="form.questions.length === 0" class="empty-section">
						Keine Fragen konfiguriert.
					</div>
					<div
						v-for="q in form.questions"
						:key="q.id"
						class="question-preview-card"
						:data-type="q.question_type"
					>
						<div class="qp-type">{{ questionTypeLabel(q.question_type) }}</div>
						<div class="qp-text">{{ q.question_text }}</div>
						<div v-if="q.question_type !== 'section'" class="qp-meta">
							<span v-if="q.is_required" class="badge required">Pflicht</span>
							<span v-else class="badge optional">Optional</span>
							<span v-if="q.metadata.choices?.length" class="badge choices">
								{{ q.metadata.choices.length }} Optionen
							</span>
						</div>
					</div>
				</div>

				<!-- Tab: Antworten -->
				<div v-else-if="activeTab === 'responses'" class="tab-content">
					<div v-if="loadingResponses" class="loading-state"><span class="spinner" /> Wird geladen…</div>
					<template v-else>
						<div v-if="responses.length === 0" class="empty-section">
							Noch keine Antworten eingegangen.
						</div>
						<div
							v-for="r in responses"
							:key="r.id"
							class="response-row"
						>
							<div class="response-info">
								<span class="response-date">
									{{ formatDate(r.submitted_at) }}
								</span>
								<span class="mode-badge small" :data-type="r.submission_mode">
									{{ r.submission_mode === 'registration' ? 'Anmeldung' : 'Abmeldung' }}
								</span>
							</div>
							<div class="response-actions">
								<button class="btn-sm" @click="openResponse(r.id)">Details</button>
								<button class="btn-sm danger" @click="removeResponse(r.id)">Löschen</button>
							</div>
						</div>

						<!-- Response detail drawer -->
						<div v-if="selectedResponse" class="response-detail">
							<div class="detail-header">
								<span>Antwort-Details</span>
								<button class="icon-btn" @click="selectedResponse = null" aria-label="Schliessen"><X :size="14" aria-hidden="true" /></button>
							</div>
							<div
								v-for="answer in selectedResponse.answers ?? []"
								:key="answer.question_id"
								class="answer-row"
							>
								<span class="answer-q">{{ questionText(answer.question_id) }}</span>
								<span class="answer-v">{{ answer.answer_value }}</span>
							</div>
						</div>
					</template>
				</div>

				<!-- Tab: Statistik -->
				<div v-else-if="activeTab === 'stats'" class="tab-content">
					<div v-if="loadingStats" class="loading-state"><span class="spinner" /> Wird geladen…</div>
					<FormStats
						v-else
						:stats="stats"
						:form="form"
					/>
				</div>
			</template>

			<!-- Delete confirmation modal -->
			<div v-if="showDeleteModal" class="modal-overlay" @click.self="showDeleteModal = false">
				<div class="modal">
					<h3 class="modal-title">Formular löschen?</h3>
					<p class="modal-desc">
						Alle Fragen und Antworten werden unwiderruflich gelöscht.
					</p>
					<div class="modal-actions">
						<button class="btn-secondary" @click="showDeleteModal = false">Abbrechen</button>
						<button class="btn-danger" @click="doDelete">Löschen</button>
					</div>
				</div>
			</div>
		</div>
	</main>
</template>

<script setup lang="ts">
import { ref, onMounted, watch, computed } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import type { Activity, SignupForm, SignupFormInput, FormResponse, FormQuestionInput, FormType } from '../types';
import { useForms, useFormTemplates } from '../composables/useForms';
import { useActivities } from '../composables/useActivities';
import { usePermissions } from '../composables/usePermissions';
import { user } from '../composables/useAuth';
import { config } from '../config';
import { ArrowLeft, ClipboardList, X, Check, Share2 } from 'lucide-vue-next';
import FormBuilder from '../components/FormBuilder.vue';
import FormStats from '../components/FormStats.vue';

const route = useRoute();
const router = useRouter();
const activityId = computed(() => String(route.params.id ?? ''));

const {
	form,
	responses,
	stats,
	loading,
	error,
	fetchForm,
	createForm,
	updateForm,
	deleteForm,
	fetchResponses,
	fetchResponse,
	deleteResponse,
	fetchStats,
	getFormDraft,
	saveFormDraft,
	deleteFormDraft,
} = useForms();

const { fetchActivity } = useActivities();
const { canForms, fetchMyPermissions, myPermissions } = usePermissions();
const { templates: deptTemplates, fetchTemplates: fetchDeptTemplates } = useFormTemplates();

const activity = ref<Activity | null>(null);
const activityDepartment = ref<string | undefined>(undefined);

const formBuilderRef = ref<InstanceType<typeof FormBuilder> | null>(null);
const showBuilder = ref(false);
const editingForm = ref<SignupForm | null>(null);
const showDeleteModal = ref(false);
const activeTab = ref<'questions' | 'responses' | 'stats'>('questions');
const loadingResponses = ref(false);
const loadingStats = ref(false);
const selectedResponse = ref<FormResponse | null>(null);

// Pre-built initial form from template (auto-filled when no form exists)
const templateInitial = ref<SignupForm | null>(null);

// Draft persistence state
const hadDraftOnEntry = ref(false);
const linkCopied = ref(false);
const currentDraft = ref<SignupForm | null>(null);

const tabs = [
	{ id: 'questions' as const, label: 'Fragen' },
	{ id: 'responses' as const, label: 'Antworten' },
	{ id: 'stats' as const, label: 'Statistik' },
];

function publicFormUrl(slug: string | undefined): string {
	if (!slug) return '#'
	const baseUrl = (config.PUBLIC_BASE_URL || '').trim().replace(/\/+$/, '')
	return `${baseUrl}/forms/${slug}`
}

function canOpenPublicFormLink(slug: string | undefined): boolean {
	if (!slug) return false
	return !!(config.PUBLIC_BASE_URL || '').trim()
}

function copyPublicFormLink(slug: string | undefined) {
	if (!canOpenPublicFormLink(slug)) return;
	navigator.clipboard.writeText(publicFormUrl(slug));
	linkCopied.value = true;
	setTimeout(() => { linkCopied.value = false; }, 2000);
}

async function loadActivityFormsPage(id: string) {
	if (!id) return;
	form.value = null;
	responses.value = [];
	stats.value = null;
	error.value = null;
	showBuilder.value = false;
	editingForm.value = null;
	templateInitial.value = null;
	hadDraftOnEntry.value = false;
	currentDraft.value = null;
	activeTab.value = 'questions';
	selectedResponse.value = null;

	const permissionTask = fetchMyPermissions().catch(() => null)
	await Promise.race([
		permissionTask,
		new Promise<null>((resolve) => setTimeout(() => resolve(null), 1200)),
	])
	const act = await fetchActivity(id);
	activity.value = act;
	activityDepartment.value = act?.department ?? undefined;

	// Permission gate: redirect if no form access
	if (
		act &&
		user.value &&
		myPermissions.value &&
		!canForms(act, user.value.display_name, user.value.department)
	) {
		router.replace(`/activities/${id}`);
		return;
	}

	const existingForm = await fetchForm(id);

	// If no form exists, auto-open builder
	if (!existingForm && activityDepartment.value) {
		await fetchDeptTemplates(activityDepartment.value);

		const draft = await getFormDraft(id);
		if (draft) {
			hadDraftOnEntry.value = true;
			currentDraft.value = draft;
			templateInitial.value = draft;
		} else {
			const tpl = deptTemplates.value.find(t => t.is_default) ?? deptTemplates.value[0];
			if (tpl) {
				templateInitial.value = {
					id: '',
					activity_id: id,
					public_slug: '',
					form_type: tpl.form_type,
					title: tpl.name,
					created_by: '',
					created_at: '',
					updated_at: '',
					questions: (tpl.template_config ?? []).map((q, i) => ({
						id: `tpl-${i}`,
						form_id: '',
						created_at: '',
						question_text: q.question_text,
						question_type: q.question_type,
						position: q.position ?? i,
						is_required: q.is_required,
						metadata: q.metadata ?? {},
					})),
				};
			}
		}

		showBuilder.value = true;
	}
}

onMounted(async () => {
	await loadActivityFormsPage(activityId.value);
});

watch(
	() => route.params.id,
	async (nextId, prevId) => {
		if (nextId && nextId !== prevId) {
			await loadActivityFormsPage(String(nextId));
		}
	}
);

watch(activeTab, async (tab) => {
	if (tab === 'responses' && responses.value.length === 0) {
		loadingResponses.value = true;
		await fetchResponses(activityId.value);
		loadingResponses.value = false;
	}
	if (tab === 'stats') {
		loadingStats.value = true;
		await fetchStats(activityId.value);
		loadingStats.value = false;
	}
});

function openCreate() {
	editingForm.value = null;
	showBuilder.value = true;
}

function openEdit() {
	editingForm.value = form.value;
	showBuilder.value = true;
}

function goBack() {
	if (showBuilder.value && formBuilderRef.value) {
		formBuilderRef.value.flushAutoSave();
		if (form.value) {
			showBuilder.value = false;
			editingForm.value = null;
			return;
		}
	}
	router.push(`/activities/${activityId.value}`);
}

async function cancelBuilder() {
	// If we had a draft on entry, don't delete it; let user come back to it later
	if (!hadDraftOnEntry.value) {
		await deleteFormDraft(activityId.value);
	}
	showBuilder.value = false;
	editingForm.value = null;
	// If there's no form (auto-opened builder), go back to the activity
	if (!form.value) {
		router.push(`/activities/${activityId.value}`);
	}
}

async function onSave(payload: SignupFormInput) {
	let saved: SignupForm | null = null;
	if (editingForm.value) {
		saved = await updateForm(activityId.value, payload);
	} else {
		saved = await createForm(activityId.value, payload);
	}

	if (!saved) {
		return;
	}

	// Delete draft after successful save
	await deleteFormDraft(activityId.value);
	showBuilder.value = false;
	editingForm.value = null;
}

async function onAutoSave(payload: SignupFormInput) {
	if (editingForm.value) {
		await updateForm(activityId.value, payload);
	} else {
		// Save to draft instead of main form
		await saveFormDraft(
			activityId.value,
			payload.form_type,
			payload.title,
			payload.questions,
		);
	}
}

function confirmDelete() {
	showDeleteModal.value = true;
}

async function doDelete() {
	await deleteForm(activityId.value);
	showDeleteModal.value = false;
}

async function openResponse(id: string) {
	const r = await fetchResponse(activityId.value, id);
	selectedResponse.value = r;
}

async function removeResponse(id: string) {
	await deleteResponse(activityId.value, id);
	if (selectedResponse.value?.id === id) selectedResponse.value = null;
	if (activeTab.value === 'stats') {
		loadingStats.value = true;
		await fetchStats(activityId.value);
		loadingStats.value = false;
	}
}

function questionText(qid: string): string {
	return form.value?.questions.find((q) => q.id === qid)?.question_text ?? qid;
}

function questionTypeLabel(type: string): string {
	const m: Record<string, string> = {
		section: 'Abschnitt',
		text_input: 'Freitext',
		single_choice: 'Single Choice',
		multiple_choice: 'Multiple Choice',
		dropdown: 'Dropdown',
	};
	return m[type] ?? type;
}

function formatDate(iso: string): string {
	return new Date(iso).toLocaleString('de-CH', {
		day: '2-digit', month: '2-digit', year: 'numeric',
		hour: '2-digit', minute: '2-digit',
	});
}
</script>

<style scoped>
.forms-page { padding: 0; }

.loading-state {
	display: flex;
	align-items: center;
	gap: 0.5rem;
	color: var(--text-muted);
	padding: 2rem;
	justify-content: center;
}

.spinner {
	width: 1.1rem;
	height: 1.1rem;
	border: 2px solid var(--border);
	border-top-color: #6366f1;
	border-radius: 50%;
	animation: spin 0.7s linear infinite;
	display: inline-block;
}

@keyframes spin { to { transform: rotate(360deg); } }

.error-banner {
	background: #fee2e2;
	color: var(--error-color);
	border-radius: 0.375rem;
	padding: 0.75rem 1rem;
	font-size: 0.875rem;
}

.empty-form-state {
	text-align: center;
	padding: 3rem 1rem;
}
.empty-icon { font-size: 3rem; margin-bottom: 0.75rem; }
.empty-title { font-size: 1.1rem; font-weight: 600; color: var(--text-primary); margin: 0; }
.empty-desc { font-size: 0.875rem; color: var(--text-muted); margin: 0.25rem 0 1rem; }

.page-header {
	display: flex;
	justify-content: space-between;
	align-items: flex-start;
	margin-bottom: 1.25rem;
	gap: 1rem;
	flex-wrap: wrap;
}

.form-info {
	display: flex;
	align-items: center;
	gap: 0.75rem;
}

.page-title {
	font-size: 1.1rem;
	font-weight: 700;
	color: var(--text-primary);
	margin: 0;
}

.header-actions {
	display: flex;
	gap: 0.5rem;
	flex-wrap: wrap;
}

.mode-badge {
	font-size: 0.75rem;
	font-weight: 600;
	padding: 0.2rem 0.65rem;
	border-radius: 9999px;
	background: #d1fae5;
	color: #065f46;
}
.mode-badge[data-type="deregistration"] { background: #fee2e2; color: var(--error-color); }
.mode-badge.small { font-size: 0.7rem; padding: 0.1rem 0.5rem; }

.tabs {
	display: flex;
	border-bottom: 2px solid var(--border);
	margin-bottom: 1rem;
}

.tab-btn {
	padding: 0.5rem 1.1rem;
	background: none;
	border: none;
	border-bottom: 2px solid transparent;
	margin-bottom: -2px;
	font-size: 0.875rem;
	color: var(--text-muted);
	cursor: pointer;
	transition: all 0.15s;
}
.tab-btn.active { color: #6366f1; border-bottom-color: #6366f1; font-weight: 600; }
.tab-btn:hover:not(.active) { color: var(--text-secondary); }

.tab-content { padding: 0.25rem 0; }

.question-preview-card {
	border: 1px solid var(--border);
	border-radius: 0.375rem;
	padding: 0.65rem 1rem;
	margin-bottom: 0.5rem;
	display: flex;
	align-items: center;
	gap: 0.75rem;
}
.question-preview-card[data-type="section"] { background: #fef9c3; border-color: #fef08a; }

.qp-type {
	font-size: 0.7rem;
	font-weight: 600;
	text-transform: uppercase;
	letter-spacing: 0.04em;
	color: var(--text-muted);
	min-width: 7rem;
}

.qp-text {
	flex: 1;
	font-size: 0.9rem;
	color: var(--text-primary);
}

.qp-meta { display: flex; gap: 0.35rem; }

.badge {
	font-size: 0.68rem;
	font-weight: 600;
	padding: 0.1rem 0.45rem;
	border-radius: 9999px;
}
.badge.required { background: #fee2e2; color: var(--error-color); }
.badge.optional { background: var(--bg-hover); color: var(--text-muted); }
.badge.choices { background: #e0e7ff; color: #4338ca; }

.response-row {
	display: flex;
	justify-content: space-between;
	align-items: center;
	padding: 0.6rem 0.75rem;
	border: 1px solid var(--border);
	border-radius: 0.375rem;
	margin-bottom: 0.4rem;
}

.response-info { display: flex; gap: 0.6rem; align-items: center; }
.response-date { font-size: 0.82rem; color: var(--text-secondary); }
.response-actions { display: flex; gap: 0.4rem; }

.response-detail {
	margin-top: 1rem;
	border: 1px solid var(--border-strong);
	border-radius: 0.375rem;
	padding: 0.75rem 1rem;
	background: var(--bg-elevated);
}

.detail-header {
	display: flex;
	justify-content: space-between;
	align-items: center;
	font-weight: 600;
	font-size: 0.875rem;
	margin-bottom: 0.75rem;
}

.answer-row {
	display: flex;
	gap: 0.5rem;
	font-size: 0.85rem;
	padding: 0.25rem 0;
	border-bottom: 1px solid var(--border);
}
.answer-row:last-child { border-bottom: none; }
.answer-q { color: var(--text-muted); min-width: 10rem; }
.answer-v { color: var(--text-primary); }

.empty-section {
	text-align: center;
	color: var(--text-subtle);
	padding: 2rem;
	font-size: 0.875rem;
}

/* Buttons */
.btn-primary {
	padding: 0.5rem 1.25rem;
	background: var(--btn-primary-bg);
	color: #fff;
	border: none;
	border-radius: 0.375rem;
	font-size: 0.875rem;
	font-weight: 600;
	cursor: pointer;
}
.btn-primary:hover { background: var(--btn-primary-bg-hover); }

.btn-secondary {
	padding: 0.45rem 1rem;
	background: var(--bg-surface);
	color: var(--text-secondary);
	border: 1px solid var(--border-strong);
	border-radius: 0.375rem;
	font-size: 0.875rem;
	cursor: pointer;
}
.btn-secondary:hover { background: var(--bg-hover); }

.btn-outline {
	padding: 0.45rem 1rem;
	background: transparent;
	color: #6366f1;
	border: 1.5px solid #6366f1;
	border-radius: 0.375rem;
	font-size: 0.875rem;
	cursor: pointer;
	text-decoration: none;
	display: inline-flex;
	align-items: center;
}
.btn-outline:hover:not(:disabled) { background: #eef2ff; }
.btn-outline:disabled {
	opacity: 0.55;
	cursor: not-allowed;
}

.btn-danger {
	padding: 0.45rem 1rem;
	background: var(--bg-surface);
	color: #dc2626;
	border: 1px solid #fca5a5;
	border-radius: 0.375rem;
	font-size: 0.875rem;
	cursor: pointer;
}
.btn-danger:hover { background: #fee2e2; }

.btn-sm {
	padding: 0.2rem 0.6rem;
	background: var(--bg-surface);
	color: var(--text-secondary);
	border: 1px solid var(--border-strong);
	border-radius: 0.25rem;
	font-size: 0.78rem;
	cursor: pointer;
}
.btn-sm:hover { background: var(--bg-hover); }
.btn-sm.danger { color: #dc2626; border-color: #fca5a5; }
.btn-sm.danger:hover { background: #fee2e2; }

.icon-btn {
	padding: 0.2rem 0.45rem;
	border: 1px solid var(--border-strong);
	border-radius: 0.25rem;
	background: var(--bg-surface);
	font-size: 0.75rem;
	cursor: pointer;
}
.icon-btn:hover { background: var(--bg-hover); }

/* Modal */
.modal-overlay {
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
	border-radius: 0.5rem;
	padding: 1.5rem;
	min-width: 300px;
	max-width: 420px;
	box-shadow: 0 4px 24px rgba(0,0,0,0.15);
}

.modal-title { font-size: 1rem; font-weight: 700; color: var(--text-primary); margin: 0 0 0.5rem; }
.modal-desc { font-size: 0.875rem; color: var(--text-muted); margin: 0 0 1.25rem; }
.modal-actions { display: flex; justify-content: flex-end; gap: 0.75rem; }

</style>
