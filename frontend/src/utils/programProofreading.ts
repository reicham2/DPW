export interface ProofreadingMatch {
	offset: number;
	length: number;
	message: string;
	shortMessage: string;
	replacements: { value: string }[];
	rule: { id?: string; description?: string; issueType?: string };
}

interface TextSegment {
	start: number;
	end: number;
	node: Text;
}

export interface ProgramText {
	text: string;
	segments: TextSegment[];
}

function isBlockElement(element: Element): boolean {
	return /^(ADDRESS|BLOCKQUOTE|DIV|H[1-6]|LI|OL|P|PRE|UL)$/.test(element.tagName);
}

export function getProgramText(root: HTMLElement): ProgramText {
	let text = '';
	const segments: TextSegment[] = [];

	function addSeparator() {
		if (text && !text.endsWith('\n')) text += '\n';
	}

	function visit(node: Node) {
		if (node.nodeType === Node.TEXT_NODE) {
			const value = node.textContent ?? '';
			if (value) {
				const start = text.length;
				text += value;
				segments.push({ start, end: text.length, node: node as Text });
			}
			return;
		}
		if (node.nodeType !== Node.ELEMENT_NODE) return;

		const element = node as Element;
		if (element.tagName === 'BR' || element.tagName === 'HR') {
			text += '\n';
			return;
		}
		if (element.tagName === 'IMG') {
			addSeparator();
			return;
		}
		if (isBlockElement(element)) addSeparator();
		for (const child of Array.from(element.childNodes)) visit(child);
	}

	for (const child of Array.from(root.childNodes)) visit(child);
	return { text, segments };
}

export function proofreadingResultIsCurrent(root: HTMLElement, htmlSnapshot: string): boolean {
	return root.innerHTML === htmlSnapshot;
}

export function getProofreadingRange(
	programText: ProgramText,
	offset: number,
	length: number,
): Range | null {
	const end = offset + length;
	if (!Number.isInteger(offset) || !Number.isInteger(length) || offset < 0 || length <= 0 ||
		end > programText.text.length) return null;

	const affected = programText.segments
		.map(segment => ({
			segment,
			start: Math.max(offset, segment.start),
			end: Math.min(end, segment.end),
		}))
		.filter(part => part.start < part.end);
	const covered = affected.reduce((sum, part) => sum + part.end - part.start, 0);
	if (covered !== length || affected.length !== 1) return null;

	const { segment } = affected[0];
	const range = document.createRange();
	range.setStart(segment.node, offset - segment.start);
	range.setEnd(segment.node, end - segment.start);
	return range;
}
