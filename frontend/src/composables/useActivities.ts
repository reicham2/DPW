import { ref } from 'vue';
import type {
	Activity,
	ActivityInput,
	Attachment,
	DeletedActivityRecord,
	Department,
	WsEvent,
} from '../types';
import { useWebSocket } from './useWebSocket';
import { apiFetch, formatApiError } from './useApi';
import { getGraphAccessToken } from './useAuth';
import { validateProgramImageFile } from '../utils/programImages';

const BASE = '/api';

function guestEditFetch(path: string, token: string, options: RequestInit = {}): Promise<Response> {
	const headers = new Headers(options.headers);
	headers.set('X-Guest-Edit-Token', token);
	if (options.body && !(options.body instanceof FormData)) {
		headers.set('Content-Type', 'application/json');
	}
	return fetch(`${BASE}/guest-edit${path}`, {
		...options,
		headers,
		cache: 'no-store',
	});
}

// ---- Shared state (module scope) -------------------------------------------
const activities = ref<Activity[]>([]);
const lastUpdatedActivity = ref<Activity | null>(null);
const departments = ref<Department[]>([]);
const predefinedLocations = ref<string[]>([]);
const predefinedMaterials = ref<string[]>([]);

interface CreatedActivity {
	id: string;
	guiding_thread: unknown;
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
		} else if (event.event === 'department_deleted') {
			departments.value = departments.value.filter((d) => d !== event.name);
			void fetchActivities();
		} else if (event.event === 'role_deleted') {
			void fetchActivities();
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
			error.value = formatApiError(e);
		} finally {
			loading.value = false;
		}
	}

	async function fetchActivity(id: string): Promise<Activity | null> {
		error.value = null;
		const url = `${BASE}/activities/${id}`;

		async function requestOnce(timeoutMs: number): Promise<Activity | null> {
			const controller = new AbortController();
			const timeout = setTimeout(() => controller.abort(), timeoutMs);
			try {
				const res = await apiFetch(url, { signal: controller.signal });
				if (res.status === 404) return null;
				if (!res.ok) throw new Error(await res.text());
				return (await res.json()) as Activity;
			} finally {
				clearTimeout(timeout);
			}
		}

		try {
			return await requestOnce(2500);
		} catch (e) {
			const isAbort =
				e instanceof Error &&
				(e.name === 'AbortError' || e.message.toLowerCase().includes('abort'));

			if (isAbort) {
				try {
					return await requestOnce(2500);
				} catch (retryError) {
					error.value = formatApiError(retryError);
					return null;
				}
			}

			error.value = formatApiError(e);
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

	async function createActivity(input: ActivityInput): Promise<CreatedActivity | null> {
		error.value = null;
		try {
			let notificationAccessToken = '';
			try {
				notificationAccessToken = await getGraphAccessToken();
			} catch {
				// Email notifications stay disabled when token consent is missing.
			}
			const res = await apiFetch(`${BASE}/activities`, {
				method: 'POST',
				body: JSON.stringify({
					...input,
					notification_access_token: notificationAccessToken || undefined,
				}),
			});
			if (!res.ok) throw new Error(await res.text());
			const created: unknown = await res.json();
			if (!created || typeof created !== 'object' || !('id' in created) ||
				typeof created.id !== 'string' || !created.id) {
				throw new Error('Die Antwort auf das Erstellen der Aktivität enthält keine ID.');
			}
			const guidingThread = 'guiding_thread' in created ? created.guiding_thread : undefined;
			return { id: created.id, guiding_thread: guidingThread };
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function updateActivity(
		id: string,
		input: ActivityInput,
	): Promise<boolean> {
		error.value = null;
		try {
			let notificationAccessToken = '';
			try {
				notificationAccessToken = await getGraphAccessToken();
			} catch {
				// Email notifications stay disabled when token consent is missing.
			}
			const res = await apiFetch(`${BASE}/activities/${id}`, {
				method: 'PATCH',
				body: JSON.stringify({
					...input,
					notification_access_token: notificationAccessToken || undefined,
				}),
			});
			if (!res.ok) throw new Error(await res.text());
			if (input.guiding_thread !== undefined) {
				const updated: unknown = await res.json();
				const expectedGuidingThread = input.guiding_thread === '' ? null : input.guiding_thread;
				if (!updated || typeof updated !== 'object' ||
					!('guiding_thread' in updated) || updated.guiding_thread !== expectedGuidingThread) {
					throw new Error('Das Backend hat den roten Faden nicht bestätigt. Bitte den Text kopieren und nach dem Backend-Update erneut speichern.');
				}
			}
			return true;
		} catch (e) {
			error.value = formatApiError(e);
			return false;
		}
	}

	async function updateGuestActivity(
		input: ActivityInput,
		token: string,
		expectedUpdatedAt: string,
	): Promise<Activity | null> {
		error.value = null;
		try {
			const res = await guestEditFetch('/activity', token, {
				method: 'PATCH',
				body: JSON.stringify({ ...input, updated_at: expectedUpdatedAt }),
			});
			if (!res.ok) {
				const payload = await res.json().catch(() => null) as { error?: unknown } | null;
				throw new Error(typeof payload?.error === 'string' ? payload.error : 'Änderungen konnten nicht gespeichert werden.');
			}
			return await res.json() as Activity;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
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
			error.value = formatApiError(e);
			await fetchActivities();
		}
	}

	async function fetchDeletedActivities(): Promise<DeletedActivityRecord[]> {
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/admin/activities/trash`);
			if (!res.ok) throw new Error(await res.text());
			return (await res.json()) as DeletedActivityRecord[];
		} catch (e) {
			error.value = formatApiError(e);
			return [];
		}
	}

	async function restoreActivity(id: string): Promise<Activity | null> {
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/admin/activities/${id}/restore`, {
				method: 'POST',
			});
			if (!res.ok) throw new Error(await res.text());
			const restored = (await res.json()) as Activity;
			if (!activities.value.find((a) => a.id === restored.id)) {
				activities.value.unshift(restored);
			}
			return restored;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function deleteDeletedActivity(id: string): Promise<boolean> {
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/admin/activities/${id}/trash`, {
				method: 'DELETE',
			});
			if (!res.ok) throw new Error(await res.text());
			activities.value = activities.value.filter(
				(activity) => activity.id !== id,
			);
			return true;
		} catch (e) {
			error.value = formatApiError(e);
			return false;
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

	// ---- Predefined materials ------------------------------------------------

	async function fetchMaterials(): Promise<void> {
		try {
			const res = await apiFetch(`${BASE}/materials/predefined`);
			if (res.ok) predefinedMaterials.value = (await res.json()) as string[];
		} catch {
			/* non-critical */
		}
	}

	// ---- Attachments ---------------------------------------------------------

	async function fetchAttachments(activityId: string, guestToken?: string): Promise<Attachment[]> {
		try {
			const res = guestToken
				? await guestEditFetch('/attachments', guestToken)
				: await apiFetch(`${BASE}/activities/${activityId}/attachments`);
			if (!res.ok) return [];
			return (await res.json()) as Attachment[];
		} catch {
			return [];
		}
	}

	async function uploadAttachment(
		activityId: string,
		file: File,
		programImage = false,
		guestToken?: string,
	): Promise<Attachment | null> {
		try {
			if (programImage) validateProgramImageFile(file);
			const buf = await file.arrayBuffer();
			const bytes = new Uint8Array(buf);
			let binary = '';
			for (let i = 0; i < bytes.length; i++)
				binary += String.fromCharCode(bytes[i]);
			const data = btoa(binary);
			const path = guestToken
				? `${BASE}/guest-edit/${programImage ? 'program-images' : 'attachments'}`
				: `${BASE}/activities/${activityId}/${programImage ? 'program-images' : 'attachments'}`;
			const options: RequestInit = {
					method: 'POST',
					body: JSON.stringify({
						filename: file.name,
						content_type: file.type || 'application/octet-stream',
						data,
					}),
				};
			const res = guestToken
				? await guestEditFetch(path.slice(`${BASE}/guest-edit`.length), guestToken, options)
				: await apiFetch(path, options);
			if (!res.ok) throw new Error(await res.text());
			return (await res.json()) as Attachment;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function deleteAttachment(attachmentId: string, guestToken?: string): Promise<boolean> {
		try {
			const res = guestToken
				? await guestEditFetch(`/attachments/${attachmentId}`, guestToken, { method: 'DELETE' })
				: await apiFetch(`${BASE}/attachments/${attachmentId}`, { method: 'DELETE' });
			return res.ok;
		} catch {
			return false;
		}
	}

	async function getAttachmentBlobUrl(
		attachmentId: string,
		guestToken?: string,
	): Promise<string | null> {
		try {
			const res = guestToken
				? await guestEditFetch(`/attachments/${attachmentId}/download`, guestToken)
				: await apiFetch(`${BASE}/attachments/${attachmentId}/download`);
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
		predefinedMaterials,
		fetchActivities,
		fetchActivity,
		fetchDepartments,
		fetchLocations,
		fetchMaterials,
		createActivity,
		updateActivity,
		updateGuestActivity,
		deleteActivity,
		fetchDeletedActivities,
		restoreActivity,
		deleteDeletedActivity,
		fetchAttachments,
		uploadAttachment,
		deleteAttachment,
		getAttachmentBlobUrl,
	};
}
