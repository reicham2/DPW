import { router } from '../router';
import { user } from './useAuth';
import { getWsDebugState } from './useWebSocket';

// ─── Ring buffers ────────────────────────────────────────────────────────────

interface ConsoleEntry {
	level: 'error' | 'warn';
	message: string;
	stack?: string;
	timestamp: string;
}

interface NetworkEntry {
	method: string;
	url: string;
	status: number;
	duration: number;
	timestamp: string;
}

interface Breadcrumb {
	type: 'click' | 'navigation' | 'input' | 'visibility' | 'custom';
	message: string;
	timestamp: string;
}

const MAX_ENTRIES = 30;
const consoleLog: ConsoleEntry[] = [];
const networkLog: NetworkEntry[] = [];
const jsErrors: string[] = [];
const breadcrumbs: Breadcrumb[] = [];
const resourceErrors: string[] = [];

const sessionStart = new Date().toISOString();

// ─── Intercept console.error / console.warn ──────────────────────────────────

const _origError = console.error;
const _origWarn = console.warn;

console.error = (...args: unknown[]) => {
	const entry: ConsoleEntry = {
		level: 'error',
		message: args.map(String).join(' ').slice(0, 500),
		timestamp: new Date().toISOString(),
	};
	// Try to grab stack from Error objects
	for (const a of args) {
		if (a instanceof Error && a.stack) {
			entry.stack = a.stack.slice(0, 800);
			break;
		}
	}
	pushEntry(consoleLog, entry);
	_origError.apply(console, args);
};

console.warn = (...args: unknown[]) => {
	pushEntry(consoleLog, {
		level: 'warn',
		message: args.map(String).join(' ').slice(0, 500),
		timestamp: new Date().toISOString(),
	});
	_origWarn.apply(console, args);
};

// ─── Intercept fetch to capture ALL API calls ────────────────────────────────

const _origFetch = window.fetch;
window.fetch = async function (...args: Parameters<typeof fetch>) {
	const req = new Request(...args);
	const start = performance.now();
	try {
		const res = await _origFetch.apply(window, args);
		if (req.url.includes('/api/')) {
			pushEntry(networkLog, {
				method: req.method,
				url: stripOrigin(req.url),
				status: res.status,
				duration: Math.round(performance.now() - start),
				timestamp: new Date().toISOString(),
			});
		}
		return res;
	} catch (err) {
		if (req.url.includes('/api/')) {
			pushEntry(networkLog, {
				method: req.method,
				url: stripOrigin(req.url),
				status: 0,
				duration: Math.round(performance.now() - start),
				timestamp: new Date().toISOString(),
			});
		}
		throw err;
	}
};

// ─── Capture unhandled JS errors ─────────────────────────────────────────────

window.addEventListener('error', (ev) => {
	if (ev.filename) {
		pushEntry(
			jsErrors,
			`${ev.message} at ${ev.filename}:${ev.lineno}:${ev.colno}`,
		);
	}
});

window.addEventListener('unhandledrejection', (ev) => {
	const reason =
		ev.reason instanceof Error
			? `${ev.reason.message}\n${(ev.reason.stack ?? '').slice(0, 400)}`
			: String(ev.reason).slice(0, 400);
	pushEntry(jsErrors, `Unhandled promise: ${reason}`);
});

// ─── Resource loading errors (images, scripts, stylesheets) ──────────────────

window.addEventListener(
	'error',
	(ev) => {
		const target = ev.target as HTMLElement | null;
		if (target && target !== window && 'tagName' in target) {
			const tag = target.tagName;
			const src =
				(target as HTMLImageElement).src ||
				(target as HTMLLinkElement).href ||
				'?';
			pushEntry(resourceErrors, `${tag} failed: ${stripOrigin(src)}`);
		}
	},
	true,
); // capture phase to catch resource errors

// ─── Breadcrumbs: user interaction trace ─────────────────────────────────────

// Click tracking
document.addEventListener(
	'click',
	(ev) => {
		const el = ev.target as HTMLElement;
		const descriptor = describeElement(el);
		if (descriptor) {
			pushEntry(breadcrumbs, {
				type: 'click',
				message: descriptor,
				timestamp: new Date().toISOString(),
			});
		}
	},
	true,
);

// Route change tracking
router.afterEach((to, from) => {
	pushEntry(breadcrumbs, {
		type: 'navigation',
		message: `${from.fullPath} → ${to.fullPath}`,
		timestamp: new Date().toISOString(),
	});
});

// Visibility change (tab switch)
document.addEventListener('visibilitychange', () => {
	pushEntry(breadcrumbs, {
		type: 'visibility',
		message: document.visibilityState,
		timestamp: new Date().toISOString(),
	});
});

// ─── Vue error handler ──────────────────────────────────────────────────────

export function installVueErrorHandler(app: { config: { errorHandler: any } }) {
	const prev = app.config.errorHandler;
	app.config.errorHandler = (err: unknown, instance: any, info: string) => {
		const name = instance?.$options?.name || instance?.$.type?.name || '?';
		const msg = err instanceof Error ? err.message : String(err);
		const stack = err instanceof Error ? (err.stack ?? '').slice(0, 600) : '';
		pushEntry(consoleLog, {
			level: 'error',
			message: `[Vue ${info}] <${name}>: ${msg}`,
			stack,
			timestamp: new Date().toISOString(),
		});
		if (prev) prev(err, instance, info);
	};
}

// ─── Public: add custom breadcrumb ──────────────────────────────────────────

export function addBreadcrumb(message: string) {
	pushEntry(breadcrumbs, {
		type: 'custom',
		message,
		timestamp: new Date().toISOString(),
	});
}

// ─── Collect snapshot ────────────────────────────────────────────────────────

export function collectDebugInfo(): Record<string, unknown> {
	const route = router.currentRoute.value;
	const perf = performance.getEntriesByType('navigation')[0] as
		| PerformanceNavigationTiming
		| undefined;
	const wsState = getWsDebugState();

	return {
		timestamp: new Date().toISOString(),
		sessionStart,
		sessionDuration: Math.round(
			(Date.now() - new Date(sessionStart).getTime()) / 1000,
		),

		// Route
		route: {
			path: route.path,
			fullPath: route.fullPath,
			params: route.params,
			query: route.query,
			name: route.name ?? null,
		},

		// User context (no tokens / secrets)
		user: user.value
			? {
					id: user.value.id,
					role: user.value.role,
					department: user.value.department,
					email: user.value.email,
				}
			: null,

		// Viewport & device
		viewport: {
			width: window.innerWidth,
			height: window.innerHeight,
			devicePixelRatio: window.devicePixelRatio,
			screenWidth: screen.width,
			screenHeight: screen.height,
			orientation: screen.orientation?.type ?? null,
		},

		// Touch / input capability
		inputCapabilities: {
			touchPoints: navigator.maxTouchPoints,
			pointerType: matchMedia('(pointer: fine)').matches
				? 'fine'
				: matchMedia('(pointer: coarse)').matches
					? 'coarse'
					: 'none',
		},

		// Performance
		performance: perf
			? {
					domContentLoaded: Math.round(
						perf.domContentLoadedEventEnd - perf.startTime,
					),
					loadComplete: Math.round(perf.loadEventEnd - perf.startTime),
					transferSize: perf.transferSize,
					redirectCount: perf.redirectCount,
				}
			: null,

		// Memory (Chrome only)
		memory: (performance as any).memory
			? {
					usedJSHeapSize: (performance as any).memory.usedJSHeapSize,
					totalJSHeapSize: (performance as any).memory.totalJSHeapSize,
					jsHeapSizeLimit: (performance as any).memory.jsHeapSizeLimit,
				}
			: null,

		// DOM complexity
		dom: {
			elementCount: document.querySelectorAll('*').length,
			bodyScrollHeight: document.body.scrollHeight,
			activeElement: describeElement(document.activeElement as HTMLElement),
		},

		// WebSocket state
		webSocket: wsState,

		// Breadcrumbs (user interaction trace)
		breadcrumbs: [...breadcrumbs],

		// All recent API requests (with timing)
		recentApiRequests: [...networkLog],

		// Console errors/warnings
		recentConsole: [...consoleLog],

		// JS errors
		recentJsErrors: [...jsErrors],

		// Resource errors
		recentResourceErrors: [...resourceErrors],

		// Connection
		connection: (navigator as any).connection
			? {
					effectiveType: (navigator as any).connection.effectiveType,
					downlink: (navigator as any).connection.downlink,
					rtt: (navigator as any).connection.rtt,
					saveData: (navigator as any).connection.saveData,
				}
			: null,

		// LocalStorage summary (sanitised — no IDs or tokens)
		localStorage: (() => {
			try {
				const keys = Array.from(
					{ length: localStorage.length },
					(_, i) => localStorage.key(i)!,
				);
				return {
					count: keys.length,
					keys: keys.map(sanitizeStorageKey),
				};
			} catch {
				return { count: 0, keys: [] };
			}
		})(),

		// Misc
		language: navigator.language,
		languages: [...navigator.languages],
		cookiesEnabled: navigator.cookieEnabled,
		online: navigator.onLine,
		referrer: document.referrer || null,
		platform: navigator.platform,
		hardwareConcurrency: navigator.hardwareConcurrency ?? null,
		documentTitle: document.title,
	};
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

const UUID_RE =
	/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/gi;

/** Replace UUIDs / OIDs in localStorage key names so no PII leaks */
function sanitizeStorageKey(key: string): string {
	return key.replace(UUID_RE, '***');
}

function pushEntry<T>(arr: T[], entry: T) {
	arr.push(entry);
	if (arr.length > MAX_ENTRIES) arr.shift();
}

function stripOrigin(url: string): string {
	try {
		const u = new URL(url, location.origin);
		return u.pathname + u.search;
	} catch {
		return url;
	}
}

function describeElement(el: HTMLElement | null): string | null {
	if (!el || el === document.body || el === document.documentElement)
		return null;
	const tag = el.tagName?.toLowerCase() ?? '?';
	const text = el.textContent?.trim().slice(0, 40) ?? '';
	const cls =
		el.className && typeof el.className === 'string'
			? '.' + el.className.split(/\s+/).slice(0, 2).join('.')
			: '';
	const id = el.id ? `#${el.id}` : '';
	return `<${tag}${id}${cls}>${text ? ` "${text}"` : ''}`;
}
