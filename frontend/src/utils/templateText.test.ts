import { describe, expect, it } from 'vitest';
import { replaceGuidingThreadPlaceholder } from './templateText';

describe('replaceGuidingThreadPlaceholder', () => {
	it('preserves formatting and line breaks without interpreting story text as HTML or variables', () => {
		const container = document.createElement('div');
		container.innerHTML = '<p><strong>{{ROTER_FADEN}}</strong> und {{roter_faden}} / {{titel}}</p>';
		replaceGuidingThreadPlaceholder(container, 'Erste Zeile\n<b>{{titel}}</b>');
		expect(container.innerHTML).toBe(
			'<p><strong>Erste Zeile<br>&lt;b&gt;{{titel}}&lt;/b&gt;</strong> und Erste Zeile<br>&lt;b&gt;{{titel}}&lt;/b&gt; / {{titel}}</p>',
		);
	});

	it('replaces an empty story without leaving a placeholder', () => {
		const container = document.createElement('div');
		container.innerHTML = '<p>Vorher {{roter_faden}} nachher</p>';
		replaceGuidingThreadPlaceholder(container, '');
		expect(container.textContent).toBe('Vorher  nachher');
	});
});
