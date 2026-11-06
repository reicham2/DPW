import { beforeEach, describe, expect, it, vi } from 'vitest';

const { apiFetchMock, formatApiErrorMock, getGraphAccessTokenMock } =
	vi.hoisted(() => ({
		apiFetchMock: vi.fn(),
		formatApiErrorMock: vi.fn((e: unknown) => `fmt:${String(e)}`),
		getGraphAccessTokenMock: vi.fn(),
	}));

vi.mock('./useApi', () => ({
	apiFetch: apiFetchMock,
	formatApiError: formatApiErrorMock,
}));

vi.mock('./useAuth', () => ({
	getGraphAccessToken: getGraphAccessTokenMock,
}));

import { useMailTemplates } from './useMailTemplates';

function response(
	data: unknown,
	ok = true,
	status = 200,
	text = 'error',
): Response {
	return {
		ok,
		status,
		json: vi.fn().mockResolvedValue(data),
		text: vi.fn().mockResolvedValue(text),
	} as unknown as Response;
}

describe('useMailTemplates', () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		apiFetchMock.mockReset();
		formatApiErrorMock.mockClear();
		getGraphAccessTokenMock.mockReset();
		getGraphAccessTokenMock.mockResolvedValue('graph-token');
	});

	it('fetchTemplates populates templates list', async () => {
		const m = useMailTemplates();
		apiFetchMock.mockResolvedValueOnce(response([{ id: 't1' }]));

		await m.fetchTemplates();
		expect(m.templates.value).toHaveLength(1);
		expect(m.loading.value).toBe(false);
	});

	it('fetchTemplate returns null on 404', async () => {
		const m = useMailTemplates();
		apiFetchMock.mockResolvedValueOnce(response({}, false, 404));
		const tpl = await m.fetchTemplate('Pfadi');
		expect(tpl).toBeNull();
	});

	it('saveTemplate returns parsed template on success', async () => {
		const m = useMailTemplates();
		apiFetchMock.mockResolvedValueOnce(
			response({ id: 'p', department: 'Pfadi' }),
		);

		const tpl = await m.saveTemplate('Pfadi', 'S', 'B', ['a@x'], ['c@x']);
		expect(tpl?.id).toBe('p');
		expect(apiFetchMock).toHaveBeenCalledWith(
			'/api/mail-templates/Pfadi',
			expect.objectContaining({
				method: 'PUT',
				body: JSON.stringify({ subject: 'S', body: 'B', recipients: ['a@x'], cc: ['c@x'] }),
			}),
		);
	});

	it('creates, updates, and deletes named templates by id', async () => {
		const m = useMailTemplates();
		apiFetchMock
			.mockResolvedValueOnce(response({ id: 't1', name: 'Eltern', is_default: true }))
			.mockResolvedValueOnce(response({ id: 't1', name: 'Eltern aktualisiert', is_default: false }))
			.mockResolvedValueOnce(response({ ok: true }));

		await m.createTemplate('Eltern', 'Pfadi', 'Betreff', '<p>Text</p>', ['a@x'], ['c@x'], true);
		expect(apiFetchMock).toHaveBeenNthCalledWith(
			1,
			'/api/mail-templates',
			expect.objectContaining({
				method: 'POST',
				body: JSON.stringify({
					name: 'Eltern',
					department: 'Pfadi',
					subject: 'Betreff',
					body: '<p>Text</p>',
					recipients: ['a@x'],
					cc: ['c@x'],
					is_default: true,
				}),
			}),
		);
		await m.updateTemplate('t1', 'Eltern aktualisiert', 'Neu', '<p>Neu</p>', [], [], false);
		expect(apiFetchMock).toHaveBeenNthCalledWith(
			2,
			'/api/mail-templates/t1',
			expect.objectContaining({
				method: 'PUT',
				body: JSON.stringify({
					name: 'Eltern aktualisiert',
					subject: 'Neu',
					body: '<p>Neu</p>',
					recipients: [],
					cc: [],
					is_default: false,
				}),
			}),
		);
		await expect(m.deleteTemplate('t1')).resolves.toBe(true);
		expect(apiFetchMock).toHaveBeenNthCalledWith(
			3,
			'/api/mail-templates/t1',
			{ method: 'DELETE' },
		);
	});

	it('sendMail posts graph token and toggles sending flag', async () => {
		const m = useMailTemplates();
		apiFetchMock.mockResolvedValueOnce(response({}, true));

		const ok = await m.sendMail(
			['to@example.com'],
			'Subject',
			'<p>Body</p>',
			'from@example.com',
			'act-1',
			['cc@example.com'],
		);

		expect(ok).toBe(true);
		expect(m.sending.value).toBe(false);
		expect(apiFetchMock).toHaveBeenCalledWith(
			'/api/send-mail',
			expect.objectContaining({ method: 'POST' }),
		);
	});

	it('sendMail returns false and formats error on non-ok', async () => {
		const m = useMailTemplates();
		apiFetchMock.mockResolvedValueOnce(response({}, false, 500, 'mail failed'));

		const ok = await m.sendMail(['to'], 's', 'b', 'from');
		expect(ok).toBe(false);
		expect(m.error.value).toContain('fmt:');
		expect(m.sending.value).toBe(false);
	});

	it('fetchSentMails returns [] on non-ok', async () => {
		const m = useMailTemplates();
		apiFetchMock.mockResolvedValueOnce(response({}, false));
		const list = await m.fetchSentMails('a1');
		expect(list).toEqual([]);
	});

	it('fetchDraft returns null on 404', async () => {
		const m = useMailTemplates();
		apiFetchMock.mockResolvedValueOnce(response({}, false, 404));
		const draft = await m.fetchDraft('a1');
		expect(draft).toBeNull();
	});

	it('saveDraft returns null when request fails', async () => {
		const m = useMailTemplates();
		apiFetchMock.mockResolvedValueOnce(response({}, false));
		const draft = await m.saveDraft('a1', ['x'], 's', 'b');
		expect(draft).toBeNull();
	});

	it('deleteDraft ignores thrown errors', async () => {
		const m = useMailTemplates();
		apiFetchMock.mockRejectedValueOnce(new Error('down'));
		await expect(m.deleteDraft('a1')).resolves.toBeUndefined();
	});

	it('fetchComposerContext returns null on failed response', async () => {
		const m = useMailTemplates();
		apiFetchMock.mockResolvedValueOnce(response({}, false));
		const ctx = await m.fetchComposerContext('a1');
		expect(ctx).toBeNull();
	});
});
