import assert from 'node:assert/strict';
import { test } from 'node:test';

const baseUrl = (process.env.DPW_API_BASE_URL || 'http://127.0.0.1:8000/api').replace(/\/+$/, '');
const adminToken = 'debug:a0000000-0000-0000-0000-000000000001';
const deniedToken = 'debug:f0000000-0000-0000-0000-000000000001';
const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aRZkAAAAASUVORK5CYII=', 'base64');
const expiredToken = 'e'.repeat(64);

async function response(method, path, { token = adminToken, guestToken, body } = {}) {
	const headers = {};
	if (token) headers.Authorization = `Bearer ${token}`;
	if (guestToken) headers['X-Guest-Edit-Token'] = guestToken;
	if (body !== undefined) headers['Content-Type'] = 'application/json';
	const res = await fetch(`${baseUrl}${path}`, {
		method,
		headers,
		body: body === undefined ? undefined : JSON.stringify(body),
		signal: AbortSignal.timeout(20000),
	});
	const text = await res.text();
	let json = null;
	try {
		json = text ? JSON.parse(text) : null;
	} catch {
		// File-download endpoints return arbitrary bytes rather than JSON.
	}
	return {
		status: res.status,
		text,
		json,
		headers: res.headers,
		bytes: Buffer.from(text),
	};
}

function expectStatus(result, status) {
	assert.equal(result.status, status, result.text);
	return result.json;
}

async function purgeActivity(id) {
	expectStatus(await response('DELETE', `/activities/${id}`), 204);
	expectStatus(await response('DELETE', `/admin/activities/${id}/trash`), 200);
}

test('guest edit links are narrowly scoped, expiring, revocable and independent from read-only shares',
	{ timeout: 120000 }, async () => {
		const activityIds = [];
		let activityId = '';
		let token = '';
		let otherImageId = '';
		let guestEditMaxDaysChanged = false;
		try {
			expectStatus(await response('GET', '/guest-edit/activity', {
				token: null, guestToken: expiredToken,
			}), 404);
			const initial = expectStatus(await response('POST', '/activities', {
				body: {
					title: 'Guest edit regression',
					date: '2026-07-15',
					start_time: '14:00',
					end_time: '15:00',
					goal: 'Original goal',
					guiding_thread: 'Original red thread',
					location: 'Wald',
					department: 'Pfadi',
					responsible: ['a0000000-0000-0000-0000-000000000002'],
					material: [{ name: 'Rope', responsible: ['a0000000-0000-0000-0000-000000000003'] }],
					tn_material: [],
					siko_text: 'Original safety notes',
					bad_weather_info: 'Original weather notes',
					programs: [{
						title: 'Opening',
						duration_minutes: 10,
						description: '<p>Original <strong>rich text</strong></p>',
						responsible: ['a0000000-0000-0000-0000-000000000002'],
					}],
				},
			}), 201);
			activityId = initial.id;
			activityIds.push(activityId);

			expectStatus(await response('POST', `/activities/${activityId}/guest-edit-link`, {
				token: deniedToken, body: {},
			}), 403);
			for (const expiresInDays of [0, -1, 31, '7']) {
				expectStatus(await response('POST', `/activities/${activityId}/guest-edit-link`, {
					body: { expires_in_days: expiresInDays },
				}), 400);
			}

			const link = expectStatus(await response('POST', `/activities/${activityId}/guest-edit-link`, { body: {} }), 201);
			const url = new URL(link.share_url);
			assert.equal(url.pathname, '/guest-edit');
			assert.equal(url.search, '');
			assert.match(url.hash, /^#[a-f0-9]{64}$/);
			token = url.hash.slice(1);
			const defaultExpiry = new Date(link.expires_at).getTime();
			assert.ok(defaultExpiry > Date.now() + 6 * 24 * 60 * 60 * 1000);
			assert.ok(defaultExpiry < Date.now() + 8 * 24 * 60 * 60 * 1000);

			const metadata = expectStatus(await response('GET', `/activities/${activityId}/guest-edit-link`), 200);
			assert.equal(metadata.expires_at, link.expires_at);
			assert.equal(metadata.max_expires_in_days, 30);
			assert.equal('token' in metadata, false);
			assert.equal('share_token' in metadata, false);
			expectStatus(await response('GET', '/guest-edit/activity', { token: null }), 404);
			expectStatus(await response('GET', '/guest-edit/activity', { token: null, guestToken: 'a'.repeat(64) }), 404);
			expectStatus(await response('GET', '/guest-edit/people', {
				token: null, guestToken: 'a'.repeat(64),
			}), 404);
			const guestRead = expectStatus(await response('GET', '/guest-edit/activity', { token: null, guestToken: token }), 200);
			const original = guestRead.activity;
			assert.equal(original.id, activityId);
			assert.equal(guestRead.expires_at, link.expires_at);
			const guestPeople = expectStatus(await response('GET', '/guest-edit/people', {
				token: null, guestToken: token,
			}), 200);
			assert.deepEqual(new Set(guestPeople.people.map((person) => person.id)), new Set([
				'a0000000-0000-0000-0000-000000000002',
				'a0000000-0000-0000-0000-000000000003',
			]));
			assert.equal(guestPeople.people.find((person) => person.id === 'a0000000-0000-0000-0000-000000000002').display_name, 'Stufen Leiter');
			assert.equal(guestPeople.people.find((person) => person.id === 'a0000000-0000-0000-0000-000000000003').display_name, 'Leiter Eins');
			for (const person of guestPeople.people) {
				assert.deepEqual(Object.keys(person).sort(), ['display_name', 'id']);
			}

			const otherActivity = expectStatus(await response('POST', '/activities', {
				body: {
					title: 'Other guest scope',
					date: '2026-07-16',
					start_time: '14:00',
					end_time: '15:00',
					goal: '',
					location: 'Wiese',
					department: 'Pfadi',
					responsible: [],
					material: [],
					tn_material: [],
					programs: [],
				},
			}), 201);
			activityIds.push(otherActivity.id);
			const otherImage = expectStatus(await response('POST', `/activities/${otherActivity.id}/program-images`, {
				body: { filename: 'other.png', content_type: 'image/png', data: png.toString('base64') },
			}), 201);
			otherImageId = otherImage.id;
			const otherAttachment = expectStatus(await response('POST', `/activities/${otherActivity.id}/attachments`, {
				body: {
					filename: 'other.txt',
					content_type: 'text/plain',
					data: Buffer.from('other activity attachment').toString('base64'),
				},
			}), 201);
			expectStatus(await response('GET', `/guest-edit/attachments/${otherAttachment.id}/download`, {
				token: null, guestToken: token,
			}), 404);
			for (const path of ['/activities', '/users', '/admin/roles']) {
				const denied = await response('GET', path, { token: null, guestToken: token });
				assert.ok(denied.status >= 400 && denied.status < 500, `${path} unexpectedly allowed guest access`);
			}

			const forgedInput = {
				...original,
				title: 'Must remain original',
				date: '2027-01-01',
				start_time: '09:00',
				end_time: '10:00',
				location: 'Must remain original',
				department: 'Allgemein',
				responsible: ['forged-user-id'],
				planned_participants_estimate: 999,
				guiding_thread: 'Edited red thread',
				goal: 'Edited goal',
				siko_text: 'Edited safety notes',
				bad_weather_info: 'Edited weather notes',
				material: [{ name: 'Rope', responsible: ['name'] }],
				tn_material: ['Water bottle'],
				programs: [{ ...original.programs[0], title: 'Edited program', description: '<p>Edited <em>rich text</em></p>' }],
			};
			const saved = expectStatus(await response('PATCH', '/guest-edit/activity', {
				token: null, guestToken: token, body: { ...forgedInput, updated_at: original.updated_at },
			}), 200);
			assert.equal(saved.title, original.title);
			assert.equal(saved.date, original.date);
			assert.equal(saved.start_time, original.start_time);
			assert.equal(saved.end_time, original.end_time);
			assert.equal(saved.location, original.location);
			assert.equal(saved.department, original.department);
			assert.deepEqual(saved.responsible, original.responsible);
			assert.equal(saved.planned_participants_estimate, original.planned_participants_estimate);
			assert.equal(saved.guiding_thread, 'Edited red thread');
			assert.equal(saved.goal, 'Edited goal');
			assert.equal(saved.siko_text, 'Edited safety notes');
			assert.equal(saved.bad_weather_info, 'Edited weather notes');
			assert.deepEqual(saved.material, [{ name: 'Rope', responsible: ['name'] }]);
			assert.deepEqual(saved.tn_material, ['Water bottle']);
			assert.equal(saved.programs[0].description, '<p>Edited <em>rich text</em></p>');

			const stale = { ...forgedInput, updated_at: original.updated_at, goal: 'stale overwrite' };
			expectStatus(await response('PATCH', '/guest-edit/activity', {
				token: null, guestToken: token, body: stale,
			}), 409);

			const upload = expectStatus(await response('POST', '/guest-edit/program-images', {
				token: null,
				guestToken: token,
				body: { filename: 'guest.png', content_type: 'image/png', data: png.toString('base64') },
			}), 201);
			const withImage = {
				...saved,
				updated_at: saved.updated_at,
				programs: [{
					...saved.programs[0],
					description: `<p>Text<img src="/api/program-images/${upload.id}" alt="Guest image"></p>`,
				}],
			};
			const imageSaved = expectStatus(await response('PATCH', '/guest-edit/activity', {
				token: null, guestToken: token, body: withImage,
			}), 200);
			assert.match(imageSaved.programs[0].description, new RegExp(upload.id));
			expectStatus(await response('GET', `/guest-edit/program-images/${otherImageId}`, {
				token: null, guestToken: token,
			}), 404);
			expectStatus(await response('PATCH', '/guest-edit/activity', {
				token: null,
				guestToken: token,
				body: {
					...imageSaved,
					programs: [{
						...imageSaved.programs[0],
						description: `<img src="/api/program-images/${otherImageId}">`,
					}],
				},
			}), 403);
			const concurrentBase = imageSaved;
			const concurrentResults = await Promise.all([
				response('PATCH', '/guest-edit/activity', {
					token: null, guestToken: token,
					body: { ...concurrentBase, goal: 'Concurrent edit A' },
				}),
				response('PATCH', '/guest-edit/activity', {
					token: null, guestToken: token,
					body: { ...concurrentBase, goal: 'Concurrent edit B' },
				}),
			]);
			assert.deepEqual(concurrentResults.map(result => result.status).sort(), [200, 409]);
			const imageResponse = await response('GET', `/guest-edit/program-images/${upload.id}`, {
				token: null, guestToken: token,
			});
			assert.equal(imageResponse.status, 200, imageResponse.text);
			assert.match(imageResponse.headers.get('cache-control') || '', /no-store/);
			assert.deepEqual(Buffer.from(await fetch(`${baseUrl}/guest-edit/program-images/${upload.id}`, {
				headers: { 'X-Guest-Edit-Token': token },
			}).then((res) => res.arrayBuffer())), png);

			const attachment = expectStatus(await response('POST', '/guest-edit/attachments', {
				token: null,
				guestToken: token,
				body: { filename: 'guest.txt', content_type: 'text/plain', data: Buffer.from('guest attachment').toString('base64') },
			}), 201);
			const download = await response('GET', `/guest-edit/attachments/${attachment.id}/download`, {
				token: null, guestToken: token,
			});
			assert.equal(download.status, 200, download.text);
			assert.equal(download.text, 'guest attachment');
			expectStatus(await response('DELETE', `/guest-edit/attachments/${attachment.id}`, {
				token: null, guestToken: token,
			}), 200);
			expectStatus(await response('GET', `/guest-edit/attachments/${attachment.id}/download`, {
				token: null, guestToken: token,
			}), 404);

			for (const path of [
				`/activities/${activityId}/form`,
				`/activities/${activityId}/form-draft`,
				`/activities/${activityId}/event-publication`,
			]) {
				const denied = await response('GET', path, { token: null, guestToken: token });
				assert.ok(denied.status >= 400 && denied.status < 500, `${path} unexpectedly allowed guest access`);
				for (const method of ['POST', 'PUT', 'DELETE']) {
					const writeDenied = await response(method, path, {
						token: null, guestToken: token, body: { title: 'No guest action' },
					});
					assert.ok(writeDenied.status >= 400 && writeDenied.status < 500,
						`${method} ${path} unexpectedly allowed guest access`);
				}
			}

			const publicShare = expectStatus(await response('POST', `/activities/${activityId}/share`), 201);
			const shared = expectStatus(await response('GET', `/shared/${publicShare.share_token}`, { token: null }), 200);
			assert.equal(shared.title, original.title);
			expectStatus(await response('GET', '/guest-edit/activity', {
				token: null, guestToken: publicShare.share_token,
			}), 404);

			const maximum = expectStatus(await response('POST', `/activities/${activityId}/guest-edit-link`, {
				body: { expires_in_days: 30 },
			}), 201);
			assert.equal(maximum.max_expires_in_days, 30);
			assert.ok(new Date(maximum.expires_at).getTime() > Date.now() + 29 * 24 * 60 * 60 * 1000);
			expectStatus(await response('GET', '/guest-edit/activity', {
				token: null, guestToken: token,
			}), 404);
			expectStatus(await response('GET', '/guest-edit/people', {
				token: null, guestToken: token,
			}), 404);
			token = new URL(maximum.share_url).hash.slice(1);

			const settings = expectStatus(await response('GET', '/admin/app-settings'), 200);
			const maxDaysSetting = settings.find(setting => setting.key === 'guest_edit.max_days');
			assert.ok(maxDaysSetting, 'guest edit maximum must be manageable in admin settings');
			assert.equal(maxDaysSetting.locked_by_env, false, 'integration environment must not lock this setting with ENV');
			for (const value of ['0', '31', '2days', '1.5']) {
				expectStatus(await response('PUT', '/admin/app-settings/guest_edit.max_days', { body: { value } }), 400);
			}
			expectStatus(await response('PUT', '/admin/app-settings/guest_edit.max_days', {
				body: { value: '2' },
			}), 200);
			guestEditMaxDaysChanged = true;
			const currentMaximumMetadata = expectStatus(
				await response('GET', `/activities/${activityId}/guest-edit-link`), 200,
			);
			assert.equal(currentMaximumMetadata.max_expires_in_days, 2);
			assert.equal(currentMaximumMetadata.expires_at, maximum.expires_at,
				'changing the configured maximum must not alter an already-issued expiry');
			expectStatus(await response('POST', `/activities/${activityId}/guest-edit-link`, {
				body: { expires_in_days: 3 },
			}), 400);
			const configuredMaximum = expectStatus(await response('POST', `/activities/${activityId}/guest-edit-link`, {
				body: {},
			}), 201);
			assert.equal(configuredMaximum.expires_in_days, 2,
				'the default link duration must be capped by the configured maximum');
			token = new URL(configuredMaximum.share_url).hash.slice(1);

			expectStatus(await response('PUT', '/admin/app-settings/guest_edit.max_days', {
				body: { value: '1' },
			}), 200);
			const loweredMaximumMetadata = expectStatus(
				await response('GET', `/activities/${activityId}/guest-edit-link`), 200,
			);
			assert.equal(loweredMaximumMetadata.max_expires_in_days, 1);
			assert.equal(loweredMaximumMetadata.expires_at, configuredMaximum.expires_at,
				'lowering the maximum must preserve the expiry already issued to this guest');
			const existingLinkStillWorks = expectStatus(await response('GET', '/guest-edit/activity', {
				token: null, guestToken: token,
			}), 200);
			assert.equal(existingLinkStillWorks.expires_at, configuredMaximum.expires_at);
			expectStatus(await response('POST', `/activities/${activityId}/guest-edit-link`, {
				body: { expires_in_days: 2 },
			}), 400);

			const minimum = expectStatus(await response('POST', `/activities/${activityId}/guest-edit-link`, {
				body: { expires_in_days: 1 },
			}), 201);
			assert.ok(new Date(minimum.expires_at).getTime() > Date.now() + 23 * 60 * 60 * 1000);
			assert.ok(new Date(minimum.expires_at).getTime() < Date.now() + 25 * 60 * 60 * 1000);
			expectStatus(await response('GET', '/guest-edit/activity', {
				token: null, guestToken: token,
			}), 404);
			token = new URL(minimum.share_url).hash.slice(1);
			expectStatus(await response('DELETE', `/activities/${activityId}/guest-edit-link`), 200);
			expectStatus(await response('GET', '/guest-edit/activity', {
				token: null, guestToken: token,
			}), 404);
			expectStatus(await response('GET', `/shared/${publicShare.share_token}`, { token: null }), 200);
		} finally {
			if (guestEditMaxDaysChanged) {
				expectStatus(await response('PUT', '/admin/app-settings/guest_edit.max_days', {
					body: { value: null },
				}), 200);
			}
			for (const id of activityIds) await purgeActivity(id);
		}
	});
