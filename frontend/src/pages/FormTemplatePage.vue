<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue';
import { useFormTemplates } from '../composables/useForms';
import { usePermissions } from '../composables/usePermissions';
import { user } from '../composables/useAuth';
import DepartmentBadge from '../components/DepartmentBadge.vue';
import FormBuilder from '../components/FormBuilder.vue';
import ErrorAlert from '../components/ErrorAlert.vue';
import TemplateVarsDropdown from '../components/TemplateVarsDropdown.vue';
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
	showBuilder.value = true;
}

function openEdit(tpl: FormTemplate) {
	editingTemplate.value = tpl;
	editingIsDefault.value = tpl.is_default;
	showBuilder.value = true;
}

function closeBuilder() {
	showBuilder.value = false;
	editingTemplate.value = null;
	editingIsDefault.value = false;
}

// Synthesize a SignupForm-shaped object so FormBuilder can render it.
// FormBuilder only reads { title, form_type, questions[*].{question_text,…,metadata} }.
const builderInitial = computed<SignupForm | null>(() => {
	const tpl = editingTemplate.value;
	if (!tpl) return null;
	return {
		id: tpl.id,
		activity_id: '',
		public_slug: '',
		form_type: tpl.form_type,
		title: tpl.name,
		created_by: tpl.created_by,
		created_at: tpl.created_at,
		updated_at: tpl.updated_at,
		questions: tpl.template_config.map((q, i) => ({
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
		await updateTemplate(editingTemplate.value.id, name, payload.form_type, payload.questions, editingIsDefault.value);
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
	await updateTemplate(editingTemplate.value.id, name, payload.form_type, payload.questions, editingIsDefault.value);
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


</style>
