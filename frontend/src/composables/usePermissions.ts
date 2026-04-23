import { ref } from 'vue';
import { apiFetch } from './useApi';
import { wsAddGlobalHandler } from './useWebSocket';
import type {
	DepartmentRecord,
	LocationRecord,
	RoleRecord,
	RolePermission,
	RoleDeptAccess,
	MyPermissions,
	Activity,
	WsEvent,
} from '../types';

const departments = ref<DepartmentRecord[]>([]);
const roles = ref<RoleRecord[]>([]);
const rolePermissions = ref<RolePermission[]>([]);
const myPermissions = ref<MyPermissions | null>(null);
const loading = ref(false);
const error = ref<string | null>(null);
let myPermissionsRequest: Promise<MyPermissions | null> | null = null;

// ── Live updates via WebSocket ──────────────────────────────────────────────

wsAddGlobalHandler((event: WsEvent) => {
	if (event.event === 'department_deleted') {
		departments.value = departments.value.filter((d) => d.name !== event.name);
	} else if (event.event === 'role_deleted') {
		roles.value = roles.value.filter((r) => r.name !== event.name);
		rolePermissions.value = rolePermissions.value.filter(
			(p) => p.role !== event.name,
		);
	}
});

// ── Departments CRUD ────────────────────────────────────────────────────────

async function fetchDepartments() {
	const res = await apiFetch('/api/departments');
	if (!res.ok) throw new Error(await res.text());
	departments.value = await res.json();
}

async function createDepartment(dept: Partial<DepartmentRecord>) {
	const res = await apiFetch('/api/admin/departments', {
		method: 'POST',
		body: JSON.stringify(dept),
	});
	if (!res.ok) throw new Error(await res.text());
	return (await res.json()) as DepartmentRecord;
}

async function updateDepartment(
	oldName: string,
	dept: Partial<DepartmentRecord>,
) {
	const res = await apiFetch(
		`/api/admin/departments/${encodeURIComponent(oldName)}`,
		{
			method: 'PATCH',
			body: JSON.stringify(dept),
		},
	);
	if (!res.ok) throw new Error(await res.text());
	return (await res.json()) as DepartmentRecord;
}

async function deleteDepartment(name: string) {
	const res = await apiFetch(
		`/api/admin/departments/${encodeURIComponent(name)}`,
		{ method: 'DELETE' },
	);
	if (!res.ok) throw new Error(await res.text());
}

// ── Roles CRUD ──────────────────────────────────────────────────────────────

async function fetchRoles() {
	const res = await apiFetch('/api/admin/roles');
	if (!res.ok) throw new Error(await res.text());
	roles.value = await res.json();
}

async function createRole(role: Partial<RoleRecord>) {
	const res = await apiFetch('/api/admin/roles', {
		method: 'POST',
		body: JSON.stringify(role),
	});
	if (!res.ok) throw new Error(await res.text());
	return (await res.json()) as RoleRecord;
}

async function updateRole(oldName: string, role: Partial<RoleRecord>) {
	const res = await apiFetch(
		`/api/admin/roles/${encodeURIComponent(oldName)}`,
		{
			method: 'PATCH',
			body: JSON.stringify(role),
		},
	);
	if (!res.ok) throw new Error(await res.text());
	return (await res.json()) as RoleRecord;
}

async function deleteRole(name: string) {
	const res = await apiFetch(`/api/admin/roles/${encodeURIComponent(name)}`, {
		method: 'DELETE',
	});
	if (!res.ok) throw new Error(await res.text());
}

async function moveRole(name: string, direction: 'up' | 'down') {
	const res = await apiFetch(
		`/api/admin/roles/${encodeURIComponent(name)}/move`,
		{
			method: 'POST',
			body: JSON.stringify({ direction }),
		},
	);
	if (!res.ok) throw new Error(await res.text());
}

async function reorderRoles(order: string[]) {
	const res = await apiFetch('/api/admin/roles/reorder', {
		method: 'POST',
		body: JSON.stringify({ order }),
	});
	if (!res.ok) throw new Error(await res.text());
}

// ── Role Permissions ────────────────────────────────────────────────────────

async function fetchRolePermissions() {
	const res = await apiFetch('/api/admin/role-permissions');
	if (!res.ok) throw new Error(await res.text());
	rolePermissions.value = await res.json();
}

async function updateRolePermission(perm: RolePermission) {
	const res = await apiFetch('/api/admin/role-permissions', {
		method: 'PUT',
		body: JSON.stringify(perm),
	});
	if (!res.ok) throw new Error(await res.text());
}

// ── Role Department Access ──────────────────────────────────────────────────

async function fetchRoleDeptAccess(role: string): Promise<RoleDeptAccess[]> {
	const res = await apiFetch(
		`/api/admin/role-dept-access?role=${encodeURIComponent(role)}`,
	);
	if (!res.ok) throw new Error(await res.text());
	return await res.json();
}

async function updateRoleDeptAccess(access: RoleDeptAccess) {
	const res = await apiFetch('/api/admin/role-dept-access', {
		method: 'PUT',
		body: JSON.stringify(access),
	});
	if (!res.ok) throw new Error(await res.text());
}

// ── Current user permissions ────────────────────────────────────────────────

function resetMyPermissions() {
	myPermissions.value = null;
	myPermissionsRequest = null;
}

async function fetchMyPermissions(force = false) {
	if (!force && myPermissions.value) return myPermissions.value;
	if (!force && myPermissionsRequest) return myPermissionsRequest;

	myPermissionsRequest = (async () => {
		const res = await apiFetch('/api/my-permissions');
		if (!res.ok) throw new Error(await res.text());
		const data = await res.json();
		myPermissions.value = data.role ? data : null;
		return myPermissions.value;
	})();

	try {
		return await myPermissionsRequest;
	} finally {
		myPermissionsRequest = null;
	}
}

// ── Permission helpers ──────────────────────────────────────────────────────

function canReadDept(dept: string, userDept?: string | null): boolean {
	const p = myPermissions.value;
	if (!p) return false;
	if (p.activity_read_scope === 'all') return true;
	if (p.activity_read_scope === 'same_dept' && userDept && userDept === dept)
		return true;
	return (
		p.dept_access?.some((d) => d.department === dept && d.can_read) ?? false
	);
}

function canCreateDept(dept: string, userDept?: string | null): boolean {
	const p = myPermissions.value;
	if (!p) return false;
	if (p.activity_create_scope === 'all') return true;
	if (p.activity_create_scope === 'own_dept' && userDept && userDept === dept)
		return true;
	return (
		p.dept_access?.some((d) => d.department === dept && d.can_write) ?? false
	);
}

function canReadActivity(
	activity: Activity,
	userDisplayName?: string | null,
	userDept?: string | null,
): boolean {
	const p = myPermissions.value;
	if (!p) return false;
	if (p.activity_read_scope === 'all') return true;
	if (
		p.activity_read_scope === 'same_dept' &&
		activity.department &&
		userDept &&
		activity.department === userDept
	) {
		return true;
	}
	if (activity.department) {
		return (
			p.dept_access?.some(
				(d) => d.department === activity.department && d.can_read,
			) ?? false
		);
	}
	return p.activity_read_scope !== 'none';
}

function canManageUsers(): boolean {
	const p = myPermissions.value;
	if (!p) return false;
	return p.user_dept_scope !== 'none' || p.user_role_scope !== 'none';
}

function canManageSystem(): boolean {
	const p = myPermissions.value;
	if (!p) return false;
	return p.user_role_scope === 'all';
}

function canForms(
	activity: Activity,
	userDisplayName?: string | null,
	userDept?: string | null,
): boolean {
	const p = myPermissions.value;
	if (!p) return false;
	if (p.form_scope === 'all') return true;
	if (
		p.form_scope === 'same_dept' &&
		activity.department &&
		userDept &&
		activity.department === userDept
	)
		return true;
	if (
		p.form_scope === 'own' &&
		userDisplayName &&
		activity.responsible.includes(userDisplayName)
	)
		return true;
	return false;
}

function canFormTemplates(): boolean {
	const p = myPermissions.value;
	if (!p) return false;
	return p.form_templates_scope !== 'none';
}

function canEventTemplates(): boolean {
	const p = myPermissions.value;
	if (!p) return false;
	return p.event_templates_scope !== 'none';
}

function canPublishEvents(): boolean {
	const p = myPermissions.value;
	if (!p) return false;
	return p.event_publish_scope !== 'none';
}

function readableDepts(userDept: string | null | undefined): string[] {
	const p = myPermissions.value;
	return departments.value
		.map((d) => d.name)
		.filter((name) => canReadDept(name, userDept));
}

function writableDepts(userDept: string | null | undefined): string[] {
	return departments.value
		.map((d) => d.name)
		.filter((name) => canCreateDept(name, userDept));
}

function canManageLocations(): boolean {
	return myPermissions.value?.locations_manage_scope === 'all';
}

// ── Locations CRUD ──────────────────────────────────────────────────────────

async function fetchLocationsAdmin(): Promise<LocationRecord[]> {
	const token = await getIdToken();
	const res = await fetch('/api/admin/locations', {
		headers: { Authorization: `Bearer ${token}` },
	});
	if (!res.ok) throw new Error(await res.text());
	return await res.json();
}

async function createLocation(name: string): Promise<LocationRecord> {
	const token = await getIdToken();
	const res = await fetch('/api/admin/locations', {
		method: 'POST',
		headers: {
			'Content-Type': 'application/json',
			Authorization: `Bearer ${token}`,
		},
		body: JSON.stringify({ name }),
	});
	if (!res.ok) throw new Error(await res.text());
	return await res.json();
}

async function updateLocation(
	id: string,
	name: string,
): Promise<LocationRecord> {
	const token = await getIdToken();
	const res = await fetch(`/api/admin/locations/${encodeURIComponent(id)}`, {
		method: 'PATCH',
		headers: {
			'Content-Type': 'application/json',
			Authorization: `Bearer ${token}`,
		},
		body: JSON.stringify({ name }),
	});
	if (!res.ok) throw new Error(await res.text());
	return await res.json();
}

async function deleteLocation(id: string): Promise<void> {
	const token = await getIdToken();
	const res = await fetch(`/api/admin/locations/${encodeURIComponent(id)}`, {
		method: 'DELETE',
		headers: { Authorization: `Bearer ${token}` },
	});
	if (!res.ok) throw new Error(await res.text());
}

// ── Fetch all ───────────────────────────────────────────────────────────────

async function fetchAll() {
	loading.value = true;
	error.value = null;
	try {
		await Promise.all([
			fetchDepartments(),
			fetchRoles(),
			fetchRolePermissions(),
		]);
	} catch (e) {
		error.value = String(e);
	} finally {
		loading.value = false;
	}
}

export function usePermissions() {
	return {
		departments,
		roles,
		rolePermissions,
		myPermissions,
		loading,
		error,
		fetchAll,
		fetchDepartments,
		fetchRoles,
		fetchRolePermissions,
		fetchMyPermissions,
		resetMyPermissions,
		createDepartment,
		updateDepartment,
		deleteDepartment,
		createRole,
		updateRole,
		moveRole,
		reorderRoles,
		deleteRole,
		updateRolePermission,
		fetchRoleDeptAccess,
		updateRoleDeptAccess,
		canReadDept,
		canCreateDept,
		canReadActivity,
		canManageUsers,
		canManageSystem,
		canForms,
		canFormTemplates,
		canEventTemplates,
		canPublishEvents,
		canManageLocations,
		readableDepts,
		writableDepts,
		fetchLocationsAdmin,
		createLocation,
		updateLocation,
		deleteLocation,
	};
}
