import { ref } from 'vue';
import type {
	Activity,
	ActivityInput,
	Attachment,
	Department,
	WsEvent,
} from '../types';
import { getIdToken } from './useAuth';
import { useWebSocket } from './useWebSocket';

const BASE = '/api';

// ---- Shared state (module scope) -------------------------------------------
const activities = ref<Activity[]>([]);
const lastUpdatedActivity = ref<Activity | null>(null);
const departments = ref<Department[]>([]);
const predefinedLocations = ref<string[]>([]);

async function apiFetch(
	url: string,
	options: RequestInit = {},
): Promise<Response> {
	const token = await getIdToken();
	return fetch(url, {
		...options,
		headers: {
			...((options.headers as Record<string, string>) ?? {}),
			Authorization: `Bearer ${token}`,
			...(options.body && !(options.body instanceof FormData)
				? { 'Content-Type': 'application/json' }
				: {}),
		},
	});
}

export function useActivities() {
	const loading = ref(false);
	const error = ref<string | null>(null);

	function handleWsEvent(event: WsEvent) {
		if (event.event === 'created') {
			if (!activities.value.find((a) => a.id === event.activity.id))
				activities.value.unshift(event.activity);
		} else if (event.event === 'updated') {
			lastUpdatedActivity.value = event.activity;
			const idx = activities.value.findIndex((a) => a.id === event.activity.id);
			if (idx !== -1) activities.value[idx] = event.activity;
		} else if (event.event === 'deleted') {
			activities.value = activities.value.filter((a) => a.id !== event.id);
		}
	}

	const { connected } = useWebSocket(handleWsEvent);

	// ---- REST ----------------------------------------------------------------

	async function fetchActivities() {
		loading.value = true;
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/activities`);
			if (!res.ok) throw new Error(await res.text());
			activities.value = (await res.json()) as Activity[];
		} catch (e) {
			error.value = String(e);
		} finally {
			loading.value = false;
		}
	}

	async function fetchActivity(id: string): Promise<Activity | null> {
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/activities/${id}`);
			if (res.status === 404) return null;
			if (!res.ok) throw new Error(await res.text());
			return (await res.json()) as Activity;
		} catch (e) {
			error.value = String(e);
			return null;
		}
	}

	async function fetchDepartments(): Promise<void> {
		try {
			const res = await apiFetch(`${BASE}/departments`);
			if (res.ok) {
				const data = await res.json();
				departments.value = data.map((d: any) => d.name ?? d) as Department[];
			}
		} catch {
			/* non-critical */
		}
	}

	async function createActivity(input: ActivityInput): Promise<string | null> {
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/activities`, {
				method: 'POST',
				body: JSON.stringify(input),
			});
			if (!res.ok) throw new Error(await res.text());
			const created = await res.json();
			return created.id ?? null;
		} catch (e) {
			error.value = String(e);
			return null;
		}
	}

	async function updateActivity(
		id: string,
		input: ActivityInput,
	): Promise<void> {
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/activities/${id}`, {
				method: 'PATCH',
				body: JSON.stringify(input),
			});
			if (!res.ok) throw new Error(await res.text());
		} catch (e) {
			error.value = String(e);
		}
	}

	async function deleteActivity(id: string): Promise<void> {
		activities.value = activities.value.filter((a) => a.id !== id);
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/activities/${id}`, {
				method: 'DELETE',
			});
			if (!res.ok) throw new Error(await res.text());
		} catch (e) {
			error.value = String(e);
			await fetchActivities();
		}
	}

	// ---- Predefined locations ------------------------------------------------

	async function fetchLocations(): Promise<void> {
		try {
			const res = await apiFetch(`${BASE}/locations`);
			if (res.ok) predefinedLocations.value = (await res.json()) as string[];
		} catch {
			/* non-critical */
		}
	}

	// ---- Attachments ---------------------------------------------------------

	async function fetchAttachments(activityId: string): Promise<Attachment[]> {
		try {
			const res = await apiFetch(
				`${BASE}/activities/${activityId}/attachments`,
			);
			if (!res.ok) return [];
			return (await res.json()) as Attachment[];
		} catch {
			return [];
		}
	}

	async function uploadAttachment(
		activityId: string,
		file: File,
	): Promise<Attachment | null> {
		try {
			const buf = await file.arrayBuffer();
			const bytes = new Uint8Array(buf);
			let binary = '';
			for (let i = 0; i < bytes.length; i++)
				binary += String.fromCharCode(bytes[i]);
			const data = btoa(binary);
			const res = await apiFetch(
				`${BASE}/activities/${activityId}/attachments`,
				{
					method: 'POST',
					body: JSON.stringify({
						filename: file.name,
						content_type: file.type || 'application/octet-stream',
						data,
					}),
				},
			);
			if (!res.ok) return null;
			return (await res.json()) as Attachment;
		} catch {
			return null;
		}
	}

	async function deleteAttachment(attachmentId: string): Promise<boolean> {
		try {
			const res = await apiFetch(`${BASE}/attachments/${attachmentId}`, {
				method: 'DELETE',
			});
			return res.ok;
		} catch {
			return false;
		}
	}

	async function getAttachmentBlobUrl(
		attachmentId: string,
	): Promise<string | null> {
		try {
			const res = await apiFetch(
				`${BASE}/attachments/${attachmentId}/download`,
			);
			if (!res.ok) return null;
			const blob = await res.blob();
			return URL.createObjectURL(blob);
		} catch {
			return null;
		}
	}

	return {
		activities,
		loading,
		error,
		connected,
		lastUpdatedActivity,
		departments,
		predefinedLocations,
		fetchActivities,
		fetchActivity,
		fetchDepartments,
		fetchLocations,
		createActivity,
		updateActivity,
		deleteActivity,
		fetchAttachments,
		uploadAttachment,
		deleteAttachment,
		getAttachmentBlobUrl,
	};
}
