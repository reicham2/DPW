import { onMounted, onUnmounted, ref } from 'vue'
import type { WsEvent } from '../types'

export function useWebSocket(onMessage: (event: WsEvent) => void) {
  const connected = ref(false)
  let ws: WebSocket | null = null
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
        onMessage(payload)
      } catch {
        // ignore malformed frames
      }
    }

    ws.onclose = () => {
      connected.value = false
      reconnectTimer = setTimeout(connect, 3000)
    }

    ws.onerror = () => {
      ws?.close()
    }
  }

  onMounted(connect)

  onUnmounted(() => {
    if (reconnectTimer) clearTimeout(reconnectTimer)
    ws?.close()
  })

  return { connected }
}
