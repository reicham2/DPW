import { apiFetch } from '../composables/useApi';
import { sanitizeHtml } from './sanitizeHtml';
import DOMPurify from 'dompurify';

export const MAX_PROGRAM_IMAGE_BYTES = 10 * 1024 * 1024;
export const PROGRAM_IMAGE_TYPES = ['image/png', 'image/jpeg', 'image/gif', 'image/webp'];
export const PROGRAM_IMAGE_ACCEPT = PROGRAM_IMAGE_TYPES.join(',');
const imagePath = /^\/api\/program-images\/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})$/i;
let uploadSequence = 0;

export function programImageId(src: string): string | null {
	return imagePath.exec(src)?.[1]?.toLowerCase() ?? null;
}

export function validateProgramImageFile(file: File): void {
	if (!PROGRAM_IMAGE_TYPES.includes(file.type)) {
		throw new Error('Erlaubte Bildformate: PNG, JPEG, GIF oder WebP.');
	}
	if (!file.size || file.size > MAX_PROGRAM_IMAGE_BYTES) {
		throw new Error('Bilder müssen zwischen 1 Byte und 10 MiB gross sein.');
	}
}

function removeUnsupportedImageMarkup(fragment: DocumentFragment): boolean {
	const unsupported = fragment.querySelectorAll('svg, math, base, template, source');
	unsupported.forEach(el => el.remove());
	fragment.querySelectorAll('[srcset]').forEach(el => el.removeAttribute('srcset'));
	return unsupported.length > 0;
}

export function sanitizeProgramDescription(html: string): string {
	const template = document.createElement('template');
	template.innerHTML = sanitizeHtml(html);
	removeUnsupportedImageMarkup(template.content);
	template.content.querySelectorAll('[data-program-image], [data-program-image-upload], [data-program-image-error]').forEach(el => {
		el.removeAttribute('data-program-image');
		el.removeAttribute('data-program-image-upload');
		el.removeAttribute('data-program-image-error');
	});
	const wrapper = document.createElement('div');
	wrapper.append(template.content);
	return wrapper.innerHTML;
}

export function serializeProgramDescription(el: HTMLElement): string {
	const clone = el.cloneNode(true) as HTMLElement;
	clone.querySelectorAll('[data-program-image-upload]').forEach(node => node.remove());
	clone.querySelectorAll('img[data-program-image]').forEach(img => {
		const id = programImageId(`/api/program-images/${img.getAttribute('data-program-image') ?? ''}`);
		if (id) img.setAttribute('src', `/api/program-images/${encodeURIComponent(id)}`);
		if (img.hasAttribute('data-program-image-error')) img.removeAttribute('title');
	});
	return sanitizeProgramDescription(clone.innerHTML);
}

export class ProgramImageRenderer {
	private urls = new Map<string, string>();
	private pending = new Map<string, Promise<string>>();
	private disposed = false;

	constructor(private sharedToken?: string, private guestToken?: string) {}

	private load(id: string): Promise<string> {
		const cached = this.urls.get(id);
		if (cached) return Promise.resolve(cached);
		const pending = this.pending.get(id);
		if (pending) return pending;
		const request = (async () => {
			const path = this.guestToken
				? `/api/guest-edit/program-images/${id}`
				: this.sharedToken
					? `/api/shared/${encodeURIComponent(this.sharedToken)}/program-images/${id}`
					: `/api/program-images/${id}`;
			const response = this.guestToken
				? await fetch(path, {
						headers: { 'X-Guest-Edit-Token': this.guestToken },
						cache: 'no-store',
					})
				: this.sharedToken ? await fetch(path) : await apiFetch(path);
			if (!response.ok) throw new Error('Ein Bild konnte nicht geladen werden. Bitte Berechtigung und Verbindung prüfen.');
			const blob = await response.blob();
			if (!PROGRAM_IMAGE_TYPES.includes(blob.type)) throw new Error('Der Server hat kein unterstütztes Bild geliefert.');
			const url = URL.createObjectURL(blob);
			if (this.disposed) {
				URL.revokeObjectURL(url);
				return '';
			}
			this.urls.set(id, url);
			return url;
		})().finally(() => this.pending.delete(id));
		this.pending.set(id, request);
		return request;
	}

	async render(el: HTMLElement): Promise<void> {
		if (this.disposed) return;
		await Promise.all(Array.from(el.querySelectorAll<HTMLImageElement>('img')).map(async img => {
			const id = programImageId(img.getAttribute('src') ?? '')
				?? programImageId(`/api/program-images/${img.dataset.programImage ?? ''}`);
			if (!id) return;
			img.dataset.programImage = id;
			if (!img.src.startsWith('blob:')) img.removeAttribute('src');
			try {
				const url = await this.load(id);
				if (this.disposed || !el.contains(img) || img.dataset.programImage !== id) return;
				img.src = url;
				img.removeAttribute('data-program-image-error');
			} catch (error) {
				if (this.disposed || !el.contains(img)) return;
				img.dataset.programImageError = 'true';
				img.title = 'Bild konnte nicht geladen werden';
				throw error;
			}
		}));
	}

	dispose(): void {
		this.disposed = true;
		this.urls.forEach(url => URL.revokeObjectURL(url));
		this.urls.clear();
	}
}

export interface ProgramImageUpload {
	marker: string;
	file: File;
	alt: string;
}

export interface ProgramPaste {
	html: string;
	uploads: ProgramImageUpload[];
	warning: string | null;
}

function uploadPlaceholder(file: File, alt: string, uploads: ProgramImageUpload[]): HTMLElement {
	validateProgramImageFile(file);
	const marker = String(++uploadSequence);
	uploads.push({ marker, file, alt: alt || file.name });
	const el = document.createElement('span');
	el.dataset.programImageUpload = marker;
	el.contentEditable = 'false';
	el.textContent = 'Bild wird hochgeladen…';
	return el;
}

export function prepareProgramImageFiles(files: File[]): ProgramPaste {
	const wrapper = document.createElement('div');
	const uploads: ProgramImageUpload[] = [];
	files.forEach(file => wrapper.append(uploadPlaceholder(file, file.name, uploads)));
	return { html: wrapper.innerHTML, uploads, warning: null };
}

function imageDataFile(src: string): File {
	const match = /^data:(image\/(?:png|jpeg|gif|webp));base64,([a-z0-9+/=\s]+)$/i.exec(src);
	if (!match || match[2]!.length > 4 * Math.ceil(MAX_PROGRAM_IMAGE_BYTES / 3) + 4096) {
		throw new Error('Eingefügtes Bild ist ungültig oder zu gross. Erlaubt: PNG, JPEG, GIF oder WebP bis 10 MiB.');
	}
	const bytes = Uint8Array.from(atob(match[2]!.replace(/\s/g, '')), char => char.charCodeAt(0));
	const type = match[1]!.toLowerCase();
	const extension = type.slice(6) === 'jpeg' ? 'jpg' : type.slice(6);
	return new File([bytes], `Eingefügtes Bild.${extension}`, { type });
}

export function prepareProgramPaste(data: DataTransfer): ProgramPaste {
	const files = Array.from(data.files).filter(file => file.type.startsWith('image/'));
	if (!files.length) {
		for (const item of Array.from(data.items)) {
			if (item.kind !== 'file' || !item.type.startsWith('image/')) continue;
			const file = item.getAsFile();
			if (file) files.push(file);
		}
	}
	const html = data.getData('text/html');
	if (!html) {
		const pasted = prepareProgramImageFiles(files);
		const wrapper = document.createElement('div');
		const lines = data.getData('text/plain').split(/\r\n|\r|\n/);
		lines.forEach((line, index) => {
			if (index) wrapper.append(document.createElement('br'));
			wrapper.append(document.createTextNode(line));
		});
		return { ...pasted, html: wrapper.innerHTML + pasted.html };
	}
	const template = document.createElement('template');
	template.innerHTML = DOMPurify.sanitize(html, {
		ALLOWED_URI_REGEXP: /^(?:https?:|mailto:|tel:|blob:|data:image\/|\/|#)/i,
	});
	const unsupportedMarkup = removeUnsupportedImageMarkup(template.content);
	template.content.querySelectorAll<HTMLImageElement>('img[data-program-image]').forEach(img => {
		const id = programImageId(`/api/program-images/${img.dataset.programImage ?? ''}`);
		if (id && img.getAttribute('src')?.startsWith('blob:')) img.setAttribute('src', `/api/program-images/${encodeURIComponent(id)}`);
	});
	template.content.querySelectorAll('*').forEach(el => {
		el.removeAttribute('data-program-image');
		el.removeAttribute('data-program-image-upload');
		el.removeAttribute('data-program-image-error');
	});
	const uploads: ProgramImageUpload[] = [];
	let fileIndex = 0;
	let warning: string | null = unsupportedMarkup ? 'Nicht unterstützte Bildinhalte (z.B. SVG) wurden entfernt.' : null;
	template.content.querySelectorAll('img').forEach(img => {
		const src = img.getAttribute('src') ?? '';
		img.removeAttribute('srcset');
		if (programImageId(src)) return;
		const file = files[fileIndex++] ?? (src.startsWith('data:') ? imageDataFile(src) : null);
		if (!file) {
			img.remove();
			warning = 'Externe Bilder werden nicht übernommen. Bitte die Bilddatei hochladen oder das Bild selbst kopieren.';
			return;
		}
		img.replaceWith(uploadPlaceholder(file, img.alt, uploads));
	});
	while (fileIndex < files.length) {
		const file = files[fileIndex++]!;
		template.content.append(uploadPlaceholder(file, file.name, uploads));
	}
	const wrapper = document.createElement('div');
	wrapper.append(template.content);
	return { html: sanitizeHtml(wrapper.innerHTML), uploads, warning };
}

export function insertProgramHtml(el: HTMLElement, html: string, replaceSelection = true): void {
	const selection = window.getSelection();
	const current = selection?.rangeCount ? selection.getRangeAt(0) : null;
	const inEditor = current && el.contains(current.startContainer) && el.contains(current.endContainer);
	const range = inEditor
		? current.cloneRange() : document.createRange();
	if (!inEditor) {
		range.selectNodeContents(el);
		range.collapse(false);
	}
	if (replaceSelection) range.deleteContents();
	else range.collapse(false);
	const template = document.createElement('template');
	template.innerHTML = html;
	const last = template.content.lastChild;
	range.insertNode(template.content);
	el.focus();
	if (last && selection) {
		range.setStartAfter(last);
		range.collapse(true);
		selection.removeAllRanges();
		selection.addRange(range);
	}
}
