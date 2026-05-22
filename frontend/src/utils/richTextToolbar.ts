export type RichTextToolbarState = {
	bold: boolean;
	italic: boolean;
	underline: boolean;
	ul: boolean;
	ol: boolean;
	font: string;
	size: string;
	color: string;
	bgColor: string;
};

export function defaultRichTextToolbarState(): RichTextToolbarState {
	return {
		bold: false,
		italic: false,
		underline: false,
		ul: false,
		ol: false,
		font: 'Arial',
		size: '12',
		color: '#000000',
		bgColor: '#ffffff',
	};
}

export function rgbToHex(rgb: string): string {
	const m = rgb.match(/^rgb\((\d+),\s*(\d+),\s*(\d+)\)$/);
	if (!m) return rgb;
	return (
		'#' +
		[m[1], m[2], m[3]]
			.map((x) => parseInt(x, 10).toString(16).padStart(2, '0'))
			.join('')
	);
}

export function readRichTextToolbarState(
	node: HTMLElement,
): RichTextToolbarState {
	const cs = window.getComputedStyle(node);
	return {
		bold: document.queryCommandState('bold'),
		italic: document.queryCommandState('italic'),
		underline: document.queryCommandState('underline'),
		ul: document.queryCommandState('insertUnorderedList'),
		ol: document.queryCommandState('insertOrderedList'),
		font: cs.fontFamily.replace(/["']/g, '').split(',')[0].trim() || 'Arial',
		size: parseInt(cs.fontSize, 10).toString() || '12',
		color: rgbToHex(cs.color) || '#000000',
		bgColor:
			cs.backgroundColor === 'rgba(0, 0, 0, 0)' ||
			cs.backgroundColor === 'transparent'
				? '#ffffff'
				: rgbToHex(cs.backgroundColor) || '#ffffff',
	};
}
