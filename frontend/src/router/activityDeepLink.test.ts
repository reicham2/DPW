import { flushPromises } from '@vue/test-utils';
import { beforeEach, describe, expect, it, vi } from 'vitest';

vi.mock('../composables/useSetupAuthConfig', () => ({
	ensureSetupStatus: vi.fn().mockResolvedValue(false),
	activityGuidingThreadSupported: { value: true },
}));

import { router } from './index';
import { authLoading, user } from '../composables/useAuth';
import { ensureSetupStatus } from '../composables/useSetupAuthConfig';

describe('Activity deep-link login navigation', () => {
	beforeEach(async () => {
		user.value = null;
		authLoading.value = false;
		vi.mocked(ensureSetupStatus).mockResolvedValue(false);
		await router.replace('/');
	});

	it('retains the requested normal activity while signed out, without making it public', async () => {
		await router.push('/activities/activity-1');
		expect(router.currentRoute.value.fullPath).toBe('/activities/activity-1');
		expect(router.currentRoute.value.meta.public).not.toBe(true);
		expect(user.value).toBeNull();
	});

	it('still redirects other protected routes to the login homepage', async () => {
		await router.push('/activities/activity-1/forms');
		expect(router.currentRoute.value.path).toBe('/');
	});

	it('still checks setup before allowing the activity login overlay', async () => {
		vi.mocked(ensureSetupStatus).mockResolvedValue(true);
		await router.push('/activities/activity-1');
		expect(router.currentRoute.value.path).toBe('/setup');
	});

	it.each(['/shared/share-token', '/forms/public-form'])('keeps %s public even when setup is required', async (path) => {
		vi.mocked(ensureSetupStatus).mockResolvedValue(true);
		await router.push(path);
		expect(router.currentRoute.value.path).toBe(path);
		expect(router.currentRoute.value.meta.public).toBe(true);
	});

	it('waits for authentication initialization before resolving the activity route', async () => {
		authLoading.value = true;
		const navigation = router.push('/activities/activity-1');
		await flushPromises();
		expect(router.currentRoute.value.path).toBe('/');
		authLoading.value = false;
		await navigation;
		expect(router.currentRoute.value.path).toBe('/activities/activity-1');
	});
});
