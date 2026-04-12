export type Department = 'Leiter' | 'Pio' | 'Pfadi' | 'Wölfe' | 'Biber';
export type UserRole = 'admin' | 'Stufenleiter' | 'Leiter' | 'Pio';

export interface Program {
	id: string;
	activity_id: string;
	time: string;
	title: string;
	description: string;
	responsible: string;
}

export interface ProgramInput {
	time: string;
	title: string;
	description: string;
	responsible: string;
}

export interface MaterialItem {
	name: string;
	responsible?: string;
}

export interface Activity {
	id: string;
	title: string;
	date: string; // "YYYY-MM-DD"
	start_time: string; // "HH:MM"
	end_time: string; // "HH:MM"
	goal: string;
	location: string;
	responsible: string[];
	department: Department | null;
	material: MaterialItem[];
	needs_siko: boolean;
	has_siko: boolean;
	bad_weather_info: string | null;
	created_at: string;
	updated_at: string;
	programs: Program[];
}

export interface ActivityInput {
	title: string;
	date: string;
	start_time: string;
	end_time: string;
	goal: string;
	location: string;
	responsible: string[];
	department?: Department | null;
	material: MaterialItem[];
	needs_siko: boolean;
	siko_base64?: string;
	bad_weather_info?: string | null;
	programs: ProgramInput[];
}

export type EditSection =
	| 'title'
	| 'datetime'
	| 'location'
	| `program_${number}`
	| `material_${number}`
	| 'siko'
	| 'goal_weather'
	| 'tpl_recipients'
	| 'tpl_subject'
	| 'tpl_body';

export interface SectionLock {
	section: EditSection;
	user: string;
}

export type WsEvent =
	| { event: 'created'; activity: Activity }
	| { event: 'updated'; activity: Activity }
	| { event: 'deleted'; id: string }
	| { event: 'template_updated'; template: MailTemplate }
	| { event: 'lock'; activity_id: string; section: EditSection; user: string }
	| { event: 'unlock'; activity_id: string; section: EditSection }
	| { event: 'editors'; activity_id: string; users: string[] }
	| { event: 'locks_state'; activity_id: string; locks: SectionLock[] };

export interface User {
	id: string;
	microsoft_oid: string;
	email: string;
	display_name: string;
	department: Department | null;
	role: UserRole;
	created_at: string;
	updated_at: string;
}

export interface MailTemplate {
	id: string;
	department: Department;
	subject: string;
	body: string;
	recipients: string[];
	created_at: string;
	updated_at: string;
}

export interface SentMail {
	id: string;
	activity_id: string;
	sender_id: string;
	sender_email: string;
	to_emails: string[];
	subject: string;
	body_html: string;
	sent_at: string;
}
