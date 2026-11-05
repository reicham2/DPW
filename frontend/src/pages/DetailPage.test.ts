import { flushPromises, shallowMount } from '@vue/test-utils';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { Activity, User, WsEvent } from '../types';

const mocks = vi.hoisted(() => ({
	replace: vi.fn(),
	fetchActivity: vi.fn(),
	onMessage: undefined as ((event: WsEvent) => void) | undefined,
	editable: false,
	activities: [] as Activity[],
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
			activities: ref(mocks.activities),
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
			myPermissions: ref(mocks.editable ? { activity_edit_scope: 'all' } : null),
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
import { apiFetch } from '../composables/useApi';
import { user } from '../composables/useAuth';
import { config } from '../config';

function makeActivity(overrides: Partial<Activity> = {}): Activity {
	return {
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
		created_at: '2026-01-01T00:00:00Z',
		updated_at: '2026-01-01T00:00:00Z',
		programs: [],
		...overrides,
	};
}

function allowEditing() {
	mocks.editable = true;
	const editableUser: User = {
		id: 'user-1',
		microsoft_oid: 'oid-1',
		email: 'test@example.com',
		display_name: 'Test User',
		department: null,
		role: 'leader',
		time_display_mode: 'minutes',
		notify_material_assigned: false,
		notify_activity_assigned: false,
		notify_program_assigned: false,
		notify_mail_own_activity: false,
		notify_mail_department: false,
		notify_channel_websocket: false,
		notify_channel_email: false,
		avatar_color: '#000000',
		created_at: '',
		updated_at: '',
	};
	user.value = editableUser;
}

describe('DetailPage activity access', () => {
	beforeEach(() => {
		config.MIDATA_ENABLED = false;
		mocks.editable = false;
		mocks.activities = [];
		user.value = null;
		sessionStorage.clear();
		vi.mocked(apiFetch).mockReset().mockResolvedValue(new Response('{}', { status: 200 }));
		mocks.replace.mockReset().mockResolvedValue(undefined);
		mocks.onMessage = undefined;
		mocks.fetchActivity.mockReset().mockResolvedValue(makeActivity());
	});

	afterEach(() => {
		vi.clearAllMocks();
		vi.useRealTimers();
		config.MIDATA_ENABLED = false;
		user.value = null;
		localStorage.removeItem('dpw:activity-edit-draft:activity-1');
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

	it('loads MiData immediately when runtime configuration arrives after the activity', async () => {
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		vi.mocked(apiFetch).mockResolvedValue(
			new Response(JSON.stringify({ configured: true, children_count: 24 }), { status: 200 }),
		);
		config.MIDATA_ENABLED = true;
		await flushPromises();
		expect(apiFetch).toHaveBeenCalledWith('/api/activities/activity-1/midata/children-count');
		expect(wrapper.text()).toContain('24');
		wrapper.unmount();
	});

	it('shows MiData request failures rather than hiding them as missing configuration', async () => {
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		vi.mocked(apiFetch).mockRejectedValue(new Error('MiData connection failed'));
		config.MIDATA_ENABLED = true;
		await flushPromises();
		expect(wrapper.text()).toContain('MiData connection failed');
		wrapper.unmount();
	});

	it('keeps the similar-activity estimate together in view and edit mode', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({ department: 'Pfadi' }));
		mocks.activities = [makeActivity({ id: 'activity-2', date: '2026-06-24', department: 'Pfadi' })];
		vi.mocked(apiFetch).mockImplementation(async (url) => new Response(JSON.stringify(
			url === '/api/activities/activity-2/form/stats'
				? { exists: true, stats: { expected_current: 24 } }
				: {},
		), { status: 200 }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();

		const estimateRow = () => wrapper.get('.detail-stats-sidebar > .detail-stats-card:nth-child(2) .detail-stats-row');
		const checkEstimate = () => {
			expect(estimateRow().classes()).toContain('detail-stats-row--estimate');
			expect(estimateRow().get('.detail-value').text()).toBe('24');
		};
		checkEstimate();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(wrapper.find('.detail-form').exists()).toBe(true);
		checkEstimate();
		wrapper.unmount();
	});

	it('waits for a detailed forecast beyond the previous 2.5 second timeout', async () => {
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-06-29T12:00:00Z'));
		let weatherSignal: AbortSignal | null | undefined;
		vi.mocked(apiFetch).mockImplementation(async (url, options) => {
			if (url.endsWith('/weather-location')) {
				return new Response(JSON.stringify({ location: '6300 Zug' }), { status: 200 });
			}
			if (url.includes('/weather-expected')) {
				weatherSignal = options?.signal;
				return new Promise<Response>((resolve, reject) => {
					options?.signal?.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')));
					setTimeout(() => resolve(new Response(JSON.stringify({
						available: true,
						mode: 'forecast',
						temperature_min_c: 14,
						temperature_max_c: 18,
						source: 'MeteoSwiss Local Forecast',
						hourly_temps: [
							{ ts_unix: 1782892800, temperature_c: 14 },
							{ ts_unix: 1782896400, temperature_c: 18 },
						],
					}), { status: 200 })), 5000);
				});
			}
			return new Response('{}', { status: 200 });
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(2600);
		expect(weatherSignal).toBeDefined();
		expect(weatherSignal?.aborted).toBe(false);
		await vi.advanceTimersByTimeAsync(2400);
		await flushPromises();
		expect(wrapper.text()).toContain('14° | 18°');
		expect(wrapper.text()).toContain('MeteoSwiss Local Forecast');
		wrapper.unmount();
	});

	it.each([
		{
			mode: 'forecast',
			source: 'MeteoSwiss Local Forecast',
			note: 'Vorhersage basiert auf tre200h0 (stundlicher Mittelwert, naechster Zeitpunkt).',
		},
		{
			mode: 'seasonal-average',
			source: 'MeteoSwiss (saisonaler Durchschnitt CH)',
			note: 'Termin liegt ausserhalb des MeteoSwiss-Vorhersagehorizonts.',
		},
	])('hides $mode weather notes in view and edit mode', async (weather) => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-06-29T12:00:00Z'));
		vi.mocked(apiFetch).mockImplementation(async (url) => new Response(JSON.stringify(
			url.endsWith('/weather-location')
				? { location: '6300 Zug' }
				: { available: true, temperature_min_c: 14, temperature_max_c: 18, ...weather },
		), { status: 200 }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();

		const weatherCard = () => wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child');
		const checkWeather = () => {
			expect(weatherCard().text()).toContain('14° | 18°');
			expect(weatherCard().text()).toContain(weather.source);
			expect(weatherCard().text()).not.toContain('Hinweis');
			expect(weatherCard().text()).not.toContain(weather.note);
		};
		checkWeather();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(wrapper.find('.detail-form').exists()).toBe(true);
		checkWeather();
		wrapper.unmount();
	});

	it('shows forecast errors and does not cache them as successful weather', async () => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-06-29T12:00:00Z'));
		vi.mocked(apiFetch).mockImplementation(async (url) => new Response(JSON.stringify(
			url.endsWith('/weather-location')
				? { location: '6300 Zug' }
				: { available: false, error: 'meteo-temp-point-missing' },
		), { status: 200 }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();
		const weatherCard = () => wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child');
		expect(weatherCard().text()).toContain('Wetterfehler');
		expect(weatherCard().text()).toContain('meteo-temp-point-missing');
		expect(weatherCard().text()).not.toContain('Hinweis');
		expect(sessionStorage.length).toBe(0);
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(wrapper.find('.detail-form').exists()).toBe(true);
		expect(weatherCard().text()).toContain('Wetterfehler');
		expect(weatherCard().text()).toContain('meteo-temp-point-missing');
		expect(weatherCard().text()).not.toContain('Hinweis');
		wrapper.unmount();
	});

	it.each([
		{ name: 'future', now: '2026-06-29T12:00:00Z' },
		{ name: 'past', now: '2026-07-02T12:00:00Z' },
	])('does not show a weather error without a saved location for $name activities', async ({ now }) => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date(now));
		vi.mocked(apiFetch).mockImplementation(async (url) => new Response(JSON.stringify(
			url.endsWith('/weather-location')
				? { location: null }
				: { available: false, error: 'meteo-temp-point-missing' },
		), { status: 200 }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();

		const checkWeather = () => {
			const card = wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child');
			expect(card.text()).toContain('Vorhersage—');
			expect(card.text()).toContain('Temperatur—');
			expect(card.text()).not.toContain('Wetterfehler');
			expect(card.text()).not.toContain('meteo-temp-point-missing');
		};
		checkWeather();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(wrapper.find('.detail-form').exists()).toBe(true);
		checkWeather();
		wrapper.unmount();
	});

	it('keeps a frozen weather snapshot visible without a saved location', async () => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-07-02T12:00:00Z'));
		vi.mocked(apiFetch).mockImplementation(async (url) => new Response(JSON.stringify(
			url.endsWith('/weather-location')
				? { location: null }
				: { available: true, mode: 'frozen', temperature_min_c: 14, temperature_max_c: 18 },
		), { status: 200 }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();
		expect(wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child').text()).toContain('14° | 18°');
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child').text()).toContain('14° | 18°');
		wrapper.unmount();
	});

	it('clears a missing-location error while loading a newly saved weather location', async () => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-07-02T12:00:00Z'));
		let resolveForecast: (response: Response) => void = () => {
			throw new Error('New forecast request was not started');
		};
		vi.mocked(apiFetch).mockImplementation(async (url, options) => {
			if (url.endsWith('/weather-location')) {
				return new Response(JSON.stringify({ location: options?.method === 'PUT' ? '6300 Zug' : null }), { status: 200 });
			}
			if (url.includes('/weather-expected?location=')) {
				return new Promise<Response>((resolve) => { resolveForecast = resolve; });
			}
			return new Response(JSON.stringify({ available: false, error: 'meteo-temp-point-missing' }), { status: 200 });
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.detail-weather-input').setValue('6300 Zug');
		await wrapper.get('.detail-weather-input-wrap button').trigger('click');
		await flushPromises();

		const weatherCard = () => wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child');
		expect(weatherCard().text()).not.toContain('Wetterfehler');
		expect(weatherCard().text()).not.toContain('meteo-temp-point-missing');
		resolveForecast(new Response(JSON.stringify({ available: false, error: 'meteo-location-not-found' }), { status: 200 }));
		await flushPromises();
		expect(weatherCard().text()).toContain('Wetterfehler');
		expect(weatherCard().text()).toContain('meteo-location-not-found');
		wrapper.unmount();
	});

	it.each(['request', 'response body'])('does not let a stale %s override a cached forecast after saving a location', async (pendingStage) => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-07-02T12:00:00Z'));
		sessionStorage.setItem(
			'dpw.weatherExpected:activity-1:2026-07-01:10:00:11:00:past:6300 zug',
			JSON.stringify({
				ts: Date.now(),
				data: { available: true, mode: 'frozen', temperature_min_c: 14, temperature_max_c: 18 },
			}),
		);
		let oldSignal: AbortSignal | null | undefined;
		let finishOldRequest: () => void = () => { throw new Error('Old request was not started'); };
		vi.mocked(apiFetch).mockImplementation(async (url, options) => {
			if (url.endsWith('/weather-location')) {
				return new Response(JSON.stringify({ location: options?.method === 'PUT' ? '6300 Zug' : null }), { status: 200 });
			}
			if (url.includes('/weather-expected')) {
				oldSignal = options?.signal;
				if (pendingStage === 'request') {
					return new Promise<Response>((resolve) => {
						finishOldRequest = () => resolve(new Response(JSON.stringify({ available: false, error: 'meteo-temp-point-missing' }), { status: 200 }));
					});
				}
				const response = new Response('{}', { status: 200 });
				vi.spyOn(response, 'json').mockImplementation(() => new Promise((resolve) => {
					finishOldRequest = () => resolve({ available: false, error: 'meteo-temp-point-missing' });
				}));
				return response;
			}
			return new Response('{}', { status: 200 });
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.detail-weather-input').setValue('6300 Zug');
		await wrapper.get('.detail-weather-input-wrap button').trigger('click');
		await flushPromises();

		const weatherCard = () => wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child');
		expect(weatherCard().text()).toContain('14° | 18°');
		expect(oldSignal?.aborted).toBe(true);
		finishOldRequest();
		await flushPromises();
		expect(weatherCard().text()).toContain('14° | 18°');
		expect(weatherCard().text()).not.toContain('Wetterfehler');
		wrapper.unmount();
	});
});
