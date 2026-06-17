import { describe, it, expect } from 'vitest';
import { buildCampPrintHtml } from './campPrint';
import type { CampDetail } from '../types';

function makeCamp(): CampDetail {
	return {
		id: 'c', title: 'Test Lager', short_title: '', motto: 'Mein Motto', kind: 'Zeltlager',
		organizer: '', address_name: '', address_street: '', address_zipcode: '', address_city: '',
		coach_name: '', course_number: '', color: '#0080ff', department: null, created_by: null,
		is_prototype: false, created_at: '', updated_at: '',
		periods: [{ id: 'p1', camp_id: 'c', description: 'Hauptlager', start_date: '2026-08-01', end_date: '2026-08-02', position: 0, created_at: '', updated_at: '' }],
		categories: [{ id: 'lp', camp_id: 'c', short_name: 'LP', name: 'Lagerprogramm', color: '#0080ff', numbering_style: '1', position: 0, created_at: '', updated_at: '' }],
		collaborations: [{ id: 'co1', camp_id: 'c', user_id: null, display_name: 'Max', role: 'manager', camp_role: 'PR', abbreviation: 'MX', color: '#000', status: 'established', created_at: '', updated_at: '' }],
		activities: [{
			id: 'a1', camp_id: 'c', category_id: 'lp', title: 'Quidditch', location: 'Feld',
			schedule_entries: [{ id: 's1', activity_id: 'a1', period_id: 'p1', period_offset: 540, length: 90, left_fraction: 0, width_fraction: 1 }],
			responsible_collaboration_ids: ['co1'], content_nodes: [], created_at: '',
		}],
		material_lists: [],
		day_responsibles: [{ id: 'dr1', period_id: 'p1', day_offset: 0, collaboration_id: 'co1' }],
	};
}

describe('buildCampPrintHtml', () => {
	it('produces a full HTML document with camp title and motto', () => {
		const html = buildCampPrintHtml(makeCamp());
		expect(html).toContain('<!doctype html>');
		expect(html).toContain('Test Lager');
		expect(html).toContain('Mein Motto');
	});

	it('renders the activity with numbered category, time and responsible', () => {
		const html = buildCampPrintHtml(makeCamp());
		expect(html).toContain('Quidditch');
		expect(html).toContain('LP1');          // numbering applied
		expect(html).toContain('09:00–10:30');  // start–end
		expect(html).toContain('MX');           // responsible abbreviation
	});

	it('escapes HTML in user content', () => {
		const camp = makeCamp();
		camp.activities[0].title = '<script>x</script>';
		const html = buildCampPrintHtml(camp);
		expect(html).not.toContain('<script>x</script>');
		expect(html).toContain('&lt;script&gt;');
	});

	it('handles an empty camp gracefully', () => {
		const camp = makeCamp();
		camp.periods = [];
		camp.activities = [];
		const html = buildCampPrintHtml(camp);
		expect(html).toContain('Noch kein Programm erfasst.');
	});
});
