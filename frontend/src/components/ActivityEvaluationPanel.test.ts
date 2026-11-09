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

function evaluationResponse(overrides: Record<string, unknown> = {}) {
	return {
		actual_participants: null,
		feedback: [],
		mine: null,
		...overrides,
	};
}

describe('ActivityEvaluationPanel', () => {
	beforeEach(() => {
		mocks.apiFetch.mockReset();
		mocks.apiFetch.mockResolvedValue(new Response(JSON.stringify(evaluationResponse()), { status: 200 }));
	});

	it('shows the central feedback read-only to activity readers', async () => {
		mocks.apiFetch.mockResolvedValue(new Response(JSON.stringify(evaluationResponse({
			actual_participants: 18,
			feedback: [{
				author: 'Leitung',
				positive: 'Gute Stimmung\nGutes Team',
				improvements: 'Mehr Zeit einplanen',
				action_items: 'Material früher bereitstellen',
				notes: '',
				created_at: '2026-01-01T10:00:00Z',
				updated_at: '2026-01-01T10:00:00Z',
			}],
		})), { status: 200 }));

		const wrapper = mount(ActivityEvaluationPanel, {
			props: { activityId: 'activity-1', canEdit: false },
		});
		await flushPromises();

		expect(wrapper.text()).toContain('18');
		expect(wrapper.text()).toContain('Mehr Zeit einplanen');
		expect(wrapper.findAll('.evaluation-entry li').map((item) => item.text())).toEqual([
			'Gute Stimmung',
			'Gutes Team',
			'Mehr Zeit einplanen',
			'Material früher bereitstellen',
		]);
		expect(wrapper.text()).toContain('1 Rückmeldung gesammelt');
		expect(wrapper.find('form').exists()).toBe(false);
	});

	it('keeps existing multiline feedback and saves added points with actual attendance', async () => {
		mocks.apiFetch
			.mockResolvedValueOnce(new Response(JSON.stringify(evaluationResponse({
				mine: {
					positive: 'Gute Stimmung\nGutes Team',
					improvements: 'Mehr Zeit',
					action_items: 'Material früher bereitstellen',
					notes: '',
				},
			})), { status: 200 }))
			.mockResolvedValueOnce(new Response(JSON.stringify(evaluationResponse({
				actual_participants: 18,
				feedback: [{
					author: 'Leitung',
					positive: 'Gute Stimmung\nGutes Team',
					improvements: 'Mehr Zeit\nAblauf klarer kommunizieren',
					action_items: 'Material früher bereitstellen',
					notes: '',
					created_at: '2026-01-01T10:00:00Z',
					updated_at: '2026-01-01T10:00:00Z',
				}],
				mine: {
					positive: 'Gute Stimmung\nGutes Team',
					improvements: 'Mehr Zeit\nAblauf klarer kommunizieren',
					action_items: 'Material früher bereitstellen',
					notes: '',
				},
			})), { status: 200 }));

		const wrapper = mount(ActivityEvaluationPanel, {
			props: { activityId: 'activity-1', canEdit: true },
		});
		await flushPromises();
		await wrapper.get('#actual-participants').setValue('18');
		const improvementGroup = wrapper.findAll('fieldset')[1];
		await improvementGroup.find('input[type="text"]').setValue('Mehr Zeit');
		await improvementGroup.get('button.evaluation-add-button').trigger('click');
		const improvementInputs = improvementGroup.findAll('input[type="text"]');
		await improvementInputs[1].setValue('Ablauf klarer kommunizieren');
		await wrapper.get('form').trigger('submit');
		await flushPromises();

		expect(mocks.apiFetch).toHaveBeenLastCalledWith('/api/activities/activity-1/evaluation', expect.objectContaining({
			method: 'PUT',
			body: JSON.stringify({
				actual_participants: 18,
				positive: 'Gute Stimmung\nGutes Team',
				improvements: 'Mehr Zeit\nAblauf klarer kommunizieren',
				action_items: 'Material früher bereitstellen',
				notes: '',
			}),
		}));
		expect(wrapper.text()).toContain('Gespeichert');
	});

	it('lets editors add and remove point fields without removing the last input', async () => {
		const wrapper = mount(ActivityEvaluationPanel, {
			props: { activityId: 'activity-1', canEdit: true },
		});
		await flushPromises();

		const positiveGroup = wrapper.findAll('fieldset')[0];
		expect(positiveGroup.findAll('input[type="text"]')).toHaveLength(1);
		await positiveGroup.get('button.evaluation-add-button').trigger('click');
		expect(positiveGroup.findAll('input[type="text"]')).toHaveLength(2);
		await positiveGroup.get('button[aria-label*="Punkt 1"]').trigger('click');
		expect(positiveGroup.findAll('input[type="text"]')).toHaveLength(1);

		await positiveGroup.get('button[aria-label*="Punkt 1"]').trigger('click');
		expect(positiveGroup.findAll('input[type="text"]')).toHaveLength(1);
	});

	it('validates the combined text in each category before saving', async () => {
		const wrapper = mount(ActivityEvaluationPanel, {
			props: { activityId: 'activity-1', canEdit: true },
		});
		await flushPromises();

		const positiveGroup = wrapper.findAll('fieldset')[0];
		await positiveGroup.find('input[type="text"]').setValue('x'.repeat(2500));
		await positiveGroup.get('button.evaluation-add-button').trigger('click');
		await positiveGroup.findAll('input[type="text"]')[1].setValue('y'.repeat(1501));
		await wrapper.get('form').trigger('submit');
		await flushPromises();

		expect(mocks.apiFetch).toHaveBeenCalledTimes(1);
		expect(wrapper.text()).toContain('höchstens 4000 Zeichen pro Bereich');
	});

	it('rejects invalid participant counts before sending a write request', async () => {
		const wrapper = mount(ActivityEvaluationPanel, {
			props: { activityId: 'activity-1', canEdit: true },
		});
		await flushPromises();
		await wrapper.get('#actual-participants').setValue('-1');
		await wrapper.get('form').trigger('submit');
		await flushPromises();

		expect(mocks.apiFetch).toHaveBeenCalledTimes(1);
		expect(wrapper.text()).toContain('ganze, nicht negative Teilnehmendenzahl');
	});
});
