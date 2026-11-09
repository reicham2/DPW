import { flushPromises, mount } from '@vue/test-utils';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { FormType, SignupForm } from '../types';
import { config } from '../config';

vi.mock('vue-router', () => ({
	useRoute: () => ({ params: { slug: 'public-form' } }),
}));

import FormPublicPage from './FormPublicPage.vue';

const form: SignupForm = {
	id: 'form-1',
	activity_id: 'activity-1',
	public_slug: 'public-form',
	form_type: 'registration',
	title: 'Anmeldung zum Waldspiel',
	created_by: 'user-1',
	created_at: '',
	updated_at: '',
	questions: [
		{ id: 'section', form_id: 'form-1', question_text: 'Deine Angaben', question_type: 'section', position: 0, is_required: true, metadata: { subtitle: 'Bitte ausfüllen' }, created_at: '' },
		{ id: 'name', form_id: 'form-1', question_text: 'Name', question_type: 'text_input', position: 1, is_required: true, metadata: {}, created_at: '' },
		{ id: 'choice', form_id: 'form-1', question_text: 'Stufe', question_type: 'single_choice', position: 2, is_required: true, metadata: { choices: [{ id: 'pfadi', label: 'Pfadi' }] }, created_at: '' },
		{ id: 'multi', form_id: 'form-1', question_text: 'Material', question_type: 'multiple_choice', position: 3, is_required: true, metadata: { choices: [{ id: 'bottle', label: 'Trinkflasche' }] }, created_at: '' },
		{ id: 'dropdown', form_id: 'form-1', question_text: 'Treffpunkt', question_type: 'dropdown', position: 4, is_required: true, metadata: { choices: [{ id: 'forest', label: 'Wald' }] }, created_at: '' },
	],
};

describe('FormPublicPage branding and submission', () => {
	let wrapper: ReturnType<typeof mount> | undefined;

	beforeEach(() => {
		config.PUBLIC_BASE_URL = 'https://dpw.example/';
	});

	afterEach(() => {
		wrapper?.unmount();
		config.PUBLIC_BASE_URL = '';
		vi.unstubAllGlobals();
	});

	function mountPage() {
		wrapper = mount(FormPublicPage, { global: { stubs: ['router-link'] } });
		return wrapper;
	}

	async function fillAnswers(wrapper: ReturnType<typeof mount>) {
		await wrapper.get('input[type="text"]').setValue('Alex');
		await wrapper.get('input[type="radio"]').setValue(true);
		await wrapper.get('input[type="checkbox"]').setValue(true);
		await wrapper.get('select').setValue('forest');
	}

	it('shows the homepage logo while the public form is loading', async () => {
		vi.stubGlobal('fetch', vi.fn().mockReturnValue(new Promise(() => {})));
		const wrapper = mountPage();
		await flushPromises();
		expect(wrapper.get('.public-brand').attributes('href')).toBe('https://dpw.example/');
		expect(wrapper.get('.public-brand img').attributes('src')).toBe('/logo.png');
		expect(wrapper.text()).toContain('Formular wird geladen');
	});

	it.each<FormType>(['registration', 'deregistration'])('preserves %s answers and confirmation without authentication', async (formType) => {
		const fetch = vi.fn()
			.mockResolvedValueOnce(new Response(JSON.stringify({ ...form, form_type: formType }), { status: 200 }))
			.mockResolvedValueOnce(new Response(JSON.stringify({ id: 'response-1' }), { status: 201 }));
		vi.stubGlobal('fetch', fetch);
		const wrapper = mountPage();
		await flushPromises();
		await fillAnswers(wrapper);
		await wrapper.get('form').trigger('submit');
		await flushPromises();

		expect(fetch).toHaveBeenNthCalledWith(1, '/api/forms/public-form');
		expect(fetch).toHaveBeenNthCalledWith(2, '/api/forms/public-form/submit', {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify({ answers: [
				{ question_id: 'name', answer_value: 'Alex' },
				{ question_id: 'choice', answer_value: 'pfadi' },
				{ question_id: 'multi', answer_value: '["bottle"]' },
				{ question_id: 'dropdown', answer_value: 'forest' },
			] }),
		});
		expect(wrapper.get('.success-state').text()).toContain(formType === 'registration' ? 'Anmeldung erfolgreich!' : 'Abmeldung erfolgreich!');
		expect(wrapper.get('.public-brand').attributes('href')).toBe('https://dpw.example/');
	});

	it('preserves required field validation before submitting', async () => {
		const fetch = vi.fn().mockResolvedValue(new Response(JSON.stringify(form), { status: 200 }));
		vi.stubGlobal('fetch', fetch);
		const wrapper = mountPage();
		await flushPromises();
		await wrapper.get('form').trigger('submit');
		expect(wrapper.findAll('.validation-error')).toHaveLength(4);
		expect(fetch).toHaveBeenCalledOnce();
		await fillAnswers(wrapper);
		expect(wrapper.findAll('.validation-error')).toHaveLength(0);
	});

	it('keeps the form and branding mounted while a submission is pending', async () => {
		let finishSubmit!: (response: Response) => void;
		const fetch = vi.fn()
			.mockResolvedValueOnce(new Response(JSON.stringify(form), { status: 200 }))
			.mockReturnValueOnce(new Promise<Response>((resolve) => { finishSubmit = resolve; }));
		vi.stubGlobal('fetch', fetch);
		const wrapper = mountPage();
		await flushPromises();
		await fillAnswers(wrapper);
		await wrapper.get('form').trigger('submit');
		expect(wrapper.get('.btn-submit').attributes('disabled')).toBeDefined();
		expect(wrapper.get('input[type="text"]').element).toHaveProperty('value', 'Alex');
		expect(wrapper.find('.public-brand').exists()).toBe(true);
		finishSubmit(new Response(JSON.stringify({ id: 'response-1' }), { status: 201 }));
		await flushPromises();
		expect(wrapper.find('.success-state').exists()).toBe(true);
	});

	it('shows submission errors without losing the answers', async () => {
		vi.stubGlobal('fetch', vi.fn()
			.mockResolvedValueOnce(new Response(JSON.stringify(form), { status: 200 }))
			.mockResolvedValueOnce(new Response('Anmeldung geschlossen', { status: 400 })));
		const wrapper = mountPage();
		await flushPromises();
		await fillAnswers(wrapper);
		await wrapper.get('form').trigger('submit');
		await flushPromises();
		expect(wrapper.get('.submit-error').text()).toContain('Anmeldung geschlossen');
		expect(wrapper.get('input[type="text"]').element).toHaveProperty('value', 'Alex');
		expect(wrapper.find('.success-state').exists()).toBe(false);
	});

	it.each([404, 503])('keeps branding on a form load failure (HTTP %s)', async (status) => {
		vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('Server nicht erreichbar', { status })));
		const wrapper = mountPage();
		await flushPromises();
		expect(wrapper.get('.public-brand').attributes('href')).toBe('https://dpw.example/');
		expect(wrapper.get('.error-state').text()).toContain(status === 404 ? 'Formular nicht gefunden' : 'Server nicht erreichbar');
		expect(wrapper.find('form').exists()).toBe(false);
	});
});
