import { beforeEach, describe, expect, it, vi } from 'vitest';

const { apiFetchMock } = vi.hoisted(() => ({
	apiFetchMock: vi.fn(),
}));

vi.mock('./useApi', () => ({
	apiFetch: apiFetchMock,
}));

import { useUsers } from './useUsers';

function response(data: unknown, ok = true): Response {
	return {
		ok,
		json: vi.fn().mockResolvedValue(data),
	} as unknown as Response;
}

describe('useUsers', () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		apiFetchMock.mockReset();
		const store = useUsers();
		store.users.value = [];
	});

	it('fetchUsers loads users on success', async () => {
		const store = useUsers();
		apiFetchMock.mockResolvedValueOnce(
			response([{ id: 'u1', display_name: 'Anna' }]),
		);

		await store.fetchUsers(true);
		expect(apiFetchMock).toHaveBeenCalledWith('/api/users');
		expect(store.users.value).toHaveLength(1);
	});

	it('fetchUsers does not overwrite users on non-ok response', async () => {
		const store = useUsers();
		store.users.value = [{ id: 'existing' } as any];
		apiFetchMock.mockResolvedValueOnce(response([], false));

		await store.fetchUsers(true);
		expect(store.users.value).toHaveLength(1);
		expect(store.users.value[0].id).toBe('existing');
	});

	it('fetchUsers swallows network errors', async () => {
		const store = useUsers();
		store.users.value = [{ id: 'existing' } as any];
		apiFetchMock.mockRejectedValueOnce(new Error('down'));

		await expect(store.fetchUsers(true)).resolves.toBeUndefined();
		expect(store.users.value[0].id).toBe('existing');
	});

	it('fetchUsers deduplicates concurrent non-force requests', async () => {
		const store = useUsers();
		apiFetchMock.mockResolvedValueOnce(response([{ id: 'u1' }]));

		await Promise.all([store.fetchUsers(), store.fetchUsers()]);
		expect(apiFetchMock).toHaveBeenCalledTimes(1);
	});
});
