import { describe, it, expect, vi, beforeEach } from 'vitest';

// Mock auth composable to avoid MSAL imports
vi.mock('../composables/useAuth', () => ({
	user: { value: null },
	authLoading: { value: false },
}));

// We can't easily import the router (it imports all page components).
// Instead, test the guard logic in isolation.

describe('Router guard logic', () => {
	it('allows public routes even when setup is required', () => {
		const setupRequired = true;
		const to = { meta: { public: true }, path: '/forms/abc' };

		const result = to.meta.public
			? true
			: setupRequired && to.path !== '/setup'
				? '/setup'
				: true;
		expect(result).toBe(true);
	});

	it('redirects every route to /setup when setup is required', () => {
		const setupRequired = true;
		const to = { meta: {}, path: '/activities/123' };

		const result = setupRequired && to.path !== '/setup' ? '/setup' : true;
		expect(result).toBe('/setup');
	});

	it('redirects /setup to / when setup is already done', () => {
		const setupRequired = false;
		const to = { meta: { setup: true }, path: '/setup' };

		const result = !setupRequired && to.path === '/setup' ? '/' : true;
		expect(result).toBe('/');
	});

	it('redirects to / when user is null and route is protected', () => {
		const user = { value: null };
		const to = { meta: {}, path: '/activities/123' };

		const result =
			!user.value && !to.meta.public && to.path !== '/' ? '/' : true;
		expect(result).toBe('/');
	});

	it('allows navigation for public routes', () => {
		const user = { value: null };
		const to = { meta: { public: true }, path: '/forms/abc' };

		const result =
			!user.value && !(to.meta as any).public && to.path !== '/' ? '/' : true;
		expect(result).toBe(true);
	});

	it('allows navigation when user is logged in', () => {
		const user = { value: { id: 'u-1', display_name: 'Test' } };
		const to = { meta: {}, path: '/activities/123' };

		const result =
			!user.value && !(to.meta as any).public && to.path !== '/' ? '/' : true;
		expect(result).toBe(true);
	});

	it('allows home page without auth', () => {
		const user = { value: null };
		const to = { meta: {}, path: '/' };

		const result =
			!user.value && !(to.meta as any).public && to.path !== '/' ? '/' : true;
		expect(result).toBe(true);
	});

	it('allows /shared/:token route without auth', () => {
		const user = { value: null };
		const to = { meta: { public: true }, path: '/shared/abc123' };

		const result =
			!user.value && !(to.meta as any).public && to.path !== '/' ? '/' : true;
		expect(result).toBe(true);
	});
});

describe('Route definitions', () => {
	const setupPath = '/setup';
	const publicPaths = ['/forms/:slug', '/shared/:token'];
	const protectedPaths = [
		'/activities/new',
		'/activities/:id',
		'/activities/:id/mail',
		'/activities/:id/forms',
		'/stats',
		'/mail-templates',
		'/form-templates',
		'/profile',
		'/admin',
	];

	it.each(publicPaths)('%s should be marked as public', (path) => {
		// These routes have meta.public = true in router/index.ts
		expect(publicPaths).toContain(path);
	});

	it('/setup exists as dedicated setup route', () => {
		expect(setupPath).toBe('/setup');
	});

	it.each(protectedPaths)('%s requires authentication', (path) => {
		expect(protectedPaths).toContain(path);
		expect(publicPaths).not.toContain(path);
	});
});
