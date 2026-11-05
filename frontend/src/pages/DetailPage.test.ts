import { flushPromises, shallowMount } from '@vue/test-utils';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { WsEvent } from '../types';

const mocks = vi.hoisted(() => ({
	replace: vi.fn(),
	fetchActivity: vi.fn(),
	onMessage: undefined as ((event: WsEvent) => void) | undefined,
}));

vi.mock('vue-router', () => ({
	useRoute: () => ({ params: { id: 'activity-1' }, query: {} }),
	useRouter: () => ({ replace: mocks.replace, push: vi.fn() }),
}));

vi.mock('../composables/useActivities', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	const noop = vi.fn().mockResolvedValue(undefined);
	return {
		useActivities: () => ({
			activities: ref([]),
			fetchActivities: noop,
			fetchActivity: mocks.fetchActivity,
			fetchDepartments: noop,
			fetchLocations: noop,
			fetchMaterials: noop,
			updateActivity: noop,
			deleteActivity: noop,
			fetchAttachments: vi.fn().mockResolvedValue([]),
			uploadAttachment: noop,
			deleteAttachment: noop,
			getAttachmentBlobUrl: noop,
			error: ref(null),
			lastUpdatedActivity: ref(null),
			departments: ref([]),
			predefinedLocations: ref([]),
			predefinedMaterials: ref([]),
		}),
	};
});

vi.mock('../composables/useUsers', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { useUsers: () => ({ users: ref([]), fetchUsers: vi.fn() }) };
});

vi.mock('../composables/usePermissions', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		usePermissions: () => ({
			myPermissions: ref(null),
			fetchMyPermissions: vi.fn().mockResolvedValue(null),
			writableDepts: () => [],
			canReadActivity: () => true,
			canForms: () => false,
			canIdeenkiste: () => false,
			canIdeenkisteAdd: () => false,
		}),
	};
});

vi.mock('../composables/useAuth', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { user: ref(null) };
});

vi.mock('../composables/useWebSocket', () => ({
	wsSend: vi.fn(),
	wsRegister: vi.fn(),
	wsJoin: vi.fn(),
	wsLeave: vi.fn(),
	useWebSocket: (handler: (event: WsEvent) => void) => {
		mocks.onMessage = handler;
		return {};
	},
}));

vi.mock('../composables/useForms', () => ({ useForms: () => ({ fetchForm: vi.fn() }) }));
vi.mock('../composables/useEventTemplates', () => ({
	useEventTemplates: () => ({
		fetchTemplate: vi.fn(),
		fetchPublication: vi.fn(),
		publishEvent: vi.fn(),
		unpublishEvent: vi.fn(),
	}),
}));
vi.mock('../composables/useApi', () => ({
	apiFetch: vi.fn().mockResolvedValue(new Response('{}', { status: 200 })),
}));
vi.mock('../composables/useIdeaBox', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		useIdeaBox: () => ({
			items: ref([]),
			fetchItems: vi.fn(),
			createItem: vi.fn(),
			deleteItem: vi.fn(),
		}),
	};
});
vi.mock('../composables/useActivityExport', () => ({ useActivityExport: () => ({ exportToWord: vi.fn() }) }));
vi.mock('../composables/useUserResolver', () => ({ useUserResolver: () => ({ resolveResponsibleName: vi.fn() }) }));

import DetailPage from './DetailPage.vue';

describe('DetailPage activity access', () => {
	beforeEach(() => {
		mocks.replace.mockReset().mockResolvedValue(undefined);
		mocks.onMessage = undefined;
		mocks.fetchActivity.mockReset().mockResolvedValue({
			id: 'activity-1',
			title: 'Test activity',
			date: '2026-07-01',
			start_time: '10:00',
			end_time: '11:00',
			goal: '',
			location: '',
			responsible: [],
			department: null,
			material: [],
			tn_material: [],
			siko_text: null,
			bad_weather_info: null,
			planned_participants_estimate: null,
			programs: [],
		});
	});

	afterEach(() => {
		vi.clearAllMocks();
	});

	it('leaves the detail page only when access to the displayed activity is removed', async () => {
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		expect(wrapper.text()).toContain('Test activity');
		expect(mocks.onMessage).toBeDefined();

		mocks.onMessage?.({ event: 'deleted', id: 'another-activity' });
		expect(mocks.replace).not.toHaveBeenCalled();

		mocks.onMessage?.({ event: 'deleted', id: 'activity-1' });
		await flushPromises();
		expect(mocks.replace).toHaveBeenCalledWith('/');
		expect(wrapper.text()).not.toContain('Test activity');
		wrapper.unmount();
	});
});
