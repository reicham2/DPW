import { flushPromises, shallowMount } from '@vue/test-utils';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { Activity } from '../types';

vi.mock('vue-router', () => ({
	useRoute: () => ({ params: { token: 'share-token' } }),
}));

import SharedActivityPage from './SharedActivityPage.vue';

const activity: Activity = {
	id: 'activity-1',
	title: 'Geteilte Aktivität',
	date: '2026-07-01',
	start_time: '10:00',
	end_time: '11:00',
	goal: '',
	guiding_thread: 'Erste Zeile\n<b>Zweite Zeile</b>',
	location: '',
	responsible: [],
	department: null,
	material: [],
	tn_material: [],
	siko_text: null,
	bad_weather_info: null,
	planned_participants_estimate: null,
	created_at: '',
	updated_at: '',
	programs: [],
};

describe('SharedActivityPage guiding thread', () => {
	afterEach(() => {
		vi.unstubAllGlobals();
		document.title = '';
	});

	it('shows the shared story with line breaks as text', async () => {
		vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify(activity), { status: 200 })));
		const wrapper = shallowMount(SharedActivityPage);
		await flushPromises();
		const section = wrapper.findAll('.detail-section').find((item) => item.text().includes('Roter Faden'));
		expect(section).toBeDefined();
		expect(section!.get('.detail-value').element.textContent).toBe(activity.guiding_thread);
		expect(section!.find('b').exists()).toBe(false);
		wrapper.unmount();
	});
});
