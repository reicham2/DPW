import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { User } from '../types';

vi.mock('../composables/useAuth', async () => {
	const { ref } = await vi.importActual<typeof import('vue')>('vue');
	return { user: ref<{ role: string } | null>(null) };
});

vi.mock('../composables/useApi', () => ({ apiFetch: vi.fn() }));

import MaintenanceBanner from './MaintenanceBanner.vue';
import { user } from '../composables/useAuth';
import {
	applyMaintenanceStatus,
	maintenanceActive,
	maintenanceMessage,
	maintenanceScheduledEnd,
	maintenanceScheduledStart,
} from '../composables/useMaintenance';

describe('MaintenanceBanner', () => {
	let wrapper: ReturnType<typeof mount>;

	beforeEach(() => {
		maintenanceActive.value = false;
		maintenanceMessage.value = '';
		maintenanceScheduledStart.value = '';
		maintenanceScheduledEnd.value = '';
		user.value = null;
	});

	afterEach(() => wrapper?.unmount());

	function signIn(role = 'admin') {
		const signedInUser: User = {
			id: 'admin-1', microsoft_oid: 'oid-1', email: 'admin@example.com',
			display_name: 'Admin', department: null, role,
			time_display_mode: 'minutes', avatar_color: '#123456',
			notify_material_assigned: true, notify_activity_assigned: true,
			notify_program_assigned: true, notify_mail_own_activity: true,
			notify_mail_department: true, notify_channel_websocket: true,
			notify_channel_email: false, created_at: '', updated_at: '',
		};
		user.value = signedInUser;
	}

	it('shows a persistent active notice on initial admin login without a schedule or message', () => {
		signIn();
		maintenanceActive.value = true;
		wrapper = mount(MaintenanceBanner);

		expect(wrapper.get('[role="status"]').text()).toContain('Wartungsmodus ist aktiv.');
		expect(wrapper.text()).toContain('Nur Admins können sich anmelden.');
		expect(wrapper.find('button').exists()).toBe(false);
		expect(wrapper.text()).not.toContain('Geplante Wartung');
	});

	it.each([null, 'Mitglied'])('does not show the active admin notice for %s', (role) => {
		if (role) signIn(role);
		maintenanceActive.value = true;
		wrapper = mount(MaintenanceBanner);
		expect(wrapper.find('[role="status"]').exists()).toBe(false);
	});

	it('tracks live activation, message updates, deactivation and reactivation', async () => {
		signIn();
		wrapper = mount(MaintenanceBanner);
		expect(wrapper.text()).toBe('');

		applyMaintenanceStatus({ active: true, message: 'Datenbank-Update' });
		await nextTick();
		expect(wrapper.get('[role="status"]').text()).toContain('Datenbank-Update');

		applyMaintenanceStatus({ message: 'Abschlussarbeiten' });
		await nextTick();
		expect(wrapper.text()).toContain('Abschlussarbeiten');

		applyMaintenanceStatus({ active: false, message: '' });
		await nextTick();
		expect(wrapper.find('[role="status"]').exists()).toBe(false);
		expect(wrapper.text()).toContain('Wartung beendet:');

		applyMaintenanceStatus({ active: true });
		await nextTick();
		expect(wrapper.find('[role="status"]').exists()).toBe(true);
		expect(wrapper.text()).not.toContain('Wartung beendet:');
	});

	it('prompts for reload when maintenance already active at mount ends', async () => {
		signIn();
		maintenanceActive.value = true;
		wrapper = mount(MaintenanceBanner);
		maintenanceActive.value = false;
		await nextTick();
		expect(wrapper.text()).toContain('Wartung beendet:');
	});

	it('preserves dismissible upcoming notices without dismissing active maintenance', async () => {
		signIn();
		applyMaintenanceStatus({
			active: false,
			scheduled_start: new Date(Date.now() + 60_000).toISOString(),
			scheduled_end: new Date(Date.now() + 120_000).toISOString(),
		});
		wrapper = mount(MaintenanceBanner);
		expect(wrapper.text()).toContain('Geplante Wartung:');
		await wrapper.get('button[aria-label="Schliessen"]').trigger('click');
		expect(wrapper.text()).toBe('');

		applyMaintenanceStatus({ active: true });
		await nextTick();
		expect(wrapper.get('[role="status"]').text()).toContain('Wartungsmodus ist aktiv.');
	});
});
