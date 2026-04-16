import { ref } from 'vue';
import { getIdToken } from './useAuth';
import type {
	DepartmentRecord,
	RoleRecord,
	RolePermission,
	RoleDeptAccess,
	MyPermissions,
	Activity,
} from '../types';

const departments = ref<DepartmentRecord[]>([]);
const roles = ref<RoleRecord[]>([]);
const rolePermissions = ref<RolePermission[]>([]);
const myPermissions = ref<MyPermissions | null>(null);
const loading = ref(false);
const error = ref<string | null>(null);

// ── Departments CRUD ────────────────────────────────────────────────────────

async function fetchDepartments() {
	const res = await fetch('/api/departments');
	if (!res.ok) throw new Error(await res.text());
	departments.value = await res.json();
}

async function createDepartment(dept: Partial<DepartmentRecord>) {
	const token = await getIdToken();
	const res = await fetch('/api/admin/departments', {
		method: 'POST',
		headers: {
			'Content-Type': 'application/json',
			Authorization: `Bearer ${token}`,
		},
		body: JSON.stringify(dept),
	});
	if (!res.ok) throw new Error(await res.text());
	return (await res.json()) as DepartmentRecord;
}

async function updateDepartment(
	oldName: string,
	dept: Partial<DepartmentRecord>,
) {
	const token = await getIdToken();
	const res = await fetch(
		`/api/admin/departments/${encodeURIComponent(oldName)}`,
		{
			method: 'PATCH',
			headers: {
				'Content-Type': 'application/json',
				Authorization: `Bearer ${token}`,
			},
			body: JSON.stringify(dept),
		},
	);
	if (!res.ok) throw new Error(await res.text());
	return (await res.json()) as DepartmentRecord;
}

async function deleteDepartment(name: string) {
	const token = await getIdToken();
	const res = await fetch(
		`/api/admin/departments/${encodeURIComponent(name)}`,
		{
			method: 'DELETE',
			headers: { Authorization: `Bearer ${token}` },
		},
	);
	if (!res.ok) throw new Error(await res.text());
}

// ── Roles CRUD ──────────────────────────────────────────────────────────────

async function fetchRoles() {
	const token = await getIdToken();
	const res = await fetch('/api/admin/roles', {
		headers: { Authorization: `Bearer ${token}` },
	});
	if (!res.ok) throw new Error(await res.text());
	roles.value = await res.json();
}

async function createRole(role: Partial<RoleRecord>) {
	const token = await getIdToken();
	const res = await fetch('/api/admin/roles', {
		method: 'POST',
		headers: {
			'Content-Type': 'application/json',
			Authorization: `Bearer ${token}`,
		},
		body: JSON.stringify(role),
	});
	if (!res.ok) throw new Error(await res.text());
	return (await res.json()) as RoleRecord;
}

async function updateRole(oldName: string, role: Partial<RoleRecord>) {
	const token = await getIdToken();
	const res = await fetch(`/api/admin/roles/${encodeURIComponent(oldName)}`, {
		method: 'PATCH',
		headers: {
			'Content-Type': 'application/json',
			Authorization: `Bearer ${token}`,
		},
		body: JSON.stringify(role),
	});
	if (!res.ok) throw new Error(await res.text());
	return (await res.json()) as RoleRecord;
}

async function deleteRole(name: string) {
	const token = await getIdToken();
	const res = await fetch(`/api/admin/roles/${encodeURIComponent(name)}`, {
		method: 'DELETE',
		headers: { Authorization: `Bearer ${token}` },
	});
	if (!res.ok) throw new Error(await res.text());
}

async function moveRole(name: string, direction: 'up' | 'down') {
	const token = await getIdToken();
	const res = await fetch(`/api/admin/roles/${encodeURIComponent(name)}/move`, {
		method: 'POST',
		headers: {
			'Content-Type': 'application/json',
			Authorization: `Bearer ${token}`,
		},
		body: JSON.stringify({ direction }),
	});
	if (!res.ok) throw new Error(await res.text());
}

async function reorderRoles(order: string[]) {
	const token = await getIdToken();
	const res = await fetch('/api/admin/roles/reorder', {
		method: 'POST',
		headers: {
			'Content-Type': 'application/json',
			Authorization: `Bearer ${token}`,
		},
		body: JSON.stringify({ order }),
	});
	if (!res.ok) throw new Error(await res.text());
}

// ── Role Permissions ────────────────────────────────────────────────────────

async function fetchRolePermissions() {
	const token = await getIdToken();
	const res = await fetch('/api/admin/role-permissions', {
		headers: { Authorization: `Bearer ${token}` },
	});
	if (!res.ok) throw new Error(await res.text());
	rolePermissions.value = await res.json();
}

async function updateRolePermission(perm: RolePermission) {
	const token = await getIdToken();
	const res = await fetch('/api/admin/role-permissions', {
		method: 'PUT',
		headers: {
			'Content-Type': 'application/json',
			Authorization: `Bearer ${token}`,
		},
		body: JSON.stringify(perm),
	});
	if (!res.ok) throw new Error(await res.text());
}

// ── Role Department Access ──────────────────────────────────────────────────

async function fetchRoleDeptAccess(role: string): Promise<RoleDeptAccess[]> {
	const token = await getIdToken();
	const res = await fetch(
		`/api/admin/role-dept-access?role=${encodeURIComponent(role)}`,
		{
			headers: { Authorization: `Bearer ${token}` },
		},
	);
	if (!res.ok) throw new Error(await res.text());
	return await res.json();
}

async function updateRoleDeptAccess(access: RoleDeptAccess) {
	const token = await getIdToken();
	const res = await fetch('/api/admin/role-dept-access', {
		method: 'PUT',
		headers: {
			'Content-Type': 'application/json',
			Authorization: `Bearer ${token}`,
		},
		body: JSON.stringify(access),
	});
	if (!res.ok) throw new Error(await res.text());
}

// ── Current user permissions ────────────────────────────────────────────────

async function fetchMyPermissions() {
	const token = await getIdToken();
	const res = await fetch('/api/my-permissions', {
		headers: { Authorization: `Bearer ${token}` },
	});
	if (!res.ok) throw new Error(await res.text());
	const data = await res.json();
	myPermissions.value = data.role ? data : null;
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
		readableDepts,
		writableDepts,
	};
}
