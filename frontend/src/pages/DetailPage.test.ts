// @vitest-environment jsdom
import { flushPromises, shallowMount } from '@vue/test-utils';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { Activity, ActivityInput, IdeenkisteItem, User, WsEvent } from '../types';

const mocks = vi.hoisted(() => ({
	replace: vi.fn(),
	fetchActivity: vi.fn(),
	updateActivity: vi.fn(),
	uploadAttachment: vi.fn(),
	fetchForm: vi.fn(),
	fetchEventTemplate: vi.fn(),
	fetchPublication: vi.fn(),
	setLastUpdated: undefined as ((activity: Activity | null) => void) | undefined,
	setActivityError: undefined as ((message: string | null) => void) | undefined,
	setConnected: undefined as ((connected: boolean) => void) | undefined,
	onMessage: undefined as ((event: WsEvent) => void) | undefined,
	editable: false,
	activities: [] as Activity[],
	ideaItems: [] as IdeenkisteItem[],
}));

vi.mock('vue-router', () => ({
	useRoute: () => ({ params: { id: 'activity-1' }, query: {} }),
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
vi.mock('../composables/useEventTemplates', () => ({
	useEventTemplates: () => ({
		fetchTemplate: mocks.fetchEventTemplate,
		fetchPublication: mocks.fetchPublication,
		publishEvent: vi.fn(),
		unpublishEvent: vi.fn(),
	}),
}));
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
		mocks.activities = [];
		mocks.ideaItems = [];
		user.value = null;
		sessionStorage.clear();
		vi.mocked(apiFetch).mockReset().mockResolvedValue(new Response('{}', { status: 200 }));
		mocks.replace.mockReset().mockResolvedValue(undefined);
		mocks.onMessage = undefined;
		mocks.fetchActivity.mockReset().mockResolvedValue(makeActivity());
		mocks.updateActivity.mockReset().mockResolvedValue(true);
		mocks.uploadAttachment.mockReset().mockResolvedValue(null);
		mocks.fetchForm.mockReset().mockResolvedValue(null);
		mocks.fetchEventTemplate.mockReset().mockResolvedValue(null);
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
				replacements: [{ value: 'einen Fehler' }],
				rule: { id: 'OBJECT_AGREEMENT' },
			}));
			return new Response(JSON.stringify({ language: 'de-CH', matches }), { status: 200 });
		});

		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Aktivität bearbeiten"]').trigger('click');
		await flushPromises();
		const editor = wrapper.get('.rich-editor').element as HTMLElement;
		const execCommand = stubInsertTextCommand(editor);
		try {
			await wrapper.get('button[aria-label="Rechtschreibung prüfen"]').trigger('click');
			await flushPromises();
			expect(wrapper.findAll('.program-proofreading__suggestion')).toHaveLength(2);

			await wrapper.findAll('.program-proofreading__suggestion')[0].trigger('click');
			await flushPromises();
			expect(editor.textContent).toBe('Ich habe einen Fehler und ein Fehler.');
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(true);
			expect(wrapper.findAll('.program-proofreading__suggestion')).toHaveLength(1);
			expect(requestTexts[1]).toBe(editor.textContent);

			await wrapper.find('.program-proofreading__suggestion').trigger('click');
			await flushPromises();
			expect(editor.textContent).toBe('Ich habe einen Fehler und einen Fehler.');
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(true);
			expect(wrapper.find('.program-proofreading__results').text()).toContain('Keine Vorschläge gefunden.');
			expect(requestTexts).toEqual([
				description,
				'Ich habe einen Fehler und ein Fehler.',
				'Ich habe einen Fehler und einen Fehler.',
			]);
			expect(execCommand.command).toHaveBeenCalledTimes(2);
		} finally {
			execCommand.restore();
			wrapper.unmount();
		}
	});

	it('hides stale suggestions after typing, rechecks the changed text, and allows closing results', async () => {
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
		await flushPromises();
		const editor = wrapper.get('.rich-editor').element as HTMLElement;
		const execCommand = stubInsertTextCommand(editor);
		try {
			await wrapper.get('button[aria-label="Rechtschreibung prüfen"]').trigger('click');
			await flushPromises();
			expect(wrapper.findAll('.program-proofreading__suggestion')).toHaveLength(1);

			editor.innerHTML = 'Ich habe ein Fehler geändert.';
			editor.dispatchEvent(new Event('input', { bubbles: true }));
			await flushPromises();
			expect(editor.textContent).toBe('Ich habe ein Fehler geändert.');
			expect(wrapper.findAll('.program-proofreading__suggestion')).toHaveLength(0);
			expect(wrapper.find('.program-proofreading__results').text()).toContain('Bitte erneut prüfen.');
			expect(requestTexts).toHaveLength(1);

			await wrapper.get('button[aria-label="Rechtschreibung prüfen"]').trigger('click');
			await flushPromises();
			expect(requestTexts[1]).toBe('Ich habe ein Fehler geändert.');
			expect(wrapper.findAll('.program-proofreading__suggestion')).toHaveLength(1);
			await wrapper.get('button[aria-label="Prüfung schliessen"]').trigger('click');
			expect(wrapper.find('.program-proofreading__results').exists()).toBe(false);
			expect(editor.textContent).toBe('Ich habe ein Fehler geändert.');
		} finally {
			execCommand.restore();
			wrapper.unmount();
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
		mocks.fetchEventTemplate.mockResolvedValue({
			title: 'Motto: {{roter_faden}}',
			body: '<p>{{roter_faden}} / {{formular_link|Anmelden}}</p>',
		});
		const wrapper = shallowMount(DetailPage);
		await flushPromises();
		await wrapper.get('button[title="Als Event veröffentlichen"]').trigger('click');
		await flushPromises();
		expect((wrapper.get('.event-preview-input').element as HTMLInputElement).value).toContain('Erste Zeile');
		expect((wrapper.get('.event-preview-input').element as HTMLInputElement).value).not.toContain('{{roter_faden}}');
		const editor = wrapper.get('.event-preview-form .rich-editor');
		expect(editor.element.textContent).toContain(story.replace('\n', ''));
		expect(editor.find('br').exists()).toBe(true);
		expect(editor.find('script').exists()).toBe(false);
		expect(editor.get('a').attributes('href')).toBe('https://example.test/forms/form-1');
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
