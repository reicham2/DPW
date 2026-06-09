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
