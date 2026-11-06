// @vitest-environment jsdom
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { apiFetch } from '../composables/useApi';
import { MAX_PROGRAM_IMAGE_BYTES, ProgramImageRenderer, insertProgramHtml, prepareProgramImageFiles, prepareProgramPaste, sanitizeProgramDescription, serializeProgramDescription, validateProgramImageFile } from './programImages';

vi.mock('../composables/useApi', () => ({ apiFetch: vi.fn() }));

const id = '12345678-1234-1234-1234-123456789abc';
const path = `/api/program-images/${id}`;
const png = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aRZkAAAAASUVORK5CYII=';
const image = () => new File([Uint8Array.from(atob(png), c => c.charCodeAt(0))], 'Bild.png', { type: 'image/png' });

function clipboard(html = '', files: File[] = [], text = ''): DataTransfer {
	return {
		files, items: [],
		getData: (type: string) => type === 'text/html' ? html : text,
	} as unknown as DataTransfer;
}

describe('inline program images', () => {
	beforeEach(() => {
		vi.mocked(apiFetch).mockReset().mockResolvedValue(new Response(image(), { headers: { 'Content-Type': 'image/png' } }));
		vi.stubGlobal('URL', class extends URL {
			static createObjectURL = vi.fn(() => 'blob:program-image');
			static revokeObjectURL = vi.fn();
		});
	});
	afterEach(() => {
		document.body.innerHTML = '';
		vi.restoreAllMocks();
		vi.unstubAllGlobals();
	});

	it('accepts supported nonempty files at the exact size limit', () => {
		expect(() => validateProgramImageFile(image())).not.toThrow();
		expect(() => validateProgramImageFile(new File([new Uint8Array(MAX_PROGRAM_IMAGE_BYTES)], 'max.png', { type: 'image/png' }))).not.toThrow();
		expect(() => validateProgramImageFile(new File([new Uint8Array(MAX_PROGRAM_IMAGE_BYTES + 1)], 'large.png', { type: 'image/png' }))).toThrow('10 MiB');
		expect(() => validateProgramImageFile(new File([], 'empty.png', { type: 'image/png' }))).toThrow('1 Byte');
		expect(() => validateProgramImageFile(new File(['<svg/>'], 'bad.svg', { type: 'image/svg+xml' }))).toThrow('PNG');
	});

	it('pastes image bytes between sanitized rich text in order', () => {
		const paste = prepareProgramPaste(clipboard(
			`<p>Vorher <strong>fett</strong><img src="data:image/png;base64,${png}" onerror="alert(1)"> Danach</p><script>alert(1)</script>`,
		));
		expect(paste.uploads).toHaveLength(1);
		expect(paste.uploads[0]!.file.type).toBe('image/png');
		expect(paste.uploads[0]!.file.size).toBe(image().size);
		expect(paste.html).toMatch(/Vorher.*<strong>fett<\/strong>.*data-program-image-upload=.*Danach/);
		expect(paste.html).not.toMatch(/data:image|onerror|script/);
	});

	it('uses clipboard files without duplicating an accompanying HTML image', () => {
		const paste = prepareProgramPaste(clipboard('<p>A<img src="file:///clipboard.png">B</p>', [image()]));
		expect(paste.uploads).toHaveLength(1);
		expect(paste.uploads[0]!.file).toBeInstanceOf(File);
		expect(paste.html).toMatch(/A.*data-program-image-upload=.*B/);
		const files = prepareProgramPaste(clipboard('', [image(), image()], 'Text\nZeile'));
		expect(files.uploads).toHaveLength(2);
		expect(files.html).toContain('Text<br>Zeile');
	});

	it('rejects unsupported embedded bytes and refuses external or temporary images', () => {
		expect(() => prepareProgramPaste(clipboard('<img src="data:image/svg+xml;base64,PHN2Zy8+">'))).toThrow('ungültig');
		const paste = prepareProgramPaste(clipboard('<b>Text</b><img src="https://example.com/image.png" srcset="javascript:alert(1)"><img src="blob:unowned">'));
		expect(paste.html).toBe('<b>Text</b>');
		expect(paste.warning).toContain('Externe Bilder');
		expect(paste.uploads).toHaveLength(0);
	});

	it('preserves durable references copied from another app editor without trusting other markup', () => {
		const paste = prepareProgramPaste(clipboard(`<img src="blob:program-image" data-program-image="${id}" onerror="alert(1)">`));
		expect(paste.html).toBe(`<img src="${path}">`);
		expect(paste.uploads).toHaveLength(0);
		const el = document.createElement('div');
		el.innerHTML = '<img src="blob:unowned" data-program-image="javascript:alert(1)">';
		expect(serializeProgramDescription(el)).toBe('<img>');
	});

	it('inserts at the caret and never stores upload placeholders or runtime blob URLs', async () => {
		const el = document.createElement('div');
		el.contentEditable = 'true';
		el.innerHTML = 'Vorher Danach';
		document.body.append(el);
		const range = document.createRange();
		range.setStart(el.firstChild!, 7);
		range.collapse(true);
		window.getSelection()!.removeAllRanges();
		window.getSelection()!.addRange(range);
		insertProgramHtml(el, `<img src="${path}" alt="Bild">`, false);
		const renderer = new ProgramImageRenderer();
		await renderer.render(el);
		expect(apiFetch).toHaveBeenCalledWith(path);
		expect(el.querySelector('img')!.src).toBe('blob:program-image');
		expect(serializeProgramDescription(el)).toBe(`Vorher <img alt="Bild" src="${path}">Danach`);
		el.insertAdjacentHTML('beforeend', prepareProgramImageFiles([image()]).html);
		expect(serializeProgramDescription(el)).not.toContain('hochgeladen');
		expect(serializeProgramDescription(el)).not.toMatch(/blob:|data-program-image/);
		renderer.dispose();
		expect(URL.revokeObjectURL).toHaveBeenCalledWith('blob:program-image');
	});

	it('hydrates after reload and uses a share-scoped request without authentication', async () => {
		const el = document.createElement('div');
		el.innerHTML = sanitizeProgramDescription(`A<img src="${path}" alt="Bild">B`);
		const publicFetch = vi.spyOn(globalThis, 'fetch').mockResolvedValue(new Response(image(), { headers: { 'Content-Type': 'image/png' } }));
		const renderer = new ProgramImageRenderer('share-token');
		await renderer.render(el);
		expect(publicFetch).toHaveBeenCalledWith(`/api/shared/share-token/program-images/${id}`);
		expect(apiFetch).not.toHaveBeenCalled();
		expect(serializeProgramDescription(el)).toBe(`A<img alt="Bild" src="${path}">B`);
		renderer.dispose();
	});

	it('surfaces read failures without discarding the durable reference', async () => {
		vi.mocked(apiFetch).mockResolvedValue(new Response('', { status: 403 }));
		const el = document.createElement('div');
		el.innerHTML = `<img src="${path}" alt="Bild">`;
		const renderer = new ProgramImageRenderer();
		await expect(renderer.render(el)).rejects.toThrow('Berechtigung');
		expect(el.querySelector('img')!.dataset.programImageError).toBe('true');
		expect(serializeProgramDescription(el)).toBe(`<img alt="Bild" src="${path}">`);
		renderer.dispose();
	});

	it('keeps legacy text and formatting while removing unsafe image attributes', () => {
		expect(sanitizeProgramDescription('Text\nZweite Zeile')).toBe('Text\nZweite Zeile');
		expect(sanitizeProgramDescription('<p><b>Text</b><img src="javascript:alert(1)" srcset="data:text/html,unsafe" onerror="alert(1)"></p>'))
			.toBe('<p><b>Text</b><img></p>');
		const active = 'Text<svg><a><animate attributeName="href" values="javascript:alert(1)"></animate></a></svg><math><mtext>x</mtext></math><picture><source srcset="https://example.com/image.png"></picture><base href="https://example.com/">';
		expect(sanitizeProgramDescription(active)).toBe('Text<picture></picture>');
		expect(prepareProgramPaste(clipboard(active)).html).toBe('Text<picture></picture>');
	});

	it('preserves safe typography and links in mixed clipboard contents', () => {
		const html = `<p style="color: red"><strong>Text</strong><a href="/local">Link</a><img src="data:image/png;base64,${png}"></p>`;
		const paste = prepareProgramPaste(clipboard(html));
		expect(paste.uploads).toHaveLength(1);
		expect(paste.html).toContain('<p style="color: red"><strong>Text</strong><a href="/local">Link</a>');
	});

	it('sanitizes active clipboard markup before processing embedded image bytes', () => {
		const paste = prepareProgramPaste(clipboard(`<p><a href="javascript:alert(1)">Text</a><img src="data:image/png;base64,${png}" onerror="alert(1)"></p><script>alert(1)</script>`));
		expect(paste.uploads).toHaveLength(1);
		expect(paste.html).toContain('<a>Text</a>');
		expect(paste.html).not.toMatch(/javascript:|onerror|script|data:image/);
	});
});
