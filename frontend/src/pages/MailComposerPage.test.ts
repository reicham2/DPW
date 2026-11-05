import { flushPromises, shallowMount } from '@vue/test-utils';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { Activity } from '../types';

const mocks = vi.hoisted(() => ({ activity: undefined as Activity | undefined }));

vi.mock('vue-router', () => ({
	useRoute: () => ({ params: { id: 'activity-1' } }),
	useRouter: () => ({ replace: vi.fn(), push: vi.fn() }),
}));

vi.mock('../composables/useActivities', () => ({
	useActivities: () => ({ fetchActivity: vi.fn().mockImplementation(async () => mocks.activity) }),
}));

vi.mock('../composables/useMailTemplates', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return {
		useMailTemplates: () => ({
			sendMail: vi.fn(),
			sending: ref(false),
			error: ref(null),
			fetchSentMails: vi.fn().mockResolvedValue([]),
			fetchDraft: vi.fn().mockResolvedValue(null),
			saveDraft: vi.fn(),
			deleteDraft: vi.fn(),
			fetchComposerContext: vi.fn().mockResolvedValue({
				form: { exists: false, public_slug: null },
				template: {
					subject: 'Motto: {{roter_faden}}',
					body: '<p>{{roter_faden}} / {{formular_link|Anmelden}}</p>',
					recipients: [],
					cc: [],
				},
			}),
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
	return { useContactSearch: () => ({ results: ref([]), searching: ref(false), search: vi.fn(), clear: vi.fn() }) };
});

vi.mock('../composables/useAuth', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { user: ref(null) };
});

vi.mock('../composables/useUserResolver', () => ({
	useUserResolver: () => ({ resolveResponsibleName: (id: string) => id }),
}));

import MailComposerPage from './MailComposerPage.vue';

describe('MailComposerPage guiding thread template', () => {
	afterEach(() => {
		localStorage.removeItem('dpw:mail-composer-draft:activity-1');
	});

	it('substitutes multiline story text safely alongside a form link', async () => {
		mocks.activity = {
			id: 'activity-1',
			title: 'Aktivität',
			date: '2026-07-01',
			start_time: '10:00',
			end_time: '11:00',
			goal: '',
			guiding_thread: 'Erste Zeile\n<script>Beispiel</script>',
			location: '',
			responsible: [],
			department: null,
			material: [],
			tn_material: [],
			siko_text: null,
			bad_weather_info: null,
			planned_participants_estimate: null,
			created_at: '',
			updated_at: '',
			programs: [],
		};
		const wrapper = shallowMount(MailComposerPage);
		await flushPromises();
		const subject = wrapper.get('input[required]').element as HTMLInputElement;
		expect(subject.value).toContain('Erste Zeile');
		expect(subject.value).not.toContain('{{roter_faden}}');
		const editor = wrapper.get('.rich-editor');
		expect(editor.element.textContent).toContain('Erste Zeile<script>Beispiel</script>');
		expect(editor.find('br').exists()).toBe(true);
		expect(editor.find('script').exists()).toBe(false);
		expect(editor.get('a').text()).toBe('Anmelden');
		wrapper.unmount();
	});
});
