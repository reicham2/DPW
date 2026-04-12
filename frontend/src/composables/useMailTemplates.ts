import { ref } from 'vue';
import type { MailTemplate, Department, SentMail } from '../types';
import { getIdToken, getGraphAccessToken } from './useAuth';

const BASE = '/api';

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
			...(options.body ? { 'Content-Type': 'application/json' } : {}),
		},
	});
}

export function useMailTemplates() {
	const templates = ref<MailTemplate[]>([]);
	const loading = ref(false);
	const error = ref<string | null>(null);
	const sending = ref(false);

	async function fetchTemplates(): Promise<void> {
		loading.value = true;
		error.value = null;
		try {
			const res = await apiFetch(`${BASE}/mail-templates`);
			if (!res.ok) throw new Error(await res.text());
			templates.value = (await res.json()) as MailTemplate[];
		} catch (e) {
			error.value = String(e);
		} finally {
			loading.value = false;
		}
	}

	async function fetchTemplate(
		department: Department,
	): Promise<MailTemplate | null> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/mail-templates/${encodeURIComponent(department)}`,
			);
			if (res.status === 404) return null;
			if (!res.ok) throw new Error(await res.text());
			return (await res.json()) as MailTemplate;
		} catch (e) {
			error.value = String(e);
			return null;
		}
	}

	async function saveTemplate(
		department: Department,
		subject: string,
		body: string,
		recipients: string[] = [],
	): Promise<MailTemplate | null> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/mail-templates/${encodeURIComponent(department)}`,
				{
					method: 'PUT',
					body: JSON.stringify({ subject, body, recipients }),
				},
			);
			if (!res.ok) throw new Error(await res.text());
			return (await res.json()) as MailTemplate;
		} catch (e) {
			error.value = String(e);
			return null;
		}
	}

	async function sendMail(
		to: string[],
		subject: string,
		bodyHtml: string,
		fromEmail: string,
		activityId?: string,
	): Promise<boolean> {
		sending.value = true;
		error.value = null;
		try {
			const graphToken = await getGraphAccessToken();
			const idToken = await getIdToken();
			const res = await fetch(`${BASE}/send-mail`, {
				method: 'POST',
				headers: {
					Authorization: `Bearer ${idToken}`,
					'Content-Type': 'application/json',
				},
				body: JSON.stringify({
					to,
					subject,
					body: bodyHtml,
					from: fromEmail,
					access_token: graphToken,
					...(activityId ? { activity_id: activityId } : {}),
				}),
			});
			if (!res.ok) {
				const text = await res.text();
				throw new Error(text || `Fehler: ${res.status}`);
			}
			return true;
		} catch (e) {
			error.value = String(e);
			return false;
		} finally {
			sending.value = false;
		}
	}

	async function fetchSentMails(activityId: string): Promise<SentMail[]> {
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/sent-mails`,
			);
			if (!res.ok) return [];
			return (await res.json()) as SentMail[];
		} catch {
			return [];
		}
	}

	return {
		templates,
		loading,
		error,
		sending,
		fetchTemplates,
		fetchTemplate,
		saveTemplate,
		sendMail,
		fetchSentMails,
	};
}
