import { beforeEach, describe, expect, it, vi } from 'vitest';

const { apiFetchMock, formatApiErrorMock } = vi.hoisted(() => ({
	apiFetchMock: vi.fn(),
	formatApiErrorMock: vi.fn((e: unknown) => `fmt:${String(e)}`),
}));

vi.mock('./useApi', () => ({
	apiFetch: apiFetchMock,
	formatApiError: formatApiErrorMock,
}));

import { useEventTemplates } from './useEventTemplates';

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

describe('useEventTemplates', () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		apiFetchMock.mockReset();
		formatApiErrorMock.mockClear();
	});

	it('fetchTemplates sets templates on success', async () => {
		const e = useEventTemplates();
		apiFetchMock.mockResolvedValueOnce(response([{ id: 't1' }]));

		await e.fetchTemplates();
		expect(e.templates.value).toHaveLength(1);
		expect(e.loading.value).toBe(false);
	});

	it('fetchTemplates stores formatted error on failure', async () => {
		const e = useEventTemplates();
		apiFetchMock.mockResolvedValueOnce(response({}, false, 500, 'bad'));

		await e.fetchTemplates();
		expect(e.error.value).toContain('fmt:');
		expect(e.loading.value).toBe(false);
	});

	it('fetchTemplate returns null on 404', async () => {
		const e = useEventTemplates();
		apiFetchMock.mockResolvedValueOnce(response({}, false, 404));
		const tpl = await e.fetchTemplate('Pfadi');
		expect(tpl).toBeNull();
	});

	it('saveTemplate returns template on success', async () => {
		const e = useEventTemplates();
		apiFetchMock.mockResolvedValueOnce(
			response({ id: 's1', department: 'Pfadi' }),
		);
		const tpl = await e.saveTemplate('Pfadi', 'Titel', 'Body');
		expect(tpl?.id).toBe('s1');
	});

	it('creates a named template with an optional Stufe default', async () => {
		const events = useEventTemplates();
		apiFetchMock.mockResolvedValueOnce(response({ id: 'new-template', name: 'Abend', is_default: true }));
		expect((await events.createTemplate('Abend', 'Pfadi', '{{titel}}', '<p>Inhalt</p>', true))?.id)
			.toBe('new-template');
		expect(apiFetchMock).toHaveBeenCalledWith('/api/event-templates', {
			method: 'POST',
			body: JSON.stringify({ name: 'Abend', department: 'Pfadi', title: '{{titel}}', body: '<p>Inhalt</p>', is_default: true }),
		});
	});

	it('updates by ID and sends only changed fields to preserve concurrent edits', async () => {
		const events = useEventTemplates();
		apiFetchMock.mockResolvedValueOnce(response({ id: 'template-1', is_default: true }));
		await events.updateTemplate('template-1', { is_default: true });
		expect(apiFetchMock).toHaveBeenCalledWith('/api/event-templates/template-1', {
			method: 'PUT', body: '{"is_default":true}',
		});
	});

	it('removes a deleted template from the list and retains it on a failed delete', async () => {
		const events = useEventTemplates();
		apiFetchMock.mockResolvedValueOnce(response([{ id: 'one' }, { id: 'two' }]));
		await events.fetchTemplates();
		apiFetchMock.mockResolvedValueOnce(response(null, false, 403, 'Keine Berechtigung'));
		expect(await events.deleteTemplate('one')).toBe(false);
		expect(events.templates.value).toHaveLength(2);
		apiFetchMock.mockResolvedValueOnce(response(null, true, 204));
		expect(await events.deleteTemplate('one')).toBe(true);
		expect(events.templates.value.map(template => template.id)).toEqual(['two']);
	});

	it('loads all activity-authorized templates, not the management-only endpoint', async () => {
		const events = useEventTemplates();
		apiFetchMock.mockResolvedValueOnce(response({ templates: [{ id: 'one' }, { id: 'two' }] }));
		expect(await events.fetchActivityTemplates('activity-1')).toHaveLength(2);
		expect(apiFetchMock).toHaveBeenCalledWith('/api/activities/activity-1/event-template-context');
	});

	it('reports creation, update and preview template loading errors explicitly', async () => {
		const events = useEventTemplates();
		apiFetchMock.mockRejectedValue(new Error('Server nicht erreichbar'));
		expect(await events.createTemplate('Neue', 'Pfadi')).toBeNull();
		expect(events.error.value).toContain('Server nicht erreichbar');
		expect(await events.updateTemplate('one', { title: 'Titel' })).toBeNull();
		expect(events.error.value).toContain('Server nicht erreichbar');
		expect(await events.fetchActivityTemplates('activity-1')).toBeNull();
		expect(events.error.value).toContain('Server nicht erreichbar');
	});

	it('publishEvent returns null and sets error on non-ok response', async () => {
		const e = useEventTemplates();
		apiFetchMock.mockResolvedValueOnce(response({}, false, 500, 'failed'));
		const pub = await e.publishEvent('a1', 'T', '<p>B</p>');
		expect(pub).toBeNull();
		expect(e.error.value).toContain('fmt:');
	});

	it('fetchPublication returns null on 404', async () => {
		const e = useEventTemplates();
		apiFetchMock.mockResolvedValueOnce(response({}, false, 404));
		const pub = await e.fetchPublication('a1');
		expect(pub).toBeNull();
	});

	it('unpublishEvent ignores thrown errors', async () => {
		const e = useEventTemplates();
		apiFetchMock.mockRejectedValueOnce(new Error('down'));
		await expect(e.unpublishEvent('a1')).resolves.toBeUndefined();
	});
});
