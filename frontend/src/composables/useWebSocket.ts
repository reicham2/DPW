import { ref, onUnmounted } from 'vue';
import type { WsEvent } from '../types';

// --- Singleton state (module-scoped, one WS connection for the whole app) ---
let ws: WebSocket | null = null;
const connected = ref(false);
const handlers = new Set<(e: WsEvent) => void>();
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let pendingRegister: { display_name: string; oid: string } | null = null;
let pendingJoin: { activity_id: string } | null = null;
let wsConnectCount = 0;
let wsDisconnectCount = 0;
let wsLastError: string | null = null;
let wsMessageCount = 0;
let wsLastMessageAt: string | null = null;

function connect() {
	const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
	ws = new WebSocket(`${protocol}://${location.host}/ws`);

	ws.onopen = () => {
		connected.value = true;
		wsConnectCount++;
		// Re-send registration on reconnect
		if (pendingRegister) {
			wsSend({ type: 'register', ...pendingRegister });
		}
		// Re-join activity room on reconnect
		if (pendingJoin) {
			wsSend({ type: 'join', ...pendingJoin });
		}
	};

	ws.onmessage = (e: MessageEvent) => {
		wsMessageCount++;
		wsLastMessageAt = new Date().toISOString();
		try {
			const payload = JSON.parse(e.data as string) as WsEvent;
			handlers.forEach((h) => h(payload));
		} catch {
			// ignore malformed frames
		}
	};

	ws.onclose = () => {
		connected.value = false;
		wsDisconnectCount++;
		ws = null;
		reconnectTimer = setTimeout(connect, 3000);
	};

	ws.onerror = () => {
		wsLastError = new Date().toISOString();
		ws?.close();
	};
}

/** Send a JSON message to the WebSocket server */
export function wsSend(data: Record<string, unknown>) {
	if (ws && ws.readyState === WebSocket.OPEN) {
		ws.send(JSON.stringify(data));
	}
}

/** Register the current user identity with the WS server */
export function wsRegister(display_name: string, oid: string) {
	pendingRegister = { display_name, oid };
	wsSend({ type: 'register', display_name, oid });
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
		registered: pendingRegister !== null,
	};
}

// --- Composable: register / deregister a message handler -------------------
export function useWebSocket(onMessage: (e: WsEvent) => void) {
	// Start the singleton connection on first use
	if (!ws && !reconnectTimer) connect();

	handlers.add(onMessage);

	onUnmounted(() => {
		handlers.delete(onMessage);
	});

	return { connected };
}
