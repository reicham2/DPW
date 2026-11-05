import { flushPromises, shallowMount } from '@vue/test-utils';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { ActivityInput } from '../types';

const mocks = vi.hoisted(() => ({
	createActivity: vi.fn(),
	push: vi.fn(),
	replace: vi.fn(),
}));

vi.mock('vue-router', () => ({
	useRouter: () => ({ push: mocks.push, replace: mocks.replace }),
}));

vi.mock('../composables/useActivities', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	const error = ref<string | null>(null);
	return { useActivities: () => ({ createActivity: mocks.createActivity, error }) };
});

vi.mock('../composables/useSetupAuthConfig', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { activityGuidingThreadSupported: ref(true) };
});

vi.mock('../composables/usePermissions', () => ({
	usePermissions: () => ({
		fetchDepartments: vi.fn().mockResolvedValue(undefined),
		fetchMyPermissions: vi.fn().mockResolvedValue(undefined),
		writableDepts: () => ['Pfadi'],
	}),
}));

vi.mock('../composables/useAuth', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { user: ref({ id: 'user-1', department: 'Pfadi' }) };
});

import CreatePage from './CreatePage.vue';
import { useActivities } from '../composables/useActivities';
import { activityGuidingThreadSupported } from '../composables/useSetupAuthConfig';

describe('CreatePage guiding thread', () => {
	beforeEach(() => {
		mocks.createActivity.mockReset().mockImplementation(async (input: ActivityInput) => ({
			id: 'activity-1', guiding_thread: input.guiding_thread,
		}));
		mocks.push.mockReset();
		mocks.replace.mockReset();
		activityGuidingThreadSupported.value = true;
		useActivities().error.value = null;
	});

	it.each([
		{ input: '  Eine Geschichte\nmit zwei Zeilen  ', expected: 'Eine Geschichte\nmit zwei Zeilen' },
		{ input: '  ', expected: null },
	])('creates an activity with guiding thread $expected', async ({ input, expected }) => {
		const wrapper = shallowMount(CreatePage);
		await flushPromises();
		await wrapper.get('#title').setValue('Aktivität');
		await wrapper.get('#date').setValue('2026-07-01');
		await wrapper.get('#start_time').setValue('10:00');
		await wrapper.get('#end_time').setValue('11:00');
		await wrapper.get('#guiding-thread').setValue(input);
		await wrapper.get('form').trigger('submit');
		await flushPromises();

		expect(mocks.createActivity).toHaveBeenCalledWith(expect.objectContaining({
			title: 'Aktivität',
			guiding_thread: expected,
			tn_material: [],
		}));
		expect(mocks.push).toHaveBeenCalledWith('/activities/activity-1');
		wrapper.unmount();
	});

	it('disables the story field for older backends without preventing normal creation', async () => {
		activityGuidingThreadSupported.value = false;
		const wrapper = shallowMount(CreatePage);
		await flushPromises();
		expect((wrapper.get('#guiding-thread').element as HTMLTextAreaElement).disabled).toBe(true);
		expect(wrapper.text()).toContain('nach dem Backend-Update verfügbar');
		await wrapper.get('form').trigger('submit');
		await flushPromises();
		expect(mocks.createActivity).toHaveBeenCalledWith(expect.objectContaining({ guiding_thread: null }));
		expect(mocks.push).toHaveBeenCalledWith('/activities/activity-1');
		wrapper.unmount();
	});

	it('keeps entered text and prevents creation if support disappears before submission', async () => {
		const wrapper = shallowMount(CreatePage);
		await flushPromises();
		await wrapper.get('#guiding-thread').setValue('Noch nicht gespeichert');
		activityGuidingThreadSupported.value = false;
		await wrapper.get('form').trigger('submit');
		await flushPromises();
		expect(mocks.createActivity).not.toHaveBeenCalled();
		expect(useActivities().error.value).toContain('Bitte den Text bis dahin kopieren');
		const field = wrapper.get('#guiding-thread').element as HTMLTextAreaElement;
		expect(field.value).toBe('Noch nicht gespeichert');
		expect(field.readOnly).toBe(true);
		expect(field.disabled).toBe(false);
		wrapper.unmount();
	});

	it('keeps the story visible and prevents duplicate creation if a backend ignores it', async () => {
		mocks.createActivity.mockResolvedValueOnce({ id: 'activity-1' });
		const wrapper = shallowMount(CreatePage);
		await flushPromises();
		await wrapper.get('#guiding-thread').setValue('Noch nicht gespeichert');
		await wrapper.get('form').trigger('submit');
		await flushPromises();
		expect(useActivities().error.value).toContain('Die Aktivität wurde erstellt, aber der rote Faden wurde nicht gespeichert');
		expect(mocks.push).not.toHaveBeenCalled();
		const field = wrapper.get('#guiding-thread').element as HTMLTextAreaElement;
		expect(field.value).toBe('Noch nicht gespeichert');
		expect(field.readOnly).toBe(true);
		await wrapper.get('form').trigger('submit');
		expect(mocks.createActivity).toHaveBeenCalledTimes(1);
		const goToActivity = wrapper.findAll('button').find((button) => button.text() === 'Zur erstellten Aktivität');
		expect(goToActivity).toBeDefined();
		await goToActivity!.trigger('click');
		expect(mocks.push).toHaveBeenCalledWith('/activities/activity-1');
		wrapper.unmount();
	});

	it('reports an invalid story in a successful response without creating a duplicate', async () => {
		mocks.createActivity.mockResolvedValueOnce({ id: 'activity-1', guiding_thread: 42 });
		const wrapper = shallowMount(CreatePage);
		await flushPromises();
		await wrapper.get('form').trigger('submit');
		await flushPromises();
		expect(useActivities().error.value).toContain('die Serverantwort zum roten Faden ist ungültig');
		expect(mocks.push).not.toHaveBeenCalled();
		await wrapper.get('form').trigger('submit');
		expect(mocks.createActivity).toHaveBeenCalledTimes(1);
		wrapper.unmount();
	});
});
