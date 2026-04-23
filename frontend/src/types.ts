export type Department = string;
export type UserRole = string;

export interface Program {
	id: string;
	activity_id: string;
	duration_minutes: number;
	title: string;
	description: string;
	responsible: string[];
}

export interface ProgramInput {
	duration_minutes: number;
	title: string;
	description: string;
	responsible: string[];
}

export interface MaterialItem {
	name: string;
	responsible?: string[];
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
	siko_text: string | null;
	bad_weather_info: string | null;
	planned_participants_estimate: number | null;
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
	siko_text?: string | null;
	bad_weather_info?: string | null;
	planned_participants_estimate?: number | null;
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
	| 'tpl_cc'
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
	| { event: 'notification'; notification: NotificationRecord }
	| { event: 'department_deleted'; name: string }
	| { event: 'role_deleted'; name: string }
	| { event: 'lock'; activity_id: string; section: EditSection; user: string }
	| { event: 'unlock'; activity_id: string; section: EditSection }
	| { event: 'editors'; activity_id: string; users: string[] }
	| { event: 'locks_state'; activity_id: string; locks: SectionLock[] };

export type TimeDisplayMode = 'minutes' | 'clock';

export interface User {
	id: string;
	microsoft_oid: string;
	email: string;
	display_name: string;
	department: Department | null;
	role: UserRole;
	time_display_mode: TimeDisplayMode;
	notify_material_assigned: boolean;
	notify_activity_assigned: boolean;
	notify_program_assigned: boolean;
	notify_mail_own_activity: boolean;
	notify_mail_department: boolean;
	notify_channel_websocket: boolean;
	notify_channel_email: boolean;
	created_at: string;
	updated_at: string;
}

export type NotificationCategory =
	| 'material_assigned'
	| 'activity_assigned'
	| 'program_assigned'
	| 'mail_own_activity'
	| 'mail_department';

export interface NotificationRecord {
	id: string;
	user_id: string;
	category: NotificationCategory;
	title: string;
	message: string;
	link: string | null;
	payload: Record<string, unknown>;
	is_read: boolean;
	created_at: string;
}

export interface MailTemplate {
	id: string;
	department: Department;
	subject: string;
	body: string;
	recipients: string[];
	cc: string[];
	created_at: string;
	updated_at: string;
}

export interface EventTemplate {
	id: string;
	department: Department;
	title: string;
	body: string;
	created_at: string;
	updated_at: string;
}

export interface EventPublication {
	id: string;
	activity_id: string;
	published_by: string;
	title: string;
	body_html: string;
	wp_event_id: string | null;
	published_at: string;
}

export interface SentMail {
	id: string;
	activity_id: string;
	sender_id: string;
	sender_email: string;
	to_emails: string[];
	cc_emails: string[];
	subject: string;
	body_html: string;
	sent_at: string;
}

export interface MailDraft {
	id: string;
	activity_id: string;
	recipients: string[];
	cc: string[];
	subject: string;
	body_html: string;
	updated_by: string;
	updated_at: string;
}

export interface Attachment {
	id: string;
	activity_id: string;
	filename: string;
	content_type: string;
	created_at: string;
}

export interface DepartmentRecord {
	name: string;
	color: string;
	midata_group_id?: string | null;
}

export interface LocationRecord {
	id: string;
	name: string;
	created_at: string;
	updated_at: string;
}

export interface RoleRecord {
	name: string;
	color: string;
	sort_order: number;
}

export interface RolePermission {
	role: string;
	activity_read_scope: 'none' | 'same_dept' | 'all';
	activity_create_scope: 'none' | 'own_dept' | 'all';
	activity_edit_scope: 'none' | 'own' | 'same_dept' | 'all';
	mail_send_scope: 'none' | 'own' | 'same_dept' | 'all';
	mail_templates_scope: 'none' | 'own_dept' | 'all';
	form_scope: 'none' | 'own' | 'same_dept' | 'all';
	form_templates_scope: 'none' | 'own_dept' | 'all';
	event_templates_scope: 'none' | 'own_dept' | 'all';
	event_publish_scope: 'none' | 'own_dept' | 'all';
	user_dept_scope: 'none' | 'own' | 'own_dept' | 'all';
	user_role_scope: 'none' | 'own' | 'own_dept' | 'all';
	locations_manage_scope: 'none' | 'all';
}

export interface MyPermissions extends RolePermission {
	dept_access: { department: string; can_read: boolean; can_write: boolean }[];
}

export interface RoleDeptAccess {
	role: string;
	department: string;
	can_read: boolean;
	can_write: boolean;
}

// ── Forms ─────────────────────────────────────────────────────────────────────

export type FormType = 'registration' | 'deregistration';
export type QuestionType =
	| 'section'
	| 'text_input'
	| 'single_choice'
	| 'multiple_choice'
	| 'dropdown';

export interface ChoiceOption {
	id: string;
	label: string;
}

export interface QuestionMetadata {
	// section
	subtitle?: string;
	// text_input
	multiline?: boolean;
	max_length?: number;
	// choice types
	choices?: ChoiceOption[];
}

export interface FormQuestion {
	id: string;
	form_id: string;
	question_text: string;
	question_type: QuestionType;
	position: number;
	is_required: boolean;
	metadata: QuestionMetadata;
	created_at: string;
}

export interface FormQuestionInput {
	question_text: string;
	question_type: QuestionType;
	position: number;
	is_required: boolean;
	metadata: QuestionMetadata;
}

export interface SignupForm {
	id: string;
	activity_id: string;
	public_slug: string;
	form_type: FormType;
	title: string;
	created_by: string;
	created_at: string;
	updated_at: string;
	questions: FormQuestion[];
}

export interface SignupFormInput {
	form_type: FormType;
	title: string;
	questions: FormQuestionInput[];
}

export interface FormResponseAnswer {
	question_id: string;
	answer_value: string;
}

export interface FormResponse {
	id: string;
	form_id: string;
	submission_mode: FormType;
	submitted_at: string;
	answers?: FormResponseAnswer[];
}

export interface FormSubmitPayload {
	answers: FormResponseAnswer[];
}

export interface FormTemplate {
	id: string;
	name: string;
	department: string;
	form_type: FormType;
	template_config: FormQuestionInput[];
	is_default: boolean;
	created_by: string;
	created_at: string;
	updated_at: string;
}

export interface FormStats {
	form_type: FormType;
	total: number;
	by_mode: Record<string, number>;
	registration_count?: number;
	deregistration_count?: number;
	expected_current?: number;
	questions: Record<
		string,
		{
			question_text: string;
			question_type: QuestionType;
			answers: Record<string, number>;
		}
	>;
}

export interface ActivityExpectedWeather {
	available: boolean;
	mode?: 'forecast' | 'seasonal-average' | 'frozen';
	temperature_c?: number;
	temperature_min_c?: number | null;
	temperature_max_c?: number | null;
	hourly_temps?: Array<{ ts_unix: number; temperature_c: number }>;
	hourly_rain_probability?: Array<{
		ts_unix: number;
		probability_percent: number;
	}>;
	rain_probability_percent?: number | null;
	weather_symbol?: 'sun' | 'partly-cloudy' | 'cloud' | 'rain' | 'snow' | null;
	season?: string | null;
	point_name?: string | null;
	postal_code?: string | null;
	source?: string;
	note?: string | null;
	error?: string;
}
