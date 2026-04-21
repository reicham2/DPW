import { ref } from 'vue';
import type { MailTemplate, Department, SentMail, MailDraft } from '../types';
import { getGraphAccessToken } from './useAuth';
import { apiFetch, formatApiError } from './useApi';

const BASE = '/api';

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
			error.value = formatApiError(e);
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
			error.value = formatApiError(e);
			return null;
		}
	}

	async function saveTemplate(
		department: Department,
		subject: string,
		body: string,
		recipients: string[] = [],
		cc: string[] = [],
	): Promise<MailTemplate | null> {
		error.value = null;
		try {
			const res = await apiFetch(
				`${BASE}/mail-templates/${encodeURIComponent(department)}`,
				{
					method: 'PUT',
					body: JSON.stringify({ subject, body, recipients, cc }),
				},
			);
			if (!res.ok) throw new Error(await res.text());
			return (await res.json()) as MailTemplate;
		} catch (e) {
			error.value = formatApiError(e);
			return null;
		}
	}

	async function sendMail(
		to: string[],
		subject: string,
		bodyHtml: string,
		fromEmail: string,
		activityId?: string,
		cc: string[] = [],
	): Promise<boolean> {
		sending.value = true;
		error.value = null;
		try {
			const graphToken = await getGraphAccessToken();
			const res = await apiFetch(`${BASE}/send-mail`, {
				method: 'POST',
				body: JSON.stringify({
					to,
					cc,
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
			error.value = formatApiError(e);
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

	async function fetchDraft(activityId: string): Promise<MailDraft | null> {
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/mail-draft`,
			);
			if (res.status === 404) return null;
			if (!res.ok) return null;
			return (await res.json()) as MailDraft;
		} catch {
			return null;
		}
	}

	async function saveDraft(
		activityId: string,
		recipients: string[],
		subject: string,
		bodyHtml: string,
		cc: string[] = [],
	): Promise<MailDraft | null> {
		try {
			const res = await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/mail-draft`,
				{
					method: 'PUT',
					body: JSON.stringify({ recipients, cc, subject, body_html: bodyHtml }),
				},
			);
			if (!res.ok) return null;
			return (await res.json()) as MailDraft;
		} catch {
			return null;
		}
	}

	async function deleteDraft(activityId: string): Promise<void> {
		try {
			await apiFetch(
				`${BASE}/activities/${encodeURIComponent(activityId)}/mail-draft`,
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
		sending,
		fetchTemplates,
		fetchTemplate,
		saveTemplate,
		sendMail,
		fetchSentMails,
		fetchDraft,
		saveDraft,
		deleteDraft,
	};
}
