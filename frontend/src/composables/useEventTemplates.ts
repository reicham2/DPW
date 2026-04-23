import { ref } from 'vue';
import type { EventTemplate, EventPublication, Department } from '../types';
import { apiFetch, formatApiError } from './useApi';

const BASE = '/api';

export function useEventTemplates() {
	const templates = ref<EventTemplate[]>([]);
	const loading = ref(false);
	const error = ref<string | null>(null);

	async function fetchTemplates(): Promise<void> {
		loading.value = true;
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/event-templates`);
			if (!res.ok) throw new Error(await res.text());
			templates.value = (await res.json()) as EventTemplate[];
		} catch (e) {
			error.value = formatApiError(e);
		} finally {
			loading.value = false;
		}
	}

	async function fetchTemplate(
		department: Department,
	): Promise<EventTemplate | null> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/event-templates/${encodeURIComponent(department)}`,
			);
			if (res.status === 404) return null;
			if (!res.ok) throw new Error(await res.text());
			return (await res.json()) as EventTemplate;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function saveTemplate(
		department: Department,
		title: string,
		body: string,
	): Promise<EventTemplate | null> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/event-templates/${encodeURIComponent(department)}`,
				{
					method: 'PUT',
					body: JSON.stringify({ title, body }),
				},
			);
			if (!res.ok) throw new Error(await res.text());
			return (await res.json()) as EventTemplate;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function fetchPublication(
		activityId: string,
	): Promise<EventPublication | null> {
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/event-publication`,
			);
			if (res.status === 404) return null;
			if (!res.ok) return null;
			return (await res.json()) as EventPublication;
		} catch {
			return null;
		}
	}

	async function publishEvent(
		activityId: string,
		title: string,
		bodyHtml: string,
	): Promise<EventPublication | null> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/event-publication`,
				{
					method: 'PUT',
					body: JSON.stringify({ title, body_html: bodyHtml }),
				},
			);
			if (!res.ok) throw new Error(await res.text());
			return (await res.json()) as EventPublication;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function unpublishEvent(activityId: string): Promise<void> {
		try {
			await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/event-publication`,
				{ method: 'DELETE' },
			);
		} catch {
			// ignore
		}
	}

	return {
		templates,
		loading,
		error,
		fetchTemplates,
		fetchTemplate,
		saveTemplate,
		fetchPublication,
		publishEvent,
		unpublishEvent,
	};
}
