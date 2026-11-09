import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, shallowMount } from '@vue/test-utils';
import type { EventTemplate, WsEvent } from '../types';

const mocks = vi.hoisted(() => ({
	templates: [] as EventTemplate[],
	fetchTemplates: vi.fn(),
	createTemplate: vi.fn(),
	updateTemplate: vi.fn(),
	deleteTemplate: vi.fn(),
	onMessage: undefined as ((event: WsEvent) => void) | undefined,
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
		useEventTemplates: () => {
			const templates = ref(mocks.templates);
			return {
			templates,
			loading: ref(false),
			error: ref(null),
			fetchTemplates: mocks.fetchTemplates,
			createTemplate: mocks.createTemplate,
			updateTemplate: mocks.updateTemplate,
			deleteTemplate: async (id: string) => {
				const success = await mocks.deleteTemplate(id);
				if (success) templates.value = templates.value.filter(template => template.id !== id);
				return success;
			},
		}; },
	};
});

vi.mock('../composables/useWebSocket', () => ({
	wsSend: mocks.send,
	wsJoin: mocks.join,
	wsLeave: mocks.leave,
	useWebSocket: (handler: (event: WsEvent) => void) => { mocks.onMessage = handler; },
}));

import EventTemplatePage from './EventTemplatePage.vue';

function template(overrides: Partial<EventTemplate> = {}): EventTemplate {
	return {
		id: 'event-template-pfadi',
		name: 'Standard',
		department: 'Pfadi',
		title: '{{abteilung}}: {{titel}}',
		body: '<p>Eventbeschreibung</p>',
		is_default: true,
		created_at: '',
		updated_at: '',
		...overrides,
	};
}

describe('EventTemplatePage template overview and editor', () => {
	beforeEach(() => {
		vi.clearAllMocks();
		localStorage.clear();
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
		mocks.updateTemplate.mockReset().mockImplementation(async (id, changes) => ({
			...mocks.templates.find((item) => item.id === id)!,
			...changes,
		}));
		mocks.createTemplate.mockReset().mockImplementation(async (name, department, title, body, isDefault) =>
			template({ id: 'new-template', name, department, title, body, is_default: isDefault }));
		mocks.deleteTemplate.mockReset().mockResolvedValue(true);
	});

	afterEach(() => {
		vi.unstubAllGlobals();
		vi.restoreAllMocks();
		vi.useRealTimers();
		localStorage.clear();
	});

	it('shows the active department in the shared overview layout with a Stufe switcher', async () => {
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();

		expect(wrapper.find('.template-overview-header').text()).toContain('Vorlagen für Pfadi');
		expect(wrapper.findAll('.filter-tab--badge')).toHaveLength(2);
		expect(wrapper.find('.template-department-tabs').exists()).toBe(true);
		expect(wrapper.findAll('.event-template-overview-row')).toHaveLength(1);
		expect(wrapper.text()).toContain('{{abteilung}}: {{titel}}');

		await wrapper.findAll('.filter-tab--badge')[1].trigger('click');
		await flushPromises();

		expect(wrapper.findAll('.event-template-overview-row')).toHaveLength(1);
		expect(wrapper.text()).toContain('Leiter: {{titel}}');
		expect(wrapper.text()).not.toContain('{{abteilung}}: {{titel}}');
		wrapper.unmount();
	});

	it('highlights only the selected default and removes the highlight when unset', async () => {
		mocks.templates.push(template({ id: 'alternative', name: 'Alternative', is_default: false }));
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();

		expect(wrapper.get('button[title="Als Standard setzen"]').classes()).not.toContain('active-default');
		const selected = wrapper.get('button[title="Standard entfernen"]');
		expect(selected.classes()).toContain('active-default');

		await selected.trigger('click');
		await flushPromises();

		expect(mocks.updateTemplate).toHaveBeenCalledWith('event-template-pfadi', { is_default: false });
		expect(wrapper.find('button.active-default').exists()).toBe(false);
		wrapper.unmount();
	});

	it('opens the existing editor layout with a name, optional default and Stufe tabs', async () => {
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();

		expect(wrapper.get('.event-template-overview-row button:nth-child(2)').classes()).toEqual(
			expect.arrayContaining(['btn-sm', 'template-overview-row-action']),
		);
		await wrapper.get('.event-template-overview-row button:nth-child(2)').trigger('click');
		await flushPromises();

		expect(wrapper.get('.template-editor-header h2').text()).toBe('Vorlage bearbeiten');
		expect(wrapper.get('.template-editor-back').text()).toBe('← Zurück zu Vorlagen');
		expect(wrapper.findAll('.filter-tab--badge')).toHaveLength(2);
		expect(wrapper.get('.event-template-default').text()).toContain('Standard-Vorlage');
		expect(wrapper.get('#event-template-name').element).toHaveProperty('value', 'Standard');
		expect(wrapper.get('#event-template-name').classes()).toContain('template-editor-field-input');
		expect(wrapper.get('.template-editor-card').find('#event-template-title').exists()).toBe(false);
		expect(wrapper.get('.detail-form > .form-group label').text()).toBe('Titel-Vorlage');
		expect(wrapper.find('.template-editor-header .btn-primary').exists()).toBe(false);
		expect(wrapper.get('.event-template-editor-actions .btn-secondary').text()).toBe('Zurück');
		expect(wrapper.get('.event-template-editor-actions .btn-primary').text()).toBe('Speichern');
		expect(wrapper.get('input[type="checkbox"]').element).toHaveProperty('checked', true);
		expect(wrapper.find('.rich-editor').exists()).toBe(true);

		wrapper.unmount();
	});

	it('shows the same empty-list and create affordances as other template pages', async () => {
		mocks.templates = [];
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		expect(wrapper.get('.empty-state').text()).toBe('Noch keine Vorlagen in dieser Stufe.');
		expect(wrapper.get('.template-overview-create').text()).toBe('+ Neue Vorlage');
		await wrapper.get('.template-overview-create').trigger('click');
		await flushPromises();
		expect(wrapper.get('.event-template-editor-actions .btn-primary').text()).toBe('Speichern');
		expect(wrapper.get('input[type="checkbox"]').element).toHaveProperty('checked', true);
		wrapper.unmount();
	});

	it('saves pending title edits before returning through the editor footer', async () => {
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();

		await wrapper.get('.event-template-overview-row button:nth-child(2)').trigger('click');
		await flushPromises();
		await wrapper.get('#event-template-title').setValue('Neuer Eventtitel');
		await wrapper.get('.event-template-editor-actions .btn-secondary').trigger('click');
		await flushPromises();

		expect(mocks.updateTemplate).toHaveBeenCalledWith(
			'event-template-pfadi',
			{ title: 'Neuer Eventtitel' },
		);
		expect(wrapper.findAll('.event-template-overview-row')).toHaveLength(1);
		wrapper.unmount();
	});

	it('saves before switching Stufe while editing and stays in the editor', async () => {
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();

		await wrapper.get('.event-template-overview-row button:nth-child(2)').trigger('click');
		await flushPromises();
		await wrapper.get('#event-template-title').setValue('Aktualisierter Titel');
		await wrapper.findAll('.filter-tab--badge')[1].trigger('click');
		await flushPromises();

		expect(mocks.updateTemplate).toHaveBeenCalledWith(
			'event-template-pfadi',
			{ title: 'Aktualisierter Titel' },
		);
		expect(wrapper.get('#event-template-title').element).toHaveProperty('value', 'Leiter: {{titel}}');
		expect(wrapper.find('.template-editor-header').exists()).toBe(true);
		wrapper.unmount();
	});

	it('lists multiple templates alphabetically with only the selected Stufe visible', async () => {
		mocks.templates.push(template({ id: 'alternate', name: 'Abend', is_default: false }));
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		const rows = wrapper.findAll('.event-template-overview-row');
		expect(rows).toHaveLength(2);
		expect(rows[0].text()).toContain('Abend');
		expect(rows[1].get('.default-badge').text()).toBe('Standard');
		expect(rows[1].get('.event-template-overview-meta').text()).toContain('{{abteilung}}: {{titel}}');
		expect(rows[1].get('.event-template-overview-meta .event-template-preview').text()).toBe('Eventbeschreibung');
		expect(rows[1].get('button[title="Standard entfernen"]').text()).toBe('⭐ Standard');
		expect(rows[1].get('.template-overview-row-action.danger').text()).toBe('Löschen');
		expect(mocks.join).not.toHaveBeenCalled();
		wrapper.unmount();
	});

	it('creates new templates only on explicit Save, not through autosave', async () => {
		vi.useFakeTimers();
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		await wrapper.get('.template-overview-create').trigger('click');
		await wrapper.get('#event-template-name').setValue('Abend');
		await wrapper.get('#event-template-title').setValue('{{titel}} am Abend');
		await vi.advanceTimersByTimeAsync(500);
		expect(mocks.createTemplate).not.toHaveBeenCalled();
		expect(mocks.updateTemplate).not.toHaveBeenCalled();
		expect(localStorage.getItem('dpw:event-template-new-draft:Pfadi')).toContain('Abend');
		await wrapper.get('.event-template-editor-actions .btn-primary').trigger('click');
		await flushPromises();
		expect(mocks.createTemplate).toHaveBeenCalledWith('Abend', 'Pfadi', '{{titel}} am Abend', '', false);
		expect(mocks.join).toHaveBeenCalledWith('evt_tpl_Pfadi:new-template');
		expect(localStorage.getItem('dpw:event-template-new-draft:Pfadi')).toBeNull();
		await wrapper.get('.template-editor-back').trigger('click');
		await flushPromises();
		expect(wrapper.findAll('.event-template-overview-row')).toHaveLength(2);
		wrapper.unmount();
	});

	it('saves a new draft before Back and restores it without creating a server row', async () => {
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		await wrapper.get('.template-overview-create').trigger('click');
		await wrapper.get('#event-template-name').setValue('Ungespeichert');
		await wrapper.get('.template-editor-back').trigger('click');
		await flushPromises();
		expect(mocks.createTemplate).not.toHaveBeenCalled();
		await wrapper.get('.template-overview-create').trigger('click');
		await flushPromises();
		const restore = wrapper.findAll('button').find(button => button.text() === 'Wiederherstellen')!;
		await restore.trigger('click');
		expect(wrapper.get('#event-template-name').element).toHaveProperty('value', 'Ungespeichert');
		wrapper.unmount();
	});

	it('changes the default without sending potentially stale title or body fields', async () => {
		mocks.templates.push(template({ id: 'alternate', name: 'Abend', is_default: false }));
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		await wrapper.get('button[title="Als Standard setzen"]').trigger('click');
		await flushPromises();
		expect(mocks.updateTemplate).toHaveBeenCalledWith('alternate', { is_default: true });
		expect(wrapper.findAll('.default-badge')).toHaveLength(1);
		expect(wrapper.get('.event-template-overview-row .default-badge').text()).toBe('Standard');
		wrapper.unmount();
	});

	it('confirms deletion and does not promote an alternative when the default is deleted', async () => {
		mocks.templates.push(template({ id: 'alternate', name: 'Zweiter', is_default: false }));
		vi.stubGlobal('confirm', vi.fn(() => false));
		const confirm = vi.mocked(window.confirm);
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		await wrapper.get('.event-template-overview-row .danger').trigger('click');
		expect(mocks.deleteTemplate).not.toHaveBeenCalled();
		confirm.mockReturnValue(true);
		await wrapper.get('.event-template-overview-row .danger').trigger('click');
		await flushPromises();
		expect(mocks.deleteTemplate).toHaveBeenCalledWith('event-template-pfadi');
		expect(wrapper.findAll('.event-template-overview-row')).toHaveLength(1);
		expect(wrapper.find('.default-badge').exists()).toBe(false);
		wrapper.unmount();
	});

	it('retains the editor and Stufe if the pending save fails', async () => {
		mocks.updateTemplate.mockResolvedValue(null);
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		await wrapper.get('.event-template-overview-row button:nth-child(2)').trigger('click');
		await wrapper.get('#event-template-title').setValue('Lokal erhalten');
		await wrapper.findAll('.filter-tab--badge')[1].trigger('click');
		await flushPromises();
		expect(wrapper.get('#event-template-title').element).toHaveProperty('value', 'Lokal erhalten');
		expect(localStorage.getItem('dpw:event-template-draft:Pfadi:event-template-pfadi')).toContain('Lokal erhalten');
		await wrapper.get('.template-editor-back').trigger('click');
		await flushPromises();
		expect(wrapper.find('.template-editor-header').exists()).toBe(true);
		wrapper.unmount();
	});

	it('preserves edits made while autosave is in flight and flushes the newest value before Back', async () => {
		vi.useFakeTimers();
		let completeSave!: (value: EventTemplate) => void;
		mocks.updateTemplate.mockImplementationOnce(() => new Promise<EventTemplate>(resolve => { completeSave = resolve; }));
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		await wrapper.get('.event-template-overview-row button:nth-child(2)').trigger('click');
		await wrapper.get('#event-template-title').setValue('Erste Änderung');
		await vi.advanceTimersByTimeAsync(100);
		await wrapper.get('#event-template-title').setValue('Neueste Änderung');
		await wrapper.get('.template-editor-back').trigger('click');
		completeSave(template({ title: 'Erste Änderung' }));
		await flushPromises();
		expect(mocks.updateTemplate).toHaveBeenLastCalledWith('event-template-pfadi', { title: 'Neueste Änderung' });
		expect(wrapper.find('.template-editor-header').exists()).toBe(false);
		expect(localStorage.getItem('dpw:event-template-draft:Pfadi:event-template-pfadi')).toBeNull();
		wrapper.unmount();
	});

	it('isolates template rooms and merges remote fields without overwriting dirty local edits', async () => {
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		await wrapper.get('.event-template-overview-row button:nth-child(2)').trigger('click');
		expect(mocks.join).toHaveBeenLastCalledWith('evt_tpl_Pfadi:event-template-pfadi');
		await wrapper.get('#event-template-title').setValue('Meine Änderung');
		mocks.onMessage!({ event: 'event_template_updated', template: template({ title: 'Fremder Titel', body: '<p>Gemeinsam</p>' }) });
		await flushPromises();
		expect(wrapper.get('#event-template-title').element).toHaveProperty('value', 'Meine Änderung');
		expect(wrapper.get('.rich-editor').element.innerHTML).toBe('<p>Gemeinsam</p>');
		mocks.onMessage!({ event: 'locks_state', activity_id: 'evt_tpl_Pfadi:event-template-pfadi',
			locks: [{ section: 'evt_tpl_body', user: 'Ben' }] });
		await flushPromises();
		expect(wrapper.get('.rich-editor').attributes('contenteditable')).toBe('false');
		wrapper.unmount();
	});

	it('recovers a legacy department draft into the original template', async () => {
		localStorage.setItem('dpw:event-template-draft:Pfadi', JSON.stringify({
			version: 1, department: 'Pfadi', savedAt: Date.now(), title: 'Alter Entwurf', body: '<p>Erhalten</p>',
		}));
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		await wrapper.get('.event-template-overview-row button:nth-child(2)').trigger('click');
		await flushPromises();
		await wrapper.findAll('button').find(button => button.text() === 'Wiederherstellen')!.trigger('click');
		await wrapper.get('.template-editor-back').trigger('click');
		await flushPromises();
		expect(mocks.updateTemplate).toHaveBeenCalledWith('event-template-pfadi', {
			name: 'Standard', is_default: true, title: 'Alter Entwurf', body: '<p>Erhalten</p>',
		});
		expect(localStorage.getItem('dpw:event-template-draft:Pfadi')).toBeNull();
		wrapper.unmount();
	});

	it('keeps a remotely deleted template as an explicit new draft without automatic recreation', async () => {
		vi.useFakeTimers();
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		await wrapper.get('.event-template-overview-row button:nth-child(2)').trigger('click');
		await wrapper.get('#event-template-title').setValue('Mein Inhalt');
		mocks.onMessage!({ event: 'event_template_deleted', id: 'event-template-pfadi', department: 'Pfadi' });
		await vi.advanceTimersByTimeAsync(500);
		expect(mocks.createTemplate).not.toHaveBeenCalled();
		expect(wrapper.get('#event-template-title').element).toHaveProperty('value', 'Mein Inhalt');
		expect(localStorage.getItem('dpw:event-template-new-draft:Pfadi')).toContain('Mein Inhalt');
		wrapper.unmount();
	});

	it('does not let a response from a closed editor delete a newer local draft', async () => {
		vi.useFakeTimers();
		let completeSave!: (value: EventTemplate) => void;
		mocks.updateTemplate.mockImplementationOnce(() => new Promise<EventTemplate>(resolve => { completeSave = resolve; }));
		const wrapper = shallowMount(EventTemplatePage);
		await flushPromises();
		await wrapper.get('.event-template-overview-row button:nth-child(2)').trigger('click');
		await wrapper.get('#event-template-title').setValue('Erste Änderung');
		await vi.advanceTimersByTimeAsync(100);
		wrapper.unmount();
		localStorage.setItem('dpw:event-template-draft:Pfadi:event-template-pfadi', JSON.stringify({
			version: 1, department: 'Pfadi', templateId: 'event-template-pfadi', savedAt: Date.now(),
			title: 'Neuerer Entwurf', body: '<p>Erhalten</p>',
		}));
		completeSave(template({ title: 'Erste Änderung' }));
		await flushPromises();
		expect(localStorage.getItem('dpw:event-template-draft:Pfadi:event-template-pfadi')).toContain('Neuerer Entwurf');
	});
});
