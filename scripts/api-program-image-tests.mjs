import assert from 'node:assert/strict';
import { test } from 'node:test';

const baseUrl = (process.env.DPW_API_BASE_URL || 'http://127.0.0.1:8000/api').replace(/\/+$/, '');
const admin = 'a0000000-0000-0000-0000-000000000001';
const writer = 'f0000000-0000-0000-0000-000000000002';
const denied = 'f0000000-0000-0000-0000-000000000001';
const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aRZkAAAAASUVORK5CYII=', 'base64');
const maximum = 10 * 1024 * 1024;

async function response(method, path, user = admin, body, expected = 200) {
	const res = await fetch(`${baseUrl}${path}`, {
		method,
		headers: {
			...(user ? { Authorization: `Bearer debug:${user}` } : {}),
			...(body === undefined ? {} : { 'Content-Type': 'application/json' }),
		},
		body: body === undefined ? undefined : JSON.stringify(body),
		signal: AbortSignal.timeout(20000),
	});
	assert.equal(res.status, expected, `${method} ${path}: ${res.status === expected ? '' : await res.text()}`);
	return res;
}

async function request(method, path, user = admin, body, expected = 200) {
	const res = await response(method, path, user, body, expected);
	if (res.status === 204) return null;
	return res.json();
}

async function imageBytes(id, user = admin, sharedToken) {
	const res = await response('GET', sharedToken ? `/shared/${sharedToken}/program-images/${id}` : `/program-images/${id}`, user);
	assert.equal(res.headers.get('content-type'), 'image/png');
	assert.match(res.headers.get('x-content-type-options'), /^nosniff(?:,\s*nosniff)*$/);
	assert.match(res.headers.get('cache-control'), /no-store/);
	assert.deepEqual(Buffer.from(await res.arrayBuffer()), png);
}

function imageId(description) {
	return /\/api\/program-images\/([0-9a-f-]{36})/.exec(description)?.[1];
}

test('program images persist inline with scoped access, validation, sharing, ordering and independent copies',
	{ timeout: 120000 }, async () => {
		const activities = new Set();
		const ideas = new Set();
		const body = {
			title: 'Inline image regression', date: '2026-07-15', start_time: '14:00', end_time: '15:00',
			department: 'Pfadi', responsible: [admin], goal: '', location: 'Wald', material: [],
			tn_material: [], programs: [{ title: 'Legacy', duration_minutes: 10, description: 'Legacy plain text', responsible: [] }],
		};
		const upload = { filename: 'Bild.png', content_type: 'image/png', data: png.toString('base64') };
		async function create(input = body, user = admin) {
			const activity = await request('POST', '/activities', user, input, 201);
			activities.add(activity.id);
			return activity;
		}
		async function purge(id) {
			await response('DELETE', `/activities/${id}`, admin, undefined, 204);
			await response('DELETE', `/admin/activities/${id}/trash`);
			activities.delete(id);
		}
		try {
			const source = await create();
			const own = await create(body, writer);
			await response('POST', `/activities/${source.id}/program-images`, null, upload, 401);
			await response('POST', `/activities/${source.id}/program-images`, denied, upload, 403);
			await response('POST', '/activities/00000000-0000-0000-0000-000000000000/program-images', admin, upload, 404);
			const stored = await request('POST', `/activities/${source.id}/program-images`, admin, upload, 201);
			const unused = await request('POST', `/activities/${source.id}/program-images`, admin, upload, 201);
			assert.deepEqual(await request('GET', `/activities/${source.id}/attachments`), [], 'Inline pictures are not a gallery or attachments list');
			await imageBytes(stored.id);
			await response('GET', `/program-images/${stored.id}`, null, undefined, 401);
			await response('GET', `/program-images/${stored.id}`, writer, undefined, 403);
			await response('GET', '/program-images/not-a-uuid', admin, undefined, 400);
			for (const invalid of [
				{ ...upload, filename: '' }, { ...upload, filename: 12 },
				{ ...upload, content_type: 'image/svg+xml' },
				{ ...upload, content_type: 'image/jpeg' },
				{ ...upload, data: '!!!!' }, { ...upload, data: '' },
				{ ...upload, data: Buffer.from('<html>not an image</html>').toString('base64') },
			]) {
				await response('POST', `/activities/${source.id}/program-images`, admin, invalid, 400);
			}
			const exact = Buffer.alloc(maximum);
			png.copy(exact);
			const limit = await request('POST', `/activities/${source.id}/program-images`, admin,
				{ ...upload, data: exact.toString('base64') }, 201);
			await response('DELETE', `/program-images/${limit.id}`, admin, undefined, 204);
			await response('POST', `/activities/${source.id}/program-images`, admin,
				{ ...upload, data: Buffer.concat([exact, Buffer.from([0])]).toString('base64') }, 400);
			await response('POST', `/activities/${source.id}/program-images`, admin,
				{ ...upload, data: 'A'.repeat(4 * Math.ceil(maximum / 3) + 4097) }, 413);

			const description = `<p>Before<img src="/api/program-images/${stored.id}" alt="Bild">Between<img src="/api/program-images/${stored.id}" alt="Bild">After</p>`;
			const withImages = { ...body, programs: [
				{ ...body.programs[0], title: 'Images', description },
				{ title: 'Second', duration_minutes: 20, description: 'Still plain text', responsible: [] },
			] };
			const saved = await request('PATCH', `/activities/${source.id}`, admin, withImages);
			assert.equal(saved.programs[0].description, description);
			assert.equal((await request('GET', `/activities/${source.id}`)).programs[0].description, description);
			await response('DELETE', `/program-images/${stored.id}`, admin, undefined, 409);
			await response('DELETE', `/program-images/${unused.id}`, denied, undefined, 403);
			await response('POST', '/activities', writer, withImages, 403);
			await response('PATCH', `/activities/${own.id}`, writer, withImages, 403);
			await response('POST', '/ideaBox', writer, { title: 'Denied copy', duration_minutes: 10, description, department: 'Pfadi' }, 403);
			await response('POST', '/activities', admin, {
				...body, programs: [{ ...body.programs[0], description: '<img src="/api/program-images/00000000-0000-0000-0000-000000000000">' }],
			}, 400);

			const share = await request('POST', `/activities/${source.id}/share`, admin, undefined, 201);
			await imageBytes(stored.id, null, share.share_token);
			await response('GET', `/shared/${share.share_token}/program-images/${unused.id}`, null, undefined, 404);
			await response('GET', `/shared/not-a-share-token/program-images/${stored.id}`, null, undefined, 404);
			const reordered = await request('PATCH', `/activities/${source.id}`, admin, { ...withImages, programs: [...withImages.programs].reverse() });
			assert.deepEqual(reordered.programs.map(program => program.title), ['Second', 'Images']);
			assert.equal(reordered.programs[1].description, description);
			await imageBytes(stored.id, null, share.share_token);

			const copied = await create(withImages);
			const copiedId = imageId(copied.programs[0].description);
			assert.notEqual(copiedId, stored.id);
			assert.equal(copied.programs[0].description, description.replaceAll(stored.id, copiedId));
			await imageBytes(copiedId);
			await response('GET', `/shared/${share.share_token}/program-images/${copiedId}`, null, undefined, 404);

			const idea = await request('POST', '/ideaBox', admin, {
				title: 'Image idea', duration_minutes: 10, description, department: 'Pfadi',
			}, 201);
			ideas.add(idea.id);
			const ideaImageId = imageId(idea.description);
			assert.notEqual(ideaImageId, stored.id);
			await imageBytes(ideaImageId);
			await response('GET', `/program-images/${ideaImageId}`, writer, undefined, 403);
			const editedIdea = await request('PUT', `/ideaBox/${idea.id}`, admin, {
				title: idea.title, duration_minutes: 15, department: idea.department, description: idea.description,
			});
			assert.equal(imageId(editedIdea.description), ideaImageId);
			await purge(source.id);
			await response('GET', `/program-images/${stored.id}`, admin, undefined, 404);
			await imageBytes(copiedId);
			await imageBytes(ideaImageId);

			const fromIdea = await create({ ...body, programs: [{ ...body.programs[0], description: editedIdea.description }] });
			const importedId = imageId(fromIdea.programs[0].description);
			assert.notEqual(importedId, ideaImageId);
			await response('DELETE', `/ideaBox/${idea.id}`);
			ideas.delete(idea.id);
			await response('GET', `/program-images/${ideaImageId}`, admin, undefined, 404);
			await imageBytes(importedId);
		} finally {
			for (const id of ideas) await response('DELETE', `/ideaBox/${id}`);
			for (const id of [...activities]) await purge(id);
		}
	});
