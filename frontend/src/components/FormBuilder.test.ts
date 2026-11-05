import { beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import { ref } from 'vue';
import type { FormTemplate, SignupForm } from '../types';

const { fetchTemplates } = vi.hoisted(() => ({ fetchTemplates: vi.fn() }));
const templates = ref<FormTemplate[]>([]);

vi.mock('../composables/useForms', () => ({
	useFormTemplates: () => ({ templates, fetchTemplates }),
}));

import FormBuilder from './FormBuilder.vue';

function template(id: string, name: string, is_default = false): FormTemplate {
	return {
		id, name, is_default, department: 'Pfadi', form_type: 'registration',
		template_config: [], created_by: '', created_at: '', updated_at: '',
	};
}

function initial(title: string, id = ''): SignupForm {
	return {
		id, title, activity_id: 'act-1', public_slug: '', form_type: 'registration',
		questions: [], created_by: '', created_at: '', updated_at: '',
	};
}

describe('FormBuilder template titles', () => {
	beforeEach(() => {
		templates.value = [
			template('default', 'Anmeldung {{titel}}', true),
			template('other', '{{abteilung}} am {{datum_kurz}}'),
		];
		fetchTemplates.mockResolvedValue(templates.value);
	});

	it('copies the default template title and saves its variables unexpanded', async () => {
		const wrapper = mount(FormBuilder, { props: { department: 'Pfadi' } });
		await flushPromises();
		expect(wrapper.get<HTMLInputElement>('input[type="text"]').element.value).toBe('Anmeldung {{titel}}');
		await wrapper.get('.builder-actions .btn-primary').trigger('click');
		expect(wrapper.emitted('save')?.[0]?.[0]).toEqual({
			title: 'Anmeldung {{titel}}', form_type: 'registration', questions: [],
		});
		wrapper.unmount();
	});

	it('carries over the new template title on a clean switch', async () => {
		const wrapper = mount(FormBuilder, {
			props: { department: 'Pfadi', initial: initial('Anmeldung {{titel}}') },
		});
		await flushPromises();
		await wrapper.get('select').setValue('other');
		expect(wrapper.get<HTMLInputElement>('input[type="text"]').element.value).toBe('{{abteilung}} am {{datum_kurz}}');
		wrapper.unmount();
	});

	it('preserves a manually entered title on a template switch', async () => {
		const wrapper = mount(FormBuilder, { props: { department: 'Pfadi' } });
		await flushPromises();
		await wrapper.get('input[type="text"]').setValue('Eigener Titel {{ort}}');
		await wrapper.get('select').setValue('other');
		expect(wrapper.get<HTMLInputElement>('input[type="text"]').element.value).toBe('Eigener Titel {{ort}}');
		wrapper.unmount();
	});

	it('preserves a custom draft title when the default template loads', async () => {
		const wrapper = mount(FormBuilder, {
			props: { department: 'Pfadi', initial: initial('Entwurf {{ort}}') },
		});
		await flushPromises();
		expect(wrapper.get<HTMLInputElement>('input[type="text"]').element.value).toBe('Entwurf {{ort}}');
		wrapper.unmount();
	});

	it('does not overwrite an existing form title on a template switch', async () => {
		const wrapper = mount(FormBuilder, {
			props: { department: 'Pfadi', isEdit: true, initial: initial('Anmeldung {{titel}}', 'form-1') },
		});
		await flushPromises();
		await wrapper.get('select').setValue('other');
		expect(wrapper.get<HTMLInputElement>('input[type="text"]').element.value).toBe('Anmeldung {{titel}}');
		wrapper.unmount();
	});
});
