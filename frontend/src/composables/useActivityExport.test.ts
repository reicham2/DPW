import { Packer } from 'docx';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { Activity } from '../types';

vi.mock('./useUserResolver', () => ({
	useUserResolver: () => ({ resolveResponsibleName: (id: string) => id === 'user-1' ? 'Anna' : id }),
}));

import { useActivityExport } from './useActivityExport';

function makeActivity(guidingThread: string | null): Activity {
	return {
		id: 'activity-1',
		title: 'Aktivität',
		date: '2026-07-01',
		start_time: '10:00',
		end_time: '11:00',
		goal: '',
		guiding_thread: guidingThread,
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
		programs: [{
			id: 'program-1',
			activity_id: 'activity-1',
			duration_minutes: 30,
			title: 'Programm',
			description: '',
			responsible: ['user-1'],
		}],
	};
}

describe('Word activity export', () => {
	afterEach(() => {
		vi.restoreAllMocks();
	});

	it.each([
		{ story: 'Erste Zeile\nZweite Zeile', included: true },
		{ story: null, included: false },
	])('exports guiding thread when present: $included', async ({ story, included }) => {
		const pack = vi.spyOn(Packer, 'toBlob').mockResolvedValue(new Blob());
		vi.spyOn(URL, 'createObjectURL').mockReturnValue('blob:test');
		vi.spyOn(URL, 'revokeObjectURL').mockImplementation(() => {});
		vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {});

		await useActivityExport().exportToWord(makeActivity(story));

		const document = pack.mock.calls[0][0];
		const body = JSON.stringify(document.Document.View.prepForXml({
			file: document,
			viewWrapper: document.Document,
			stack: [],
		}));
		expect(body).toContain('Anna');
		if (included) {
			expect(body).toContain('Roter Faden');
			expect(body).toContain('Erste Zeile');
			expect(body).toContain('Zweite Zeile');
			expect(body).toContain('"w:br"');
		} else {
			expect(body).not.toContain('Roter Faden');
		}
	});
});
