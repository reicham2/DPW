import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { WsEvent } from '../types';

const { getIdTokenMock, onUnmountedCallbacks, currentUser } = vi.hoisted(() => ({
	getIdTokenMock: vi.fn(),
	onUnmountedCallbacks: [] as Array<() => void>,
	currentUser: { value: { id: 'user-1' } as { id: string } | null },
}));

vi.mock('./useAuth', () => ({
	getIdToken: getIdTokenMock,
	user: currentUser,
}));

vi.mock('vue', async () => {
	const actual = await vi.importActual<typeof import('vue')>('vue');
	return {
		...actual,
		onUnmounted: (cb: () => void) => onUnmountedCallbacks.push(cb),
	};
});

import {
	getWsDebugState,
	useWebSocket,
	wsAddGlobalHandler,
	wsDisconnect,
	wsJoin,
	wsLeave,
	wsRegister,
	wsSend,
} from './useWebSocket';

class MockWebSocket {
	static OPEN = 1;
	static CONNECTING = 0;
	static instances: MockWebSocket[] = [];

	url: string;
	readyState = MockWebSocket.CONNECTING;
	onopen: (() => void) | null = null;
	onmessage: ((e: MessageEvent) => void) | null = null;
	onclose: (() => void) | null = null;
	onerror: (() => void) | null = null;
	sent: string[] = [];

	constructor(url: string) {
		this.url = url;
		MockWebSocket.instances.push(this);
	}

	send(payload: string) {
		this.sent.push(payload);
	}

	close() {
		this.readyState = 3;
		this.onclose?.();
	}

	emitOpen() {
		this.readyState = MockWebSocket.OPEN;
		this.onopen?.();
	}

	emitMessage(data: unknown) {
		this.onmessage?.({ data: JSON.stringify(data) } as MessageEvent);
	}

	emitMalformedMessage() {
		this.onmessage?.({ data: 'not-json' } as MessageEvent);
	}

	emitError() {
		this.onerror?.();
	}
}

describe('useWebSocket', () => {
	beforeEach(() => {
		vi.useFakeTimers();
		vi.restoreAllMocks();
		MockWebSocket.instances = [];
		onUnmountedCallbacks.length = 0;
		getIdTokenMock.mockReset();
		getIdTokenMock.mockResolvedValue('token-1');
		currentUser.value = { id: 'user-1' };

		Object.defineProperty(globalThis, 'location', {
			value: { protocol: 'http:', host: 'localhost:5173' },
			configurable: true,
		});
		Object.defineProperty(globalThis, 'WebSocket', {
			value: MockWebSocket,
			configurable: true,
		});

		wsDisconnect();
	});

	afterEach(() => {
		vi.useRealTimers();
	});

	it('connects on first use and dispatches incoming events to handlers', async () => {
		const handler = vi.fn();
		const globalHandler = vi.fn();
		wsAddGlobalHandler(globalHandler);

		const state = useWebSocket(handler);
		expect(state.connected.value).toBe(false);

		await vi.advanceTimersByTimeAsync(0);
		expect(MockWebSocket.instances).toHaveLength(1);
		const sock = MockWebSocket.instances[0];
		expect(sock.url).toContain('ws://localhost:5173/ws?token=token-1');
		expect(getIdTokenMock).toHaveBeenCalledWith({ allowInteraction: false });

		sock.emitOpen();
		expect(state.connected.value).toBe(true);

		sock.emitMessage({ event: 'deleted', id: 'a1' } as WsEvent);
		expect(handler).toHaveBeenCalledTimes(1);
		expect(globalHandler).toHaveBeenCalledTimes(1);
	});

	it('does not connect until a user is authenticated', async () => {
		currentUser.value = null;
		useWebSocket(vi.fn());
		wsRegister('A', 'B');
		await vi.advanceTimersByTimeAsync(3000);
		expect(getIdTokenMock).not.toHaveBeenCalled();

		currentUser.value = { id: 'user-1' };
		wsRegister('A', 'B');
		await vi.advanceTimersByTimeAsync(0);
		expect(MockWebSocket.instances).toHaveLength(1);
	});

	it('ignores malformed websocket frames', async () => {
		const handler = vi.fn();
		useWebSocket(handler);
		await vi.advanceTimersByTimeAsync(0);
		const sock = MockWebSocket.instances[0];
		sock.emitOpen();

		sock.emitMalformedMessage();
		expect(handler).not.toHaveBeenCalled();
	});

	it('wsSend only sends when socket is open', async () => {
		useWebSocket(vi.fn());
		await vi.advanceTimersByTimeAsync(0);
		const sock = MockWebSocket.instances[0];

		wsSend({ type: 'x' });
		expect(sock.sent).toHaveLength(0);

		sock.emitOpen();
		wsSend({ type: 'x' });
		expect(sock.sent).toHaveLength(1);
		expect(sock.sent[0]).toContain('"type":"x"');
	});

	it('wsJoin queues pending room and auto-joins after reconnect', async () => {
		useWebSocket(vi.fn());
		await vi.advanceTimersByTimeAsync(0);
		const sock = MockWebSocket.instances[0];

		wsJoin('act-1');
		expect(sock.sent).toHaveLength(0);

		sock.emitOpen();
		expect(sock.sent[sock.sent.length - 1]).toContain('"type":"join"');
		expect(sock.sent[sock.sent.length - 1]).toContain('"activity_id":"act-1"');
	});

	it('wsLeave sends leave for pending join and clears it', async () => {
		useWebSocket(vi.fn());
		await vi.advanceTimersByTimeAsync(0);
		const sock = MockWebSocket.instances[0];
		sock.emitOpen();

		wsJoin('act-2');
		wsLeave();

		expect(sock.sent.some((x) => x.includes('"type":"leave"'))).toBe(true);
	});

	it('onclose schedules reconnect and wsRegister can trigger initial connect', async () => {
		wsRegister('A', 'B');
		await vi.advanceTimersByTimeAsync(0);
		const first = MockWebSocket.instances[0];
		first.emitOpen();

		first.close();
		expect(getWsDebugState().connected).toBe(false);

		await vi.advanceTimersByTimeAsync(3000);
		expect(MockWebSocket.instances.length).toBeGreaterThanOrEqual(2);
	});

	it('error path records lastError and reconnects', async () => {
		useWebSocket(vi.fn());
		await vi.advanceTimersByTimeAsync(0);
		const sock = MockWebSocket.instances[0];
		sock.emitOpen();

		sock.emitError();
		const debug = getWsDebugState();
		expect(debug.lastError).toBeTruthy();

		await vi.advanceTimersByTimeAsync(3000);
		expect(MockWebSocket.instances.length).toBeGreaterThanOrEqual(2);
	});

	it('wsDisconnect stops reconnect and clears connection state', async () => {
		useWebSocket(vi.fn());
		await vi.advanceTimersByTimeAsync(0);
		const sock = MockWebSocket.instances[0];
		sock.emitOpen();
		sock.close();

		wsDisconnect();
		await vi.advanceTimersByTimeAsync(3000);
		const debug = getWsDebugState();
		expect(debug.connected).toBe(false);
	});

	it('does not open a socket when a pending token resolves after disconnect', async () => {
		let resolveToken!: (token: string) => void;
		getIdTokenMock.mockReturnValueOnce(new Promise<string>((resolve) => {
			resolveToken = resolve;
		}));
		useWebSocket(vi.fn());
		wsDisconnect();
		resolveToken('old-token');

		await vi.advanceTimersByTimeAsync(3000);
		expect(MockWebSocket.instances).toHaveLength(0);
		expect(getIdTokenMock).toHaveBeenCalledTimes(1);

		wsRegister('New user', 'new-user');
		await vi.advanceTimersByTimeAsync(0);
		expect(MockWebSocket.instances).toHaveLength(1);
		expect(MockWebSocket.instances[0].url).toContain('token=token-1');
	});

	it('does not schedule a reconnect when a pending token fails after disconnect', async () => {
		let rejectToken!: (error: Error) => void;
		getIdTokenMock.mockReturnValueOnce(new Promise<string>((_, reject) => {
			rejectToken = reject;
		}));
		useWebSocket(vi.fn());
		wsDisconnect();
		rejectToken(new Error('signed out'));

		await vi.advanceTimersByTimeAsync(6000);
		expect(getIdTokenMock).toHaveBeenCalledTimes(1);
		expect(MockWebSocket.instances).toHaveLength(0);
	});

	it('removes handler on unmount callback', async () => {
		const handler = vi.fn();
		useWebSocket(handler);
		await vi.advanceTimersByTimeAsync(0);
		const sock = MockWebSocket.instances[0];
		sock.emitOpen();

		onUnmountedCallbacks.forEach((cb) => cb());
		sock.emitMessage({ event: 'deleted', id: 'x' });
		expect(handler).not.toHaveBeenCalled();
	});
});
