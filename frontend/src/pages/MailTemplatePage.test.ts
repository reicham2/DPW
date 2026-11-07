import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, shallowMount } from '@vue/test-utils';
import type { MailTemplate } from '../types';

const mocks = vi.hoisted(() => ({
	templates: [] as MailTemplate[],
	updateTemplate: vi.fn(),
	fetchTemplates: vi.fn(),
	createTemplate: vi.fn(),
	deleteTemplate: vi.fn(),
}));

vi.mock('vue-router', () => ({
	useRouter: () => ({ replace: vi.fn() }),
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
			departments: ref([{ name: 'Pfadi' }]),
			myPermissions: ref({ mail_templates_scope: 'all' }),
			fetchDepartments: vi.fn().mockResolvedValue(undefined),
			fetchMyPermissions: vi.fn().mockResolvedValue(undefined),
		}),
	};
});

vi.mock('../composables/useMailTemplates', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		useMailTemplates: () => ({
			templates: ref(mocks.templates),
			loading: ref(false),
			error: ref(null),
			fetchTemplates: mocks.fetchTemplates,
			createTemplate: mocks.createTemplate,
			updateTemplate: mocks.updateTemplate,
			deleteTemplate: mocks.deleteTemplate,
		}),
	};
});

vi.mock('../composables/useContactSearch', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		useContactSearch: () => ({
			results: ref([]),
			searching: ref(false),
			search: vi.fn(),
			clear: vi.fn(),
		}),
	};
});

vi.mock('../composables/useWebSocket', () => ({
	wsSend: vi.fn(),
	wsRegister: vi.fn(),
	wsJoin: vi.fn(),
	wsLeave: vi.fn(),
	useWebSocket: vi.fn(),
}));

import MailTemplatePage from './MailTemplatePage.vue';

function template(overrides: Partial<MailTemplate> = {}): MailTemplate {
	return {
		id: 'template-standard',
		name: 'Standard',
		department: 'Pfadi',
		is_default: true,
		subject: 'Einladung',
		body: '<p>Hallo zusammen</p>',
		recipients: ['eltern@example.com'],
		cc: [],
		created_at: '',
		updated_at: '',
		...overrides,
	};
}

describe('MailTemplatePage template list', () => {
	beforeEach(() => {
		vi.clearAllMocks();
		mocks.templates = [
			template(),
			template({
				id: 'template-alternative',
				name: 'Alternative',
				is_default: false,
				subject: 'Alternative Einladung',
				recipients: [],
			}),
		];
		mocks.fetchTemplates.mockResolvedValue(undefined);
		mocks.updateTemplate.mockImplementation(async (id, name, subject, body, recipients, cc, isDefault) => {
			const current = mocks.templates.find((item) => item.id === id);
			return current
				? { ...current, name, subject, body, recipients, cc, is_default: isDefault }
				: null;
		});
	});

	afterEach(() => {
		vi.unstubAllGlobals();
	});

	it('shows templates as actionable rows instead of a selector', async () => {
		const wrapper = shallowMount(MailTemplatePage);
		await flushPromises();

		expect(wrapper.find('#mail-template-admin-selector').exists()).toBe(false);
		expect(wrapper.find('.template-overview-header').exists()).toBe(true);
		expect(wrapper.find('.template-overview-create').exists()).toBe(true);
		expect(wrapper.findAll('.template-overview-row')).toHaveLength(2);
		expect(wrapper.findAll('.template-overview-row-actions')).toHaveLength(2);
		expect(wrapper.findAll('.mt-row')).toHaveLength(2);
		expect(wrapper.text()).toContain('Standard');
		expect(wrapper.text()).toContain('Alternative Einladung');
		expect(wrapper.text()).toContain('⭐ Standard');
		expect(wrapper.text()).toContain('1 Empfänger');
		wrapper.unmount();
	});

	it('opens a row for editing and returns to the list', async () => {
		const wrapper = shallowMount(MailTemplatePage);
		await flushPromises();

		await wrapper.findAll('.mt-row .btn-sm')[1].trigger('click');
		await flushPromises();
		expect(wrapper.find('#mail-template-name').exists()).toBe(true);

		await wrapper.get('.template-editor-back').trigger('click');
		await flushPromises();
		expect(wrapper.findAll('.mt-row')).toHaveLength(2);
		expect(wrapper.find('#mail-template-name').exists()).toBe(false);
		wrapper.unmount();
	});

	it('saves from the editor and stays on the editor', async () => {
		const wrapper = shallowMount(MailTemplatePage);
		await flushPromises();
		await wrapper.findAll('.mt-row .btn-sm')[1].trigger('click');
		await flushPromises();

		await wrapper.get('#mail-template-name').setValue('Standard aktualisiert');
		await wrapper.get('.mt-editor-actions .btn-primary').trigger('click');
		await flushPromises();

		expect(mocks.updateTemplate).toHaveBeenCalledWith(
			'template-standard',
			'Standard aktualisiert',
			'Einladung',
			'<p>Hallo zusammen</p>',
			['eltern@example.com'],
			[],
			true,
		);
		expect(wrapper.find('#mail-template-name').exists()).toBe(true);
		wrapper.unmount();
	});

	it('flushes pending edits before returning to the list', async () => {
		const wrapper = shallowMount(MailTemplatePage);
		await flushPromises();
		await wrapper.findAll('.mt-row .btn-sm')[1].trigger('click');
		await flushPromises();

		await wrapper.get('#mail-template-name').setValue('Zurück mit Speichern');
		await wrapper.get('.mt-editor-actions .btn-secondary').trigger('click');
		await flushPromises();

		expect(mocks.updateTemplate).toHaveBeenCalledWith(
			'template-standard',
			'Zurück mit Speichern',
			'Einladung',
			'<p>Hallo zusammen</p>',
			['eltern@example.com'],
			[],
			true,
		);
		expect(wrapper.findAll('.mt-row')).toHaveLength(2);
		wrapper.unmount();
	});

	it('lets a list row become the default', async () => {
		const wrapper = shallowMount(MailTemplatePage);
		await flushPromises();

		await wrapper.findAll('.mt-row .btn-sm')[3].trigger('click');
		await flushPromises();

		expect(mocks.updateTemplate).toHaveBeenCalledWith(
			'template-alternative',
			'Alternative',
			'Alternative Einladung',
			'<p>Hallo zusammen</p>',
			[],
			[],
			true,
		);
		expect(mocks.fetchTemplates).toHaveBeenCalled();
		wrapper.unmount();
	});
});
