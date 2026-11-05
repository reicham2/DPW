import { expect, it } from 'vitest';
import { shallowMount } from '@vue/test-utils';
import TemplateVarsDropdown from './TemplateVarsDropdown.vue';

it('documents configurable long and short deadline variables alongside existing and extra variables', async () => {
	const wrapper = shallowMount(TemplateVarsDropdown, {
		props: { extraVars: [{ var: '{{formular_link}}', desc: 'Formular' }] },
	});
	await wrapper.get('button').trigger('click');
	expect(wrapper.text()).toContain('{{datum|-7}}');
	expect(wrapper.text()).toContain('{{datum_kurz|-7}}');
	expect(wrapper.text()).toContain('positive Zahlen');
	expect(wrapper.text()).toContain('{{datum}}');
	expect(wrapper.text()).toContain('{{formular_link}}');
	wrapper.unmount();
});
