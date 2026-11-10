import { ref, onUnmounted } from 'vue';
import type { WsEvent } from '../types';
import { getIdToken, user } from './useAuth';

// --- Singleton state (module-scoped, one WS connection for the whole app) ---
let ws: WebSocket | null = null;
const connected = ref(false);
const handlers = new Set<(e: WsEvent) => void>();
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let pendingJoin: { activity_id: string } | null = null;
let wsConnectCount = 0;
let wsDisconnectCount = 0;
let wsLastError: string | null = null;
let wsMessageCount = 0;
let wsLastMessageAt: string | null = null;
let connecting = false;
let connectionVersion = 0;

function scheduleReconnect() {
	if (!user.value || reconnectTimer) return;
	reconnectTimer = setTimeout(() => {
		reconnectTimer = null;
		void connect();
	}, 3000);
}

async function connect() {
	if (!user.value || ws || connecting) return;
	connecting = true;
	const version = connectionVersion;
	const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
	let socket: WebSocket;
	try {
		const token = await getIdToken({ allowInteraction: false });
		if (version !== connectionVersion) return;
		socket = new WebSocket(
			`${protocol}://${location.host}/ws?token=${encodeURIComponent(token)}`,
		);
		ws = socket;
	} catch {
		if (version !== connectionVersion) return;
		connecting = false;
		wsLastError = new Date().toISOString();
		scheduleReconnect();
		return;
	}

	socket.onopen = () => {
		if (version !== connectionVersion || ws !== socket) return;
		connecting = false;
		connected.value = true;
		wsConnectCount++;
		if (pendingJoin) {
			wsSend({ type: 'join', ...pendingJoin });
		}
	};

	socket.onmessage = (e: MessageEvent) => {
		if (version !== connectionVersion || ws !== socket) return;
		wsMessageCount++;
		wsLastMessageAt = new Date().toISOString();
		try {
			const payload = JSON.parse(e.data as string) as WsEvent;
			handlers.forEach((h) => h(payload));
		} catch {
			// ignore malformed frames
		}
	};

	socket.onclose = () => {
		if (version !== connectionVersion || ws !== socket) return;
		connecting = false;
		connected.value = false;
		wsDisconnectCount++;
		ws = null;
		scheduleReconnect();
	};

	socket.onerror = () => {
		if (version !== connectionVersion || ws !== socket) return;
		wsLastError = new Date().toISOString();
		socket.close();
	};
}

/** Send a JSON message to the WebSocket server */
export function wsSend(data: Record<string, unknown>) {
	if (ws && ws.readyState === WebSocket.OPEN) {
		ws.send(JSON.stringify(data));
	}
}

/** Register the current user identity with the WS server */
export function wsRegister(_display_name: string, _oid: string) {
	if (user.value && !ws && !connecting) {
		void connect();
	}
}

/** Join an activity/template room for collaborative editing (survives reconnects) */
export function wsJoin(activity_id: string) {
	pendingJoin = { activity_id };
	wsSend({ type: 'join', activity_id });
}

/** Leave the current activity/template room */
export function wsLeave() {
	if (pendingJoin) {
		wsSend({ type: 'leave', activity_id: pendingJoin.activity_id });
	}
	pendingJoin = null;
}

export function createGuestWebSocket(onMessage: (event: WsEvent) => void) {
	const sessionId = typeof crypto.randomUUID === 'function'
		? crypto.randomUUID()
		: (() => {
			const bytes = crypto.getRandomValues(new Uint8Array(16));
			bytes[6] = (bytes[6] & 0x0f) | 0x40;
			bytes[8] = (bytes[8] & 0x3f) | 0x80;
			const hex = Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
			return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
		})();
	let socket: WebSocket | null = null;
	let guestToken = '';
	let activityId = '';
	let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
	let stopped = false;

	function connect() {
		if (!guestToken || !activityId || socket || stopped) return;
		const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
		const current = new WebSocket(`${protocol}://${location.host}/ws?guest=1`);
		socket = current;

		current.onopen = () => {
			if (socket !== current) return;
			current.send(JSON.stringify({ type: 'register_guest', token: guestToken, session_id: sessionId }));
			current.send(JSON.stringify({ type: 'join', activity_id: activityId }));
		};
		current.onmessage = (event: MessageEvent) => {
			if (socket !== current) return;
			try {
				onMessage(JSON.parse(event.data as string) as WsEvent);
			} catch {
				// Ignore malformed frames.
			}
		};
		current.onclose = () => {
			if (socket !== current) return;
			socket = null;
			if (guestToken && activityId && !stopped && !reconnectTimer) {
				reconnectTimer = setTimeout(() => {
					reconnectTimer = null;
					connect();
				}, 3000);
			}
		};
		current.onerror = () => current.close();
	}

	function send(message: Record<string, unknown>) {
		if (socket?.readyState === WebSocket.OPEN) {
			socket.send(JSON.stringify(message));
		}
	}

	return {
		sessionId,
		join(token: string, activity: string) {
			guestToken = token;
			activityId = activity;
			stopped = false;
			if (socket?.readyState === WebSocket.OPEN) {
				send({ type: 'join', activity_id: activityId });
			} else {
				connect();
			}
		},
		send,
		disconnect() {
			stopped = true;
			guestToken = '';
			activityId = '';
			if (reconnectTimer) {
				clearTimeout(reconnectTimer);
				reconnectTimer = null;
			}
			if (socket) {
				const current = socket;
				socket = null;
				current.onclose = null;
				current.close();
			}
		},
	};
}

/** Expose WS debug state for bug reports */
export function getWsDebugState() {
	return {
		connected: connected.value,
		readyState: ws?.readyState ?? null,
		connectCount: wsConnectCount,
		disconnectCount: wsDisconnectCount,
		messageCount: wsMessageCount,
		lastMessageAt: wsLastMessageAt,
		lastError: wsLastError,
		registered: connected.value,
	};
}

/** Forcefully close the WebSocket and stop reconnection. */
export function wsDisconnect() {
	connectionVersion++;
	if (reconnectTimer) {
		clearTimeout(reconnectTimer);
		reconnectTimer = null;
	}
	pendingJoin = null;
	if (ws) {
		ws.onclose = null;
		ws.close();
		ws = null;
	}
	connecting = false;
	connected.value = false;
}

// --- Composable: register / deregister a message handler -------------------
export function useWebSocket(onMessage: (e: WsEvent) => void) {
	// Start the singleton connection on first use
	if (user.value && !ws && !reconnectTimer && !connecting) void connect();

	handlers.add(onMessage);

	onUnmounted(() => {
		handlers.delete(onMessage);
	});

	return { connected };
}

/** Register a persistent global handler (not tied to component lifecycle). */
export function wsAddGlobalHandler(onMessage: (e: WsEvent) => void) {
	handlers.add(onMessage);
}
