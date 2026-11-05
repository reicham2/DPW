import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { ActivityInput } from '../types';

vi.mock('./useWebSocket', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { useWebSocket: () => ({ connected: ref(false) }) };
});

vi.mock('./useAuth', () => ({ getGraphAccessToken: vi.fn().mockResolvedValue('') }));
vi.mock('./useApi', () => ({
	apiFetch: vi.fn(),
	formatApiError: (error: unknown) => error instanceof Error ? error.message : String(error),
}));

import { useActivities } from './useActivities';
import { apiFetch } from './useApi';

const input: ActivityInput = {
	title: 'Aktivität',
	date: '2026-07-01',
	start_time: '10:00',
	end_time: '11:00',
	goal: '',
	guiding_thread: 'Eine Geschichte',
	location: '',
	responsible: [],
	material: [],
	tn_material: [],
	programs: [],
};

describe('activity creation response', () => {
	beforeEach(() => vi.mocked(apiFetch).mockReset());

	it.each([
		{ response: { id: 'new-id', guiding_thread: 'Eine Geschichte' }, expected: 'Eine Geschichte' },
		{ response: { id: 'new-id' }, expected: undefined },
		{ response: { id: 'new-id', guiding_thread: null }, expected: null },
		{ response: { id: 'new-id', guiding_thread: 42 }, expected: 42 },
	])('retains the response field for compatibility checks', async ({ response, expected }) => {
		vi.mocked(apiFetch).mockResolvedValue(new Response(JSON.stringify(response), { status: 201 }));
		const { createActivity, error } = useActivities();
		expect(await createActivity(input)).toEqual({ id: 'new-id', guiding_thread: expected });
		expect(error.value).toBeNull();
	});

	it('reports malformed creation responses', async () => {
		vi.mocked(apiFetch).mockResolvedValue(new Response('{}', { status: 201 }));
		const { createActivity, error } = useActivities();
		expect(await createActivity(input)).toBeNull();
		expect(error.value).toContain('enthält keine ID');
	});
});

describe('activity update response', () => {
	beforeEach(() => vi.mocked(apiFetch).mockReset());

	it.each([
		{ guidingThread: 'Eine Geschichte', response: { guiding_thread: 'Eine Geschichte' } },
		{ guidingThread: null, response: { guiding_thread: null } },
		{ guidingThread: '', response: { guiding_thread: null } },
	])('accepts a confirmed guiding thread', async ({ guidingThread, response }) => {
		vi.mocked(apiFetch).mockResolvedValue(new Response(JSON.stringify(response), { status: 200 }));
		const { updateActivity, error } = useActivities();
		expect(await updateActivity('activity-1', { ...input, guiding_thread: guidingThread })).toBe(true);
		expect(error.value).toBeNull();
	});

	it.each([
		{ guidingThread: 'Eine Geschichte', response: {} },
		{ guidingThread: 'Eine Geschichte', response: { guiding_thread: 'Alter Text' } },
		{ guidingThread: null, response: { guiding_thread: 'Alter Text' } },
		{ guidingThread: 'Eine Geschichte', response: { guiding_thread: 42 } },
	])('reports an unconfirmed guiding thread', async ({ guidingThread, response }) => {
		vi.mocked(apiFetch).mockResolvedValue(new Response(JSON.stringify(response), { status: 200 }));
		const { updateActivity, error } = useActivities();
		expect(await updateActivity('activity-1', { ...input, guiding_thread: guidingThread })).toBe(false);
		expect(error.value).toContain('den roten Faden nicht bestätigt');
	});

	it('allows updates that omit the new field against older backends', async () => {
		vi.mocked(apiFetch).mockResolvedValue(new Response('{}', { status: 200 }));
		const { guiding_thread: _guidingThread, ...legacyInput } = input;
		const { updateActivity, error } = useActivities();
		expect(await updateActivity('activity-1', legacyInput)).toBe(true);
		expect(error.value).toBeNull();
	});
});
