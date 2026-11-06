import { beforeEach, describe, expect, it, vi } from 'vitest';
import { apiFetch } from './useApi';
import { useActivities } from './useActivities';

vi.mock('./useWebSocket', () => ({ useWebSocket: () => ({ connected: true }) }));
vi.mock('./useApi', () => ({ apiFetch: vi.fn(), formatApiError: (error: Error) => error.message }));
vi.mock('./useAuth', () => ({ getGraphAccessToken: vi.fn() }));

describe('authorized program image uploads', () => {
	beforeEach(() => vi.mocked(apiFetch).mockReset());

	it('persists uploaded or clipboard File bytes through the authorized activity upload mechanism', async () => {
		const image = new File([new Uint8Array([0x89, 0x50, 0x4e, 0x47])], 'Bild.png', { type: 'image/png' });
		const stored = { id: 'image-id', activity_id: 'activity-id', filename: image.name, content_type: image.type, created_at: '' };
		vi.mocked(apiFetch).mockResolvedValue(new Response(JSON.stringify(stored), { status: 201 }));
		expect(await useActivities().uploadAttachment('activity-id', image, true)).toEqual(stored);
		expect(apiFetch).toHaveBeenCalledWith('/api/activities/activity-id/program-images', {
			method: 'POST',
			body: JSON.stringify({ filename: 'Bild.png', content_type: 'image/png', data: 'iVBORw==' }),
		});
	});

	it('reports rejected uploads and validates unsupported input before sending bytes', async () => {
		const activities = useActivities();
		const image = new File(['bytes'], 'Bild.png', { type: 'image/png' });
		vi.mocked(apiFetch).mockResolvedValue(new Response('Keine Berechtigung', { status: 403 }));
		expect(await activities.uploadAttachment('activity-id', image, true)).toBeNull();
		expect(activities.error.value).toContain('Keine Berechtigung');
		vi.mocked(apiFetch).mockClear();
		expect(await activities.uploadAttachment('activity-id', new File(['svg'], 'bad.svg', { type: 'image/svg+xml' }), true)).toBeNull();
		expect(activities.error.value).toContain('PNG');
		expect(apiFetch).not.toHaveBeenCalled();
	});
});
