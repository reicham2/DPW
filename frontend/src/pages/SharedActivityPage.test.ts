import { flushPromises, mount } from '@vue/test-utils';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import type { Activity } from '../types';
import { config } from '../config';

const mainStyles = readFileSync(join(process.cwd(), 'src', 'assets', 'main.css'), 'utf8');

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
	programs: [{
		id: 'program-1',
		activity_id: 'activity-1',
		duration_minutes: 30,
		title: 'Waldspiel',
		description: '<p>Eine <strong>Fährte</strong> suchen.</p><ul><li>Zusammen bleiben</li></ul><script>alert(1)</script>',
		responsible: ['Alex'],
	}],
};

describe('SharedActivityPage', () => {
	let wrapper: ReturnType<typeof mount> | undefined;
	const styles = document.createElement('style');
	styles.textContent = mainStyles;

	beforeEach(() => {
		config.PUBLIC_BASE_URL = '';
		document.head.append(styles);
	});

	afterEach(() => {
		wrapper?.unmount();
		styles.remove();
		delete document.documentElement.dataset.theme;
		config.PUBLIC_BASE_URL = '';
		vi.unstubAllGlobals();
		document.title = '';
	});

	function mountPage() {
		wrapper = mount(SharedActivityPage, { attachTo: document.body, global: { stubs: { DepartmentBadge: true } } });
		return wrapper;
	}

	function contrast(foreground: string, background: string): number {
		function luminance(color: string): number {
			const hex = color.length === 4 ? `#${[...color.slice(1)].map((digit) => digit + digit).join('')}` : color;
			const rgb = color.startsWith('#')
				? [1, 3, 5].map((start) => parseInt(hex.slice(start, start + 2), 16))
				: (color.match(/[\d.]+/g) ?? []).slice(0, 3).map(Number);
			expect(rgb).toHaveLength(3);
			const linear = rgb.map((channel) => {
				const value = channel / 255;
				return value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4;
			});
			return linear[0] * 0.2126 + linear[1] * 0.7152 + linear[2] * 0.0722;
		}
		const values = [luminance(foreground), luminance(background)].sort((a, b) => b - a);
		return (values[0] + 0.05) / (values[1] + 0.05);
	}

	it('shows the shared story with line breaks as text', async () => {
		vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify(activity), { status: 200 })));
		const wrapper = mountPage();
		await flushPromises();
		const section = wrapper.findAll('.detail-section').find((item) => item.text().includes('Roter Faden'));
		expect(section).toBeDefined();
		expect(section!.get('.detail-value').element.textContent).toBe(activity.guiding_thread);
		expect(section!.find('b').exists()).toBe(false);
	});

	it('brands the loaded page and links to the normal activity without a share token', async () => {
		config.PUBLIC_BASE_URL = 'https://dpw.example/';
		const fetch = vi.fn().mockResolvedValue(new Response(JSON.stringify(activity), { status: 200 }));
		vi.stubGlobal('fetch', fetch);
		const wrapper = mountPage();
		await flushPromises();
		expect(fetch).toHaveBeenCalledExactlyOnceWith('/api/shared/share-token');
		expect(wrapper.get('.public-brand').attributes('href')).toBe('https://dpw.example/');
		expect(wrapper.get('.shared-open').attributes('href')).toBe('https://dpw.example/activities/activity-1');
		expect(wrapper.get('.shared-open').text()).toBe('In DPWeb öffnen');
		expect(wrapper.get('h1').text()).toBe(activity.title);
		expect(wrapper.get('.program-desc strong').text()).toBe('Fährte');
		expect(wrapper.get('.program-desc li').text()).toBe('Zusammen bleiben');
		expect(wrapper.find('.program-desc script').exists()).toBe(false);
	});

	it('encodes the activity id and uses the current instance without a configured URL', async () => {
		vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify({
			...activity, id: 'activity/with spaces',
		}), { status: 200 })));
		const wrapper = mountPage();
		await flushPromises();
		expect(wrapper.get('.shared-open').attributes('href')).toBe('/activities/activity%2Fwith%20spaces');
	});

	it('keeps branding visible while loading and does not expose an activity target yet', () => {
		vi.stubGlobal('fetch', vi.fn().mockReturnValue(new Promise(() => {})));
		const wrapper = mountPage();
		expect(wrapper.get('.public-brand img').attributes('src')).toBe('/logo.png');
		expect(wrapper.text()).toContain('Aktivität wird geladen');
		expect(wrapper.find('.shared-open').exists()).toBe(false);
	});

	it('keeps an invalid or revoked share link unavailable with a homepage link', async () => {
		vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('', { status: 404 })));
		const wrapper = mountPage();
		await flushPromises();
		expect(wrapper.get('.error-state').text()).toContain('der Link ist nicht mehr gültig');
		expect(wrapper.get('.public-brand').attributes('href')).toBe('/');
		expect(wrapper.find('.shared-open').exists()).toBe(false);
		expect(wrapper.find('.shared-main').exists()).toBe(false);
	});

	it('distinguishes a server failure from an invalid link', async () => {
		vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('', { status: 503 })));
		const wrapper = mountPage();
		await flushPromises();
		expect(wrapper.get('[role="alert"]').text()).toContain('HTTP 503');
		expect(wrapper.text()).not.toContain('Nicht gefunden');
		expect(wrapper.find('.shared-open').exists()).toBe(false);
	});

	it('shows an explicit network error even when the failure has no message', async () => {
		vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new Error('')));
		const wrapper = mountPage();
		await flushPromises();
		expect(wrapper.get('[role="alert"]').text()).toContain('Bitte versuche es später erneut.');
		expect(wrapper.find('.public-brand').exists()).toBe(true);
		expect(wrapper.find('.shared-open').exists()).toBe(false);
	});

	it.each(['light', 'dark'])('uses readable body and label colors in the %s theme', async (theme) => {
		document.documentElement.dataset.theme = theme;
		vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify(activity), { status: 200 })));
		const wrapper = mountPage();
		await flushPromises();
		const description = getComputedStyle(wrapper.get('.program-desc').element);
		const label = getComputedStyle(wrapper.get('.detail-section-title').element);
		const background = getComputedStyle(wrapper.get('.shared-main').element).backgroundColor;
		const fontSize = parseFloat(description.fontSize);
		const lineHeight = parseFloat(description.lineHeight);
		expect(fontSize).toBeGreaterThanOrEqual(16);
		expect(description.lineHeight.endsWith('px') ? lineHeight / fontSize : lineHeight).toBeGreaterThanOrEqual(1.7);
		expect(contrast(description.color, background)).toBeGreaterThanOrEqual(7);
		expect(contrast(label.color, background)).toBeGreaterThanOrEqual(4.5);
	});
});
