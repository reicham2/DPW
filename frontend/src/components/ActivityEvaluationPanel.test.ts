// @vitest-environment jsdom
import { flushPromises, mount } from '@vue/test-utils';
import { beforeEach, describe, expect, it, vi } from 'vitest';

const mocks = vi.hoisted(() => ({
	apiFetch: vi.fn(),
}));

vi.mock('../composables/useApi', () => ({
	apiFetch: mocks.apiFetch,
	formatApiError: (error: unknown) => error instanceof Error ? error.message : String(error),
}));

import ActivityEvaluationPanel from './ActivityEvaluationPanel.vue';

function entry(overrides: Record<string, unknown> = {}) {
	return {
		author: 'Leitung',
		rating: 8,
		positive: 'Gute Stimmung\nGutes Team',
		improvements: 'Mehr Zeit einplanen',
		action_items: 'Material früher bereitstellen',
		notes: '',
		created_at: '2026-01-01T10:00:00Z',
		updated_at: '2026-01-01T10:00:00Z',
		...overrides,
	};
}

function evaluationResponse(overrides: Record<string, unknown> = {}) {
	return {
		actual_participants: null,
		actual_start_time: null,
		actual_end_time: null,
		can_edit_actuals: false,
		feedback: [],
		mine: null,
		...overrides,
	};
}

function respond(body: unknown) {
	return new Response(JSON.stringify(body), { status: 200 });
}

function mountPanel() {
	return mount(ActivityEvaluationPanel, {
		props: { activityId: 'activity-1', plannedStartTime: '14:00', plannedEndTime: '16:00' },
	});
}

describe('ActivityEvaluationPanel', () => {
	beforeEach(() => {
		mocks.apiFetch.mockReset();
		mocks.apiFetch.mockResolvedValue(respond(evaluationResponse()));
	});

	it('shows non-responsible users only the rating-first feedback form and read-only actuals', async () => {
		mocks.apiFetch.mockResolvedValue(respond(evaluationResponse({
			actual_participants: 18,
			actual_start_time: '14:05',
			actual_end_time: '16:10',
		})));
		const wrapper = mountPanel();
		await flushPromises();

		expect(wrapper.find('#actual-participants').exists()).toBe(false);
		expect(wrapper.text()).toContain('18');
		expect(wrapper.text()).toContain('14:05–16:10');
		const form = wrapper.get('form');
		expect(form.findAll('input[type="radio"]')).toHaveLength(11);
		const legends = wrapper.findAll('legend').map((legend) => legend.text());
		expect(legends).toEqual([
			'Wie gut war die Aktivität?',
			'Was lief gut?',
			'Was kann verbessert werden?',
			'Welche Massnahmen nehmen wir mit?',
		]);
	});

	it('lets responsible users save actual participants and times via the separate endpoint', async () => {
		mocks.apiFetch
			.mockResolvedValueOnce(respond(evaluationResponse({ can_edit_actuals: true })))
			.mockResolvedValueOnce(respond(evaluationResponse({
				can_edit_actuals: true,
				actual_participants: 18,
				actual_start_time: '14:05',
				actual_end_time: '16:10',
			})));
		const wrapper = mountPanel();
		await flushPromises();

		await wrapper.get('#actual-participants').setValue('18');
		await wrapper.get('#actual-start').setValue('14:05');
		await wrapper.get('#actual-end').setValue('16:10');
		await wrapper.get('form.evaluation-actuals').trigger('submit');
		await flushPromises();

		expect(mocks.apiFetch).toHaveBeenLastCalledWith('/api/activities/activity-1/evaluation/actuals', expect.objectContaining({
			method: 'PUT',
			body: JSON.stringify({ actual_participants: 18, actual_start_time: '14:05', actual_end_time: '16:10' }),
		}));
		expect(wrapper.text()).toContain('Gespeichert');
	});

	it('rejects invalid participant counts before sending a write request', async () => {
		mocks.apiFetch.mockResolvedValue(respond(evaluationResponse({ can_edit_actuals: true })));
		const wrapper = mountPanel();
		await flushPromises();
		await wrapper.get('#actual-participants').setValue('-1');
		await wrapper.get('form.evaluation-actuals').trigger('submit');
		await flushPromises();

		expect(mocks.apiFetch).toHaveBeenCalledTimes(1);
		expect(wrapper.text()).toContain('ganze, nicht negative Teilnehmendenzahl');
	});

	it('requires a rating when text is given and saves rating with repeatable points', async () => {
		mocks.apiFetch
			.mockResolvedValueOnce(respond(evaluationResponse({
				mine: {
					rating: null,
					positive: 'Gute Stimmung\nGutes Team',
					improvements: 'Mehr Zeit',
					action_items: '',
					notes: '',
				},
			})))
			.mockResolvedValueOnce(respond(evaluationResponse({ feedback: [entry()] })));
		const wrapper = mountPanel();
		await flushPromises();

		await wrapper.get('form[aria-labelledby="feedback-title"]').trigger('submit');
		await flushPromises();
		expect(mocks.apiFetch).toHaveBeenCalledTimes(1);
		expect(wrapper.text()).toContain('Bewertung von 0 bis 10');

		await wrapper.findAll('input[type="radio"]')[7].setValue(true);
		const improvementGroup = wrapper.findAll('fieldset')[2];
		await improvementGroup.get('button.evaluation-add-button').trigger('click');
		await improvementGroup.findAll('input[type="text"]')[1].setValue('Ablauf klarer');
		await wrapper.get('form[aria-labelledby="feedback-title"]').trigger('submit');
		await flushPromises();

		expect(mocks.apiFetch).toHaveBeenLastCalledWith('/api/activities/activity-1/evaluation', expect.objectContaining({
			method: 'PUT',
			body: JSON.stringify({
				rating: 7,
				positive: 'Gute Stimmung\nGutes Team',
				improvements: 'Mehr Zeit\nAblauf klarer',
				action_items: '',
				notes: '',
			}),
		}));
		expect(wrapper.text()).toContain('Gespeichert');
	});

	it('lets people add and remove point fields without removing the last input', async () => {
		const wrapper = mountPanel();
		await flushPromises();

		const positiveGroup = wrapper.findAll('fieldset')[1];
		expect(positiveGroup.findAll('input[type="text"]')).toHaveLength(1);
		await positiveGroup.get('button.evaluation-add-button').trigger('click');
		expect(positiveGroup.findAll('input[type="text"]')).toHaveLength(2);
		await positiveGroup.get('button[aria-label*="Punkt 1"]').trigger('click');
		expect(positiveGroup.findAll('input[type="text"]')).toHaveLength(1);
		await positiveGroup.get('button[aria-label*="Punkt 1"]').trigger('click');
		expect(positiveGroup.findAll('input[type="text"]')).toHaveLength(1);
	});

	it('validates the combined text in each category before saving', async () => {
		const wrapper = mountPanel();
		await flushPromises();

		await wrapper.findAll('input[type="radio"]')[5].setValue(true);
		const positiveGroup = wrapper.findAll('fieldset')[1];
		await positiveGroup.find('input[type="text"]').setValue('x'.repeat(2500));
		await positiveGroup.get('button.evaluation-add-button').trigger('click');
		await positiveGroup.findAll('input[type="text"]')[1].setValue('y'.repeat(1501));
		await wrapper.get('form').trigger('submit');
		await flushPromises();

		expect(mocks.apiFetch).toHaveBeenCalledTimes(1);
		expect(wrapper.text()).toContain('höchstens 4000 Zeichen pro Bereich');
	});

	it('lists responses in a table with scored badges, quotes and expandable details', async () => {
		mocks.apiFetch.mockResolvedValue(respond(evaluationResponse({
			feedback: [
				entry(),
				entry({ author: 'Helfer', rating: 2, positive: '', improvements: 'Zu wenig Pause', action_items: '', notes: '' }),
				entry({ author: 'Altbestand', rating: null }),
			],
		})));
		const wrapper = mountPanel();
		await flushPromises();

		expect(wrapper.text()).toContain('3 Rückmeldungen gesammelt');
		expect(wrapper.text()).toContain('Ø 5,0 / 10');
		const scores = wrapper.findAll('.evaluation-score');
		expect(scores.map((score) => score.text())).toEqual(['8', '2', '–']);
		expect(scores[0].attributes('aria-label')).toBe('Bewertung 8 von 10');
		expect(scores[2].attributes('aria-label')).toBe('Keine Bewertung abgegeben');
		expect(scores[0].attributes('style')).not.toBe(scores[1].attributes('style'));
		expect(wrapper.findAll('.evaluation-row')[0].findAll('q').map((quote) => quote.text())).toEqual([
			'Gute Stimmung',
			'Mehr Zeit einplanen',
		]);
		expect(wrapper.find('.evaluation-details').exists()).toBe(false);

		const toggle = wrapper.findAll('.evaluation-toggle')[0];
		expect(toggle.attributes('aria-expanded')).toBe('false');
		await toggle.trigger('click');
		expect(toggle.attributes('aria-expanded')).toBe('true');
		expect(wrapper.findAll('.evaluation-details li').map((item) => item.text())).toEqual([
			'Gute Stimmung',
			'Gutes Team',
			'Mehr Zeit einplanen',
			'Material früher bereitstellen',
		]);

		await wrapper.findAll('.evaluation-submissions-head button')[0].trigger('click');
		expect(wrapper.findAll('.evaluation-details')).toHaveLength(3);
		await wrapper.findAll('.evaluation-submissions-head button')[0].trigger('click');
		expect(wrapper.find('.evaluation-details').exists()).toBe(false);

		await wrapper.findAll('.evaluation-row')[1].trigger('click');
		expect(wrapper.findAll('.evaluation-details')).toHaveLength(1);
	});
});
