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
				positive: 'Gute Stimmung',
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
		expect(wrapper.find('form').exists()).toBe(false);
	});

	it('saves actual attendance and the current user feedback together', async () => {
		mocks.apiFetch
			.mockResolvedValueOnce(new Response(JSON.stringify(evaluationResponse()), { status: 200 }))
			.mockResolvedValueOnce(new Response(JSON.stringify(evaluationResponse({
				actual_participants: 18,
				feedback: [{
					author: 'Leitung',
					positive: 'Gute Stimmung',
					improvements: 'Mehr Zeit',
					action_items: '',
					notes: '',
					created_at: '2026-01-01T10:00:00Z',
					updated_at: '2026-01-01T10:00:00Z',
				}],
				mine: { positive: 'Gute Stimmung', improvements: 'Mehr Zeit', action_items: '', notes: '' },
			})), { status: 200 }));

		const wrapper = mount(ActivityEvaluationPanel, {
			props: { activityId: 'activity-1', canEdit: true },
		});
		await flushPromises();
		await wrapper.get('#actual-participants').setValue('18');
		const fields = wrapper.findAll('textarea');
		await fields[0].setValue('Gute Stimmung');
		await fields[1].setValue('Mehr Zeit');
		await wrapper.get('form').trigger('submit');
		await flushPromises();

		expect(mocks.apiFetch).toHaveBeenLastCalledWith('/api/activities/activity-1/evaluation', expect.objectContaining({
			method: 'PUT',
			body: JSON.stringify({
				actual_participants: 18,
				positive: 'Gute Stimmung',
				improvements: 'Mehr Zeit',
				action_items: '',
				notes: '',
			}),
		}));
		expect(wrapper.text()).toContain('Gespeichert');
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
