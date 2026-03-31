export interface Activity {
  id: string
  text: string
  title: string
  description: string
  responsible: string
  created_at: string
  updated_at: string
}

export type WsEvent =
  | { event: 'created'; activity: Activity }
  | { event: 'updated'; activity: Activity }
  | { event: 'deleted'; id: string }
