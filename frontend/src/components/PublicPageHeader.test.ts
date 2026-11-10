// @vitest-environment jsdom
import { mount } from '@vue/test-utils';
import { describe, expect, it } from 'vitest';
import PublicPageHeader from './PublicPageHeader.vue';

describe('PublicPageHeader', () => {
	it('shows the public brand and supplied page label in the shared public header layout', () => {
		const wrapper = mount(PublicPageHeader, { props: { label: 'Gastbearbeitung' } });

		expect(wrapper.get('header.public-page-header').exists()).toBe(true);
		expect(wrapper.get('.public-page-header-inner').exists()).toBe(true);
		expect(wrapper.get('a.public-brand').attributes('aria-label')).toBe('DPWeb-Startseite');
		expect(wrapper.get('a.public-brand').attributes('href')).toBe('/');
		expect(wrapper.get('.public-page-label').text()).toBe('Gastbearbeitung');
		expect(wrapper.findAll('button')).toHaveLength(0);
	});
});
