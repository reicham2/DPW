import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import { afterEach, describe, expect, it } from 'vitest';
import PublicPageLayout from './PublicPageLayout.vue';
import { config } from '../config';

describe('PublicPageLayout branding', () => {
	afterEach(() => {
		config.PUBLIC_BASE_URL = '';
	});

	it('links the existing DPWeb logo to the configured app homepage', async () => {
		config.PUBLIC_BASE_URL = ' https://dpw.example/app/// ';
		const wrapper = mount(PublicPageLayout, {
			props: { label: 'Geteilte Aktivität' },
			slots: { default: '<h1>Aktivität</h1>' },
		});
		const brand = wrapper.get('a.public-brand');
		expect(brand.attributes('href')).toBe('https://dpw.example/app/');
		expect(brand.attributes('aria-label')).toBe('DPWeb-Startseite');
		expect(brand.get('img').attributes('src')).toBe('/logo.png');
		expect(wrapper.get('main').text()).toBe('Aktivität');

		config.PUBLIC_BASE_URL = '';
		await nextTick();
		expect(brand.attributes('href')).toBe('/');
		wrapper.unmount();
	});
});
