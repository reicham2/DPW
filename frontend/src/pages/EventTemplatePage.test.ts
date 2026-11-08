import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, shallowMount } from '@vue/test-utils';
import type { EventTemplate } from '../types';

const mocks = vi.hoisted(() => ({
	templates: [] as EventTemplate[],
	fetchTemplates: vi.fn(),
	saveTemplate: vi.fn(),
	fetchDepartments: vi.fn(),
	fetchMyPermissions: vi.fn(),
	join: vi.fn(),
	leave: vi.fn(),
	send: vi.fn(),
}));

vi.mock('vue-router', () => ({
	useRouter: () => ({ replace: vi.fn() }),
}));

vi.mock('../config', () => ({
	config: { AUTOSAVE_INTERVAL: 100, AUTOSAVE_DEBOUNCE: true },
}));

vi.mock('../composables/useAuth', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		user: ref({ department: 'Pfadi', display_name: 'Anna', microsoft_oid: 'anna' }),
	};
});

vi.mock('../composables/usePermissions', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		usePermissions: () => ({
			departments: ref([{ name: 'Pfadi' }, { name: 'Leiter' }]),
			myPermissions: ref({ event_templates_scope: 'all' }),
			fetchDepartments: mocks.fetchDepartments,
			fetchMyPermissions: mocks.fetchMyPermissions,
		}),
	};
});

vi.mock('../composables/useEventTemplates', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		useEventTemplates: () => ({
			templates: ref(mocks.templates),
			loading: ref(false),
			error: ref(null),
			fetchTemplates: mocks.fetchTemplates,
			saveTemplate: mocks.saveTemplate,
		}),
	};
});

vi.mock('../composables/useWebSocket', () => ({
	wsSend: mocks.send,
	wsJoin: mocks.join,
	wsLeave: mocks.leave,
	useWebSocket: vi.fn(),
}));

import EventTemplatePage from './EventTemplatePage.vue';

function template(overrides: Partial<EventTemplate> = {}): EventTemplate {
	return {
		id: 'event-template-pfadi',
		department: 'Pfadi',
		title: '{{abteilung}}: {{titel}}',
		body: '<p>Eventbeschreibung</p>',
		created_at: '',
		updated_at: '',
		...overrides,
	};
}

describe('EventTemplatePage template overview and editor', () => {
	beforeEach(() => {
		vi.clearAllMocks();
		mocks.templates = [
			template(),
			template({
				id: 'event-template-leiter',
				department: 'Leiter',
				title: 'Leiter: {{titel}}',
			}),
		];
		mocks.fetchTemplates.mockResolvedValue(undefined);
		mocks.fetchDepartments.mockResolvedValue(undefined);
		mocks.fetchMyPermissions.mockResolvedValue(undefined);
		mocks.saveTemplate.mockImplementation(async (department, title, body) => ({
			...mocks.templates.find((item) => item.department === department)!,
			title,
			body,
		}));
	});

	afterEach(() => {
		vi.unstubAllGlobals();
	});

	it('shows one consistently styled overview row per permitted department', async () => {
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();

		expect(wrapper.find('.template-overview-header').text()).toContain('Vorlagen für Events');
		expect(wrapper.findAll('.event-template-overview-row')).toHaveLength(2);
		expect(wrapper.findAll('.template-overview-row-actions .template-overview-row-action')).toHaveLength(2);
		expect(wrapper.text()).toContain('Eine Event-Vorlage pro Stufe');
		expect(wrapper.text()).toContain('Leiter: {{titel}}');
		wrapper.unmount();
	});

	it('opens a form-style editor without adding a selectable default or changing event autosave behavior', async () => {
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();

		await wrapper.find('.event-template-overview-row .template-overview-row-action').trigger('click');
		await flushPromises();

		expect(wrapper.get('.template-editor-header h2').text()).toBe('Vorlage bearbeiten');
		expect(wrapper.get('.template-editor-back').text()).toBe('← Zurück zu Vorlagen');
		expect(wrapper.get('.event-template-default').text()).toContain('Eine Event-Vorlage pro Stufe');
		expect(wrapper.get('#event-template-title').classes()).toContain('template-editor-field-input');
		expect(wrapper.find('input[type="checkbox"]').exists()).toBe(false);
		expect(wrapper.find('.rich-editor').exists()).toBe(true);

		wrapper.unmount();
	});

	it('saves pending title edits before returning to the overview', async () => {
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();

		await wrapper.find('.event-template-overview-row .template-overview-row-action').trigger('click');
		await flushPromises();
		await wrapper.get('#event-template-title').setValue('Neuer Eventtitel');
		await wrapper.get('.template-editor-back').trigger('click');
		await flushPromises();

		expect(mocks.saveTemplate).toHaveBeenCalledWith(
			'Pfadi',
			'Neuer Eventtitel',
			'<p>Eventbeschreibung</p>',
		);
		expect(wrapper.findAll('.event-template-overview-row')).toHaveLength(2);
		wrapper.unmount();
	});
});
