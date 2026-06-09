import { beforeEach, describe, expect, it, vi } from 'vitest';

const { apiFetchMock, formatApiErrorMock } = vi.hoisted(() => ({
	apiFetchMock: vi.fn(),
	formatApiErrorMock: vi.fn((e: unknown) => `fmt:${String(e)}`),
}));

vi.mock('./useApi', () => ({
	apiFetch: apiFetchMock,
	formatApiError: formatApiErrorMock,
}));

import { useIdeaBox } from './useIdeaBox';

function response(data: unknown, ok = true, text = 'error'): Response {
	return {
		ok,
		json: vi.fn().mockResolvedValue(data),
		text: vi.fn().mockResolvedValue(text),
	} as unknown as Response;
}

describe('useIdeaBox', () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		apiFetchMock.mockReset();
		formatApiErrorMock.mockClear();
		const box = useIdeaBox();
		box.items.value = [];
		box.error.value = null;
		box.loading.value = false;
	});

	it('fetchItems loads items and toggles loading', async () => {
		const box = useIdeaBox();
		apiFetchMock.mockResolvedValueOnce(response([{ id: 'i1', title: 'Idee' }]));

		await box.fetchItems();
		expect(apiFetchMock).toHaveBeenCalledWith('/api/ideaBox');
		expect(box.loading.value).toBe(false);
		expect(box.items.value).toHaveLength(1);
	});

	it('fetchItems sets formatted error on failure', async () => {
		const box = useIdeaBox();
		apiFetchMock.mockResolvedValueOnce(response({}, false, 'kaputt'));

		await box.fetchItems();
		expect(box.error.value).toContain('fmt:');
	});

	it('createItem prepends new item on success', async () => {
		const box = useIdeaBox();
		box.items.value = [{ id: 'old' } as any];
		apiFetchMock.mockResolvedValueOnce(response({ id: 'new', title: 'Neu' }));

		const created = await box.createItem({
			title: 'Neu',
			duration_minutes: 30,
			description: 'd',
		});

		expect(created?.id).toBe('new');
		expect(box.items.value.map((i) => i.id)).toEqual(['new', 'old']);
	});

	it('updateItem replaces matching item only', async () => {
		const box = useIdeaBox();
		box.items.value = [
			{ id: 'a', title: 'A' } as any,
			{ id: 'b', title: 'B' } as any,
		];
		apiFetchMock.mockResolvedValueOnce(response({ id: 'b', title: 'B2' }));

		await box.updateItem('b', {
			title: 'B2',
			duration_minutes: 10,
			description: '',
		});

		expect(box.items.value[0].title).toBe('A');
		expect(box.items.value[1].title).toBe('B2');
	});

	it('deleteItem removes item on success', async () => {
		const box = useIdeaBox();
		box.items.value = [{ id: 'a' } as any, { id: 'b' } as any];
		apiFetchMock.mockResolvedValueOnce(response({}, true));

		const ok = await box.deleteItem('a');
		expect(ok).toBe(true);
		expect(box.items.value.map((i) => i.id)).toEqual(['b']);
	});

	it('createItem returns null and sets error on failure', async () => {
		const box = useIdeaBox();
		apiFetchMock.mockResolvedValueOnce(response({}, false, 'boom'));

		const created = await box.createItem({
			title: 'X',
			duration_minutes: 1,
			description: 'Y',
		});
		expect(created).toBeNull();
		expect(box.error.value).toContain('fmt:');
	});
});
