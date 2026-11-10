import { mount } from '@vue/test-utils';
import { afterEach, describe, expect, it } from 'vitest';
import LegalInfoPage from './LegalInfoPage.vue';

describe('LegalInfoPage', () => {
	afterEach(() => {
		document.title = '';
	});

	it('shows the confirmed operator and contact in the Impressum', () => {
		const wrapper = mount(LegalInfoPage, {
			props: { kind: 'impressum' },
			global: { stubs: ['router-link'] },
		});

		expect(wrapper.get('h1').text()).toBe('Impressum');
		expect(wrapper.text()).toContain('Pfadi Hünenberg (Pfadi Hü)');
		expect(wrapper.text()).toContain('Rigistrasse 1');
		expect(wrapper.text()).toContain('6331 Hünenberg');
		expect(wrapper.text()).toContain('Schweiz');
		expect(wrapper.text()).not.toContain('Pfadiheim Hünenberg');
		expect(wrapper.find('a[href="mailto:al@pfadihue.ch"]').exists()).toBe(true);
	});

	it('describes DPWeb-specific data handling and the confirmed hosting location', () => {
		const wrapper = mount(LegalInfoPage, {
			props: { kind: 'datenschutz' },
			global: { stubs: ['router-link'] },
		});

		expect(wrapper.get('h1').text()).toBe('Datenschutzerklärung');
		expect(wrapper.text()).toContain('IP-Adresse');
		expect(wrapper.text()).toContain('14 Tage');
		expect(wrapper.text()).toContain('LKlaus Hosting');
		expect(wrapper.text()).toContain('in der Schweiz');
		expect(wrapper.text()).toContain('90 Tagen');
		expect(wrapper.text()).toContain('gesendeter E-Mails');
		expect(wrapper.text()).toContain('In-App-Benachrichtigungen');
		expect(wrapper.text()).toContain('geo.admin.ch');
		expect(wrapper.text()).toContain('erweiterte Diagnosedaten');
		expect(wrapper.text()).toContain('Rigistrasse 1');
		expect(wrapper.text()).toContain('6331 Hünenberg');
		expect(wrapper.text()).toContain('Recht auf unentgeltliche Auskunft');
		expect(wrapper.text()).toContain('Recht auf Berichtigung');
		expect(wrapper.text()).toContain('Recht auf Löschung deiner Daten');
		expect(wrapper.text()).toContain('Recht auf Einschränkung');
		expect(wrapper.text()).toContain('Unsere eigenen Server und Datenbanken befinden sich ausschliesslich in der Schweiz.');
		expect(wrapper.text()).toContain('Standardvertragsklauseln (SCC)');
		expect(wrapper.text()).toContain('Swiss-U.S. Data Privacy Framework');
		expect(wrapper.text()).toContain('Rechte der betroffenen Personen');
		expect(wrapper.text()).toContain('Feldeggweg 1, 3003 Bern');
		expect(wrapper.text()).not.toContain('Organisationsentwurf');
		expect(wrapper.text()).not.toContain('Rechtsberatung');
	});
});
