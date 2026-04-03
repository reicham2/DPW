export type Department = 'Leiter' | 'Pio' | 'Pfadi' | 'Wölfe' | 'Biber'

export interface Program {
  id: string
  activity_id: string
  time: string
  title: string
  description: string
  responsible: string
}

export interface ProgramInput {
  time: string
  title: string
  description: string
  responsible: string
}

export interface Activity {
  id: string
  title: string
  date: string           // "YYYY-MM-DD"
  start_time: string     // "HH:MM"
  end_time: string       // "HH:MM"
  goal: string
  location: string
  responsible: string
  department: Department | null
  material: string[]
  needs_siko: boolean
  has_siko: boolean
  bad_weather_info: string | null
  created_at: string
  updated_at: string
  programs: Program[]
}

export interface ActivityInput {
  title: string
  date: string
  start_time: string
  end_time: string
  goal: string
  location: string
  responsible: string
  department?: Department | null
  material: string[]
  needs_siko: boolean
  siko_base64?: string
  bad_weather_info?: string | null
  programs: ProgramInput[]
}

export type WsEvent =
  | { event: 'created'; activity: Activity }
  | { event: 'updated'; activity: Activity }
  | { event: 'deleted'; id: string }
