<script setup lang="ts">
import { ref, computed, onMounted, watch, nextTick } from 'vue';
import { useFormTemplates } from '../composables/useForms';
import { usePermissions } from '../composables/usePermissions';
import { user } from '../composables/useAuth';
import DepartmentBadge from '../components/DepartmentBadge.vue';
import FormBuilder from '../components/FormBuilder.vue';
import ErrorAlert from '../components/ErrorAlert.vue';
import TemplateVarsDropdown from '../components/TemplateVarsDropdown.vue';
import { Save, Check } from 'lucide-vue-next';
import type { Department, FormTemplate, SignupForm, SignupFormInput } from '../types';

const { departments: deptRecords, fetchDepartments, myPermissions, fetchMyPermissions } = usePermissions();
const { templates, loading, error, fetchTemplates, createTemplate, updateTemplate, deleteTemplate } = useFormTemplates();



const ALL_DEPARTMENTS = computed<Department[]>(() => deptRecords.value.map((d) => d.name));

const canEditAll = computed(() => myPermissions.value?.form_templates_scope === 'all');
const canEditOwnDept = computed(() => {
	const s = myPermissions.value?.form_templates_scope;
	return s === 'own_dept' || s === 'all';
});

const visibleDepartments = computed<Department[]>(() => {
	if (canEditAll.value) return ALL_DEPARTMENTS.value;
	if (canEditOwnDept.value) {
		const own = user.value?.department as Department | undefined;
		return own ? ALL_DEPARTMENTS.value.filter((d) => d === own) : [];
	}
	return [];
});

const activeDept = ref<Department>((user.value?.department as Department) ?? ('' as Department));

const formBuilderRef = ref<InstanceType<typeof FormBuilder> | null>(null);
const showBuilder = ref(false);
const editingTemplate = ref<FormTemplate | null>(null);
const editingIsDefault = ref(false);
const editingDraftOverride = ref<SignupFormInput | null>(null);

interface LocalFormTemplateDraft {
	version: 1;
	templateId: string;
	savedAt: number;
	isDefault: boolean;
	payload: SignupFormInput;
}

const pendingLocalDraft = ref<LocalFormTemplateDraft | null>(null);
const localDraftRestoredAt = ref<number | null>(null);

function localDraftKey(templateId: string): string {
	return `dpw:form-template-draft:${templateId}`;
}

function templateToPayload(tpl: FormTemplate): SignupFormInput {
	return {
		title: tpl.name,
		form_type: tpl.form_type,
		questions: tpl.template_config,
	};
}

function clearLocalDraft(templateId: string) {
	try {
		window.localStorage.removeItem(localDraftKey(templateId));
	} catch {
		/* ignore unavailable storage */
	}
}

function saveLocalDraft(templateId: string, payload: SignupFormInput, isDefault: boolean) {
	const draft: LocalFormTemplateDraft = {
		version: 1,
		templateId,
		savedAt: Date.now(),
		isDefault,
		payload,
	};
	try {
		window.localStorage.setItem(localDraftKey(templateId), JSON.stringify(draft));
	} catch {
		/* ignore unavailable storage */
	}
}

function loadPendingLocalDraft(tpl: FormTemplate) {
	try {
		const raw = window.localStorage.getItem(localDraftKey(tpl.id));
		if (!raw) {
			pendingLocalDraft.value = null;
			return;
		}
		const parsed = JSON.parse(raw) as Partial<LocalFormTemplateDraft>;
		if (parsed.version !== 1 || parsed.templateId !== tpl.id || !parsed.payload) {
			pendingLocalDraft.value = null;
			return;
		}
		const draft = parsed as LocalFormTemplateDraft;
		if (JSON.stringify(draft.payload) === JSON.stringify(templateToPayload(tpl)) && draft.isDefault === tpl.is_default) {
			clearLocalDraft(tpl.id);
			pendingLocalDraft.value = null;
			return;
		}
		pendingLocalDraft.value = draft;
	} catch {
		pendingLocalDraft.value = null;
	}
}

function applyLocalDraft() {
	if (!pendingLocalDraft.value) return;
	editingDraftOverride.value = pendingLocalDraft.value.payload;
	editingIsDefault.value = pendingLocalDraft.value.isDefault;
	pendingLocalDraft.value = null;
	localDraftRestoredAt.value = Date.now();
	nextTick(() => {
		formBuilderRef.value?.flushAutoSave?.();
	});
}

function discardLocalDraft() {
	if (editingTemplate.value) clearLocalDraft(editingTemplate.value.id);
	pendingLocalDraft.value = null;
	localDraftRestoredAt.value = null;
}

onMounted(async () => {
	await Promise.all([fetchDepartments(), fetchMyPermissions()]);
	const vis = visibleDepartments.value;
	if (vis.length && !vis.includes(activeDept.value)) activeDept.value = vis[0];
	if (activeDept.value) await fetchTemplates(activeDept.value);
});

watch(activeDept, async (d) => {
	if (d) await fetchTemplates(d);
	closeBuilder();
});

function openCreate() {
	editingTemplate.value = null;
	editingIsDefault.value = false;
	editingDraftOverride.value = null;
	pendingLocalDraft.value = null;
	localDraftRestoredAt.value = null;
	showBuilder.value = true;
}

function openEdit(tpl: FormTemplate) {
	editingTemplate.value = tpl;
	editingIsDefault.value = tpl.is_default;
	editingDraftOverride.value = null;
	localDraftRestoredAt.value = null;
	loadPendingLocalDraft(tpl);
	showBuilder.value = true;
}

function closeBuilder() {
	showBuilder.value = false;
	editingTemplate.value = null;
	editingIsDefault.value = false;
	editingDraftOverride.value = null;
	pendingLocalDraft.value = null;
	localDraftRestoredAt.value = null;
}

// Synthesize a SignupForm-shaped object so FormBuilder can render it.
// FormBuilder only reads { title, form_type, questions[*].{question_text,…,metadata} }.
const builderInitial = computed<SignupForm | null>(() => {
	const tpl = editingTemplate.value;
	if (!tpl) return null;
	const source = editingDraftOverride.value ?? templateToPayload(tpl);
	return {
		id: tpl.id,
		activity_id: '',
		public_slug: '',
		form_type: source.form_type,
		title: source.title,
		created_by: tpl.created_by,
		created_at: tpl.created_at,
		updated_at: tpl.updated_at,
		questions: source.questions.map((q, i) => ({
			id: `tpl-${i}`,
			form_id: tpl.id,
			created_at: tpl.created_at,
			question_text: q.question_text,
			question_type: q.question_type,
			position: q.position ?? i,
			is_required: q.is_required,
			metadata: q.metadata ?? {},
		})),
	};
});

async function onSave(payload: SignupFormInput) {
	const name = payload.title.trim();
	if (!name) return;
	if (editingTemplate.value) {
		saveLocalDraft(editingTemplate.value.id, payload, editingIsDefault.value);
		await updateTemplate(editingTemplate.value.id, name, payload.form_type, payload.questions, editingIsDefault.value);
		if (!error.value) {
			clearLocalDraft(editingTemplate.value.id);
		}
	} else {
		await createTemplate(name, activeDept.value, payload.form_type, payload.questions, editingIsDefault.value);
	}
	if (editingIsDefault.value) {
		await fetchTemplates(activeDept.value);
	}
	closeBuilder();
}

async function onAutoSave(payload: SignupFormInput) {
	const name = payload.title.trim();
	if (!name || !editingTemplate.value) return;
	saveLocalDraft(editingTemplate.value.id, payload, editingIsDefault.value);
	await updateTemplate(editingTemplate.value.id, name, payload.form_type, payload.questions, editingIsDefault.value);
	if (!error.value) {
		clearLocalDraft(editingTemplate.value.id);
		pendingLocalDraft.value = null;
		localDraftRestoredAt.value = null;
	}
}

async function toggleDefault(tpl: FormTemplate) {
	const newDefault = !tpl.is_default;
	await updateTemplate(tpl.id, tpl.name, tpl.form_type, tpl.template_config, newDefault);
	await fetchTemplates(activeDept.value);
}

async function onDelete(tpl: FormTemplate) {
	if (!confirm(`Vorlage „${tpl.name}" wirklich löschen?`)) return;
	await deleteTemplate(tpl.id);
}

function typeLabel(t: string): string {
	return t === 'registration' ? 'Anmeldung' : 'Abmeldung';
}
</script>

<template>
	<header class="header">
		<h1>Formular-Vorlagen</h1>
	</header>

	<main class="main">
		<!-- Department tabs -->
		<div class="filter-tabs" style="margin-bottom: 24px">
			<button
				v-for="dept in visibleDepartments"
				:key="dept"
				class="filter-tab filter-tab--badge"
				@click="activeDept = dept"
			>
				<DepartmentBadge :department="dept" :active="activeDept === dept" />
			</button>
		</div>

		<ErrorAlert :error="error" />
		<p v-if="loading && !showBuilder" class="loading">Laden…</p>

		<!-- Builder -->
		<template v-if="showBuilder">
			<div class="ft-page-header">
				<h2 class="ft-title">{{ editingTemplate ? 'Vorlage bearbeiten' : 'Neue Vorlage' }}</h2>
			</div>
			<div v-if="pendingLocalDraft" class="editors-banner" style="margin-bottom: 12px; gap: 10px; flex-wrap: wrap;">
				<span class="editors-banner-icon"><Save :size="16" aria-hidden="true" /></span>
				<span>Ungespeicherter Entwurf gefunden ({{ new Date(pendingLocalDraft.savedAt).toLocaleString('de-DE') }}).</span>
				<button type="button" class="btn-sm" @click="applyLocalDraft">Wiederherstellen</button>
				<button type="button" class="btn-sm" @click="discardLocalDraft">Verwerfen</button>
			</div>
			<div v-else-if="localDraftRestoredAt" class="editors-banner" style="margin-bottom: 12px;">
				<span class="editors-banner-icon"><Check :size="16" aria-hidden="true" /></span>
				<span>Lokaler Entwurf wurde wiederhergestellt.</span>
			</div>
			<div class="default-toggle-section">
				<label class="checkbox-label">
					<input v-model="editingIsDefault" type="checkbox" />
					<span>Als Standard-Vorlage für diese Stufe setzen</span>
				</label>
				<p class="default-hint">Die Standard-Vorlage wird beim Erstellen eines Formulars automatisch ausgewählt.</p>
			</div>
			<FormBuilder
				ref="formBuilderRef"
				:initial="builderInitial"
				:is-edit="!!editingTemplate"
				title-label="Name der Vorlage"
				title-placeholder="z. B. Standard-Anmeldung Pio"
				@save="onSave"
				@autosave="onAutoSave"
				@cancel="closeBuilder"
			/>
			<div style="margin-top: 24px">
				<TemplateVarsDropdown />
			</div>
		</template>

		<!-- List -->
		<template v-else>
			<div class="ft-page-header">
				<h2 class="ft-title">Vorlagen für {{ activeDept || '—' }}</h2>
				<button v-if="activeDept" class="btn-primary" @click="openCreate">+ Neue Vorlage</button>
			</div>

			<div v-if="!loading && templates.length === 0" class="empty-state">
				Noch keine Vorlagen in dieser Stufe.
			</div>

			<div
				v-for="tpl in templates"
				:key="tpl.id"
				class="ft-row"
			>
				<div class="ft-row-main">
					<div class="ft-row-name">
						{{ tpl.name }}
						<span v-if="tpl.is_default" class="default-badge">Standard</span>
					</div>
					<div class="ft-row-meta">
						<span class="ft-mode" :data-type="tpl.form_type">{{ typeLabel(tpl.form_type) }}</span>
						<span class="ft-count">{{ tpl.template_config.length }} Frage(n)</span>
					</div>
				</div>
				<div class="ft-row-actions">
					<button
						:class="['btn-sm', tpl.is_default ? 'active-default' : '']"
						@click="toggleDefault(tpl)"
						:title="tpl.is_default ? 'Standard entfernen' : 'Als Standard setzen'"
					>
						{{ tpl.is_default ? '⭐ Standard' : '☆ Standard' }}
					</button>
					<button class="btn-sm" @click="openEdit(tpl)">Bearbeiten</button>
					<button class="btn-sm danger" @click="onDelete(tpl)">Löschen</button>
				</div>
			</div>
		</template>
	</main>
</template>

<style scoped>
.ft-page-header {
	display: flex;
	justify-content: space-between;
	align-items: center;
	margin-bottom: 1rem;
	gap: 1rem;
	flex-wrap: wrap;
}
.ft-title {
	font-size: 1.05rem;
	font-weight: 700;
	color: #111827;
	margin: 0;
}
.empty-state {
	text-align: center;
	color: #9ca3af;
	padding: 2rem;
	font-size: 0.875rem;
	border: 1px dashed #e5e7eb;
	border-radius: 0.5rem;
}

.editors-banner {
	display: flex;
	align-items: center;
	padding: 0.55rem 0.75rem;
	border: 1px solid #d1d5db;
	border-radius: 0.5rem;
	background: #f9fafb;
	font-size: 0.86rem;
	color: #374151;
	gap: 0.5rem;
}

.editors-banner-icon {
	font-size: 0.95rem;
}

.ft-row {
	display: flex;
	justify-content: space-between;
	align-items: center;
	padding: 0.75rem 1rem;
	border: 1px solid #e5e7eb;
	border-radius: 0.5rem;
	margin-bottom: 0.5rem;
	background: #fff;
	gap: 1rem;
}
.ft-row-main { flex: 1; min-width: 0; }
.ft-row-name { font-weight: 600; color: #111827; font-size: 0.95rem; }
.ft-row-meta { display: flex; gap: 0.5rem; margin-top: 0.25rem; font-size: 0.78rem; color: #6b7280; align-items: center; }
.ft-mode {
	font-size: 0.72rem;
	font-weight: 600;
	padding: 0.15rem 0.55rem;
	border-radius: 9999px;
	background: #d1fae5;
	color: #065f46;
}
.ft-mode[data-type="deregistration"] { background: #fee2e2; color: #991b1b; }
.ft-count { font-size: 0.75rem; color: #6b7280; }

.ft-row-actions { display: flex; gap: 0.4rem; flex-shrink: 0; }

.btn-primary {
	padding: 0.5rem 1rem;
	background: #6366f1;
	color: #fff;
	border: none;
	border-radius: 0.375rem;
	font-size: 0.85rem;
	font-weight: 600;
	cursor: pointer;
}
.btn-primary:hover { background: #4f46e5; }

.btn-sm {
	padding: 0.25rem 0.65rem;
	background: #fff;
	color: #374151;
	border: 1px solid #d1d5db;
	border-radius: 0.25rem;
	font-size: 0.78rem;
	cursor: pointer;
}
.btn-sm:hover { background: #f9fafb; }
.btn-sm.danger { color: #dc2626; border-color: #fca5a5; }
.btn-sm.danger:hover { background: #fee2e2; }

.default-badge {
	display: inline-block;
	font-size: 0.7rem;
	font-weight: 600;
	background: #fef3c7;
	color: #92400e;
	padding: 0.1rem 0.4rem;
	border-radius: 0.25rem;
	margin-left: 0.4rem;
	vertical-align: middle;
}

.active-default {
	background: #fef3c7 !important;
	border-color: #f59e0b !important;
	color: #92400e !important;
}

.default-toggle-section {
	background: #f9fafb;
	border: 1px solid #e5e7eb;
	border-radius: 0.5rem;
	padding: 0.75rem 1rem;
	margin-bottom: 1rem;
}

.checkbox-label {
	display: flex;
	align-items: center;
	gap: 0.5rem;
	font-size: 0.875rem;
	font-weight: 500;
	color: #374151;
	cursor: pointer;
}
.checkbox-label input[type="checkbox"] {
	width: 1rem;
	height: 1rem;
	accent-color: #6366f1;
}

.default-hint {
	margin: 0.25rem 0 0 1.5rem;
	font-size: 0.75rem;
	color: #9ca3af;
}

@media (max-width: 599px) {
	.filter-tabs {
		overflow-x: auto;
		-webkit-overflow-scrolling: touch;
		scrollbar-width: none;
		padding-bottom: 2px;
	}
	.filter-tabs::-webkit-scrollbar {
		display: none;
	}
	.filter-tab {
		flex-shrink: 0;
	}
	.ft-page-header {
		align-items: stretch;
		gap: 0.7rem;
	}
	.ft-title {
		font-size: 0.98rem;
		line-height: 1.4;
	}
	.btn-primary {
		width: 100%;
		min-height: 42px;
	}
	.editors-banner {
		align-items: flex-start;
		font-size: 0.8rem;
	}
	.default-toggle-section {
		padding: 0.7rem 0.75rem;
	}
	.checkbox-label {
		align-items: flex-start;
		font-size: 0.83rem;
	}
	.default-hint {
		margin-left: 1.35rem;
		font-size: 0.72rem;
	}
	.ft-row {
		flex-direction: column;
		align-items: stretch;
		gap: 0.65rem;
		padding: 0.7rem 0.75rem;
	}
	.ft-row-name {
		font-size: 0.9rem;
		overflow-wrap: anywhere;
	}
	.ft-row-meta {
		flex-wrap: wrap;
		row-gap: 0.35rem;
	}
	.ft-row-actions {
		width: 100%;
		display: grid;
		grid-template-columns: repeat(3, minmax(0, 1fr));
		gap: 0.4rem;
	}
	.btn-sm {
		min-height: 40px;
		font-size: 0.75rem;
	}
}


</style>
