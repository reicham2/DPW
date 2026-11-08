import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import type { FormTemplate } from '../types';

const mocks = vi.hoisted(() => ({
	templates: [] as FormTemplate[],
	fetchTemplates: vi.fn(),
	createTemplate: vi.fn(),
	updateTemplate: vi.fn(),
	deleteTemplate: vi.fn(),
	flushAutoSave: vi.fn(),
	hasUnsavedChanges: vi.fn(),
}));

vi.mock('../composables/useAuth', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { user: ref({ department: 'Pfadi' }) };
});

vi.mock('../composables/usePermissions', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		usePermissions: () => ({
			departments: ref([{ name: 'Pfadi' }]),
			myPermissions: ref({ form_templates_scope: 'all' }),
			fetchDepartments: vi.fn().mockResolvedValue(undefined),
			fetchMyPermissions: vi.fn().mockResolvedValue(undefined),
		}),
	};
});

vi.mock('../composables/useForms', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		useFormTemplates: () => ({
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

vi.mock('../components/FormBuilder.vue', async () => {
	const { defineComponent, h } = await vi.importActual<typeof import('vue')>('vue');
	return {
		default: defineComponent({
			name: 'FormBuilder',
			emits: ['save', 'autosave', 'cancel'],
			setup(_props, { emit, expose }) {
				expose({
					flushAutoSave: () => mocks.flushAutoSave(),
					hasUnsavedChanges: () => mocks.hasUnsavedChanges(),
				});
				return () => h('div', { class: 'form-builder-test-stub' }, [
					h('button', {
						class: 'emit-autosave',
						onClick: () => emit('autosave', {
							title: 'Geänderte Vorlage',
							form_type: 'registration',
							questions: [],
						}),
					}, 'Autosave'),
				]);
			},
		}),
	};
});

import FormTemplatePage from './FormTemplatePage.vue';

function template(overrides: Partial<FormTemplate> = {}): FormTemplate {
	return {
		id: 'form-template',
		name: 'Standard-Anmeldung',
		department: 'Pfadi',
		form_type: 'registration',
		template_config: [],
		is_default: true,
		created_by: 'admin',
		created_at: '',
		updated_at: '',
		...overrides,
	};
}

describe('FormTemplatePage overview and editor navigation', () => {
	beforeEach(() => {
		vi.clearAllMocks();
		window.localStorage.clear();
		mocks.templates = [template()];
		mocks.fetchTemplates.mockResolvedValue(mocks.templates);
		mocks.createTemplate.mockResolvedValue(template({ id: 'new-template' }));
		mocks.updateTemplate.mockImplementation(async (id, name, form_type, template_config, is_default) => {
			const current = mocks.templates.find((item) => item.id === id);
			if (!current) return null;
			Object.assign(current, { name, form_type, template_config, is_default });
			return current;
		});
		mocks.deleteTemplate.mockResolvedValue(true);
		mocks.flushAutoSave.mockResolvedValue(undefined);
		mocks.hasUnsavedChanges.mockReturnValue(false);
	});

	afterEach(() => {
		vi.restoreAllMocks();
		Reflect.deleteProperty(window, 'confirm');
	});

	function mountPage() {
		return mount(FormTemplatePage, {
			global: {
				stubs: {
					DepartmentBadge: true,
					ErrorAlert: true,
					TemplateVarsDropdown: true,
				},
			},
		});
	}

	it('uses the shared overview layout and action classes', async () => {
		const wrapper = mountPage();
		await flushPromises();

		expect(wrapper.find('.template-overview-header').exists()).toBe(true);
		expect(wrapper.find('.template-overview-create').exists()).toBe(true);
		expect(wrapper.find('.template-overview-row').exists()).toBe(true);
		expect(wrapper.find('.template-overview-row-actions').exists()).toBe(true);
		expect(wrapper.findAll('.template-overview-row-action')).toHaveLength(3);
		expect(wrapper.find('.template-department-tabs').exists()).toBe(true);
		wrapper.unmount();
	});

	it('sorts the active department templates by name without putting the default first', async () => {
		mocks.templates = [
			template({ id: 'form-zulu', name: 'Zulu', is_default: true }),
			template({ id: 'form-alpha', name: 'Alpha', is_default: false }),
		];
		const wrapper = mountPage();
		await flushPromises();

		const rows = wrapper.findAll('.ft-row');
		expect(rows).toHaveLength(2);
		expect(rows[0].text()).toContain('Alpha');
		expect(rows[0].find('.default-badge').exists()).toBe(false);
		expect(rows[1].text()).toContain('Zulu');
		expect(rows[1].find('.default-badge').exists()).toBe(true);
		wrapper.unmount();
	});

	it('flushes and waits for pending autosave before returning to the list', async () => {
		let resolveUpdate!: (value: FormTemplate | null) => void;
		mocks.updateTemplate.mockReturnValue(new Promise<FormTemplate | null>((resolve) => {
			resolveUpdate = resolve;
		}));

		const wrapper = mountPage();
		await flushPromises();
		await wrapper.findAll('.ft-row .btn-sm')[1].trigger('click');
		await flushPromises();

		expect(wrapper.get('.template-editor-back').classes()).toContain('btn-secondary');
		await wrapper.get('.emit-autosave').trigger('click');
		await wrapper.get('.template-editor-back').trigger('click');
		await flushPromises();

		expect(mocks.flushAutoSave).toHaveBeenCalledOnce();
		expect(wrapper.find('.template-editor-header').exists()).toBe(true);

		resolveUpdate(template({ name: 'Geänderte Vorlage' }));
		await flushPromises();

		expect(wrapper.find('.template-overview-header').exists()).toBe(true);
		expect(wrapper.find('.template-editor-header').exists()).toBe(false);
		wrapper.unmount();
	});

	it('asks before leaving a new template with unsaved edits', async () => {
		mocks.hasUnsavedChanges.mockReturnValue(true);
		const confirm = vi.fn().mockReturnValue(false);
		Object.defineProperty(window, 'confirm', { configurable: true, value: confirm });

		const wrapper = mountPage();
		await flushPromises();
		await wrapper.get('.template-overview-create').trigger('click');
		await wrapper.get('.template-editor-back').trigger('click');

		expect(confirm).toHaveBeenCalledOnce();
		expect(wrapper.find('.template-editor-header').exists()).toBe(true);
		wrapper.unmount();
	});
});
