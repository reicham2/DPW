// @vitest-environment jsdom
import { flushPromises, shallowMount } from '@vue/test-utils';
import { readFileSync } from 'node:fs';
import { URL as NodeURL } from 'node:url';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { Activity, ActivityInput, EventTemplate, IdeenkisteItem, User, WsEvent } from '../types';
import PublicPageHeader from '../components/PublicPageHeader.vue';

const mocks = vi.hoisted(() => ({
	replace: vi.fn(),
	fetchActivity: vi.fn(),
	updateActivity: vi.fn(),
	uploadAttachment: vi.fn(),
	fetchForm: vi.fn(),
	fetchActivityTemplates: vi.fn(),
	publishEvent: vi.fn(),
	fetchPublication: vi.fn(),
	setLastUpdated: undefined as ((activity: Activity | null) => void) | undefined,
	setActivityError: undefined as ((message: string | null) => void) | undefined,
	setConnected: undefined as ((connected: boolean) => void) | undefined,
	onMessage: undefined as ((event: WsEvent) => void) | undefined,
	guestSocketHandler: undefined as ((event: WsEvent) => void) | undefined,
	guestSocket: null as null | {
		sessionId: string;
		join: (token: string, activityId: string) => void;
		send: (message: Record<string, unknown>) => void;
		disconnect: () => void;
	},
	editable: false,
	isGuestEdit: false,
	guestToken: '',
	activities: [] as Activity[],
	ideaItems: [] as IdeenkisteItem[],
}));

vi.mock('vue-router', () => ({
	useRoute: () => ({
		params: { id: 'activity-1' },
		query: {},
		path: mocks.isGuestEdit ? '/guest-edit' : '/activities/activity-1',
		hash: mocks.isGuestEdit ? `#${mocks.guestToken}` : '',
	}),
	useRouter: () => ({ replace: mocks.replace, push: vi.fn() }),
}));

vi.mock('../composables/useActivities', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	const noop = vi.fn().mockResolvedValue(undefined);
	const lastUpdatedActivity = ref<Activity | null>(null);
	const error = ref<string | null>(null);
	const connected = ref(true);
	mocks.setLastUpdated = (activity) => { lastUpdatedActivity.value = activity; };
	mocks.setActivityError = (message) => { error.value = message; };
	mocks.setConnected = (value) => { connected.value = value; };
	return {
		useActivities: () => ({
			activities: ref(mocks.activities),
			fetchActivities: noop,
			fetchActivity: mocks.fetchActivity,
			fetchDepartments: noop,
			fetchLocations: noop,
			fetchMaterials: noop,
			updateActivity: mocks.updateActivity,
			deleteActivity: noop,
			fetchAttachments: vi.fn().mockResolvedValue([]),
			uploadAttachment: mocks.uploadAttachment,
			deleteAttachment: noop,
			getAttachmentBlobUrl: noop,
			error,
			connected,
			lastUpdatedActivity,
			departments: ref([]),
			predefinedLocations: ref([]),
			predefinedMaterials: ref([]),
		}),
	};
});

vi.mock('../composables/useUsers', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { useUsers: () => ({ users: ref([]), fetchUsers: vi.fn() }) };
});

vi.mock('../composables/usePermissions', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		usePermissions: () => ({
			myPermissions: ref(mocks.editable ? { activity_edit_scope: 'all', event_publish_scope: 'all' } : null),
			fetchMyPermissions: vi.fn().mockResolvedValue(null),
			writableDepts: () => [],
			canReadActivity: () => true,
			canForms: () => false,
			canIdeenkiste: () => mocks.editable,
			canIdeenkisteAdd: () => false,
		}),
	};
});

vi.mock('../composables/useAuth', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { user: ref(null) };
});

vi.mock('../composables/useWebSocket', () => ({
	createGuestWebSocket: (handler: (event: WsEvent) => void) => {
		mocks.guestSocketHandler = handler;
		mocks.guestSocket = {
			sessionId: '11111111-1111-4111-8111-111111111111',
			join: vi.fn(),
			send: vi.fn(),
			disconnect: vi.fn(),
		};
		return mocks.guestSocket;
	},
	wsSend: vi.fn(),
	wsRegister: vi.fn(),
	wsJoin: vi.fn(),
	wsLeave: vi.fn(),
	useWebSocket: (handler: (event: WsEvent) => void) => {
		mocks.onMessage = handler;
		return {};
	},
}));

vi.mock('../composables/useForms', () => ({ useForms: () => ({ fetchForm: mocks.fetchForm }) }));
vi.mock('../composables/useEventTemplates', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { useEventTemplates: () => ({
		fetchActivityTemplates: mocks.fetchActivityTemplates,
		fetchPublication: mocks.fetchPublication,
		publishEvent: mocks.publishEvent,
		unpublishEvent: vi.fn(),
		error: ref<string | null>(null),
	}) };
});
vi.mock('../composables/useApi', () => ({
	apiFetch: vi.fn().mockResolvedValue(new Response('{}', { status: 200 })),
	formatApiError: (error: unknown) => error instanceof Error ? error.message : String(error),
}));
vi.mock('../composables/useIdeaBox', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		useIdeaBox: () => ({
			items: ref(mocks.ideaItems),
			fetchItems: vi.fn(),
			createItem: vi.fn(),
			deleteItem: vi.fn(),
		}),
	};
});
vi.mock('../composables/useActivityExport', () => ({ useActivityExport: () => ({ exportToWord: vi.fn() }) }));
vi.mock('../composables/useUserResolver', () => ({ useUserResolver: () => ({ resolveResponsibleName: vi.fn() }) }));

import DetailPage from './DetailPage.vue';
import ErrorAlert from '../components/ErrorAlert.vue';
import { apiFetch } from '../composables/useApi';
import { user } from '../composables/useAuth';
import { config } from '../config';
import * as richTextToolbar from '../utils/richTextToolbar';

const initialAutosaveDebounce = config.AUTOSAVE_DEBOUNCE;
const initialQueryCommandState = Object.getOwnPropertyDescriptor(document, 'queryCommandState');

function makeActivity(overrides: Partial<Activity> = {}): Activity {
	return {
		id: 'activity-1',
		title: 'Test activity',
		date: '2026-07-01',
		start_time: '10:00',
		end_time: '11:00',
		goal: '',
		guiding_thread: null,
		location: '',
		responsible: [],
		department: null,
		material: [],
		tn_material: [],
		siko_text: null,
		bad_weather_info: null,
		planned_participants_estimate: null,
		created_at: '2026-01-01T00:00:00Z',
		updated_at: '2026-01-01T00:00:00Z',
		programs: [],
		...overrides,
	};
}

function stubInsertTextCommand(editor: HTMLElement) {
	const descriptor = Object.getOwnPropertyDescriptor(document, 'execCommand');
	const queryDescriptor = Object.getOwnPropertyDescriptor(document, 'queryCommandState');
	const command = vi.fn((name: string, _showUI?: boolean, value?: string) => {
		if (name !== 'insertText') return false;
		const selection = document.getSelection();
		if (!selection?.rangeCount) return false;
		const range = selection.getRangeAt(0);
		range.deleteContents();
		const inserted = document.createTextNode(value ?? '');
		range.insertNode(inserted);
		range.setStartAfter(inserted);
		range.collapse(true);
		selection.removeAllRanges();
		selection.addRange(range);
		editor.dispatchEvent(new Event('input', { bubbles: true }));
		return true;
	});
	Object.defineProperty(document, 'execCommand', {
		configurable: true,
		value: command,
	});
	Object.defineProperty(document, 'queryCommandState', {
		configurable: true,
		value: vi.fn(() => false),
	});
	return {
		command,
		restore() {
			if (descriptor) Object.defineProperty(document, 'execCommand', descriptor);
			else Reflect.deleteProperty(document, 'execCommand');
			if (queryDescriptor) Object.defineProperty(document, 'queryCommandState', queryDescriptor);
			else Reflect.deleteProperty(document, 'queryCommandState');
		},
	};
}

function stubProofreadingRects() {
	const descriptor = Object.getOwnPropertyDescriptor(Range.prototype, 'getClientRects');
	Object.defineProperty(Range.prototype, 'getClientRects', {
		configurable: true,
		value: () => [{ left: 10, right: 100, top: 10, bottom: 30 }],
	});
	return () => {
		if (descriptor) Object.defineProperty(Range.prototype, 'getClientRects', descriptor);
		else Reflect.deleteProperty(Range.prototype, 'getClientRects');
	};
}

function allowEditing() {
	mocks.editable = true;
	const editableUser: User = {
		id: 'user-1',
		microsoft_oid: 'oid-1',
		email: 'test@example.com',
		display_name: 'Test User',
		department: null,
		role: 'leader',
		time_display_mode: 'minutes',
		proofreading_enabled: true,
		notify_material_assigned: false,
		notify_activity_assigned: false,
		notify_activity_reminder: false,
		activity_reminder_hours: 24,
		notify_program_assigned: false,
		notify_mail_own_activity: false,
		notify_mail_department: false,
		notify_channel_websocket: false,
		notify_channel_email: false,
		avatar_color: '#000000',
		created_at: '',
		updated_at: '',
	};
	user.value = editableUser;
}

function makeEventTemplate(overrides: Partial<EventTemplate> = {}): EventTemplate {
	return {
		id: 'template-default', name: 'Standard', department: 'Pfadi', is_default: true,
		title: '{{abteilung}}: {{titel}}', body: '<p>{{datum_kurz|-7}} / {{formular_link|Anmelden}}</p>',
		created_at: '', updated_at: '', ...overrides,
	};
}

describe('DetailPage activity access', () => {
	beforeEach(() => {
		vi.stubGlobal('URL', class extends URL {
			static createObjectURL = vi.fn(() => 'blob:program-image');
			static revokeObjectURL = vi.fn();
		});
		Object.defineProperty(document, 'queryCommandState', { configurable: true, value: vi.fn(() => false) });
		config.MIDATA_ENABLED = false;
		config.WP_PUBLISHING_ENABLED = false;
		config.PUBLIC_BASE_URL = '';
		mocks.editable = false;
		mocks.isGuestEdit = false;
		mocks.guestToken = '';
		mocks.activities = [];
		mocks.ideaItems = [];
		user.value = null;
		sessionStorage.clear();
		vi.mocked(apiFetch).mockReset().mockResolvedValue(new Response('{}', { status: 200 }));
		mocks.replace.mockReset().mockResolvedValue(undefined);
		mocks.onMessage = undefined;
		mocks.guestSocketHandler = undefined;
		mocks.guestSocket = null;
		mocks.fetchActivity.mockReset().mockResolvedValue(makeActivity());
		mocks.updateActivity.mockReset().mockResolvedValue(true);
		mocks.uploadAttachment.mockReset().mockResolvedValue(null);
		mocks.fetchForm.mockReset().mockResolvedValue(null);
		mocks.fetchActivityTemplates.mockReset().mockResolvedValue([]);
		mocks.publishEvent.mockReset().mockResolvedValue(null);
		mocks.fetchPublication.mockReset().mockResolvedValue(null);
		mocks.setLastUpdated?.(null);
		mocks.setActivityError?.(null);
		mocks.setConnected?.(true);
	});

	afterEach(() => {
		if (initialQueryCommandState) Object.defineProperty(document, 'queryCommandState', initialQueryCommandState);
		else Reflect.deleteProperty(document, 'queryCommandState');
		vi.clearAllMocks();
		vi.restoreAllMocks();
		vi.unstubAllGlobals();
		vi.useRealTimers();
		config.MIDATA_ENABLED = false;
		config.WP_PUBLISHING_ENABLED = false;
		config.PUBLIC_BASE_URL = '';
		config.AUTOSAVE_DEBOUNCE = initialAutosaveDebounce;
		user.value = null;
		localStorage.removeItem('dpw:activity-edit-draft:activity-1');
	});

	it('uses the normal editor language for guests while showing only scoped editing controls', async () => {
		mocks.isGuestEdit = true;
		mocks.guestToken = 'a'.repeat(64);
		const activity = makeActivity({
			title: 'Sommerlager',
			date: '2026-07-01',
			start_time: '10:00',
			end_time: '11:00',
			guiding_thread: 'Gemeinsam unterwegs',
			responsible: ['activity-person'],
			material: [{ name: 'Seil', responsible: ['material-person'] }],
			programs: [{
				duration_minutes: 15,
				title: 'Ankommen',
				description: '<p>Gemeinsam starten</p>',
				responsible: ['program-person'],
			}],
		});
		const guestFetch = vi.fn().mockImplementation(async (url: string) => new Response(JSON.stringify(
			url.endsWith('/people')
				? { people: [
					{ id: 'activity-person', display_name: 'Activity Person' },
					{ id: 'program-person', display_name: 'Program Person' },
					{ id: 'material-person', display_name: 'Material Person' },
				] }
				: { activity, expires_at: '2030-07-08T12:00:00Z' },
		), { status: 200 }));
		vi.stubGlobal('fetch', guestFetch);

		const wrapper = shallowMount(DetailPage);
		await flushPromises();

		expect(wrapper.get('.guest-edit-intro').text()).toContain('Sommerlager');
		expect(wrapper.get('.guest-edit-intro').text()).toContain('Änderungen werden automatisch gespeichert');
		expect(wrapper.get('.guest-edit-intro').text()).toContain('Link gültig bis');
		expect(wrapper.get('.guest-edit-intro').classes()).toContain('guest-edit-intro--spaced');
		expect(wrapper.findComponent(PublicPageHeader).props('label')).toBe('Gastbearbeitung');
		expect(wrapper.find('.header').exists()).toBe(false);
		expect(wrapper.find('.btn-back').exists()).toBe(false);
		expect(wrapper.find('.header-right').exists()).toBe(false);
		expect(wrapper.find('#edit-title').exists()).toBe(false);
		expect(wrapper.find('#edit-guiding-thread').exists()).toBe(true);
		expect(wrapper.find('#edit-goal').exists()).toBe(true);
		expect(wrapper.find('#edit-attachment').exists()).toBe(true);
		expect(wrapper.find('.program-section-heading').exists()).toBe(true);
		expect(wrapper.get('.detail-form').classes()).toContain('detail-form--guest');
		expect(wrapper.find('.detail-stats-sidebar--edit').exists()).toBe(false);
		expect(wrapper.find('.detail-stats-popout__panel').exists()).toBe(false);
		expect(wrapper.text()).toContain('Program Person');
		expect(wrapper.text()).toContain('Material Person');
		expect(wrapper.text()).not.toContain('Veröffentlichen');
		expect(wrapper.text()).not.toContain('Formular');
		expect(wrapper.findAll('.guest-edit-sections__item').map((button) => button.text())).toEqual([
			'Roter Faden',
			'Programmpunkte',
			'Material',
			'TN-Material',
			'Sicherheit',
			'Ziel & Wetter',
			'Anhänge',
		]);
		expect(guestFetch).toHaveBeenCalledWith('/api/guest-edit/activity', expect.objectContaining({
			headers: expect.any(Headers),
			cache: 'no-store',
		}));
		const guestRequestOptions = guestFetch.mock.calls[0]?.[1] as RequestInit;
		expect(new Headers(guestRequestOptions.headers).get('X-Guest-Edit-Token')).toBe(mocks.guestToken);
		const peopleRequest = guestFetch.mock.calls.find(([url]) => url.endsWith('/people'));
		expect(peopleRequest).toBeDefined();
		expect(new Headers((peopleRequest?.[1] as RequestInit).headers).get('X-Guest-Edit-Token')).toBe(mocks.guestToken);
		expect(mocks.guestSocket?.join).toHaveBeenCalledWith(mocks.guestToken, activity.id);

		const programSearch = wrapper.find('input[placeholder="Person suchen oder eingeben…"]');
		await programSearch.trigger('focus');
		await programSearch.trigger('focusin');
		await flushPromises();
		expect(mocks.guestSocket?.send).toHaveBeenCalledWith({
			type: 'lock',
			activity_id: activity.id,
			section: 'program_0',
		});
		const searchResults = wrapper.findAll('.user-dropdown-item').map((item) => item.text());
		expect(searchResults).toContain('Activity Person');
		expect(searchResults).toContain('Material Person');
		expect(searchResults).not.toContain('Outside Person');

		mocks.guestSocketHandler?.({
			event: 'lock',
			activity_id: activity.id,
			section: 'program_0',
			user: 'Gast',
			owner_id: mocks.guestSocket?.sessionId,
		});
		await flushPromises();
		expect(programSearch.attributes('disabled')).toBeUndefined();
		mocks.guestSocketHandler?.({
			event: 'lock',
			activity_id: activity.id,
			section: 'program_0',
			user: 'Gast',
			owner_id: '22222222-2222-4222-8222-222222222222',
		});
		await flushPromises();
		expect(programSearch.attributes('disabled')).toBeDefined();
		wrapper.unmount();
		expect(mocks.guestSocket?.disconnect).toHaveBeenCalledOnce();
	});

	it('opens one sharing dialog with the read-only link selected by default and guest editing as an explicit option', async () => {
		allowEditing();
		config.PUBLIC_BASE_URL = 'https://dpw.test';
		vi.mocked(apiFetch).mockImplementation(async (url, options) => {
			if (url.endsWith('/share')) {
				return new Response(JSON.stringify({
					share_token: 'read-token',
					share_url: 'https://dpw.test/shared/read-token',
				}), { status: 200 });
			}
			if (url.endsWith('/guest-edit-link') && options?.method !== 'POST') {
				return new Response(JSON.stringify({ expires_at: null, max_expires_in_days: 4 }), { status: 200 });
			}
			if (url.endsWith('/guest-edit-link') && options?.method === 'POST') {
				return new Response(JSON.stringify({
					share_url: 'https://dpw.test/guest-edit#guest-token',
					expires_at: '2026-07-09T12:00:00Z',
				}), { status: 200 });
			}
			return new Response('{}', { status: 200 });
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('.activity-actions-desktop button[title="Freigabelink auswählen"]').trigger('click');

		const dialog = wrapper.get('[role="dialog"]');
		expect(dialog.attributes('aria-modal')).toBe('true');
		expect((dialog.get('input[value="read"]').element as HTMLInputElement).checked).toBe(true);
		expect(dialog.get('input[value="guest"]').exists()).toBe(true);
		expect(dialog.text()).toContain('Leselink kopieren');
		expect(dialog.findAll('button').filter((button) => button.text() === 'Schliessen')).toHaveLength(1);

		await dialog.get('input[value="guest"]').setValue();
		const expiryInput = dialog.get('input[type="number"]');
		expect(expiryInput.element).toHaveProperty('value', '4');
		expect(expiryInput.attributes('max')).toBe('4');
		expect(dialog.text()).toContain('Gültigkeit:');
		expect(dialog.text()).toContain('Tage (höchstens 4 Tage)');
		expect(dialog.findAll('button').some((button) => button.text() === 'Gast-Editierlink')).toBe(false);
		await expiryInput.setValue('5');
		expect(dialog.get('button.btn-primary').attributes('disabled')).toBeDefined();
		await expiryInput.setValue('4');
		await dialog.get('button.btn-primary').trigger('click');
		await flushPromises();

		expect(apiFetch).toHaveBeenCalledWith(
			'/api/activities/activity-1/guest-edit-link',
			expect.objectContaining({ method: 'POST', body: JSON.stringify({ expires_in_days: 4 }) }),
		);
		expect(dialog.text()).toContain('Link kopieren');
		wrapper.unmount();
	});

	it('keeps the read-only share dialog available without exposing guest-link controls to non-editors', async () => {
		config.PUBLIC_BASE_URL = 'https://dpw.test';
		vi.mocked(apiFetch).mockImplementation(async (url) => {
			if (url.endsWith('/share')) {
				return new Response(JSON.stringify({
					share_token: 'read-token',
					share_url: 'https://dpw.test/shared/read-token',
				}), { status: 200 });
			}
			return new Response('{}', { status: 200 });
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('.activity-actions-desktop button[title="Freigabelink auswählen"]').trigger('click');

		const dialog = wrapper.get('[role="dialog"]');
		expect((dialog.get('input[value="read"]').element as HTMLInputElement).checked).toBe(true);
		expect(dialog.find('input[value="guest"]').exists()).toBe(false);
		expect(dialog.find('select').exists()).toBe(false);
		wrapper.unmount();
	});

	it('opens the same sharing dialog from the mobile actions menu', async () => {
		config.PUBLIC_BASE_URL = 'https://dpw.test';
		vi.mocked(apiFetch).mockImplementation(async (url) => {
			if (url.endsWith('/share')) {
				return new Response(JSON.stringify({
					share_token: 'read-token',
					share_url: 'https://dpw.test/shared/read-token',
				}), { status: 200 });
			}
			return new Response('{}', { status: 200 });
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('.activity-actions-mobile-toggle').trigger('click');
		const shareAction = wrapper.get('.activity-actions-mobile-menu').findAll('button')
			.find((button) => button.text().trim() === 'Teilen');
		expect(shareAction).toBeDefined();
		await shareAction?.trigger('click');
		expect(wrapper.get('[role="dialog"]').text()).toContain('Leselink kopieren');
		wrapper.unmount();
	});

	it('uploads inline at the caret and autosaves durable references, not blob URLs', async () => {
		allowEditing();
		vi.useFakeTimers();
		const imageId = '12345678-1234-1234-1234-123456789abc';
		const path = `/api/program-images/${imageId}`;
		const file = new File(['bytes'], 'Bild.png', { type: 'image/png' });
		mocks.fetchActivity.mockResolvedValue(makeActivity({ programs: [{
			id: 'program-1', activity_id: 'activity-1', duration_minutes: 30,
			title: 'Programm', description: '<p>Vorher Danach</p>', responsible: [],
		}] }));
		mocks.uploadAttachment.mockResolvedValue({ id: imageId, activity_id: 'activity-1', filename: file.name, content_type: file.type, created_at: '' });
		vi.mocked(apiFetch).mockImplementation(async (url) => url === path
			? new Response('image', { headers: { 'Content-Type': 'image/png' } })
			: new Response('{}'));
		vi.spyOn(URL, 'createObjectURL').mockReturnValue('blob:inline-image');
		vi.spyOn(URL, 'revokeObjectURL').mockImplementation(() => {});
		const wrapper = shallowMount(DetailPage, { attachTo: document.body });
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await flushPromises();
		const editor = wrapper.get<HTMLElement>('.rich-editor--compact');
		editor.element.focus();
		const range = document.createRange();
		range.setStart(editor.element.querySelector('p')!.firstChild!, 7);
		range.collapse(true);
		window.getSelection()!.removeAllRanges();
		window.getSelection()!.addRange(range);
		const input = wrapper.get<HTMLInputElement>('#program-image-input-0');
		Object.defineProperty(input.element, 'files', { configurable: true, value: [file] });
		await input.trigger('change');
		await flushPromises();
		expect(mocks.uploadAttachment).toHaveBeenCalledWith('activity-1', file, true);
		expect(editor.get('img').attributes('src')).toBe('blob:inline-image');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		const saved = mocks.updateActivity.mock.lastCall?.[1].programs[0].description as string;
		expect(saved).toMatch(/Vorher <img[^>]*>Danach/);
		expect(saved).toContain(path);
		expect(saved).not.toMatch(/blob:|data-program-image|hochgeladen/);
		wrapper.unmount();
	});

	it('keeps a pasted image with its program when reordered during upload', async () => {
		allowEditing();
		vi.useFakeTimers();
		const imageId = '12345678-1234-1234-1234-123456789abc';
		const path = `/api/program-images/${imageId}`;
		let finishUpload: ((image: unknown) => void) | undefined;
		mocks.uploadAttachment.mockImplementation(() => new Promise(resolve => { finishUpload = resolve; }));
		mocks.fetchActivity.mockResolvedValue(makeActivity({ programs: ['First', 'Second'].map((title, index) => ({
			id: `program-${index}`, activity_id: 'activity-1', duration_minutes: 10,
			title, description: title, responsible: [],
		})) }));
		vi.mocked(apiFetch).mockImplementation(async (url) => url === path
			? new Response('image', { headers: { 'Content-Type': 'image/png' } }) : new Response('{}'));
		vi.spyOn(URL, 'createObjectURL').mockReturnValue('blob:pasted-image');
		vi.spyOn(URL, 'revokeObjectURL').mockImplementation(() => {});
		const wrapper = shallowMount(DetailPage, { attachTo: document.body });
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await flushPromises();
		const editor = wrapper.findAll<HTMLElement>('.rich-editor--compact')[0]!;
		editor.element.focus();
		const range = document.createRange();
		range.selectNodeContents(editor.element);
		range.collapse(false);
		window.getSelection()!.removeAllRanges();
		window.getSelection()!.addRange(range);
		const event = new Event('paste', { bubbles: true, cancelable: true });
		const file = new File(['bytes'], 'Paste.png', { type: 'image/png' });
		Object.defineProperty(event, 'clipboardData', { value: { files: [file], items: [], getData: () => '' } });
		editor.element.dispatchEvent(event);
		await flushPromises();
		expect(editor.find('[data-program-image-upload]').exists()).toBe(true);
		const cards = wrapper.findAll('.program-card');
		await cards[0]!.get('.program-card__drag-handle').trigger('dragstart', {
			dataTransfer: { effectAllowed: '', setData: vi.fn() },
		});
		await cards[1]!.trigger('drop');
		await flushPromises();
		expect(wrapper.findAll('.rich-editor--compact')[1]!.element).toBe(editor.element);
		expect(editor.find('[data-program-image-upload]').exists()).toBe(true);
		finishUpload!({ id: imageId, activity_id: 'activity-1', filename: file.name, content_type: file.type, created_at: '' });
		await flushPromises();
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		const programs = mocks.updateActivity.mock.lastCall?.[1].programs;
		expect(programs.map((program: { title: string }) => program.title)).toEqual(['Second', 'First']);
		expect(programs[1].description).toContain(path);
		expect(programs[0].description).not.toContain(path);
		wrapper.unmount();
	});

	it('keeps upload failures visible after text autosave and never inserts untrusted pasted HTML', async () => {
		allowEditing();
		vi.useFakeTimers();
		mocks.fetchActivity.mockResolvedValue(makeActivity({ programs: [{
			id: 'program-1', activity_id: 'activity-1', duration_minutes: 30,
			title: 'Programm', description: 'Legacy', responsible: [],
		}] }));
		mocks.uploadAttachment.mockImplementation(async () => {
			mocks.setActivityError?.('Bild-Upload fehlgeschlagen');
			return null;
		});
		mocks.updateActivity.mockImplementation(async () => { mocks.setActivityError?.(null); return true; });
		const wrapper = shallowMount(DetailPage, { attachTo: document.body });
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await flushPromises();
		const editor = wrapper.get<HTMLElement>('.rich-editor--compact');
		const event = new Event('paste', { bubbles: true, cancelable: true });
		Object.defineProperty(event, 'clipboardData', { value: {
			files: [new File(['bytes'], 'Paste.png', { type: 'image/png' })], items: [],
			getData: (type: string) => type === 'text/html' ? '<b>Text</b><img src="file:///paste.png" onerror="alert(1)"><script>alert(1)</script>' : '',
		} });
		editor.element.dispatchEvent(event);
		await flushPromises();
		expect(event.defaultPrevented).toBe(true);
		expect(editor.html()).not.toMatch(/onerror|script|hochgeladen|file:/);
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(wrapper.getComponent(ErrorAlert).props('error')).toContain('Bild-Upload fehlgeschlagen');
		expect(mocks.updateActivity.mock.lastCall?.[1].programs[0].description).toContain('<b>Text</b>');
		wrapper.unmount();
	});

	it('honors program locks for image upload and paste', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({ programs: [{
			id: 'program-1', activity_id: 'activity-1', duration_minutes: 30,
			title: 'Programm', description: 'Unchanged', responsible: [],
		}] }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		mocks.onMessage?.({ event: 'lock', activity_id: 'activity-1', section: 'program_0', user: 'Other editor' });
		await flushPromises();
		expect(wrapper.get<HTMLInputElement>('#program-image-input-0').element.disabled).toBe(true);
		expect(wrapper.get('.rich-editor--compact').attributes('contenteditable')).toBe('false');
		await wrapper.get('.rich-editor--compact').trigger('paste', {
			clipboardData: { files: [new File(['bytes'], 'Paste.png', { type: 'image/png' })], items: [], getData: () => '' },
		});
		expect(mocks.uploadAttachment).not.toHaveBeenCalled();
		wrapper.unmount();
	});

	it('leaves the detail page only when access to the displayed activity is removed', async () => {
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		expect(wrapper.text()).toContain('Test activity');
		expect(mocks.onMessage).toBeDefined();

		mocks.onMessage?.({ event: 'deleted', id: 'another-activity' });
		expect(mocks.replace).not.toHaveBeenCalled();

		mocks.onMessage?.({ event: 'deleted', id: 'activity-1' });
		await flushPromises();
		expect(mocks.replace).toHaveBeenCalledWith('/');
		expect(wrapper.text()).not.toContain('Test activity');
		wrapper.unmount();
	});

	it('keeps proofreading open and rechecks offsets after successive corrections', async () => {
		allowEditing();
		const description = 'Ich habe ein Fehler und ein Fehler.';
		mocks.fetchActivity.mockResolvedValue(makeActivity({
			programs: [{
				id: 'program-1',
				activity_id: 'activity-1',
				duration_minutes: 10,
				title: 'Start',
				description,
				responsible: [],
			}],
		}));
		const requestTexts: string[] = [];
		vi.mocked(apiFetch).mockImplementation(async (url, options) => {
			if (url !== '/api/proofreading/check') return new Response('{}', { status: 200 });
			const text = JSON.parse(options?.body as string).text as string;
			requestTexts.push(text);
			const matches = [...text.matchAll(/ein Fehler/g)].map((match) => ({
				offset: match.index!,
				length: match[0].length,
				message: 'Kasus prüfen',
				shortMessage: 'Kasus',
				replacements: [{ value: 'einen Fehler' }, { value: 'den Fehler' }],
				rule: { id: 'OBJECT_AGREEMENT' },
			}));
			return new Response(JSON.stringify({ language: 'de-CH', matches }), { status: 200 });
		});

		const wrapper = shallowMount(DetailPage, { attachTo: document.body });
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.program-card__toggle').trigger('click');
		await flushPromises();
		const editor = wrapper.get('.rich-editor').element as HTMLElement;
		const execCommand = stubInsertTextCommand(editor);
		const restoreRects = stubProofreadingRects();
		try {
			vi.useFakeTimers();
			editor.textContent = description + ' ';
			editor.dispatchEvent(new Event('input', { bubbles: true }));
			await vi.advanceTimersByTimeAsync(1500);
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(false);
			await wrapper.get('.rich-editor').trigger('click', { clientX: 20, clientY: 20 });
			const suggestions = wrapper.get('.program-proofreading__suggestions');
			expect(suggestions.findAll('button').map(button => button.text())).toEqual(['einen Fehler', 'den Fehler']);
			expect(suggestions.findAll('button').map(button => button.attributes('aria-label')))
				.toEqual(['einen Fehler übernehmen', 'den Fehler übernehmen']);
			expect(suggestions.text()).not.toContain('übernehmen');
			const style = document.createElement('style');
			const mainCss = readFileSync(new NodeURL('../assets/main.css', import.meta.url), 'utf8');
			const suggestionRule = mainCss.match(/\.program-proofreading__suggestion\s*\{[^}]*\}/);
			const layoutRule = mainCss.match(/\.program-proofreading__suggestions\s*\{[^}]*\}/);
			expect(suggestionRule).not.toBeNull();
			expect(layoutRule).not.toBeNull();
			style.textContent = `${suggestionRule![0]}\n${layoutRule![0]}`;
			document.head.appendChild(style);
			try {
				expect(getComputedStyle(suggestions.element).display).toBe('flex');
				expect(getComputedStyle(suggestions.element).flexWrap).toBe('wrap');
				expect(getComputedStyle(suggestions.element).gap).toBe('6px');
				const buttonStyle = getComputedStyle(suggestions.get('button').element);
				expect(buttonStyle.maxWidth).toBe('100%');
				expect(buttonStyle.overflowWrap).toBe('anywhere');
			} finally {
				style.remove();
			}
			expect(wrapper.text()).not.toContain('Zum persönlichen Wörterbuch hinzufügen');

			await wrapper.findAll('.program-proofreading__suggestion')[0].trigger('click');
			expect(execCommand.command).toHaveReturnedWith(true);
			expect(editor.textContent).toBe('Ich habe einen Fehler und ein Fehler. ');
			await vi.advanceTimersByTimeAsync(1500);
			expect(editor.textContent).toBe('Ich habe einen Fehler und ein Fehler. ');
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(false);
			await wrapper.get('.rich-editor').trigger('click', { clientX: 20, clientY: 20 });
			expect(wrapper.findAll('.program-proofreading__suggestion')).toHaveLength(2);
			expect(requestTexts[1]).toBe(editor.textContent);

			await wrapper.find('.program-proofreading__suggestion').trigger('click');
			await vi.advanceTimersByTimeAsync(1500);
			expect(editor.textContent).toBe('Ich habe einen Fehler und einen Fehler. ');
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(false);
			expect(requestTexts).toEqual([
				description + ' ',
				'Ich habe einen Fehler und ein Fehler. ',
				'Ich habe einen Fehler und einen Fehler. ',
			]);
			expect(execCommand.command).toHaveBeenCalledTimes(2);
		} finally {
			restoreRects();
			execCommand.restore();
			wrapper.unmount();
		}
	});

	it('preserves and shifts unaffected marks during rechecks without applying stale results', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({ programs: [{
			id: 'program-1', activity_id: 'activity-1', duration_minutes: 10,
			title: 'Start', description: '<strong>🐾 Tset und Tset</strong>', responsible: [],
		}] }));
		const pending: Array<(response: Response) => void> = [];
		const requests: string[] = [];
		vi.mocked(apiFetch).mockImplementation((url, options) => {
			if (url !== '/api/proofreading/check') return Promise.resolve(new Response('{}'));
			const text = JSON.parse(options?.body as string).text as string;
			requests.push(text);
			if (requests.length > 1) return new Promise<Response>(resolve => pending.push(resolve));
			const matches = [...text.matchAll(/Tset/g)].map(result => ({
				offset: result.index, length: 4, message: 'Rechtschreibung', shortMessage: '',
				replacements: [{ value: 'Testen' }], rule: { id: 'SPELLING', issueType: 'misspelling' },
			}));
			matches.push({ ...matches[0], length: 8 });
			return Promise.resolve(new Response(JSON.stringify({ matches })));
		});
		const highlights = new Map<string, { ranges: Range[] }>();
		vi.stubGlobal('CSS', { highlights });
		vi.stubGlobal('Highlight', class {
			constructor(...ranges: Range[]) { Object.assign(this, { ranges }); }
		});
		const wrapper = shallowMount(DetailPage, { attachTo: document.body });
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.program-card__toggle').trigger('click');
		await flushPromises();
		const editor = wrapper.get('.rich-editor').element as HTMLElement;
		const command = stubInsertTextCommand(editor);
		const restoreRects = stubProofreadingRects();
		vi.useFakeTimers();
		try {
			editor.innerHTML = '<strong>🐾 Tset und Tset!</strong>';
			editor.dispatchEvent(new Event('input', { bubbles: true }));
			await vi.advanceTimersByTimeAsync(1500);
			expect(highlights.get('program-proofreading')?.ranges.map(range => range.toString()))
				.toEqual(['Tset', 'Tset', 'Tset und']);
			await wrapper.get('.rich-editor').trigger('click', { clientX: 20, clientY: 20 });
			await wrapper.get('.program-proofreading__suggestion').trigger('click');
			expect(editor.innerHTML).toBe('<strong>🐾 Testen und Tset!</strong>');
			expect(highlights.get('program-proofreading')?.ranges.map(range => range.toString())).toEqual(['Tset']);
			await vi.advanceTimersByTimeAsync(1500);
			expect(requests[1]).toBe('🐾 Testen und Tset!');
			expect(highlights.get('program-proofreading')?.ranges.map(range => range.toString())).toEqual(['Tset']);
			await wrapper.get('.rich-editor').trigger('click', { clientX: 20, clientY: 20 });
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(true);
			await wrapper.get('.program-proofreading__suggestion').trigger('click');
			expect(editor.innerHTML).toBe('<strong>🐾 Testen und Testen!</strong>');
			expect(highlights.has('program-proofreading')).toBe(false);
			pending[0](new Response(JSON.stringify({ matches: [{
				offset: 14, length: 4, message: 'stale', shortMessage: '',
				replacements: [{ value: 'falsch' }], rule: {},
			}] })));
			await vi.advanceTimersByTimeAsync(1500);
			expect(highlights.has('program-proofreading')).toBe(false);
			expect(requests[2]).toBe('🐾 Testen und Testen!');
			pending[1](new Response(JSON.stringify({ matches: [] })));
			await vi.advanceTimersByTimeAsync(0);
			expect(editor.innerHTML).toBe('<strong>🐾 Testen und Testen!</strong>');
			expect(highlights.has('program-proofreading')).toBe(false);
			expect(command.command).toHaveBeenCalledTimes(2);
		} finally {
			restoreRects();
			command.restore();
			wrapper.unmount();
		}
	});

	it('keeps an unaffected selected issue open after refresh and a failed recheck', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({ programs: [{
			id: 'program-1', activity_id: 'activity-1', duration_minutes: 10,
			title: 'Start', description: 'Tset und Tset und Tset', responsible: [],
		}] }));
		let resolveRefresh: ((response: Response) => void) | undefined;
		let checks = 0;
		const makeMatch = (offset: number) => ({
			offset, length: 4, message: 'Rechtschreibung', shortMessage: '',
			replacements: [{ value: 'Test' }], rule: { id: 'SPELLING', issueType: 'misspelling' },
		});
		vi.mocked(apiFetch).mockImplementation((url) => {
			if (url !== '/api/proofreading/check') return Promise.resolve(new Response('{}'));
			checks++;
			if (checks === 1) return Promise.resolve(new Response(JSON.stringify({ matches: [makeMatch(0), makeMatch(9), makeMatch(18)] })));
			if (checks === 2) return new Promise<Response>(resolve => { resolveRefresh = resolve; });
			return Promise.reject(new Error('service failure'));
		});
		vi.spyOn(console, 'warn').mockImplementation(() => {});
		const highlights = new Map<string, { ranges: Range[] }>();
		vi.stubGlobal('CSS', { highlights });
		vi.stubGlobal('Highlight', class {
			constructor(...ranges: Range[]) { Object.assign(this, { ranges }); }
		});
		const wrapper = shallowMount(DetailPage, { attachTo: document.body });
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.program-card__toggle').trigger('click');
		await flushPromises();
		const editor = wrapper.get('.rich-editor').element as HTMLElement;
		const command = stubInsertTextCommand(editor);
		const restoreRects = stubProofreadingRects();
		vi.useFakeTimers();
		try {
			editor.textContent = 'Tset und Tset und Tset!';
			editor.dispatchEvent(new Event('input', { bubbles: true }));
			await vi.advanceTimersByTimeAsync(1500);
			await wrapper.get('.rich-editor').trigger('click', { clientX: 20, clientY: 20 });
			await wrapper.get('.program-proofreading__suggestion').trigger('click');
			await vi.advanceTimersByTimeAsync(1500);
			await wrapper.get('.rich-editor').trigger('click', { clientX: 20, clientY: 20 });
			resolveRefresh!(new Response(JSON.stringify({ matches: [makeMatch(9), makeMatch(18)] })));
			await vi.advanceTimersByTimeAsync(0);
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(true);
			expect(highlights.get('program-proofreading')?.ranges.map(range => range.toString())).toEqual(['Tset', 'Tset']);
			await wrapper.get('.program-proofreading__suggestion').trigger('click');
			await vi.advanceTimersByTimeAsync(1500);
			expect(editor.textContent).toBe('Test und Test und Tset!');
			expect(highlights.get('program-proofreading')?.ranges.map(range => range.toString())).toEqual(['Tset']);
			expect(wrapper.find('[role="alert"]').exists()).toBe(false);
			expect(checks).toBe(3);
		} finally {
			restoreRects();
			command.restore();
			wrapper.unmount();
		}
	});

	it('debounces changed text, hides stale suggestions, and checks silently', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({
			programs: [{
				id: 'program-1',
				activity_id: 'activity-1',
				duration_minutes: 10,
				title: 'Start',
				description: 'Ich habe ein Fehler.',
				responsible: [],
			}],
		}));
		const requestTexts: string[] = [];
		vi.mocked(apiFetch).mockImplementation(async (url, options) => {
			if (url !== '/api/proofreading/check') return new Response('{}', { status: 200 });
			const text = JSON.parse(options?.body as string).text as string;
			requestTexts.push(text);
			const offset = text.indexOf('ein Fehler');
			return new Response(JSON.stringify({
				language: 'de-CH',
				matches: offset < 0 ? [] : [{
					offset,
					length: 'ein Fehler'.length,
					message: 'Kasus prüfen',
					shortMessage: 'Kasus',
					replacements: [{ value: 'einen Fehler' }],
					rule: { id: 'OBJECT_AGREEMENT' },
				}],
			}), { status: 200 });
		});

		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.program-card__toggle').trigger('click');
		await flushPromises();
		const editor = wrapper.get('.rich-editor').element as HTMLElement;
		const execCommand = stubInsertTextCommand(editor);
		const restoreRects = stubProofreadingRects();
		try {
			vi.useFakeTimers();
			editor.textContent = 'Ich habe ein Fehler. ';
			editor.dispatchEvent(new Event('input', { bubbles: true }));
			await vi.advanceTimersByTimeAsync(1500);
			expect(wrapper.find('button[aria-label="Rechtschreibung prüfen"]').exists()).toBe(false);
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(false);
			await wrapper.get('.rich-editor').trigger('click', { clientX: 20, clientY: 20 });
			expect(wrapper.findAll('.program-proofreading__suggestion')).toHaveLength(1);

			editor.innerHTML = 'Ich habe ein Fehler geändert.';
			editor.dispatchEvent(new Event('input', { bubbles: true }));
			await wrapper.vm.$nextTick();
			expect(editor.textContent).toBe('Ich habe ein Fehler geändert.');
			expect(wrapper.findAll('.program-proofreading__suggestion')).toHaveLength(0);
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(false);
			expect(requestTexts).toHaveLength(1);

			await vi.advanceTimersByTimeAsync(1000);
			expect(requestTexts).toHaveLength(1);
			editor.innerHTML = 'Ich habe ein Fehler nochmals geändert.';
			editor.dispatchEvent(new Event('input', { bubbles: true }));
			await vi.advanceTimersByTimeAsync(1499);
			expect(requestTexts).toHaveLength(1);
			editor.dispatchEvent(new Event('input', { bubbles: true }));
			await vi.advanceTimersByTimeAsync(1);
			expect(requestTexts[1]).toBe('Ich habe ein Fehler nochmals geändert.');
			await wrapper.get('.rich-editor').trigger('click', { clientX: 20, clientY: 20 });
			expect(wrapper.findAll('.program-proofreading__suggestion')).toHaveLength(1);
			editor.dispatchEvent(new Event('input', { bubbles: true }));
			await vi.advanceTimersByTimeAsync(2000);
			expect(requestTexts).toHaveLength(2);
			expect(editor.textContent).toBe('Ich habe ein Fehler nochmals geändert.');
		} finally {
			restoreRects();
			execCommand.restore();
			wrapper.unmount();
		}
	});

	it('switches browser spellcheck and rejects a pending result when proofreading is disabled', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({
			programs: [{
				id: 'program-1', activity_id: 'activity-1', duration_minutes: 10,
				title: 'Start', description: '<b>Ein Tset</b>', responsible: [],
			}],
		}));
		let resolveFirst: ((response: Response) => void) | undefined;
		vi.mocked(apiFetch).mockImplementation((url) => {
			if (url !== '/api/proofreading/check') return Promise.resolve(new Response('{}', { status: 200 }));
			if (!resolveFirst) return new Promise<Response>(resolve => { resolveFirst = resolve; });
			return Promise.resolve(new Response(JSON.stringify({ matches: [] }), { status: 200 }));
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.program-card__toggle').trigger('click');
		await flushPromises();
		const editor = wrapper.get('.rich-editor');
		vi.useFakeTimers();
		try {
			expect(editor.attributes('spellcheck')).toBe('false');
			editor.element.innerHTML = '<b>Ein Tset!</b>';
			await editor.trigger('input');
			await vi.advanceTimersByTimeAsync(1500);
			expect(resolveFirst).toBeDefined();
			const first = vi.mocked(apiFetch).mock.calls.filter(([url]) => url === '/api/proofreading/check')[0];
			const signal = first[1]?.signal as AbortSignal;
			user.value!.proofreading_enabled = false;
			await wrapper.vm.$nextTick();
			expect(signal.aborted).toBe(true);
			expect(editor.attributes('spellcheck')).toBe('true');
			resolveFirst!(new Response(JSON.stringify({ matches: [{
				offset: 4, length: 4, message: 'Fehler', replacements: [{ value: 'Test' }], rule: { id: 'TEST' },
			}] }), { status: 200 }));
			await vi.advanceTimersByTimeAsync(0);
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(false);
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(false);
			editor.element.innerHTML = '<b>Ein Tset!!</b>';
			await editor.trigger('input');
			await vi.advanceTimersByTimeAsync(3000);
			expect(vi.mocked(apiFetch).mock.calls.filter(([url]) => url === '/api/proofreading/check')).toHaveLength(1);
			user.value!.proofreading_enabled = true;
			await wrapper.vm.$nextTick();
			expect(editor.attributes('spellcheck')).toBe('false');
			await vi.advanceTimersByTimeAsync(1499);
			expect(vi.mocked(apiFetch).mock.calls.filter(([url]) => url === '/api/proofreading/check')).toHaveLength(1);
			await vi.advanceTimersByTimeAsync(1);
			const checks = vi.mocked(apiFetch).mock.calls.filter(([url]) => url === '/api/proofreading/check');
			expect(checks).toHaveLength(2);
			expect(JSON.parse(checks[1][1]?.body as string).text).toBe('Ein Tset!!');
			expect(editor.element.innerHTML).toBe('<b>Ein Tset!!</b>');
		} finally {
			wrapper.unmount();
		}
	});

	it('clears an open suggestion when proofreading is turned off', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({
			programs: [{
				id: 'program-1', activity_id: 'activity-1', duration_minutes: 10,
				title: 'Start', description: 'Ein Tset', responsible: [],
			}],
		}));
		vi.mocked(apiFetch).mockImplementation(async (url) => new Response(JSON.stringify(
			url === '/api/proofreading/check' ? { matches: [{
				offset: 4, length: 4, message: 'Fehler', replacements: [{ value: 'Test' }], rule: { id: 'TEST' },
			}] } : {},
		), { status: 200 }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.program-card__toggle').trigger('click');
		await flushPromises();
		const editor = wrapper.get('.rich-editor');
		const restoreRects = stubProofreadingRects();
		vi.useFakeTimers();
		try {
			editor.element.textContent = 'Ein Tset!';
			await editor.trigger('input');
			await vi.advanceTimersByTimeAsync(1500);
			await editor.trigger('click', { clientX: 20, clientY: 20 });
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(true);
			user.value!.proofreading_enabled = false;
			await wrapper.vm.$nextTick();
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(false);
			expect(editor.attributes('spellcheck')).toBe('true');
			expect(editor.element.textContent).toBe('Ein Tset!');
		} finally {
			restoreRects();
			wrapper.unmount();
		}
	});

	it('waits for composition to finish and cancels pending checks on editor close', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({
			programs: [{
				id: 'program-1', activity_id: 'activity-1', duration_minutes: 10,
				title: 'Start', description: 'Text', responsible: [],
			}],
		}));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.program-card__toggle').trigger('click');
		await flushPromises();
		const editor = wrapper.get('.rich-editor');
		vi.useFakeTimers();
		try {
			await vi.advanceTimersByTimeAsync(3000);
			expect(vi.mocked(apiFetch).mock.calls.filter(([url]) => url === '/api/proofreading/check')).toHaveLength(0);
			await editor.trigger('compositionstart');
			editor.element.textContent = 'Text geändert';
			await editor.trigger('input');
			user.value!.proofreading_enabled = false;
			user.value!.proofreading_enabled = true;
			await vi.advanceTimersByTimeAsync(3000);
			expect(vi.mocked(apiFetch).mock.calls.filter(([url]) => url === '/api/proofreading/check')).toHaveLength(0);
			await editor.trigger('compositionend');
			await vi.advanceTimersByTimeAsync(1500);
			expect(vi.mocked(apiFetch).mock.calls.filter(([url]) => url === '/api/proofreading/check')).toHaveLength(1);
			editor.element.textContent = 'Nochmals geändert';
			await editor.trigger('input');
			wrapper.unmount();
			await vi.advanceTimersByTimeAsync(2000);
			expect(vi.mocked(apiFetch).mock.calls.filter(([url]) => url === '/api/proofreading/check')).toHaveLength(1);
		} finally {
			wrapper.unmount();
		}
	});

	it('silently handles failures and timeouts and retries on later changes', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({
			programs: [{
				id: 'program-1', activity_id: 'activity-1', duration_minutes: 10,
				title: 'Start', description: 'Text', responsible: [],
			}],
		}));
		let rejectFirst: ((error: Error) => void) | undefined;
		let checks = 0;
		vi.mocked(apiFetch).mockImplementation((url, options) => {
			if (url !== '/api/proofreading/check') return Promise.resolve(new Response('{}', { status: 200 }));
			checks++;
			if (checks === 1) return new Promise<Response>((_resolve, reject) => { rejectFirst = reject; });
			if (checks === 2) return new Promise<Response>((_resolve, reject) => {
				options?.signal?.addEventListener('abort', () => reject(new Error('timeout')));
			});
			return Promise.resolve(new Response(JSON.stringify({ matches: [] }), { status: 200 }));
		});
		const warning = vi.spyOn(console, 'warn').mockImplementation(() => {});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.program-card__toggle').trigger('click');
		await flushPromises();
		const editor = wrapper.get('.rich-editor');
		vi.useFakeTimers();
		try {
			editor.element.textContent = 'Text geändert';
			await editor.trigger('input');
			await vi.advanceTimersByTimeAsync(1500);
			expect(rejectFirst).toBeDefined();
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(false);
			expect(wrapper.find('button[aria-label="Rechtschreibung prüfen"]').exists()).toBe(false);
			rejectFirst!(new Error('Dienst nicht erreichbar'));
			await vi.advanceTimersByTimeAsync(0);
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(false);
			expect(wrapper.text()).not.toContain('Dienst nicht erreichbar');
			editor.element.textContent = 'Text nochmals geändert';
			await editor.trigger('input');
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(false);
			await vi.advanceTimersByTimeAsync(1500);
			expect(vi.mocked(apiFetch).mock.calls.filter(([url]) => url === '/api/proofreading/check')).toHaveLength(2);
			await vi.advanceTimersByTimeAsync(35000);
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(false);
			expect(wrapper.text()).not.toContain('timeout');
			editor.element.textContent = 'Text weiterhin bearbeitbar';
			await editor.trigger('input');
			await vi.advanceTimersByTimeAsync(1500);
			expect(checks).toBe(3);
			expect(editor.element.textContent).toBe('Text weiterhin bearbeitbar');
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(false);
			const proofreadWarnings = warning.mock.calls.filter(call => call[0] === 'Proofreading check failed or timed out.');
			expect(proofreadWarnings).toHaveLength(2);
			expect(proofreadWarnings.flat().join(' ')).not.toContain('Text');
		} finally {
			wrapper.unmount();
		}
	});

	it('adds only an explicitly selected spelling word and rechecks after persistence', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({
			programs: [{
				id: 'program-1', activity_id: 'activity-1', duration_minutes: 10,
				title: 'Start', description: 'Ein einfahcer Text', responsible: [],
			}],
		}));
		let added = false;
		let fail = true;
		vi.mocked(apiFetch).mockImplementation(async (url, options) => {
			if (url === '/api/proofreading/dictionary') {
				expect(JSON.parse(options?.body as string)).toEqual({ word: 'einfahcer' });
				if (fail) return new Response(JSON.stringify({ error: 'Wörterbuch nicht verfügbar' }), { status: 503 });
				added = true;
				return new Response(JSON.stringify({ added: true }), { status: 200 });
			}
			if (url !== '/api/proofreading/check') return new Response('{}', { status: 200 });
			return new Response(JSON.stringify({ matches: added ? [] : [{
				offset: 4, length: 9, message: 'Rechtschreibfehler', shortMessage: '',
				replacements: [{ value: 'einfacher' }], rule: { issueType: 'misspelling' },
			}] }), { status: 200 });
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.program-card__toggle').trigger('click');
		await flushPromises();
		const restoreRects = stubProofreadingRects();
		const editor = wrapper.get('.rich-editor');
		vi.useFakeTimers();
		try {
			editor.element.textContent = 'Ein einfahcer Text!';
			await editor.trigger('input');
			await vi.advanceTimersByTimeAsync(1500);
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(false);
			expect(vi.mocked(apiFetch).mock.calls.filter(([url]) => url === '/api/proofreading/dictionary')).toHaveLength(0);
			await editor.trigger('click', { clientX: 20, clientY: 20 });
			const addButton = () => wrapper.findAll('.program-proofreading__popover button')
				.find(button => button.text().includes('Wörterbuch'))!;
			await addButton().trigger('click');
			await vi.advanceTimersByTimeAsync(0);
			expect(wrapper.find('.program-proofreading__popover [role="alert"]').exists()).toBe(false);
			expect(wrapper.text()).not.toContain('Wörterbuch nicht verfügbar');
			expect(editor.element.textContent).toBe('Ein einfahcer Text!');
			fail = false;
			await addButton().trigger('click');
			await vi.advanceTimersByTimeAsync(0);
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(false);
			await vi.advanceTimersByTimeAsync(1500);
			await editor.trigger('click', { clientX: 20, clientY: 20 });
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(false);
			expect(editor.element.textContent).toBe('Ein einfahcer Text!');
		} finally {
			restoreRects();
			wrapper.unmount();
		}
	});

	it('does not apply a late dictionary response to an edited description', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({ programs: [{
			id: 'program-1', activity_id: 'activity-1', duration_minutes: 10,
			title: 'Start', description: 'einfahcer', responsible: [],
		}] }));
		let resolveAdd: ((value: Response) => void) | undefined;
		vi.mocked(apiFetch).mockImplementation((url) => {
			if (url === '/api/proofreading/dictionary') return new Promise(resolve => { resolveAdd = resolve; });
			if (url !== '/api/proofreading/check') return Promise.resolve(new Response('{}', { status: 200 }));
			return Promise.resolve(new Response(JSON.stringify({ matches: [{
				offset: 0, length: 9, message: 'Rechtschreibfehler', shortMessage: '',
				replacements: [], rule: { issueType: 'misspelling' },
			}] }), { status: 200 }));
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.program-card__toggle').trigger('click');
		await flushPromises();
		const restoreRects = stubProofreadingRects();
		const editor = wrapper.get('.rich-editor');
		vi.useFakeTimers();
		try {
			editor.element.textContent = 'einfahcer!';
			await editor.trigger('input');
			await vi.advanceTimersByTimeAsync(1500);
			await editor.trigger('click', { clientX: 20, clientY: 20 });
			await wrapper.get('.program-proofreading__popover .program-proofreading__suggestion').trigger('click');
			expect(resolveAdd).toBeDefined();
			editor.element.textContent = 'Neu geschrieben';
			await editor.trigger('input');
			resolveAdd!(new Response(JSON.stringify({ added: true }), { status: 200 }));
			await vi.advanceTimersByTimeAsync(1500);
			expect(editor.element.textContent).toBe('Neu geschrieben');
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(false);
			const checks = vi.mocked(apiFetch).mock.calls.filter(([url]) => url === '/api/proofreading/check');
			expect(JSON.parse(checks.at(-1)?.[1]?.body as string).text).toBe('Neu geschrieben');
		} finally {
			restoreRects();
			wrapper.unmount();
		}
	});

	it('ignores late responses for old text and opens the inline suggestion at the clicked range', async () => {
		allowEditing();
		const imageId = '12345678-1234-1234-1234-123456789abc';
		const imagePath = `/api/program-images/${imageId}`;
		const imageHtml = `<img src="blob:program-image" data-program-image="${imageId}">`;
		mocks.fetchActivity.mockResolvedValue(makeActivity({
			programs: [{
				id: 'program-1', activity_id: 'activity-1', duration_minutes: 10,
				title: 'Start', description: `<strong>🐾 ein Fehler</strong><img src="${imagePath}">`,
				responsible: [],
			}],
		}));
		const pending: Array<(response: Response) => void> = [];
		vi.mocked(apiFetch).mockImplementation((url) => {
			if (url === imagePath) return Promise.resolve(new Response('image', { headers: { 'Content-Type': 'image/png' } }));
			if (url !== '/api/proofreading/check') return Promise.resolve(new Response('{}', { status: 200 }));
			return new Promise<Response>(resolve => pending.push(resolve));
		});
		const response = (offset: number) => new Response(JSON.stringify({
			language: 'de-CH',
			matches: [{
				offset, length: 10, message: 'Kasus prüfen', shortMessage: 'Kasus',
				replacements: [{ value: 'einen Fehler' }], rule: { id: 'TEST' },
			}],
		}), { status: 200 });

		const wrapper = shallowMount(DetailPage, { attachTo: document.body });
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.program-card__toggle').trigger('click');
		await flushPromises();
		const editor = wrapper.get('.rich-editor').element as HTMLElement;
		const command = stubInsertTextCommand(editor);
		const highlights = { set: vi.fn(), delete: vi.fn() };
		vi.stubGlobal('CSS', { highlights });
		vi.stubGlobal('Highlight', class {
			ranges: Range[];
			constructor(...ranges: Range[]) { this.ranges = ranges; }
		});
		const descriptor = Object.getOwnPropertyDescriptor(Range.prototype, 'getClientRects');
		Object.defineProperty(Range.prototype, 'getClientRects', {
			configurable: true,
			value: () => [{ left: 10, right: 100, top: 10, bottom: 30 }],
		});
		try {
			vi.useFakeTimers();
			editor.innerHTML = `<strong>🐾 ein Fehler geändert</strong>${imageHtml}`;
			editor.dispatchEvent(new Event('input', { bubbles: true }));
			await vi.advanceTimersByTimeAsync(1500);
			expect(pending).toHaveLength(1);
			editor.innerHTML = `<strong>🐾 ein Fehler nochmals geändert</strong>${imageHtml}`;
			editor.dispatchEvent(new Event('input', { bubbles: true }));
			await vi.advanceTimersByTimeAsync(1500);
			expect(pending).toHaveLength(2);
			pending[1](response(3));
			await vi.advanceTimersByTimeAsync(0);
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(false);
			expect(highlights.set).toHaveBeenCalledWith('program-proofreading', expect.objectContaining({
				ranges: expect.arrayContaining([expect.any(Range)]),
			}));
			const highlightCalls = highlights.set.mock.calls.length;
			pending[0](response(0));
			await vi.advanceTimersByTimeAsync(0);
			expect(highlights.set).toHaveBeenCalledTimes(highlightCalls);
			await wrapper.get('.rich-editor').trigger('click', { clientX: 20, clientY: 20 });
			expect(wrapper.find('.program-proofreading__popover').exists()).toBe(true);
			await wrapper.get('.program-proofreading__suggestion').trigger('click');
			expect(command.command).toHaveBeenCalled();
			expect(editor.innerHTML).toContain('<strong>🐾 einen Fehler nochmals geändert</strong>');
			expect(editor.querySelector('img')?.getAttribute('data-program-image')).toBe(imageId);
			await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
			const saved = mocks.updateActivity.mock.lastCall?.[1].programs[0].description as string;
			expect(saved).toContain(imagePath);
			expect(saved).toContain('einen Fehler');
			expect(saved).not.toContain('blob:');
		} finally {
			if (descriptor) Object.defineProperty(Range.prototype, 'getClientRects', descriptor);
			else Reflect.deleteProperty(Range.prototype, 'getClientRects');
			command.restore();
			wrapper.unmount();
			vi.unstubAllGlobals();
		}
	});

	it('shows the guiding thread as escaped multiline text', async () => {
		const story = 'Erste Zeile\n<b>Zweite Zeile</b>';
		mocks.fetchActivity.mockResolvedValue(makeActivity({ guiding_thread: story }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		const section = wrapper.findAll('.detail-section').find((item) => item.text().includes('Roter Faden'));
		expect(section).toBeDefined();
		expect(section!.get('.detail-value').element.textContent).toBe(story);
		expect(section!.find('b').exists()).toBe(false);
		wrapper.unmount();
	});

	it('starts program cards collapsed, expands them independently, and keeps edits while folded', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({
			programs: [
				{ id: 'program-1', activity_id: 'activity-1', duration_minutes: 20, title: 'Ankommen', description: '', responsible: [] },
				{ id: 'program-2', activity_id: 'activity-1', duration_minutes: 30, title: 'Spiel', description: '', responsible: [] },
			],
		}));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');

		const cards = wrapper.findAll('.program-card');
		expect(cards).toHaveLength(2);
		for (const card of cards) {
			expect(card.get('.program-card__toggle').attributes('aria-expanded')).toBe('false');
			expect((card.get('.program-card__fields').element as HTMLElement).style.display).toBe('none');
		}

		await cards[0].get('.program-card__toggle').trigger('click');
		expect(cards[0].get('.program-card__toggle').attributes('aria-expanded')).toBe('true');
		expect((cards[0].get('.program-card__fields').element as HTMLElement).style.display).not.toBe('none');
		await cards[0].get('input[placeholder="Titel"]').setValue('Ankommen und begrüssen');
		await cards[0].get('.program-card__toggle').trigger('click');
		expect(cards[0].get('.program-card__toggle').attributes('aria-expanded')).toBe('false');
		await cards[0].get('.program-card__toggle').trigger('click');
		expect((cards[0].get('input[placeholder="Titel"]').element as HTMLInputElement).value)
			.toBe('Ankommen und begrüssen');

		await cards[1].get('.program-card__toggle').trigger('click');
		expect(cards[0].get('.program-card__toggle').attributes('aria-expanded')).toBe('true');
		expect(cards[1].get('.program-card__toggle').attributes('aria-expanded')).toBe('true');
		await cards[0].get('.program-card__toggle').trigger('click');
		expect(cards[0].get('.program-card__toggle').attributes('aria-expanded')).toBe('false');
		expect(cards[1].get('.program-card__toggle').attributes('aria-expanded')).toBe('true');

		const toggleAll = wrapper.get('.program-card__toggle-all');
		expect(toggleAll.text()).toBe('Alle öffnen');
		await toggleAll.trigger('click');
		expect(wrapper.findAll('.program-card').every((card) =>
			card.get('.program-card__toggle').attributes('aria-expanded') === 'true'
		)).toBe(true);
		expect(toggleAll.text()).toBe('Alle schliessen');
		await toggleAll.trigger('click');
		expect(wrapper.findAll('.program-card').every((card) =>
			card.get('.program-card__toggle').attributes('aria-expanded') === 'false'
		)).toBe(true);
		expect(toggleAll.text()).toBe('Alle öffnen');
		wrapper.unmount();
	});

	it('keeps the evaluation entry point disabled until the activity date', async () => {
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-07-01T12:00:00'));
		mocks.fetchActivity.mockResolvedValue(makeActivity({ date: '2026-07-02' }));

		const wrapper = shallowMount(DetailPage);
		await flushPromises();

		const button = wrapper.get('button[title="Auswertung ist erst am Aktivitätstag verfügbar"]');
		expect((button.element as HTMLButtonElement).disabled).toBe(true);
		await button.trigger('click');
		expect(wrapper.find('.detail-view--evaluation').exists()).toBe(false);
		vi.setSystemTime(new Date('2026-07-02T00:01:00'));
		await vi.advanceTimersByTimeAsync(60_000);
		const availableButton = wrapper.get('button[title="Auswertung öffnen"]');
		expect((availableButton.element as HTMLButtonElement).disabled).toBe(false);
		wrapper.unmount();
	});

	it('opens the evaluation on the activity date regardless of its start time', async () => {
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-07-01T00:01:00'));
		mocks.fetchActivity.mockResolvedValue(makeActivity({
			date: '2026-07-01',
			start_time: '23:59',
		}));

		const wrapper = shallowMount(DetailPage);
		await flushPromises();

		const button = wrapper.get('button[title="Auswertung öffnen"]');
		expect((button.element as HTMLButtonElement).disabled).toBe(false);
		await button.trigger('click');
		expect(wrapper.find('.detail-view--evaluation').exists()).toBe(true);
		await wrapper.get('button[title="Zur Aktivität zurückkehren"]').trigger('click');
		expect(wrapper.find('.detail-view--evaluation').exists()).toBe(false);
		wrapper.unmount();
	});

	it('keeps expansion attached to a program when adding, removing, and reordering cards', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({
			programs: [
				{ id: 'program-1', activity_id: 'activity-1', duration_minutes: 20, title: 'Erster', description: '', responsible: [] },
				{ id: 'program-2', activity_id: 'activity-1', duration_minutes: 30, title: 'Zweiter', description: '', responsible: [] },
			],
		}));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const originalCards = wrapper.findAll('.program-card');
		await originalCards[1].get('.program-card__toggle').trigger('click');
		await wrapper.get('.btn-add-split__main, .btn-add-single .btn-add').trigger('click');
		expect(wrapper.findAll('.program-card')).toHaveLength(3);
		expect(wrapper.findAll('.program-card')[2].get('.program-card__toggle').attributes('aria-expanded')).toBe('false');

		await wrapper.findAll('.program-card')[0].get('.program-card__remove').trigger('click');
		let cards = wrapper.findAll('.program-card');
		expect(cards[0].get('.program-card__toggle').text()).toContain('Zweiter');
		expect(cards[0].get('.program-card__toggle').attributes('aria-expanded')).toBe('true');

		await cards[0].get('.program-card__drag-handle').trigger('dragstart', {
			dataTransfer: { effectAllowed: '', setData: vi.fn() },
			clientY: 20,
		});
		await cards[1].trigger('drop', { dataTransfer: { dropEffect: 'move' } });
		cards = wrapper.findAll('.program-card');
		expect(cards[1].get('.program-card__toggle').text()).toContain('Zweiter');
		expect(cards[1].get('.program-card__toggle').attributes('aria-expanded')).toBe('true');
		wrapper.unmount();
	});

	it('autosaves and clears the guiding thread without affecting other fields', async () => {
		allowEditing();
		vi.useFakeTimers();
		mocks.fetchActivity.mockResolvedValue(makeActivity({ goal: 'Ziel', guiding_thread: 'Bisher' }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const field = wrapper.get('#edit-guiding-thread');
		expect((field.element as HTMLTextAreaElement).value).toBe('Bisher');
		await field.setValue('  Anfang\nEnde  ');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity).toHaveBeenLastCalledWith('activity-1', expect.objectContaining({
			goal: 'Ziel',
			guiding_thread: 'Anfang\nEnde',
		}));
		await field.setValue('  ');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity).toHaveBeenLastCalledWith('activity-1', expect.objectContaining({
			guiding_thread: null,
		}));
		wrapper.unmount();
	});

	it('shows imported rich text immediately and preserves it through editing, autosave and reload', async () => {
		allowEditing();
		vi.useFakeTimers();
		const description = '<p><strong>Spurensuche</strong> mit <em>Karte</em></p><ul><li>Start</li></ul>';
		mocks.ideaItems = [{
			id: 'idea-1', title: 'Schnitzeljagd', duration_minutes: 25, description,
			department: null, created_at: '', updated_at: '',
		}];
		let persisted = makeActivity({
			programs: [{
				id: 'program-1', activity_id: 'activity-1',
				duration_minutes: 10, title: 'Begruessung', description: '<p>Bestehend</p>', responsible: ['user-1'],
			}],
		});
		mocks.fetchActivity.mockImplementation(async () => persisted);
		mocks.updateActivity.mockImplementation(async (_id: string, input: ActivityInput) => {
			persisted = makeActivity({
				...input,
				programs: input.programs.map((program, index) => ({
					...program, id: `program-${index + 1}`, activity_id: 'activity-1',
				})),
			});
			return true;
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('button[title="Aus Ideenkiste"]').trigger('click');
		await wrapper.get('.ideen-dropdown__item').trigger('click');

		const editors = wrapper.findAll('.program-card .rich-editor');
		expect(editors).toHaveLength(2);
		expect(editors[0].element.innerHTML).toBe('<p>Bestehend</p>');
		expect(editors[1].element.innerHTML).toBe(description);
		expect(mocks.updateActivity).not.toHaveBeenCalled();
		const importedElement = editors[1].element;
		const edited = `${description}<p>Treffpunkt <a href="https://example.com/">Wald</a></p>`;
		importedElement.innerHTML = edited;
		await editors[1].trigger('input');
		await wrapper.findAll('.program-card input[placeholder="Titel"]')[1].setValue('Schnitzeljagd im Wald');
		expect(wrapper.findAll('.program-card .rich-editor')[1].element).toBe(importedElement);
		expect(importedElement.innerHTML).toBe(edited);
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity).toHaveBeenLastCalledWith('activity-1', expect.objectContaining({
			programs: [
				{ title: 'Begruessung', duration_minutes: 10, description: '<p>Bestehend</p>', responsible: ['user-1'] },
				{ title: 'Schnitzeljagd im Wald', duration_minutes: 25, description: edited, responsible: [] },
			],
		}));
		wrapper.unmount();

		const reloaded = shallowMount(DetailPage);
		await flushPromises();
		await reloaded.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(reloaded.findAll('.program-card .rich-editor').map(editor => editor.element.innerHTML))
			.toEqual(['<p>Bestehend</p>', edited]);
		reloaded.unmount();
	});

	it('hydrates newly attached editors without rewriting a focused editor or losing descriptions after removal', async () => {
		allowEditing();
		vi.useFakeTimers();
		vi.spyOn(richTextToolbar, 'readRichTextToolbarState').mockReturnValue(richTextToolbar.defaultRichTextToolbarState());
		mocks.ideaItems = [{
			id: 'idea-1', title: 'Import', duration_minutes: 25, description: '<p><b>Importiert</b></p>',
			department: null, created_at: '', updated_at: '',
		}];
		const wrapper = shallowMount(DetailPage, { attachTo: document.body });
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.btn-add-split__main').trigger('click');
		const editor = wrapper.get<HTMLElement>('.program-card .rich-editor');
		expect(editor.element.innerHTML).toBe('');
		editor.element.focus();
		editor.element.innerHTML = '<p>Gerade geschrieben</p>';
		const textNode = editor.element.firstChild!.firstChild!;
		const selection = window.getSelection()!;
		const range = document.createRange();
		range.setStart(textNode, 5);
		range.collapse(true);
		selection.removeAllRanges();
		selection.addRange(range);
		await editor.trigger('input');
		await wrapper.get('button[title="Aus Ideenkiste"]').trigger('click');
		await wrapper.get('.ideen-dropdown__item').trigger('click');
		expect(document.activeElement).toBe(editor.element);
		expect(editor.element.firstChild!.firstChild).toBe(textNode);
		expect(selection.anchorNode).toBe(textNode);
		expect(selection.anchorOffset).toBe(5);
		expect(wrapper.findAll('.program-card .rich-editor')[1].element.innerHTML).toBe('<p><b>Importiert</b></p>');
		editor.element.blur();
		await wrapper.findAll('.program-card__remove')[0].trigger('click');
		expect(wrapper.get('.program-card .rich-editor').element.innerHTML).toBe('<p><b>Importiert</b></p>');
		wrapper.unmount();
	});

	it('restores legacy drafts without clearing a guiding thread saved after the draft was made', async () => {
		allowEditing();
		vi.useFakeTimers();
		mocks.fetchActivity.mockResolvedValue(makeActivity({ guiding_thread: 'Bereits gespeichert' }));
		localStorage.setItem('dpw:activity-edit-draft:activity-1', JSON.stringify({
			version: 1,
			activityId: 'activity-1',
			savedAt: Date.now(),
			data: {
				title: 'Entwurf', date: '2026-07-01', startTime: '10:00', endTime: '11:00',
				goal: '', location: '', responsible: [], department: '',
				material: [{ name: '', responsible: [] }], tnMaterial: [''],
				sikoText: '', badWeather: '', programs: [],
			},
		}));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const restoreButton = wrapper.findAll('button').find((button) => button.text() === 'Wiederherstellen');
		expect(restoreButton).toBeDefined();
		await restoreButton!.trigger('click');
		expect((wrapper.get('#edit-title').element as HTMLInputElement).value).toBe('Entwurf');
		expect((wrapper.get('#edit-guiding-thread').element as HTMLTextAreaElement).value).toBe('Bereits gespeichert');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity).toHaveBeenLastCalledWith('activity-1', expect.objectContaining({
			title: 'Entwurf',
			guiding_thread: 'Bereits gespeichert',
		}));
		wrapper.unmount();
	});

	it('keeps unsaved guiding thread text in local drafts across reloads', async () => {
		allowEditing();
		vi.useFakeTimers();
		const first = shallowMount(DetailPage);
		await flushPromises();
		await first.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await first.get('#edit-guiding-thread').setValue('Noch nicht gespeichert');
		await vi.advanceTimersByTimeAsync(350);
		const draft = JSON.parse(localStorage.getItem('dpw:activity-edit-draft:activity-1')!);
		expect(draft.data.guidingThread).toBe('Noch nicht gespeichert');
		first.unmount();

		const second = shallowMount(DetailPage);
		await flushPromises();
		await second.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const restoreButton = second.findAll('button').find((button) => button.text() === 'Wiederherstellen');
		expect(restoreButton).toBeDefined();
		await restoreButton!.trigger('click');
		expect((second.get('#edit-guiding-thread').element as HTMLTextAreaElement).value).toBe('Noch nicht gespeichert');
		second.unmount();
	});

	it('keeps a newer story draft when an earlier save finishes', async () => {
		allowEditing();
		vi.useFakeTimers();
		let resolveFirstSave: (() => void) | undefined;
		mocks.updateActivity.mockImplementationOnce(() => new Promise<boolean>((resolve) => {
			resolveFirstSave = () => resolve(true);
		}));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const field = wrapper.get('#edit-guiding-thread');
		await field.setValue('Version 1');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(resolveFirstSave).toBeDefined();
		await field.setValue('Version 2');
		await vi.advanceTimersByTimeAsync(350);
		expect(JSON.parse(localStorage.getItem('dpw:activity-edit-draft:activity-1')!).data.guidingThread)
			.toBe('Version 2');

		resolveFirstSave!();
		await vi.advanceTimersByTimeAsync(0);
		expect(JSON.parse(localStorage.getItem('dpw:activity-edit-draft:activity-1')!).data.guidingThread)
			.toBe('Version 2');

		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity).toHaveBeenLastCalledWith('activity-1', expect.objectContaining({
			guiding_thread: 'Version 2',
		}));
		expect(localStorage.getItem('dpw:activity-edit-draft:activity-1')).toBeNull();
		wrapper.unmount();
	});

	it('keeps an unresolved story draft when an earlier save finishes and the page closes', async () => {
		allowEditing();
		vi.useFakeTimers();
		const resolveSaves: Array<() => void> = [];
		mocks.updateActivity.mockImplementation(() => new Promise<boolean>((resolve) => {
			resolveSaves.push(() => resolve(true));
		}));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('#edit-guiding-thread').setValue('Version 1');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		await wrapper.get('#edit-guiding-thread').setValue('Version 2');
		await vi.advanceTimersByTimeAsync(350);
		await wrapper.get('button.btn-toggle.btn-mail').trigger('click');
		expect(resolveSaves).toHaveLength(1);
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(wrapper.text()).toContain('Ungespeicherter Entwurf gefunden');
		resolveSaves[0]!();
		await vi.advanceTimersByTimeAsync(0);
		expect(resolveSaves).toHaveLength(2);
		expect(wrapper.text()).toContain('Ungespeicherter Entwurf gefunden');
		expect(JSON.parse(localStorage.getItem('dpw:activity-edit-draft:activity-1')!).data.guidingThread)
			.toBe('Version 2');
		wrapper.unmount();
		expect(JSON.parse(localStorage.getItem('dpw:activity-edit-draft:activity-1')!).data.guidingThread)
			.toBe('Version 2');
	});

	it('does not let a completed save from a closed page delete a newer page draft', async () => {
		allowEditing();
		vi.useFakeTimers();
		let resolveFirstSave: (() => void) | undefined;
		mocks.updateActivity.mockImplementationOnce(() => new Promise<boolean>((resolve) => {
			resolveFirstSave = () => resolve(true);
		}));
		const first = shallowMount(DetailPage);
		await flushPromises();
		await first.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await first.get('#edit-guiding-thread').setValue('Version 1');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		first.unmount();

		const second = shallowMount(DetailPage);
		await flushPromises();
		await second.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const discard = second.findAll('button').find((button) => button.text() === 'Verwerfen');
		expect(discard).toBeDefined();
		await discard!.trigger('click');
		await second.get('#edit-guiding-thread').setValue('Version 2');
		await vi.advanceTimersByTimeAsync(350);
		resolveFirstSave!();
		await vi.advanceTimersByTimeAsync(0);
		expect(JSON.parse(localStorage.getItem('dpw:activity-edit-draft:activity-1')!).data.guidingThread)
			.toBe('Version 2');
		second.unmount();
	});

	it.each([true, false])('flushes a pending story when switching to view (debounce: %s)', async (debounce) => {
		allowEditing();
		vi.useFakeTimers();
		config.AUTOSAVE_DEBOUNCE = debounce;
		mocks.updateActivity.mockImplementation(() => new Promise<boolean>(() => {}));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('#edit-guiding-thread').setValue('Sofort zur Ansicht');
		await wrapper.get('button.btn-toggle.btn-mail').trigger('click');
		expect(mocks.updateActivity).toHaveBeenLastCalledWith('activity-1', expect.objectContaining({
			guiding_thread: 'Sofort zur Ansicht',
		}));
		expect(JSON.parse(localStorage.getItem('dpw:activity-edit-draft:activity-1')!).data.guidingThread)
			.toBe('Sofort zur Ansicht');
		wrapper.unmount();
	});

	it('does not repeat an already completed autosave when switching to view', async () => {
		allowEditing();
		vi.useFakeTimers();
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('#edit-guiding-thread').setValue('Bereits gespeichert');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity).toHaveBeenCalledTimes(1);
		await wrapper.get('button.btn-toggle.btn-mail').trigger('click');
		expect(mocks.updateActivity).toHaveBeenCalledTimes(1);
		wrapper.unmount();
	});

	it('shows an unconfirmed update when the user has switched to view', async () => {
		allowEditing();
		vi.useFakeTimers();
		mocks.updateActivity.mockImplementation(async () => {
			mocks.setActivityError?.('Das Backend hat den roten Faden nicht bestätigt.');
			return false;
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('#edit-guiding-thread').setValue('Noch nicht gespeichert');
		await wrapper.get('button.btn-toggle.btn-mail').trigger('click');
		await vi.advanceTimersByTimeAsync(0);
		expect(wrapper.getComponent(ErrorAlert).props('error')).toContain('den roten Faden nicht bestätigt');
		expect(JSON.parse(localStorage.getItem('dpw:activity-edit-draft:activity-1')!).data.guidingThread)
			.toBe('Noch nicht gespeichert');
		wrapper.unmount();
	});

	it('honors guiding thread locks and merges remote changes without clobbering newer typing', async () => {
		allowEditing();
		vi.useFakeTimers();
		mocks.fetchActivity.mockResolvedValue(makeActivity({ guiding_thread: 'Ausgang' }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const field = wrapper.get('#edit-guiding-thread');
		mocks.onMessage?.({ event: 'lock', activity_id: 'activity-1', section: 'guiding_thread', user: 'Andere Person' });
		await flushPromises();
		expect((field.element as HTMLTextAreaElement).disabled).toBe(true);
		mocks.onMessage?.({ event: 'unlock', activity_id: 'activity-1', section: 'guiding_thread' });
		await flushPromises();
		expect((field.element as HTMLTextAreaElement).disabled).toBe(false);

		await field.setValue('Version 1');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		await field.setValue('Version 2');
		mocks.setLastUpdated?.(makeActivity({ guiding_thread: 'Version 1' }));
		await flushPromises();
		expect((field.element as HTMLTextAreaElement).value).toBe('Version 2');
		mocks.setLastUpdated?.(makeActivity({ guiding_thread: 'Von anderer Person' }));
		await flushPromises();
		expect((field.element as HTMLTextAreaElement).value).toBe('Von anderer Person');
		wrapper.unmount();
	});

	it('keeps newer story text when multiple save echoes arrive late', async () => {
		allowEditing();
		vi.useFakeTimers();
		const resolveSaves: Array<() => void> = [];
		mocks.updateActivity.mockImplementation(() => new Promise<boolean>((resolve) => {
			resolveSaves.push(() => resolve(true));
		}));
		mocks.fetchActivity.mockResolvedValue(makeActivity({ guiding_thread: 'Ausgang' }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const field = wrapper.get('#edit-guiding-thread');
		await field.setValue('Version 1');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		await field.setValue('Version 2');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity).toHaveBeenCalledTimes(1);
		await field.setValue('Version 3');
		resolveSaves[0]!();
		await vi.advanceTimersByTimeAsync(0);
		expect(mocks.updateActivity).toHaveBeenCalledTimes(2);
		expect(mocks.updateActivity).toHaveBeenLastCalledWith('activity-1', expect.objectContaining({
			guiding_thread: 'Version 2',
		}));
		mocks.setLastUpdated?.(makeActivity({ guiding_thread: 'Version 1' }));
		await flushPromises();
		expect((field.element as HTMLTextAreaElement).value).toBe('Version 3');
		mocks.setLastUpdated?.(makeActivity({ guiding_thread: 'Version 2' }));
		await flushPromises();
		expect((field.element as HTMLTextAreaElement).value).toBe('Version 3');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity).toHaveBeenCalledTimes(2);
		resolveSaves[1]!();
		await vi.advanceTimersByTimeAsync(0);
		expect(mocks.updateActivity).toHaveBeenLastCalledWith('activity-1', expect.objectContaining({
			guiding_thread: 'Version 3',
		}));
		wrapper.unmount();
	});

	it('continues queued saves after an earlier update fails', async () => {
		allowEditing();
		vi.useFakeTimers();
		let rejectFirstSave: (() => void) | undefined;
		mocks.updateActivity.mockImplementationOnce(() => new Promise<boolean>((resolve) => {
			rejectFirstSave = () => {
				mocks.setActivityError?.('Speichern fehlgeschlagen');
				resolve(false);
			};
		}));
		mocks.updateActivity.mockImplementationOnce(async () => {
			mocks.setActivityError?.(null);
			return true;
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const field = wrapper.get('#edit-guiding-thread');
		await field.setValue('Version 1');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		await field.setValue('Version 2');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity).toHaveBeenCalledTimes(1);
		rejectFirstSave!();
		await vi.advanceTimersByTimeAsync(0);
		expect(mocks.updateActivity).toHaveBeenLastCalledWith('activity-1', expect.objectContaining({
			guiding_thread: 'Version 2',
		}));
		expect(localStorage.getItem('dpw:activity-edit-draft:activity-1')).toBeNull();
		wrapper.unmount();
	});

	it('does not mistake remote updates for failed or disconnected save echoes', async () => {
		allowEditing();
		vi.useFakeTimers();
		mocks.fetchActivity.mockResolvedValue(makeActivity({ guiding_thread: 'Ausgang' }));
		mocks.updateActivity.mockImplementationOnce(async () => {
			mocks.setActivityError?.('Speichern fehlgeschlagen');
			return false;
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const field = wrapper.get('#edit-guiding-thread');
		await field.setValue('Fehlgeschlagene Version');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		await field.setValue('Anderer lokaler Text');
		mocks.setLastUpdated?.(makeActivity({ guiding_thread: 'Fehlgeschlagene Version' }));
		await flushPromises();
		expect((field.element as HTMLTextAreaElement).value).toBe('Fehlgeschlagene Version');

		await field.setValue('Gesendete Version');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		await field.setValue('Neuer lokaler Text');
		mocks.setConnected?.(false);
		await flushPromises();
		mocks.setConnected?.(true);
		await flushPromises();
		mocks.setLastUpdated?.(makeActivity({ guiding_thread: 'Gesendete Version' }));
		await flushPromises();
		expect((field.element as HTMLTextAreaElement).value).toBe('Gesendete Version');
		wrapper.unmount();
	});

	it('does not send a guiding thread when connected to an older backend', async () => {
		allowEditing();
		vi.useFakeTimers();
		const { guiding_thread: _guidingThread, ...legacyActivity } = makeActivity();
		mocks.fetchActivity.mockResolvedValue(legacyActivity);
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect((wrapper.get('#edit-guiding-thread').element as HTMLTextAreaElement).disabled).toBe(true);
		expect(wrapper.text()).toContain('nach dem Backend-Update verfügbar');
		await wrapper.get('#edit-title').setValue('Geänderter Titel');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity).toHaveBeenCalled();
		expect(mocks.updateActivity.mock.lastCall?.[1]).not.toHaveProperty('guiding_thread');
		wrapper.unmount();
	});

	it('keeps the story draft when an older backend does not confirm the update', async () => {
		allowEditing();
		vi.useFakeTimers();
		mocks.updateActivity.mockImplementation(async () => {
			mocks.setActivityError?.('Das Backend hat den roten Faden nicht bestätigt.');
			return false;
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('#edit-guiding-thread').setValue('Noch nicht gespeichert');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(wrapper.getComponent(ErrorAlert).props('error')).toContain('den roten Faden nicht bestätigt');
		expect(JSON.parse(localStorage.getItem('dpw:activity-edit-draft:activity-1')!).data.guidingThread)
			.toBe('Noch nicht gespeichert');
		wrapper.unmount();
	});

	it('retains a restored legacy-backend story when switching to view', async () => {
		allowEditing();
		vi.useFakeTimers();
		const { guiding_thread: _guidingThread, ...legacyActivity } = makeActivity();
		mocks.fetchActivity.mockResolvedValue(legacyActivity);
		localStorage.setItem('dpw:activity-edit-draft:activity-1', JSON.stringify({
			version: 1,
			activityId: 'activity-1',
			savedAt: Date.now(),
			data: {
				title: 'Entwurf', date: '2026-07-01', startTime: '10:00', endTime: '11:00',
				goal: '', guidingThread: 'Lokal erhaltene Geschichte', location: '', responsible: [], department: '',
				material: [{ name: '', responsible: [] }], tnMaterial: [''],
				sikoText: '', badWeather: '', programs: [],
			},
		}));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const restoreButton = wrapper.findAll('button').find((button) => button.text() === 'Wiederherstellen');
		expect(restoreButton).toBeDefined();
		await restoreButton!.trigger('click');
		await wrapper.get('button.btn-toggle.btn-mail').trigger('click');
		await vi.advanceTimersByTimeAsync(0);
		expect(wrapper.findComponent(ErrorAlert).exists()).toBe(false);
		expect(JSON.parse(localStorage.getItem('dpw:activity-edit-draft:activity-1')!).data.guidingThread)
			.toBe('Lokal erhaltene Geschichte');
		wrapper.unmount();
	});

	it('retains a restored story locally until an older backend has been upgraded', async () => {
		allowEditing();
		vi.useFakeTimers();
		const { guiding_thread: _guidingThread, ...legacyActivity } = makeActivity();
		mocks.fetchActivity.mockResolvedValue(legacyActivity);
		localStorage.setItem('dpw:activity-edit-draft:activity-1', JSON.stringify({
			version: 1,
			activityId: 'activity-1',
			savedAt: Date.now(),
			data: {
				title: 'Entwurf', date: '2026-07-01', startTime: '10:00', endTime: '11:00',
				goal: '', guidingThread: 'Lokal erhaltene Geschichte', location: '', responsible: [], department: '',
				material: [{ name: '', responsible: [] }], tnMaterial: [''],
				sikoText: '', badWeather: '', programs: [],
			},
		}));
		const oldBackendPage = shallowMount(DetailPage);
		await flushPromises();
		await oldBackendPage.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const restoreOld = oldBackendPage.findAll('button').find((button) => button.text() === 'Wiederherstellen');
		expect(restoreOld).toBeDefined();
		await restoreOld!.trigger('click');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity.mock.lastCall?.[1]).not.toHaveProperty('guiding_thread');
		expect(JSON.parse(localStorage.getItem('dpw:activity-edit-draft:activity-1')!).data.guidingThread)
			.toBe('Lokal erhaltene Geschichte');
		expect(oldBackendPage.text()).toContain('Der Entwurf bleibt bis dahin lokal gespeichert');
		const oldField = oldBackendPage.get('#edit-guiding-thread').element as HTMLTextAreaElement;
		expect(oldField.readOnly).toBe(true);
		expect(oldField.disabled).toBe(false);
		oldBackendPage.unmount();

		mocks.fetchActivity.mockResolvedValue(makeActivity());
		const upgradedBackendPage = shallowMount(DetailPage);
		await flushPromises();
		await upgradedBackendPage.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const restoreAfterUpgrade = upgradedBackendPage.findAll('button').find((button) => button.text() === 'Wiederherstellen');
		expect(restoreAfterUpgrade).toBeDefined();
		await restoreAfterUpgrade!.trigger('click');
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(mocks.updateActivity).toHaveBeenLastCalledWith('activity-1', expect.objectContaining({
			guiding_thread: 'Lokal erhaltene Geschichte',
		}));
		expect(localStorage.getItem('dpw:activity-edit-draft:activity-1')).toBeNull();
		upgradedBackendPage.unmount();
	});

	it('reports when an old backend cannot persist a story and local storage fails', async () => {
		allowEditing();
		vi.useFakeTimers();
		const { guiding_thread: _guidingThread, ...legacyActivity } = makeActivity();
		mocks.fetchActivity.mockResolvedValue(legacyActivity);
		localStorage.setItem('dpw:activity-edit-draft:activity-1', JSON.stringify({
			version: 1,
			activityId: 'activity-1',
			savedAt: Date.now(),
			data: {
				title: 'Entwurf', date: '2026-07-01', startTime: '10:00', endTime: '11:00',
				goal: '', guidingThread: 'Bitte kopieren', location: '', responsible: [], department: '',
				material: [{ name: '', responsible: [] }], tnMaterial: [''],
				sikoText: '', badWeather: '', programs: [],
			},
		}));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		const restoreButton = wrapper.findAll('button').find((button) => button.text() === 'Wiederherstellen');
		expect(restoreButton).toBeDefined();
		await restoreButton!.trigger('click');
		const setItem = vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => { throw new Error('storage unavailable'); });
		await vi.advanceTimersByTimeAsync(config.AUTOSAVE_INTERVAL);
		expect(setItem).toHaveBeenCalled();
		expect(wrapper.getComponent(ErrorAlert).props('error')).toContain('der rote Faden konnte nicht lokal gesichert werden');
		expect(wrapper.text()).not.toContain('Der Entwurf bleibt bis dahin lokal gespeichert');
		expect((wrapper.get('#edit-guiding-thread').element as HTMLTextAreaElement).disabled).toBe(false);
		wrapper.unmount();
	});

	it('substitutes the guiding thread safely in event templates', async () => {
		allowEditing();
		config.WP_PUBLISHING_ENABLED = true;
		config.PUBLIC_BASE_URL = 'https://example.test';
		const story = 'Erste Zeile\n<script>Beispiel</script>';
		mocks.fetchActivity.mockResolvedValue(makeActivity({ department: 'Pfadi', guiding_thread: story }));
		mocks.fetchForm.mockResolvedValue({ public_slug: 'form-1' });
		mocks.fetchActivityTemplates.mockResolvedValue([makeEventTemplate({
			title: 'Motto: {{roter_faden}}',
			body: '<p>{{roter_faden}} / {{formular_link|Anmelden}}</p>',
		})]);
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Als Event veröffentlichen"]').trigger('click');
		await flushPromises();
		expect((wrapper.get('input.event-preview-input').element as HTMLInputElement).value).toContain('Erste Zeile');
		expect((wrapper.get('input.event-preview-input').element as HTMLInputElement).value).not.toContain('{{roter_faden}}');
		const editor = wrapper.get('.event-preview-form .rich-editor');
		expect(editor.element.textContent).toContain(story.replace('\n', ''));
		expect(editor.find('br').exists()).toBe(true);
		expect(editor.find('script').exists()).toBe(false);
		expect(editor.get('a').attributes('href')).toBe('https://example.test/forms/form-1');
		wrapper.unmount();
	});

	it('selects only the Stufe default and exposes alphabetically sorted alternatives in Event creation', async () => {
		allowEditing();
		config.WP_PUBLISHING_ENABLED = true;
		config.PUBLIC_BASE_URL = 'https://example.test';
		mocks.fetchActivity.mockResolvedValue(makeActivity({ department: 'Pfadi' }));
		mocks.fetchForm.mockResolvedValue({ public_slug: 'form-1' });
		mocks.fetchActivityTemplates.mockResolvedValue([
			makeEventTemplate(),
			makeEventTemplate({ id: 'template-alt', name: 'Abend', is_default: false, title: 'Abend: {{titel}}' }),
			makeEventTemplate({ id: 'other-dept', department: 'Leiter', name: 'Andere Stufe' }),
		]);
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Als Event veröffentlichen"]').trigger('click');
		await flushPromises();
		expect(mocks.fetchActivityTemplates).toHaveBeenCalledWith('activity-1');
		expect(wrapper.get('#event-template-selector').element).toHaveProperty('value', 'template-default');
		expect(wrapper.findAll('#event-template-selector option').map(option => option.text()))
			.toEqual(['Keine Vorlage', 'Abend', 'Standard (Standard)']);
		expect(wrapper.get('.event-preview-input[placeholder="Event-Titel"]').element)
			.toHaveProperty('value', 'Pfadi: Test activity');
		expect(wrapper.get('.event-preview-form .rich-editor').text()).toContain('24.06.2026');
		await wrapper.get('#event-template-selector').setValue('template-alt');
		await flushPromises();
		expect(wrapper.get('.event-preview-input[placeholder="Event-Titel"]').element)
			.toHaveProperty('value', 'Abend: Test activity');
		wrapper.unmount();
	});

	it('leaves Event creation without a template when no default exists', async () => {
		allowEditing();
		config.WP_PUBLISHING_ENABLED = true;
		config.PUBLIC_BASE_URL = 'https://example.test';
		mocks.fetchActivity.mockResolvedValue(makeActivity({ department: 'Pfadi' }));
		mocks.fetchForm.mockResolvedValue({ public_slug: 'form-1' });
		mocks.fetchActivityTemplates.mockResolvedValue([makeEventTemplate({ is_default: false })]);
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Als Event veröffentlichen"]').trigger('click');
		await flushPromises();
		expect(wrapper.get('#event-template-selector').element).toHaveProperty('value', '');
		expect(wrapper.get('.event-preview-input[placeholder="Event-Titel"]').element).toHaveProperty('value', 'Test activity');
		expect(wrapper.get('.event-preview-form .rich-editor').text()).toBe('');
		wrapper.unmount();
	});

	it('requires confirmation before a template replaces edited Event title or body and restores a cancelled selection', async () => {
		allowEditing();
		config.WP_PUBLISHING_ENABLED = true;
		config.PUBLIC_BASE_URL = 'https://example.test';
		mocks.fetchActivity.mockResolvedValue(makeActivity({ department: 'Pfadi' }));
		mocks.fetchForm.mockResolvedValue({ public_slug: 'form-1' });
		mocks.fetchActivityTemplates.mockResolvedValue([
			makeEventTemplate(),
			makeEventTemplate({ id: 'alternate', name: 'Alternative', is_default: false, title: 'Alternative' }),
		]);
		const confirm = vi.spyOn(window, 'confirm').mockReturnValue(false);
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Als Event veröffentlichen"]').trigger('click');
		await flushPromises();
		await wrapper.get('.event-preview-input[placeholder="Event-Titel"]').setValue('Eigener Titel');
		const editor = wrapper.get('.event-preview-form .rich-editor');
		editor.element.innerHTML = '<p>Eigener Inhalt</p>';
		await editor.trigger('input');
		await wrapper.get('#event-template-selector').setValue('alternate');
		expect(confirm).toHaveBeenCalledOnce();
		expect(wrapper.get('#event-template-selector').element).toHaveProperty('value', 'template-default');
		expect(wrapper.get('.event-preview-input[placeholder="Event-Titel"]').element).toHaveProperty('value', 'Eigener Titel');
		expect(editor.element.innerHTML).toBe('<p>Eigener Inhalt</p>');
		confirm.mockReturnValue(true);
		await wrapper.get('#event-template-selector').setValue('alternate');
		await flushPromises();
		expect(wrapper.get('.event-preview-input[placeholder="Event-Titel"]').element).toHaveProperty('value', 'Alternative');
		wrapper.unmount();
	});

	it('waits for templates before allowing edits and ignores a response after the preview is closed', async () => {
		allowEditing();
		config.WP_PUBLISHING_ENABLED = true;
		config.PUBLIC_BASE_URL = 'https://example.test';
		mocks.fetchActivity.mockResolvedValue(makeActivity({ department: 'Pfadi' }));
		mocks.fetchForm.mockResolvedValue({ public_slug: 'form-1' });
		let resolveTemplates!: (value: EventTemplate[]) => void;
		mocks.fetchActivityTemplates.mockImplementationOnce(() =>
			new Promise<EventTemplate[]>(resolve => { resolveTemplates = resolve; }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Als Event veröffentlichen"]').trigger('click');
		await flushPromises();
		expect(wrapper.get('#event-template-selector').element).toHaveProperty('disabled', true);
		expect(wrapper.get('.event-preview-form .rich-editor').attributes('contenteditable')).toBe('false');
		await wrapper.get('.modal .btn-cancel').trigger('click');
		resolveTemplates([makeEventTemplate()]);
		await flushPromises();
		expect(wrapper.find('#event-template-selector').exists()).toBe(false);
		wrapper.unmount();
	});

	it('publishes the selected template and the final edited preview instead of the template source', async () => {
		allowEditing();
		config.WP_PUBLISHING_ENABLED = true;
		config.PUBLIC_BASE_URL = 'https://example.test';
		mocks.fetchActivity.mockResolvedValue(makeActivity({ department: 'Pfadi' }));
		mocks.fetchForm.mockResolvedValue({ public_slug: 'form-1' });
		mocks.fetchActivityTemplates.mockResolvedValue([makeEventTemplate()]);
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Als Event veröffentlichen"]').trigger('click');
		await flushPromises();
		await wrapper.get('.event-preview-input[placeholder="Event-Titel"]').setValue('Fertiger Titel');
		wrapper.get('.event-preview-form .rich-editor').element.innerHTML = '<p>Bearbeiteter Inhalt</p>';
		await wrapper.get('.modal-actions .btn-primary').trigger('click');
		await flushPromises();
		expect(mocks.publishEvent).toHaveBeenCalledWith('activity-1', 'Fertiger Titel', '<p>Bearbeiteter Inhalt</p>');
		wrapper.unmount();
	});

	it('retains saved Event content when opening, even without a configured form URL, and confirms replacing it', async () => {
		allowEditing();
		config.WP_PUBLISHING_ENABLED = true;
		mocks.fetchActivity.mockResolvedValue(makeActivity({ department: 'Pfadi' }));
		mocks.fetchForm.mockResolvedValue({ public_slug: 'form-1' });
		mocks.fetchPublication.mockResolvedValue({
			id: 'publication-1', activity_id: 'activity-1', published_by: 'user-1',
			title: 'Gespeicherter eigener Titel', body_html: '<p>Gespeicherter Inhalt</p>',
			wp_event_id: null, published_at: '',
		});
		mocks.fetchActivityTemplates.mockResolvedValue([makeEventTemplate()]);
		const confirm = vi.spyOn(window, 'confirm').mockReturnValue(false);
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('.activity-actions-desktop .btn-mail--active').trigger('click');
		await flushPromises();
		expect(wrapper.get('input.event-preview-input').element).toHaveProperty('value', 'Gespeicherter eigener Titel');
		expect(wrapper.get('.event-preview-form .rich-editor').element.innerHTML).toBe('<p>Gespeicherter Inhalt</p>');
		await wrapper.get('#event-template-selector').setValue('template-default');
		expect(confirm).toHaveBeenCalledOnce();
		expect(wrapper.get('#event-template-selector').element).toHaveProperty('value', '');
		expect(wrapper.get('input.event-preview-input').element).toHaveProperty('value', 'Gespeicherter eigener Titel');
		wrapper.unmount();
	});

	it('loads MiData immediately when runtime configuration arrives after the activity', async () => {
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		vi.mocked(apiFetch).mockImplementation(async () =>
			new Response(JSON.stringify({ configured: true, children_count: 24 }), { status: 200 }),
		);
		config.MIDATA_ENABLED = true;
		await flushPromises();
		expect(apiFetch).toHaveBeenCalledWith('/api/activities/activity-1/midata/children-count');
		expect(wrapper.text()).toContain('24');
		wrapper.unmount();
	});

	it('shows MiData request failures rather than hiding them as missing configuration', async () => {
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		vi.mocked(apiFetch).mockRejectedValue(new Error('MiData connection failed'));
		config.MIDATA_ENABLED = true;
		await flushPromises();
		expect(wrapper.text()).toContain('MiData connection failed');
		wrapper.unmount();
	});

	it('keeps the similar-activity estimate together in view and edit mode', async () => {
		allowEditing();
		mocks.fetchActivity.mockResolvedValue(makeActivity({ department: 'Pfadi' }));
		mocks.activities = [makeActivity({ id: 'activity-2', date: '2026-06-24', department: 'Pfadi' })];
		vi.mocked(apiFetch).mockImplementation(async (url) => new Response(JSON.stringify(
			url === '/api/activities/activity-2/form/stats'
				? { exists: true, stats: { expected_current: 24 } }
				: {},
		), { status: 200 }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();

		const estimateRow = () => wrapper.get('.detail-stats-sidebar > .detail-stats-card:nth-child(2) .detail-stats-row');
		const checkEstimate = () => {
			expect(estimateRow().classes()).toContain('detail-stats-row--estimate');
			expect(estimateRow().get('.detail-value').text()).toBe('24');
		};
		checkEstimate();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(wrapper.find('.detail-form').exists()).toBe(true);
		checkEstimate();
		wrapper.unmount();
	});

	it('waits for a detailed forecast beyond the previous 2.5 second timeout', async () => {
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-06-29T12:00:00Z'));
		let weatherSignal: AbortSignal | null | undefined;
		vi.mocked(apiFetch).mockImplementation(async (url, options) => {
			if (url.endsWith('/weather-location')) {
				return new Response(JSON.stringify({ location: '6300 Zug' }), { status: 200 });
			}
			if (url.includes('/weather-expected')) {
				weatherSignal = options?.signal;
				return new Promise<Response>((resolve, reject) => {
					options?.signal?.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')));
					setTimeout(() => resolve(new Response(JSON.stringify({
						available: true,
						mode: 'forecast',
						temperature_min_c: 14,
						temperature_max_c: 18,
						source: 'MeteoSwiss Local Forecast',
						hourly_temps: [
							{ ts_unix: 1782892800, temperature_c: 14 },
							{ ts_unix: 1782896400, temperature_c: 18 },
						],
					}), { status: 200 })), 5000);
				});
			}
			return new Response('{}', { status: 200 });
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(2600);
		expect(weatherSignal).toBeDefined();
		expect(weatherSignal?.aborted).toBe(false);
		await vi.advanceTimersByTimeAsync(2400);
		await flushPromises();
		expect(wrapper.text()).toContain('14° | 18°');
		expect(wrapper.text()).toContain('MeteoSwiss Local Forecast');
		wrapper.unmount();
	});

	it.each([
		{
			mode: 'forecast',
			source: 'MeteoSwiss Local Forecast',
			note: 'Vorhersage basiert auf tre200h0 (stundlicher Mittelwert, naechster Zeitpunkt).',
		},
		{
			mode: 'seasonal-average',
			source: 'MeteoSwiss (saisonaler Durchschnitt CH)',
			note: 'Termin liegt ausserhalb des MeteoSwiss-Vorhersagehorizonts.',
		},
	])('hides $mode weather notes in view and edit mode', async (weather) => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-06-29T12:00:00Z'));
		vi.mocked(apiFetch).mockImplementation(async (url) => new Response(JSON.stringify(
			url.endsWith('/weather-location')
				? { location: '6300 Zug' }
				: { available: true, temperature_min_c: 14, temperature_max_c: 18, ...weather },
		), { status: 200 }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();

		const weatherCard = () => wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child');
		const checkWeather = () => {
			expect(weatherCard().text()).toContain('14° | 18°');
			expect(weatherCard().text()).toContain(weather.source);
			expect(weatherCard().text()).not.toContain('Hinweis');
			expect(weatherCard().text()).not.toContain(weather.note);
		};
		checkWeather();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(wrapper.find('.detail-form').exists()).toBe(true);
		checkWeather();
		wrapper.unmount();
	});

	it('shows forecast errors and does not cache them as successful weather', async () => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-06-29T12:00:00Z'));
		vi.mocked(apiFetch).mockImplementation(async (url) => new Response(JSON.stringify(
			url.endsWith('/weather-location')
				? { location: '6300 Zug' }
				: { available: false, error: 'meteo-temp-point-missing' },
		), { status: 200 }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();
		const weatherCard = () => wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child');
		expect(weatherCard().text()).toContain('Wetterfehler');
		expect(weatherCard().text()).toContain('meteo-temp-point-missing');
		expect(weatherCard().text()).not.toContain('Hinweis');
		expect(sessionStorage.length).toBe(0);
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(wrapper.find('.detail-form').exists()).toBe(true);
		expect(weatherCard().text()).toContain('Wetterfehler');
		expect(weatherCard().text()).toContain('meteo-temp-point-missing');
		expect(weatherCard().text()).not.toContain('Hinweis');
		wrapper.unmount();
	});

	it.each([
		{ name: 'future', now: '2026-06-29T12:00:00Z' },
		{ name: 'past', now: '2026-07-02T12:00:00Z' },
	])('does not show a weather error without a saved location for $name activities', async ({ now }) => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date(now));
		vi.mocked(apiFetch).mockImplementation(async (url) => new Response(JSON.stringify(
			url.endsWith('/weather-location')
				? { location: null }
				: { available: false, error: 'meteo-temp-point-missing' },
		), { status: 200 }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();

		const checkWeather = () => {
			const card = wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child');
			expect(card.text()).toContain('Vorhersage—');
			expect(card.text()).toContain('Temperatur—');
			expect(card.text()).not.toContain('Wetterfehler');
			expect(card.text()).not.toContain('meteo-temp-point-missing');
		};
		checkWeather();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(wrapper.find('.detail-form').exists()).toBe(true);
		checkWeather();
		wrapper.unmount();
	});

	it('keeps a frozen weather snapshot visible without a saved location', async () => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-07-02T12:00:00Z'));
		vi.mocked(apiFetch).mockImplementation(async (url) => new Response(JSON.stringify(
			url.endsWith('/weather-location')
				? { location: null }
				: { available: true, mode: 'frozen', temperature_min_c: 14, temperature_max_c: 18 },
		), { status: 200 }));
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();
		expect(wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child').text()).toContain('14° | 18°');
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		expect(wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child').text()).toContain('14° | 18°');
		wrapper.unmount();
	});

	it('clears a missing-location error while loading a newly saved weather location', async () => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-07-02T12:00:00Z'));
		let resolveForecast: (response: Response) => void = () => {
			throw new Error('New forecast request was not started');
		};
		vi.mocked(apiFetch).mockImplementation(async (url, options) => {
			if (url.endsWith('/weather-location')) {
				return new Response(JSON.stringify({ location: options?.method === 'PUT' ? '6300 Zug' : null }), { status: 200 });
			}
			if (url.includes('/weather-expected?location=')) {
				return new Promise<Response>((resolve) => { resolveForecast = resolve; });
			}
			return new Response(JSON.stringify({ available: false, error: 'meteo-temp-point-missing' }), { status: 200 });
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.detail-weather-input').setValue('6300 Zug');
		await wrapper.get('.detail-weather-input-wrap button').trigger('click');
		await flushPromises();

		const weatherCard = () => wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child');
		expect(weatherCard().text()).not.toContain('Wetterfehler');
		expect(weatherCard().text()).not.toContain('meteo-temp-point-missing');
		resolveForecast(new Response(JSON.stringify({ available: false, error: 'meteo-location-not-found' }), { status: 200 }));
		await flushPromises();
		expect(weatherCard().text()).toContain('Wetterfehler');
		expect(weatherCard().text()).toContain('meteo-location-not-found');
		wrapper.unmount();
	});

	it.each(['request', 'response body'])('does not let a stale %s override a cached forecast after saving a location', async (pendingStage) => {
		allowEditing();
		vi.useFakeTimers();
		vi.setSystemTime(new Date('2026-07-02T12:00:00Z'));
		sessionStorage.setItem(
			'dpw.weatherExpected:activity-1:2026-07-01:10:00:11:00:past:6300 zug',
			JSON.stringify({
				ts: Date.now(),
				data: { available: true, mode: 'frozen', temperature_min_c: 14, temperature_max_c: 18 },
			}),
		);
		let oldSignal: AbortSignal | null | undefined;
		let finishOldRequest: () => void = () => { throw new Error('Old request was not started'); };
		vi.mocked(apiFetch).mockImplementation(async (url, options) => {
			if (url.endsWith('/weather-location')) {
				return new Response(JSON.stringify({ location: options?.method === 'PUT' ? '6300 Zug' : null }), { status: 200 });
			}
			if (url.includes('/weather-expected')) {
				oldSignal = options?.signal;
				if (pendingStage === 'request') {
					return new Promise<Response>((resolve) => {
						finishOldRequest = () => resolve(new Response(JSON.stringify({ available: false, error: 'meteo-temp-point-missing' }), { status: 200 }));
					});
				}
				const response = new Response('{}', { status: 200 });
				vi.spyOn(response, 'json').mockImplementation(() => new Promise((resolve) => {
					finishOldRequest = () => resolve({ available: false, error: 'meteo-temp-point-missing' });
				}));
				return response;
			}
			return new Response('{}', { status: 200 });
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await vi.advanceTimersByTimeAsync(250);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await wrapper.get('.detail-weather-input').setValue('6300 Zug');
		await wrapper.get('.detail-weather-input-wrap button').trigger('click');
		await flushPromises();

		const weatherCard = () => wrapper.get('.detail-stats-sidebar .detail-stats-card:last-child');
		expect(weatherCard().text()).toContain('14° | 18°');
		expect(oldSignal?.aborted).toBe(true);
		finishOldRequest();
		await flushPromises();
		expect(weatherCard().text()).toContain('14° | 18°');
		expect(weatherCard().text()).not.toContain('Wetterfehler');
		wrapper.unmount();
	});
});
