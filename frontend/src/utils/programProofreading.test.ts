import { describe, expect, it } from 'vitest';
import {
	getProgramText,
	getProofreadingRange,
	proofreadingResultIsCurrent,
} from './programProofreading';

describe('programProofreading', () => {
	it('maps rich text, paragraphs, line breaks and UTF-16 offsets to text nodes', () => {
		const root = document.createElement('div');
		root.innerHTML = '<p><strong>🐾 Grüsse</strong></p><p>falscher<br>Text</p>';
		const programText = getProgramText(root);
		const range = getProofreadingRange(programText, 3, 6);

		expect(programText.text).toBe('🐾 Grüsse\nfalscher\nText');
		expect(range?.toString()).toBe('Grüsse');
		range!.deleteContents();
		range!.insertNode(document.createTextNode('Grüesse'));
		expect(root.innerHTML).toBe('<p><strong>🐾 Grüesse</strong></p><p>falscher<br>Text</p>');
	});

	it('preserves inline formatting and images when replacing text', () => {
		const root = document.createElement('div');
		root.innerHTML = '<span>falsch</span><img src="/picture.png"><em> weiter</em>';
		const programText = getProgramText(root);
		const range = getProofreadingRange(programText, 0, 6);

		expect(programText.text).toBe('falsch\n weiter');
		expect(range?.toString()).toBe('falsch');
		range!.deleteContents();
		range!.insertNode(document.createTextNode('richtig'));
		expect(root.innerHTML).toBe('<span>richtig</span><img src="/picture.png"><em> weiter</em>');
	});

	it('does not apply suggestions across a paragraph or image boundary', () => {
		const root = document.createElement('div');
		root.innerHTML = '<p>falscher</p><p>Text</p>';
		const original = root.innerHTML;
		const programText = getProgramText(root);

		expect(getProofreadingRange(programText, 0, programText.text.length)).toBeNull();
		expect(root.innerHTML).toBe(original);
	});

	it('detects changed editor HTML before a suggestion is applied', () => {
		const root = document.createElement('div');
		root.innerHTML = '<p>Ein Text</p>';
		const snapshot = root.innerHTML;
		expect(proofreadingResultIsCurrent(root, snapshot)).toBe(true);
		root.innerHTML = '<p>Ein geänderter Text</p>';
		expect(proofreadingResultIsCurrent(root, snapshot)).toBe(false);
	});
});
