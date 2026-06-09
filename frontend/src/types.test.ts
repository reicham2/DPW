import { describe, it, expect } from 'vitest';
import type {
	Activity,
	ActivityInput,
	FormResponse,
	NotificationRecord,
	MaterialItem,
	Program,
	RolePermission,
	User,
	WsEvent,
} from './types';

/**
 * Tests that type definitions are consistent — catches structural regressions
 * when fields are added/removed/renamed.
 */

function makeActivity(): Activity {
	return {
		id: 'uuid-1',
		title: 'Pfadilager',
		date: '2025-07-15',
		start_time: '09:00',
		end_time: '17:00',
		goal: 'Spass haben',
		location: 'Wald',
		responsible: ['user-1', 'user-2'],
		department: 'Pfadi',
		material: [{ name: 'Seil', responsible: ['user-1'] }],
		siko_text: 'Alles sicher',
		bad_weather_info: 'Drinnen',
		planned_participants_estimate: 24,
		created_at: '2025-01-01T00:00:00Z',
		updated_at: '2025-01-01T00:00:00Z',
		programs: [
			{
				id: 'prog-1',
				activity_id: 'uuid-1',
				duration_minutes: 60,
				title: 'Einstieg',
				description: 'Begrüssung',
				responsible: ['user-1'],
			},
		],
	};
}

describe('Type structures', () => {
	it('Activity has all required fields', () => {
		const act = makeActivity();
		expect(act).toHaveProperty('id');
		expect(act).toHaveProperty('title');
		expect(act).toHaveProperty('date');
		expect(act).toHaveProperty('start_time');
		expect(act).toHaveProperty('end_time');
		expect(act).toHaveProperty('goal');
		expect(act).toHaveProperty('location');
		expect(act).toHaveProperty('responsible');
		expect(act).toHaveProperty('department');
		expect(act).toHaveProperty('material');
		expect(act).toHaveProperty('planned_participants_estimate');
		expect(act).toHaveProperty('programs');
		expect(act).toHaveProperty('created_at');
		expect(act).toHaveProperty('updated_at');
	});

	it('Activity.programs contains valid Program objects', () => {
		const act = makeActivity();
		expect(act.programs).toHaveLength(1);
		const prog: Program = act.programs[0];
		expect(prog.duration_minutes).toBeGreaterThan(0);
		expect(prog.activity_id).toBe(act.id);
	});

	it('MaterialItem.responsible is optional', () => {
		const withResp: MaterialItem = { name: 'Seil', responsible: ['user-1'] };
		const withoutResp: MaterialItem = { name: 'Karte' };
		expect(withResp.responsible).toBeDefined();
		expect(withoutResp.responsible).toBeUndefined();
	});

	it('ActivityInput allows optional fields', () => {
		const input: ActivityInput = {
			title: 'Test',
			date: '2025-06-01',
			start_time: '10:00',
			end_time: '12:00',
			goal: 'Ziel',
			location: 'Ort',
			responsible: [],
			material: [],
			programs: [],
		};
		// Optional fields should be omittable
		expect(input.department).toBeUndefined();
		expect(input.siko_text).toBeUndefined();
		expect(input.bad_weather_info).toBeUndefined();
	});

	it('ActivityInput also allows nullable optional fields', () => {
		const input: ActivityInput = {
			title: 'Test',
			date: '2025-06-01',
			start_time: '10:00',
			end_time: '12:00',
			goal: 'Ziel',
			location: 'Ort',
			responsible: [],
			department: null,
			siko_text: null,
			bad_weather_info: null,
			planned_participants_estimate: null,
			material: [],
			tn_material: [],
			programs: [],
		};
		expect(input.department).toBeNull();
		expect(input.siko_text).toBeNull();
		expect(input.bad_weather_info).toBeNull();
		expect(input.planned_participants_estimate).toBeNull();
	});

	it('User has time_display_mode field', () => {
		const user: User = {
			id: 'u-1',
			microsoft_oid: 'oid-1',
			email: 'test@example.com',
			display_name: 'Test User',
			department: 'Pfadi',
			role: 'Mitglied',
			time_display_mode: 'clock',
			notify_material_assigned: true,
			notify_activity_assigned: true,
			notify_program_assigned: true,
			notify_mail_own_activity: true,
			notify_mail_department: false,
			notify_channel_websocket: true,
			notify_channel_email: false,
			created_at: '2025-01-01T00:00:00Z',
			updated_at: '2025-01-01T00:00:00Z',
		};
		expect(['minutes', 'clock']).toContain(user.time_display_mode);
	});

	it('WsEvent supports lock and maintenance_status variants', () => {
		const lockEvent: WsEvent = {
			event: 'lock',
			activity_id: 'a1',
			section: 'title',
			user: 'u1',
		};
		const maintenanceEvent: WsEvent = {
			event: 'maintenance_status',
			active: true,
			enabled: true,
			scheduled_now: false,
			message: 'Wartung',
			scheduled_start: '2026-01-01T10:00:00Z',
			scheduled_end: '2026-01-01T12:00:00Z',
		};

		expect(lockEvent.event).toBe('lock');
		expect(maintenanceEvent.event).toBe('maintenance_status');
	});

	it('NotificationRecord payload is arbitrary object data', () => {
		const n: NotificationRecord = {
			id: 'n1',
			user_id: 'u1',
			category: 'material_assigned',
			title: 'Material',
			message: 'Du wurdest eingeteilt',
			link: '/activities/a1',
			payload: { activity_id: 'a1', nested: { level: 2 } },
			is_read: false,
			created_at: '2026-01-01T00:00:00Z',
		};

		expect(n.payload).toHaveProperty('activity_id', 'a1');
		expect(n.payload).toHaveProperty('nested');
	});

	it('RolePermission accepts all documented scope literals', () => {
		const p: RolePermission = {
			role: 'Mitglied',
			activity_read_scope: 'same_dept',
			activity_create_scope: 'own_dept',
			activity_edit_scope: 'own',
			mail_send_scope: 'all',
			mail_templates_scope: 'all',
			form_scope: 'same_dept',
			form_templates_scope: 'own_dept',
			event_templates_scope: 'all',
			event_publish_scope: 'own_dept',
			user_dept_scope: 'own',
			user_role_scope: 'own_dept',
			locations_manage_scope: 'all',
			ideenkiste_scope: 'all',
			ideenkiste_add_scope: 'own_dept',
			ideenkiste_delete_scope: 'none',
		};

		expect(p.activity_edit_scope).toBe('own');
		expect(p.locations_manage_scope).toBe('all');
	});

	it('FormResponse answers field is optional', () => {
		const withAnswers: FormResponse = {
			id: 'r1',
			form_id: 'f1',
			submission_mode: 'registration',
			submitted_at: '2026-01-01T00:00:00Z',
			answers: [{ question_id: 'q1', answer_value: 'Ja' }],
		};
		const withoutAnswers: FormResponse = {
			id: 'r2',
			form_id: 'f1',
			submission_mode: 'deregistration',
			submitted_at: '2026-01-01T00:00:00Z',
		};

		expect(withAnswers.answers).toHaveLength(1);
		expect(withoutAnswers.answers).toBeUndefined();
	});
});
