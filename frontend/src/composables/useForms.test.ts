import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { FormResponse, SignupForm } from '../types';

const { apiFetchMock, formatApiErrorMock } = vi.hoisted(() => ({
	apiFetchMock: vi.fn(),
	formatApiErrorMock: vi.fn((e: unknown) => `fmt:${String(e)}`),
}));

vi.mock('./useApi', () => ({
	apiFetch: apiFetchMock,
	formatApiError: formatApiErrorMock,
}));

import { useForms, useFormTemplates } from './useForms';

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

function makeForm(overrides: Partial<SignupForm> = {}): SignupForm {
	return {
		id: 'f1',
		activity_id: 'a1',
		public_slug: 'slug',
		form_type: 'registration',
		title: 'Anmeldung',
		created_by: 'u1',
		created_at: '2026-01-01',
		updated_at: '2026-01-02',
		questions: [],
		...overrides,
	};
}

describe('useForms', () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		apiFetchMock.mockReset();
		formatApiErrorMock.mockClear();
	});

	it('fetchForm unwraps { exists, form } payload', async () => {
		const forms = useForms();
		apiFetchMock.mockResolvedValueOnce(
			response({ exists: true, form: makeForm({ id: 'fx' }) }),
		);

		const result = await forms.fetchForm('a1');
		expect(result?.id).toBe('fx');
		expect(forms.form.value?.id).toBe('fx');
	});

	it('fetchForm returns null when exists is false', async () => {
		const forms = useForms();
		apiFetchMock.mockResolvedValueOnce(response({ exists: false }));
		const result = await forms.fetchForm('a1');
		expect(result).toBeNull();
		expect(forms.form.value).toBeNull();
	});

	it('createForm resolves 409 by loading existing form', async () => {
		const forms = useForms();
		apiFetchMock
			.mockResolvedValueOnce(response({}, false, 409, 'exists'))
			.mockResolvedValueOnce(
				response({ exists: true, form: makeForm({ id: 'existing' }) }),
			);

		const result = await forms.createForm('a1', {
			form_type: 'registration',
			title: 'x',
			questions: [],
		});

		expect(result?.id).toBe('existing');
		expect(forms.error.value).toBeNull();
	});

	it('deleteForm clears form state on success', async () => {
		const forms = useForms();
		forms.form.value = makeForm();
		apiFetchMock.mockResolvedValueOnce(response({}, true));

		const ok = await forms.deleteForm('a1');
		expect(ok).toBe(true);
		expect(forms.form.value).toBeNull();
	});

	it('fetchResponse returns null on 404', async () => {
		const forms = useForms();
		apiFetchMock.mockResolvedValueOnce(response({}, false, 404));
		const result = await forms.fetchResponse('a1', 'r1');
		expect(result).toBeNull();
	});

	it('deleteResponse removes response from list', async () => {
		const forms = useForms();
		forms.responses.value = [
			{ id: 'r1' } as FormResponse,
			{ id: 'r2' } as FormResponse,
		];
		apiFetchMock.mockResolvedValueOnce(response({}, true));

		const ok = await forms.deleteResponse('a1', 'r1');
		expect(ok).toBe(true);
		expect(forms.responses.value.map((r) => r.id)).toEqual(['r2']);
	});

	it('fetchStats unwraps { exists, stats } payload', async () => {
		const forms = useForms();
		apiFetchMock.mockResolvedValueOnce(
			response({
				exists: true,
				stats: {
					form_type: 'registration',
					total: 2,
					by_mode: {},
					questions: {},
				},
			}),
		);
		const result = await forms.fetchStats('a1');
		expect(result?.total).toBe(2);
		expect(forms.stats.value?.total).toBe(2);
	});

	it('fetchPublicForm returns null on 404', async () => {
		const forms = useForms();
		vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
			response({}, false, 404),
		);
		const result = await forms.fetchPublicForm('slug');
		expect(result).toBeNull();
	});

	it('submitResponse sets formatted error on non-ok response', async () => {
		const forms = useForms();
		vi.spyOn(globalThis, 'fetch').mockResolvedValueOnce(
			response({}, false, 500, 'kaputt'),
		);

		const result = await forms.submitResponse('slug', { answers: [] });
		expect(result).toBeNull();
		expect(forms.error.value).toContain('fmt:');
	});

	it('getFormDraft maps backend draft shape', async () => {
		const forms = useForms();
		apiFetchMock.mockResolvedValueOnce(
			response({
				id: 'd1',
				activity_id: 'a1',
				form_type: 'registration',
				title: 'Draft',
				updated_by: 'u9',
				updated_at: '2026-01-01',
				questions: [],
			}),
		);

		const draft = await forms.getFormDraft('a1');
		expect(draft?.id).toBe('d1');
		expect(draft?.created_by).toBe('u9');
		expect(draft?.public_slug).toBe('');
	});

	it('saveFormDraft returns null on failed response', async () => {
		const forms = useForms();
		apiFetchMock.mockResolvedValueOnce(response({}, false));
		const result = await forms.saveFormDraft('a1', 'registration', 't', []);
		expect(result).toBeNull();
	});

	it('deleteFormDraft ignores errors', async () => {
		const forms = useForms();
		apiFetchMock.mockRejectedValueOnce(new Error('down'));
		await expect(forms.deleteFormDraft('a1')).resolves.toBeUndefined();
	});
});

describe('useFormTemplates', () => {
	beforeEach(() => {
		apiFetchMock.mockReset();
		formatApiErrorMock.mockClear();
	});

	it('fetchTemplates returns templates list', async () => {
		const t = useFormTemplates();
		apiFetchMock.mockResolvedValueOnce(response([{ id: 't1' }]));
		const list = await t.fetchTemplates('Pfadi');
		expect(list).toHaveLength(1);
		expect(t.templates.value).toHaveLength(1);
	});

	it('createTemplate unsets is_default on other templates when new default', async () => {
		const t = useFormTemplates();
		t.templates.value = [{ id: 'old', is_default: true } as any];
		apiFetchMock.mockResolvedValueOnce(
			response({
				id: 'new',
				is_default: true,
				name: 'N',
				department: 'Pfadi',
				form_type: 'registration',
				template_config: [],
				created_by: 'u',
				created_at: 'c',
				updated_at: 'u',
			}),
		);

		await t.createTemplate('N', 'Pfadi', 'registration', [], true);
		expect(t.templates.value.find((x) => x.id === 'old')?.is_default).toBe(
			false,
		);
		expect(t.templates.value.find((x) => x.id === 'new')?.is_default).toBe(
			true,
		);
	});

	it('updateTemplate updates entry and unsets others when default', async () => {
		const t = useFormTemplates();
		t.templates.value = [
			{ id: 'a', is_default: true } as any,
			{ id: 'b', is_default: false } as any,
		];
		apiFetchMock.mockResolvedValueOnce(
			response({
				id: 'b',
				is_default: true,
				name: 'B',
				department: 'Pfadi',
				form_type: 'registration',
				template_config: [],
				created_by: 'u',
				created_at: 'c',
				updated_at: 'u',
			}),
		);

		await t.updateTemplate('b', 'B', 'registration', [], true);
		expect(t.templates.value.find((x) => x.id === 'a')?.is_default).toBe(false);
		expect(t.templates.value.find((x) => x.id === 'b')?.is_default).toBe(true);
	});

	it('deleteTemplate removes entry on success', async () => {
		const t = useFormTemplates();
		t.templates.value = [{ id: 'a' } as any, { id: 'b' } as any];
		apiFetchMock.mockResolvedValueOnce(response({}, true));
		const ok = await t.deleteTemplate('a');
		expect(ok).toBe(true);
		expect(t.templates.value.map((x) => x.id)).toEqual(['b']);
	});
});
