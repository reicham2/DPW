import { Packer } from 'docx';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { Activity } from '../types';
import { apiFetch } from './useApi';

vi.mock('./useUserResolver', () => ({
	useUserResolver: () => ({ resolveResponsibleName: (id: string) => id === 'user-1' ? 'Anna' : id }),
}));
vi.mock('./useApi', () => ({ apiFetch: vi.fn(), formatApiError: (error: Error) => error.message }));

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
		vi.unstubAllGlobals();
	});

	it('embeds authorized image bytes between description text instead of exporting literal HTML', async () => {
		const id = '12345678-1234-1234-1234-123456789abc';
		const bytes = new Uint8Array([137, 80, 78, 71]);
		vi.mocked(apiFetch).mockResolvedValue(new Response(new Blob([bytes], { type: 'image/png' })));
		vi.stubGlobal('Image', class {
			naturalWidth = 320;
			naturalHeight = 240;
			onload: (() => void) | null = null;
			set src(_value: string) { queueMicrotask(() => this.onload?.()); }
		});
		vi.spyOn(URL, 'createObjectURL').mockReturnValue('blob:export');
		vi.spyOn(URL, 'revokeObjectURL').mockImplementation(() => {});
		vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {});
		const pack = vi.spyOn(Packer, 'toBlob').mockResolvedValue(new Blob());
		const activity = makeActivity(null);
		activity.programs[0]!.description = `<p><b>Vorher</b><img src="/api/program-images/${id}" alt="Bild">Nachher</p>`;
		const exporter = useActivityExport();
		await exporter.exportToWord(activity);
		expect(exporter.error.value).toBeNull();
		expect(apiFetch).toHaveBeenCalledWith(`/api/program-images/${id}`);
		const document = pack.mock.calls[0]![0];
		const body = JSON.stringify(document.Document.View.prepForXml({
			file: document, viewWrapper: document.Document, stack: [],
		}));
		expect(body.indexOf('Vorher')).toBeLessThan(body.indexOf('w:drawing'));
		expect(body.indexOf('w:drawing')).toBeLessThan(body.indexOf('Nachher'));
		expect(body).not.toContain('<img');
		expect(body).toContain('1524000');
		expect(document.Media.Array).toHaveLength(1);
		expect(document.Media.Array[0]!.data).toEqual(bytes);
	});

	it('does not create an apparently successful export when an inline image cannot be read', async () => {
		vi.mocked(apiFetch).mockResolvedValue(new Response('', { status: 403 }));
		const pack = vi.spyOn(Packer, 'toBlob');
		const activity = makeActivity(null);
		activity.programs[0]!.description = '<img src="/api/program-images/12345678-1234-1234-1234-123456789abc">';
		const exporter = useActivityExport();
		await exporter.exportToWord(activity);
		expect(pack).not.toHaveBeenCalled();
		expect(exporter.error.value).toContain('Berechtigung');
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
