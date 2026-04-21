import { describe, it, expect } from 'vitest';
import type {
	Activity,
	ActivityInput,
	MaterialItem,
	Program,
	User,
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
			notify_mail_own_activity: true,
			notify_mail_department: false,
			notify_channel_websocket: true,
			notify_channel_email: false,
			created_at: '2025-01-01T00:00:00Z',
			updated_at: '2025-01-01T00:00:00Z',
		};
		expect(['minutes', 'clock']).toContain(user.time_display_mode);
	});
});
