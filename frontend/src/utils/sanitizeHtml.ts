const BLOCKED_TAGS = new Set([
	'script',
	'iframe',
	'object',
	'embed',
	'form',
	'meta',
	'link',
	'style',
]);

const URL_ATTRS = new Set(['href', 'src', 'xlink:href']);

function isSafeUrl(value: string): boolean {
	const normalized = value.trim().toLowerCase();
	if (!normalized) return true;
	if (normalized.startsWith('#') || normalized.startsWith('/')) return true;
	return (
		normalized.startsWith('http://') ||
		normalized.startsWith('https://') ||
		normalized.startsWith('mailto:') ||
		normalized.startsWith('tel:')
	);
}

export function sanitizeHtml(html: string): string {
	const wrapper = document.createElement('div');
	wrapper.innerHTML = html;

	wrapper
		.querySelectorAll(Array.from(BLOCKED_TAGS).join(','))
		.forEach((el) => el.remove());

	wrapper.querySelectorAll('*').forEach((el) => {
		for (const attr of Array.from(el.attributes)) {
			const name = attr.name.toLowerCase();
			const value = attr.value;

			if (name.startsWith('on')) {
				el.removeAttribute(attr.name);
				continue;
			}

			if (URL_ATTRS.has(name) && !isSafeUrl(value)) {
				el.removeAttribute(attr.name);
			}
		}
	});

	return wrapper.innerHTML;
}
