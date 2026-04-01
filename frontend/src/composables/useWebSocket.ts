import { ref, onUnmounted } from 'vue'
import type { WsEvent } from '../types'

// --- Singleton state (module-scoped, one WS connection for the whole app) ---
let ws: WebSocket | null = null
const connected = ref(false)
const handlers = new Set<(e: WsEvent) => void>()
let reconnectTimer: ReturnType<typeof setTimeout> | null = null

function connect() {
  const protocol = location.protocol === 'https:' ? 'wss' : 'ws'
  ws = new WebSocket(`${protocol}://${location.host}/ws`)

  ws.onopen = () => {
    connected.value = true
  }

  ws.onmessage = (e: MessageEvent) => {
    try {
      const payload = JSON.parse(e.data as string) as WsEvent
      handlers.forEach(h => h(payload))
    } catch {
      // ignore malformed frames
    }
  }

  ws.onclose = () => {
    connected.value = false
    ws = null
    reconnectTimer = setTimeout(connect, 3000)
  }

  ws.onerror = () => {
    ws?.close()
  }
}

// --- Composable: register / deregister a message handler -------------------
export function useWebSocket(onMessage: (e: WsEvent) => void) {
  // Start the singleton connection on first use
  if (!ws && !reconnectTimer) connect()

  handlers.add(onMessage)

  onUnmounted(() => {
    handlers.delete(onMessage)
  })

  return { connected }
}
