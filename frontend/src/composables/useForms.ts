import { ref } from 'vue';
import type {
	SignupForm,
	SignupFormInput,
	FormResponse,
	FormStats,
	FormTemplate,
	FormQuestionInput,
	FormType,
	FormSubmitPayload,
} from '../types';
import { apiFetch } from './useApi';

const BASE = '/api';

export function useForms() {
	const form = ref<SignupForm | null>(null);
	const responses = ref<FormResponse[]>([]);
	const stats = ref<FormStats | null>(null);
	const loading = ref(false);
	const error = ref<string | null>(null);

	// ── Admin: form CRUD ───────────────────────────────────────────────────────

	async function fetchForm(activityId: string): Promise<SignupForm | null> {
		loading.value = true;
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/form`,
			);
			if (res.status === 404) {
				form.value = null;
				return null;
			}
			if (!res.ok) throw new Error(await res.text());
			form.value = (await res.json()) as SignupForm;
			return form.value;
		} catch (e) {
			error.value = String(e);
			return null;
		} finally {
			loading.value = false;
		}
	}

	async function createForm(
		activityId: string,
		payload: SignupFormInput,
	): Promise<SignupForm | null> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/form`,
				{
					method: 'POST',
					body: JSON.stringify(payload),
				},
			);
			if (!res.ok) throw new Error(await res.text());
			form.value = (await res.json()) as SignupForm;
			return form.value;
		} catch (e) {
			error.value = String(e);
			return null;
		}
	}

	async function updateForm(
		activityId: string,
		payload: SignupFormInput,
	): Promise<SignupForm | null> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/form`,
				{
					method: 'PUT',
					body: JSON.stringify(payload),
				},
			);
			if (!res.ok) throw new Error(await res.text());
			form.value = (await res.json()) as SignupForm;
			return form.value;
		} catch (e) {
			error.value = String(e);
			return null;
		}
	}

	async function deleteForm(activityId: string): Promise<boolean> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/form`,
				{
					method: 'DELETE',
				},
			);
			if (!res.ok) throw new Error(await res.text());
			form.value = null;
			return true;
		} catch (e) {
			error.value = String(e);
			return false;
		}
	}

	// ── Admin: responses ───────────────────────────────────────────────────────

	async function fetchResponses(activityId: string): Promise<FormResponse[]> {
		loading.value = true;
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/form/responses`,
			);
			if (!res.ok) throw new Error(await res.text());
			responses.value = (await res.json()) as FormResponse[];
			return responses.value;
		} catch (e) {
			error.value = String(e);
			return [];
		} finally {
			loading.value = false;
		}
	}

	async function fetchResponse(
		activityId: string,
		responseId: string,
	): Promise<FormResponse | null> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/form/responses/${encodeURIComponent(responseId)}`,
			);
			if (res.status === 404) return null;
			if (!res.ok) throw new Error(await res.text());
			return (await res.json()) as FormResponse;
		} catch (e) {
			error.value = String(e);
			return null;
		}
	}

	async function deleteResponse(
		activityId: string,
		responseId: string,
	): Promise<boolean> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/form/responses/${encodeURIComponent(responseId)}`,
				{ method: 'DELETE' },
			);
			if (!res.ok) throw new Error(await res.text());
			responses.value = responses.value.filter((r) => r.id !== responseId);
			return true;
		} catch (e) {
			error.value = String(e);
			return false;
		}
	}

	// ── Admin: stats ──────────────────────────────────────────────────────────

	async function fetchStats(activityId: string): Promise<FormStats | null> {
		loading.value = true;
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/form/stats`,
			);
			if (!res.ok) throw new Error(await res.text());
			stats.value = (await res.json()) as FormStats;
			return stats.value;
		} catch (e) {
			error.value = String(e);
			return null;
		} finally {
			loading.value = false;
		}
	}

	// ── Public submission (no auth) ───────────────────────────────────────────

	async function fetchPublicForm(
		publicSlug: string,
	): Promise<SignupForm | null> {
		loading.value = true;
		error.value = null;
		try {
			const res = await fetch(
				`${BASE}/forms/${encodeURIComponent(publicSlug)}`,
			);
			if (res.status === 404) return null;
			if (!res.ok) throw new Error(await res.text());
			form.value = (await res.json()) as SignupForm;
			return form.value;
		} catch (e) {
			error.value = String(e);
			return null;
		} finally {
			loading.value = false;
		}
	}

	async function submitResponse(
		publicSlug: string,
		payload: FormSubmitPayload,
	): Promise<FormResponse | null> {
		loading.value = true;
		error.value = null;
		try {
			const res = await fetch(
				`${BASE}/forms/${encodeURIComponent(publicSlug)}/submit`,
				{
					method: 'POST',
					headers: { 'Content-Type': 'application/json' },
					body: JSON.stringify(payload),
				},
			);
			if (!res.ok) {
				const text = await res.text();
				throw new Error(text || `Fehler: ${res.status}`);
			}
			return (await res.json()) as FormResponse;
		} catch (e) {
			error.value = String(e);
			return null;
		} finally {
			loading.value = false;
		}
	}

	// ── Form drafts ───────────────────────────────────────────────────────────

	async function getFormDraft(activityId: string): Promise<SignupForm | null> {
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/form-draft`,
			);
			if (res.status === 404) return null;
			if (!res.ok) return null;
			const data = (await res.json()) as any;
			return {
				id: data.id,
				activity_id: data.activity_id,
				public_slug: '',
				form_type: data.form_type,
				title: data.title,
				created_by: data.updated_by,
				created_at: data.updated_at,
				updated_at: data.updated_at,
				questions: data.questions || [],
			} as SignupForm;
		} catch {
			return null;
		}
	}

	async function saveFormDraft(
		activityId: string,
		formType: FormType,
		title: string,
		questions: FormQuestionInput[],
	): Promise<SignupForm | null> {
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/form-draft`,
				{
					method: 'PUT',
					body: JSON.stringify({
						form_type: formType,
						title,
						questions,
					}),
				},
			);
			if (!res.ok) return null;
			const data = (await res.json()) as any;
			return {
				id: data.id,
				activity_id: data.activity_id,
				public_slug: '',
				form_type: data.form_type,
				title: data.title,
				created_by: data.updated_by,
				created_at: data.updated_at,
				updated_at: data.updated_at,
				questions: data.questions || [],
			} as SignupForm;
		} catch {
			return null;
		}
	}

	async function deleteFormDraft(activityId: string): Promise<void> {
		try {
			await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/form-draft`,
				{ method: 'DELETE' },
			);
		} catch {
			// ignore
		}
	}

	return {
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
		fetchPublicForm,
		submitResponse,
		getFormDraft,
		saveFormDraft,
		deleteFormDraft,
	};
}

// ── Form templates ─────────────────────────────────────────────────────────────

export function useFormTemplates() {
	const templates = ref<FormTemplate[]>([]);
	const loading = ref(false);
	const error = ref<string | null>(null);

	async function fetchTemplates(department: string): Promise<FormTemplate[]> {
		loading.value = true;
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/form-templates?department=${encodeURIComponent(department)}`,
			);
			if (!res.ok) throw new Error(await res.text());
			templates.value = (await res.json()) as FormTemplate[];
			return templates.value;
		} catch (e) {
			error.value = String(e);
			return [];
		} finally {
			loading.value = false;
		}
	}

	async function createTemplate(
		name: string,
		department: string,
		form_type: FormType,
		template_config: FormQuestionInput[],
		is_default = false,
	): Promise<FormTemplate | null> {
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/form-templates`, {
				method: 'POST',
				body: JSON.stringify({
					name,
					department,
					form_type,
					template_config,
					is_default,
				}),
			});
			if (!res.ok) throw new Error(await res.text());
			const tpl = (await res.json()) as FormTemplate;
			if (is_default) {
				templates.value.forEach((t) => {
					if (t.id !== tpl.id) t.is_default = false;
				});
			}
			templates.value.push(tpl);
			return tpl;
		} catch (e) {
			error.value = String(e);
			return null;
		}
	}

	async function updateTemplate(
		id: string,
		name: string,
		form_type: FormType,
		template_config: FormQuestionInput[],
		is_default = false,
	): Promise<FormTemplate | null> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/form-templates/${encodeURIComponent(id)}`,
				{
					method: 'PUT',
					body: JSON.stringify({
						name,
						form_type,
						template_config,
						is_default,
					}),
				},
			);
			if (!res.ok) throw new Error(await res.text());
			const tpl = (await res.json()) as FormTemplate;
			if (is_default) {
				templates.value.forEach((t) => {
					if (t.id !== id) t.is_default = false;
				});
			}
			const idx = templates.value.findIndex((t) => t.id === id);
			if (idx !== -1) templates.value[idx] = tpl;
			return tpl;
		} catch (e) {
			error.value = String(e);
			return null;
		}
	}

	async function deleteTemplate(id: string): Promise<boolean> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/form-templates/${encodeURIComponent(id)}`,
				{
					method: 'DELETE',
				},
			);
			if (!res.ok) throw new Error(await res.text());
			templates.value = templates.value.filter((t) => t.id !== id);
			return true;
		} catch (e) {
			error.value = String(e);
			return false;
		}
	}

	return {
		templates,
		loading,
		error,
		fetchTemplates,
		createTemplate,
		updateTemplate,
		deleteTemplate,
	};
}
