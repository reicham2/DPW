import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

vi.mock('workbox-precaching', () => ({
	cleanupOutdatedCaches: vi.fn(),
	precacheAndRoute: vi.fn(),
}));

const showNotification = vi.fn();
const addEventListener = vi.fn();
const fetchMock = vi.fn();

async function push(payload: unknown) {
	await import('./sw');
	const handler = addEventListener.mock.calls.find(([name]) => name === 'push')?.[1];
	expect(handler).toBeDefined();
	let work: Promise<unknown> | undefined;
	handler({
		data: { json: () => payload },
		waitUntil: (promise: Promise<unknown>) => { work = promise; },
	});
	await work;
}

describe('reminder web push delivery', () => {
	beforeEach(() => {
		vi.resetModules();
		vi.clearAllMocks();
		vi.stubGlobal('fetch', fetchMock);
		vi.stubGlobal('self', {
			__WB_MANIFEST: [],
			addEventListener,
			registration: {
				showNotification,
				pushManager: {
					getSubscription: vi.fn().mockResolvedValue({
						toJSON: () => ({ endpoint: 'https://push.example.test', keys: { auth: 'test' } }),
					}),
				},
			},
		});
	});
	afterEach(() => vi.unstubAllGlobals());

	it('keeps the original reminder identity and link instead of resolving another unread note', async () => {
		await push({ id: 'reminder-1', category: 'activity_reminder', title: 'Reminder', body: 'Tomorrow', url: '/activities/a1' });
		expect(fetchMock).not.toHaveBeenCalled();
		expect(showNotification).toHaveBeenCalledWith('Reminder', expect.objectContaining({
			tag: 'dpw-note-reminder-1', body: 'Tomorrow', data: { url: '/activities/a1' },
		}));
	});

	it('retries use the same browser tag', async () => {
		const payload = { id: 'reminder-1', category: 'activity_reminder', title: 'Reminder' };
		await push(payload);
		await push(payload);
		expect(showNotification.mock.calls.map(([, options]) => options.tag))
			.toEqual(['dpw-note-reminder-1', 'dpw-note-reminder-1']);
	});

	it('preserves legacy push lookup and uses the resolved notification identity', async () => {
		fetchMock.mockResolvedValue({ ok: true, status: 200, json: async () => ({
			id: 'latest-note', title: 'Latest', body: 'Latest date', url: '/activities/latest',
		}) });
		await push({});
		expect(fetchMock).toHaveBeenCalledTimes(1);
		expect(showNotification).toHaveBeenCalledWith('Latest', expect.objectContaining({
			tag: 'dpw-note-latest-note', data: { url: '/activities/latest' },
		}));
	});
});
