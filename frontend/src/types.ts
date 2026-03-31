export interface Activity {
  id: string
  text: string
  created_at: string
  updated_at: string
}

export type WsEvent =
  | { event: 'created'; activity: Activity }
  | { event: 'updated'; activity: Activity }
  | { event: 'deleted'; id: string }
