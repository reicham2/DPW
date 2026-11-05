import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, shallowMount } from '@vue/test-utils';
import type { Activity } from '../types';

const mocks = vi.hoisted(() => ({
	activity: undefined as Activity | undefined,
	fetchComposerContext: vi.fn(),
	fetchDraft: vi.fn(),
	sendMail: vi.fn(),
	saveDraft: vi.fn(),
}));

vi.mock('vue-router', () => ({
	useRoute: () => ({ params: { id: 'activity-1' } }),
	useRouter: () => ({ replace: vi.fn(), back: vi.fn(), push: vi.fn() }),
}));
vi.mock('../config', () => ({ config: { PUBLIC_BASE_URL: 'https://dpw.example' } }));
vi.mock('../composables/useActivities', () => ({
	useActivities: () => ({ fetchActivity: vi.fn().mockImplementation(async () => mocks.activity) }),
}));
vi.mock('../composables/useMailTemplates', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		useMailTemplates: () => ({
			sendMail: mocks.sendMail,
			sending: ref(false),
			error: ref(null),
			fetchSentMails: vi.fn().mockResolvedValue([]),
			fetchDraft: mocks.fetchDraft,
			saveDraft: mocks.saveDraft,
			deleteDraft: vi.fn(),
			fetchComposerContext: mocks.fetchComposerContext,
		}),
	};
});
vi.mock('../composables/usePermissions', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		usePermissions: () => ({
			myPermissions: ref({ mail_send_scope: 'all' }),
			fetchMyPermissions: vi.fn().mockResolvedValue(undefined),
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
vi.mock('../composables/useAuth', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { user: ref({ id: 'user-1', email: 'anna@example.com', display_name: 'Anna & Ben' }) };
});
vi.mock('../composables/useUserResolver', () => ({
	useUserResolver: () => ({ resolveResponsibleName: (id: string) => id }),
}));

import MailComposerPage from './MailComposerPage.vue';

function activity(overrides: Partial<Activity> = {}): Activity {
	return {
		id: 'activity-1',
		title: 'Spiel & Spass <2026>',
		date: '2026-07-15',
		start_time: '14:00',
		end_time: '17:00',
		goal: '',
		location: 'Wald & Wiese',
		responsible: [],
		department: 'Pfadi',
		material: [],
		tn_material: ['Flasche', 'Regenschutz'],
		siko_text: null,
		guiding_thread: null,
		bad_weather_info: null,
		planned_participants_estimate: null,
		created_at: '',
		updated_at: '',
		programs: [],
		...overrides,
	};
}

describe('MailComposerPage template variables', () => {
	beforeEach(() => {
		vi.clearAllMocks();
		vi.stubGlobal('localStorage', {
			getItem: vi.fn().mockReturnValue(null),
			setItem: vi.fn(),
			removeItem: vi.fn(),
		});
		mocks.activity = activity();
		mocks.fetchComposerContext.mockResolvedValue({
			form: { exists: true, public_slug: 'signup' },
			template: {
				subject: '{{titel}} in {{ort}} / {{absender_name}} / {{formular_link|Anmelden}} / {{tn_material}}',
				body: '<p><strong>{{titel}}</strong></p><p>{{formular_link|Anmelden}}</p><p>{{tn_material}}</p>',
				recipients: ['to@example.com'],
				cc: [],
			},
		});
		mocks.fetchDraft.mockResolvedValue(null);
		mocks.sendMail.mockResolvedValue(true);
	});

	afterEach(() => vi.unstubAllGlobals());

	it('substitutes multiline story text safely alongside a form link', async () => {
		mocks.activity = activity({ guiding_thread: 'Erste Zeile\n<script>Beispiel</script>' });
		mocks.fetchComposerContext.mockResolvedValue({
			form: { exists: false, public_slug: null },
			template: {
				subject: 'Motto: {{roter_faden}}',
				body: '<p>{{roter_faden}} / {{formular_link|Anmelden}}</p>',
				recipients: [],
				cc: [],
			},
		});

		const wrapper = shallowMount(MailComposerPage);
		await flushPromises();
		const subject = wrapper.get<HTMLInputElement>('input[required]').element;
		expect(subject.value).toContain('Erste Zeile');
		expect(subject.value).not.toContain('{{roter_faden}}');
		const editor = wrapper.get('.rich-editor');
		expect(editor.element.textContent).toContain('Erste Zeile<script>Beispiel</script>');
		expect(editor.find('br').exists()).toBe(true);
		expect(editor.find('script').exists()).toBe(false);
		expect(editor.get('a').text()).toBe('Anmelden');
		wrapper.unmount();
	});

	it('previews and sends the expanded subject as plain text while preserving rich body output', async () => {
		const wrapper = shallowMount(MailComposerPage);
		await flushPromises();
		const expectedSubject = 'Spiel & Spass <2026> in Wald & Wiese / Anna & Ben / https://dpw.example/forms/signup / Flasche, Regenschutz';
		expect(wrapper.get<HTMLInputElement>('input[required]').element.value).toBe(expectedSubject);
		const body = wrapper.get<HTMLElement>('.rich-editor').element.innerHTML;
		expect(body).toContain('<strong>Spiel &amp; Spass &lt;2026&gt;</strong>');
		expect(body).toContain('<a href="https://dpw.example/forms/signup">Anmelden</a>');
		expect(body).toContain('<ul><li>Flasche</li><li>Regenschutz</li></ul>');
		await wrapper.get('form').trigger('submit');
		await flushPromises();
		expect(mocks.sendMail).toHaveBeenCalledWith(
			['to@example.com'],
			expectedSubject,
			expect.stringContaining('<strong>'),
			'anna@example.com',
			'activity-1',
			[],
		);
		wrapper.unmount();
	});

	it('restores an edited draft subject without re-expanding it or interpreting it as HTML', async () => {
		mocks.fetchDraft.mockResolvedValue({
			recipients: ['to@example.com'],
			cc: [],
			subject: 'Entwurf <Pfadi> & {{titel}}',
			body_html: '<p>Entwurf</p>',
		});
		const wrapper = shallowMount(MailComposerPage);
		await flushPromises();
		expect(wrapper.get<HTMLInputElement>('input[required]').element.value)
			.toBe('Entwurf <Pfadi> & {{titel}}');
		wrapper.unmount();
	});
});
