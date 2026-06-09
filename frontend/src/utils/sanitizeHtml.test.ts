import { describe, it, expect } from 'vitest';
import { sanitizeHtml } from './sanitizeHtml';

describe('sanitizeHtml', () => {
	it('removes blocked tags', () => {
		const html =
			'<div>ok</div><script>alert(1)</script><iframe src="https://evil"></iframe>';
		const out = sanitizeHtml(html);
		expect(out).toContain('<div>ok</div>');
		expect(out).not.toContain('<script');
		expect(out).not.toContain('<iframe');
	});

	it('removes inline event handler attributes', () => {
		const out = sanitizeHtml('<a href="/x" onclick="alert(1)">Link</a>');
		expect(out).toContain('<a href="/x">Link</a>');
		expect(out).not.toContain('onclick=');
	});

	it('keeps safe urls', () => {
		const out = sanitizeHtml(
			'<a href="https://example.com">A</a><a href="mailto:test@example.com">B</a><a href="tel:+410000">C</a><a href="/local">D</a><a href="#section">E</a>',
		);
		expect(out).toContain('href="https://example.com"');
		expect(out).toContain('href="mailto:test@example.com"');
		expect(out).toContain('href="tel:+410000"');
		expect(out).toContain('href="/local"');
		expect(out).toContain('href="#section"');
	});

	it('removes unsafe url schemes', () => {
		const out = sanitizeHtml(
			'<a href="javascript:alert(1)">bad</a><img src="data:text/html;base64,xxx" />',
		);
		expect(out).toContain('<a>bad</a>');
		expect(out).toContain('<img>');
		expect(out).not.toContain('javascript:');
		expect(out).not.toContain('data:text/html');
	});

	it('treats uppercase protocols as unsafe after normalization', () => {
		const out = sanitizeHtml('<a href=" JAVASCRIPT:alert(1)">x</a>');
		expect(out).toContain('<a>x</a>');
		expect(out).not.toContain('href=');
	});

	it('keeps harmless markup untouched', () => {
		const html = '<p><strong>Hallo</strong> <em>Welt</em></p>';
		expect(sanitizeHtml(html)).toBe(html);
	});
});
