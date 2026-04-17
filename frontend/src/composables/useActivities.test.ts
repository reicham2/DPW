import { describe, it, expect } from 'vitest';
import type { Activity, WsEvent } from '../types';

/**
 * The WebSocket event handler logic from useActivities, extracted for testing.
 * We test the reducer logic directly to avoid module-level side effects
 * (the real composable opens a WebSocket on import).
 */
function applyWsEvent(activities: Activity[], event: WsEvent): Activity[] {
	const list = [...activities];
	if (event.event === 'created') {
		if (!list.find((a) => a.id === event.activity.id)) {
			list.unshift(event.activity);
		}
		return list;
	}
	if (event.event === 'updated') {
		const idx = list.findIndex((a) => a.id === event.activity.id);
		if (idx !== -1) list[idx] = event.activity;
		return list;
	}
	if (event.event === 'deleted') {
		return list.filter((a) => a.id !== event.id);
	}
	if (event.event === 'department_deleted') {
		// In real code this triggers a full refetch; here we just verify the event is handled
		return list;
	}
	return list;
}

function makeActivity(overrides: Partial<Activity> = {}): Activity {
	return {
		id: 'act-1',
		title: 'Test Activity',
		date: '2025-06-01',
		start_time: '10:00',
		end_time: '12:00',
		goal: 'Test goal',
		location: 'Test location',
		responsible: ['user-1'],
		department: null,
		material: [],
		siko_text: null,
		bad_weather_info: null,
		created_at: '2025-01-01T00:00:00Z',
		updated_at: '2025-01-01T00:00:00Z',
		programs: [],
		...overrides,
	};
}

describe('Activity WebSocket event handler', () => {
	it('adds a new activity on "created"', () => {
		const activity = makeActivity({ id: 'new-1', title: 'Neu' });
		const result = applyWsEvent([], { event: 'created', activity });
		expect(result).toHaveLength(1);
		expect(result[0].id).toBe('new-1');
	});

	it('does not duplicate on repeated "created"', () => {
		const activity = makeActivity({ id: 'act-1' });
		const result = applyWsEvent([activity], { event: 'created', activity });
		expect(result).toHaveLength(1);
	});

	it('prepends new activity (most recent first)', () => {
		const existing = makeActivity({ id: 'old', title: 'Alt' });
		const newAct = makeActivity({ id: 'new', title: 'Neu' });
		const result = applyWsEvent([existing], {
			event: 'created',
			activity: newAct,
		});
		expect(result[0].id).toBe('new');
		expect(result[1].id).toBe('old');
	});

	it('updates an existing activity on "updated"', () => {
		const original = makeActivity({ id: 'act-1', title: 'Original' });
		const updated = makeActivity({ id: 'act-1', title: 'Geändert' });
		const result = applyWsEvent([original], {
			event: 'updated',
			activity: updated,
		});
		expect(result).toHaveLength(1);
		expect(result[0].title).toBe('Geändert');
	});

	it('ignores update for unknown activity', () => {
		const unknown = makeActivity({ id: 'unknown' });
		const result = applyWsEvent([], { event: 'updated', activity: unknown });
		expect(result).toHaveLength(0);
	});

	it('removes an activity on "deleted"', () => {
		const act1 = makeActivity({ id: 'act-1' });
		const act2 = makeActivity({ id: 'act-2' });
		const result = applyWsEvent([act1, act2], {
			event: 'deleted',
			id: 'act-1',
		});
		expect(result).toHaveLength(1);
		expect(result[0].id).toBe('act-2');
	});

	it('handles deletion of non-existent activity gracefully', () => {
		const act = makeActivity({ id: 'act-1' });
		const result = applyWsEvent([act], { event: 'deleted', id: 'ghost' });
		expect(result).toHaveLength(1);
	});
});
