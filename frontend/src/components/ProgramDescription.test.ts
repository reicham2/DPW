import { mount, flushPromises } from '@vue/test-utils';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { apiFetch } from '../composables/useApi';
import ProgramDescription from './ProgramDescription.vue';

vi.mock('../composables/useApi', () => ({ apiFetch: vi.fn() }));

describe('ProgramDescription', () => {
	afterEach(() => vi.restoreAllMocks());

	it('renders a persisted inline image on read and reload surfaces', async () => {
		vi.mocked(apiFetch).mockResolvedValue(new Response(new Blob(['image'], { type: 'image/png' })));
		vi.spyOn(URL, 'createObjectURL').mockReturnValue('blob:persisted-image');
		vi.spyOn(URL, 'revokeObjectURL').mockImplementation(() => {});
		const html = 'Vorher<img src="/api/program-images/12345678-1234-1234-1234-123456789abc" alt="Bild">Danach';
		const wrapper = mount(ProgramDescription, { props: { html } });
		await flushPromises();
		expect(wrapper.get('img').attributes('src')).toBe('blob:persisted-image');
		expect(wrapper.text()).toBe('VorherDanach');
		await wrapper.setProps({ html: `<b>${html}</b>` });
		await flushPromises();
		expect(wrapper.get('b img').attributes('src')).toBe('blob:persisted-image');
		wrapper.unmount();
		expect(URL.revokeObjectURL).toHaveBeenCalledWith('blob:persisted-image');
	});

	it('shows a visible read error rather than silently dropping an image', async () => {
		vi.mocked(apiFetch).mockResolvedValue(new Response('', { status: 403 }));
		const wrapper = mount(ProgramDescription, { props: { html: '<img src="/api/program-images/12345678-1234-1234-1234-123456789abc" alt="Bild">' } });
		await flushPromises();
		expect(wrapper.get('[role="alert"]').text()).toContain('Berechtigung');
		expect(wrapper.get('img').attributes('alt')).toBe('Bild');
		wrapper.unmount();
	});
});
