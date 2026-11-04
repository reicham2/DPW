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
