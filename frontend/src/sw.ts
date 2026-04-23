/// <reference lib="webworker" />
import { cleanupOutdatedCaches, precacheAndRoute } from 'workbox-precaching';

declare let self: ServiceWorkerGlobalScope & {
	__WB_MANIFEST: Array<unknown>;
};

cleanupOutdatedCaches();
precacheAndRoute(self.__WB_MANIFEST);

async function resolvePushPayload() {
	const sub = await self.registration.pushManager.getSubscription();
	if (!sub) return null;
	const json = sub.toJSON() as { endpoint?: string; keys?: { auth?: string } };
	if (!json.endpoint || !json.keys?.auth) return null;

	const res = await fetch('/api/push/payload', {
		method: 'POST',
		headers: { 'Content-Type': 'application/json' },
		body: JSON.stringify({ endpoint: json.endpoint, auth: json.keys.auth }),
	});
	if (res.status === 204 || !res.ok) return null;
	return (await res.json()) as { title?: string; body?: string; url?: string };
}

self.addEventListener('push', (event) => {
	let title = 'Neue Benachrichtigung';
	let body = 'Es gibt neue Meldungen in DPWeb.';
	let url = '/profile';

	if (event.data) {
		try {
			const payload = event.data.json() as {
				title?: string;
				body?: string;
				url?: string;
			};
			if (typeof payload.title === 'string' && payload.title.trim())
				title = payload.title;
			if (typeof payload.body === 'string' && payload.body.trim())
				body = payload.body;
			if (typeof payload.url === 'string' && payload.url.trim())
				url = payload.url;
		} catch {
			// No structured payload provided.
		}
	}

	event.waitUntil(
		(async () => {
			const dynamic = await resolvePushPayload().catch(() => null);
			if (dynamic?.title?.trim()) title = dynamic.title;
			if (dynamic?.body?.trim()) body = dynamic.body;
			if (dynamic?.url?.trim()) url = dynamic.url;

			await self.registration.showNotification(title, {
				body,
				icon: '/logo.svg',
				badge: '/logo.svg',
				data: { url },
			});
		})(),
	);
});

self.addEventListener('notificationclick', (event) => {
	event.notification.close();
	const targetUrl =
		(event.notification.data?.url as string | undefined) || '/profile';
	event.waitUntil(
		self.clients
			.matchAll({ type: 'window', includeUncontrolled: true })
			.then((clients) => {
				for (const client of clients) {
					if ('focus' in client) {
						client.navigate(targetUrl).catch(() => {});
						return client.focus();
					}
				}
				return self.clients.openWindow(targetUrl);
			}),
	);
});
