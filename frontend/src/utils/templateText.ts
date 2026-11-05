export function replaceGuidingThreadPlaceholder(container: HTMLElement, value: string): void {
	const walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT);
	const nodes: Text[] = [];
	while (walker.nextNode()) nodes.push(walker.currentNode as Text);

	for (const node of nodes) {
		const parts = node.data.split(/(\{\{roter_faden\}\})/i);
		if (parts.length === 1) continue;

		const fragment = document.createDocumentFragment();
		for (const part of parts) {
			if (/^\{\{roter_faden\}\}$/i.test(part)) {
				value.split(/\r\n|\r|\n/).forEach((line, index) => {
					if (index > 0) fragment.appendChild(document.createElement('br'));
					fragment.appendChild(document.createTextNode(line));
				});
			} else {
				fragment.appendChild(document.createTextNode(part));
			}
		}
		node.replaceWith(fragment);
	}
}
