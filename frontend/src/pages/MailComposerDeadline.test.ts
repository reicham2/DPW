import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { flushPromises, shallowMount } from '@vue/test-utils';
import type { Activity } from '../types';

const { fetchActivity, fetchComposerContext, fetchDraft, sendMail } = vi.hoisted(() => ({
	fetchActivity: vi.fn(), fetchComposerContext: vi.fn(), fetchDraft: vi.fn(), sendMail: vi.fn(),
}));

vi.mock('vue-router', () => ({
	useRoute: () => ({ params: { id: 'act-1' } }),
	useRouter: () => ({ replace: vi.fn(), back: vi.fn() }),
}));
vi.mock('../config', () => ({ config: { PUBLIC_BASE_URL: 'https://dpw.example' } }));
vi.mock('../composables/useActivities', () => ({ useActivities: () => ({ fetchActivity }) }));
vi.mock('../composables/useMailTemplates', async () => {
	const { ref } = await import('vue');
	return {
		useMailTemplates: () => ({
			fetchComposerContext, fetchDraft, sendMail, saveDraft: vi.fn(),
			fetchSentMails: vi.fn().mockResolvedValue([]), deleteDraft: vi.fn(),
			sending: ref(false), error: ref(null),
		}),
	};
});
vi.mock('../composables/useAuth', async () => {
	const { ref } = await import('vue');
	return { user: ref({ id: 'user-1', email: 'anna@example.com', display_name: 'Anna' }) };
});
vi.mock('../composables/usePermissions', async () => {
	const { ref } = await import('vue');
	return {
		usePermissions: () => ({
			myPermissions: ref({ mail_send_scope: 'all' }), fetchMyPermissions: vi.fn(),
		}),
	};
});
vi.mock('../composables/useUserResolver', () => ({
	useUserResolver: () => ({ resolveResponsibleName: (id: string) => id }),
}));
vi.mock('../composables/useContactSearch', async () => {
	const { ref } = await import('vue');
	return {
		useContactSearch: () => ({
			results: ref([]), searching: ref(false), search: vi.fn(), clear: vi.fn(),
		}),
	};
});

import MailComposerPage from './MailComposerPage.vue';

const activity: Activity = {
	id: 'act-1', title: 'Pfadilager', date: '2026-07-15',
	start_time: '14:00', end_time: '17:00', goal: '', location: 'Waldhaus',
	responsible: [], department: 'Pfadi', material: [], tn_material: ['Flasche', 'Regenschutz'],
	siko_text: null, bad_weather_info: null, planned_participants_estimate: null,
	created_at: '', updated_at: '', programs: [],
};

describe('mail deadline variables', () => {
	beforeEach(() => {
		vi.clearAllMocks();
		vi.stubGlobal('localStorage', {
			getItem: vi.fn().mockReturnValue(null), setItem: vi.fn(), removeItem: vi.fn(),
		});
		fetchActivity.mockResolvedValue(activity);
		fetchComposerContext.mockResolvedValue({
			form: { exists: true, public_slug: 'signup' },
			template: {
				subject: '{{titel}}: Anmeldung bis {{datum_kurz|-7}}',
				body: '<p><strong>{{datum|-7}}</strong></p>' +
					'<p>{{datum_kurz|-7}} {{formular_link|Anmelden}}</p>' +
					'<p>{{datum_kurz|-7}} {{tn_material}}</p><p>{{datum_kurz|abc}}</p>',
				recipients: ['to@example.com'], cc: [],
			},
		});
		fetchDraft.mockResolvedValue(null);
		sendMail.mockResolvedValue(true);
	});
	afterEach(() => vi.unstubAllGlobals());

	it('previews and sends deadline dates while preserving formatting, links and lists', async () => {
		const wrapper = shallowMount(MailComposerPage);
		await flushPromises();
		expect(wrapper.get<HTMLInputElement>('input[required]').element.value)
			.toBe('Pfadilager: Anmeldung bis 08.07.2026');
		const html = wrapper.get<HTMLElement>('.rich-editor').element.innerHTML;
		expect(html).toContain('<strong>Mittwoch, 8. Juli 2026</strong>');
		expect(html).toContain('08.07.2026 <a href="https://dpw.example/forms/signup">Anmelden</a>');
		expect(html).toContain('<p>08.07.2026 </p><ul><li>Flasche</li><li>Regenschutz</li></ul>');
		expect(html).toContain('{{datum_kurz|abc}}');
		await wrapper.get('form').trigger('submit');
		await flushPromises();
		expect(sendMail).toHaveBeenCalledWith(
			['to@example.com'], 'Pfadilager: Anmeldung bis 08.07.2026',
			expect.stringContaining('<strong>Mittwoch, 8. Juli 2026</strong>'),
			'anna@example.com', 'act-1', [],
		);
		expect(sendMail.mock.calls[0][2]).toContain('08.07.2026');
		expect(sendMail.mock.calls[0][2]).toContain('<ul><li>Flasche</li><li>Regenschutz</li></ul>');
		expect(sendMail.mock.calls[0][2]).toContain('{{datum_kurz|abc}}');
		wrapper.unmount();
	});

	it('preserves an edited draft instead of re-expanding deadlines on restore', async () => {
		fetchDraft.mockResolvedValue({
			recipients: ['to@example.com'], cc: [],
			subject: 'Entwurf {{datum_kurz|-7}}', body_html: '<p>Eigene Frist</p>',
		});
		const wrapper = shallowMount(MailComposerPage);
		await flushPromises();
		expect(wrapper.get<HTMLInputElement>('input[required]').element.value)
			.toBe('Entwurf {{datum_kurz|-7}}');
		wrapper.unmount();
	});
});
