import { beforeEach, describe, expect, it, vi } from 'vitest';
import type {
	Activity,
	DepartmentRecord,
	MyPermissions,
	RolePermission,
	WsEvent,
} from '../types';

const { apiFetchMock, wsHandlers } = vi.hoisted(() => ({
	apiFetchMock: vi.fn(),
	wsHandlers: [] as Array<(event: WsEvent) => void>,
}));

vi.mock('./useApi', () => ({
	apiFetch: apiFetchMock,
}));

vi.mock('./useWebSocket', () => ({
	wsAddGlobalHandler: (handler: (event: WsEvent) => void) => {
		wsHandlers.push(handler);
	},
}));

import { usePermissions } from './usePermissions';

function response(data: unknown, ok = true, text = 'error'): Response {
	return {
		ok,
		json: vi.fn().mockResolvedValue(data),
		text: vi.fn().mockResolvedValue(text),
	} as unknown as Response;
}

function makePermissions(
	overrides: Partial<RolePermission> = {},
): MyPermissions {
	return {
		role: 'Mitglied',
		activity_read_scope: 'none',
		activity_create_scope: 'none',
		activity_edit_scope: 'none',
		mail_send_scope: 'none',
		mail_templates_scope: 'none',
		form_scope: 'none',
		form_templates_scope: 'none',
		event_templates_scope: 'none',
		event_publish_scope: 'none',
		user_dept_scope: 'none',
		user_role_scope: 'none',
		locations_manage_scope: 'none',
		ideenkiste_scope: 'none',
		ideenkiste_add_scope: 'none',
		ideenkiste_delete_scope: 'none',
		dept_access: [],
		...overrides,
	};
}

function makeActivity(overrides: Partial<Activity> = {}): Activity {
	return {
		id: 'a1',
		title: 'Aktivitat',
		date: '2026-06-02',
		start_time: '10:00',
		end_time: '12:00',
		goal: 'Ziel',
		location: 'Ort',
		responsible: ['user-anna-id'],
		department: 'Pfadi',
		material: [],
		tn_material: [],
		siko_text: null,
		bad_weather_info: null,
		planned_participants_estimate: null,
		created_at: '2026-01-01T00:00:00Z',
		updated_at: '2026-01-01T00:00:00Z',
		programs: [],
		...overrides,
	};
}

describe('usePermissions', () => {
	beforeEach(() => {
		apiFetchMock.mockReset();
		const p = usePermissions();
		p.departments.value = [];
		p.roles.value = [];
		p.rolePermissions.value = [];
		p.error.value = null;
		p.loading.value = false;
		p.resetMyPermissions();
	});

	it('registers a global websocket handler once on module load', () => {
		expect(wsHandlers.length).toBeGreaterThan(0);
	});

	it('applies websocket department_deleted and role_deleted events', () => {
		const p = usePermissions();
		p.departments.value = [
			{ name: 'Pfadi', color: '#f00' } as DepartmentRecord,
			{ name: 'Wolfe', color: '#0f0' } as DepartmentRecord,
		];
		p.roles.value = [
			{ name: 'Mitglied', color: '#fff', sort_order: 1 },
			{ name: 'Leitung', color: '#000', sort_order: 2 },
		];
		p.rolePermissions.value = [
			{ role: 'Mitglied' } as RolePermission,
			{ role: 'Leitung' } as RolePermission,
		];

		wsHandlers[0]({ event: 'department_deleted', name: 'Pfadi' });
		expect(p.departments.value.map((d) => d.name)).toEqual(['Wolfe']);

		wsHandlers[0]({ event: 'role_deleted', name: 'Mitglied' });
		expect(p.roles.value.map((r) => r.name)).toEqual(['Leitung']);
		expect(p.rolePermissions.value.map((rp) => rp.role)).toEqual(['Leitung']);
	});

	it('deduplicates concurrent fetchMyPermissions requests', async () => {
		const p = usePermissions();
		apiFetchMock.mockResolvedValueOnce(
			response(makePermissions({ role: 'Mitglied' })),
		);

		const [a, b] = await Promise.all([
			p.fetchMyPermissions(),
			p.fetchMyPermissions(),
		]);

		expect(apiFetchMock).toHaveBeenCalledTimes(1);
		expect(a?.role).toBe('Mitglied');
		expect(b?.role).toBe('Mitglied');
	});

	it('fetchMyPermissions stores null when backend returns payload without role', async () => {
		const p = usePermissions();
		apiFetchMock.mockResolvedValueOnce(response({ dept_access: [] }));

		const result = await p.fetchMyPermissions(true);

		expect(result).toBeNull();
		expect(p.myPermissions.value).toBeNull();
	});

	it('fetchAll sets loading flags and reports errors', async () => {
		const p = usePermissions();
		apiFetchMock.mockImplementation(async (url: string) => {
			if (url === '/api/departments') return response([], true);
			if (url === '/api/admin/roles')
				return response([], false, 'roles failed');
			if (url === '/api/admin/role-permissions') return response([], true);
			return response([], true);
		});

		await p.fetchAll();

		expect(p.loading.value).toBe(false);
		expect(p.error.value).toContain('roles failed');
	});

	it('permission helpers evaluate scope and dept access branches', () => {
		const p = usePermissions();
		p.myPermissions.value = makePermissions({
			activity_read_scope: 'same_dept',
			activity_create_scope: 'own_dept',
			form_scope: 'own',
			user_role_scope: 'all',
			user_dept_scope: 'own',
			locations_manage_scope: 'all',
			ideenkiste_scope: 'all',
			ideenkiste_add_scope: 'own_dept',
			ideenkiste_delete_scope: 'none',
			dept_access: [{ department: 'Wolfe', can_read: true, can_write: true }],
		});
		p.departments.value = [
			{ name: 'Pfadi', color: '#f00' } as DepartmentRecord,
			{ name: 'Wolfe', color: '#0f0' } as DepartmentRecord,
		];

		expect(p.canReadDept('Pfadi', 'Pfadi')).toBe(true);
		expect(p.canReadDept('Wolfe', 'Pfadi')).toBe(true);
		expect(p.canCreateDept('Pfadi', 'Pfadi')).toBe(true);
		expect(p.canCreateDept('Wolfe', 'Pfadi')).toBe(true);

		const activity = makeActivity({
			department: 'Pfadi',
			responsible: ['user-anna-id'],
		});
		expect(p.canReadActivity(activity, 'user-anna-id', 'Pfadi')).toBe(true);
		expect(p.canForms(activity, 'user-anna-id', 'Wolfe')).toBe(true);
		expect(p.canManageUsers()).toBe(true);
		expect(p.canManageSystem()).toBe(true);
		expect(p.canManageLocations()).toBe(true);
		expect(p.canIdeenkiste()).toBe(true);
		expect(p.canIdeenkisteAdd()).toBe(true);
		expect(p.canIdeenkisteDelete()).toBe(false);
		expect(p.readableDepts('Pfadi')).toEqual(['Pfadi', 'Wolfe']);
		expect(p.writableDepts('Pfadi')).toEqual(['Pfadi', 'Wolfe']);
	});
});
