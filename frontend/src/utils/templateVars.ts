export function replaceTemplateVarsPlain(
	text: string,
	vars: Record<string, string>,
	formUrl: string,
): string {
	return text.replace(/\{\{(\w+)(?:\|([^}]*))?\}\}/gi, (placeholder, key: string, label: string | undefined) => {
		const name = key.toLowerCase();
		if (name === 'formular_link') return formUrl;
		if (label !== undefined) return placeholder;
		return Object.prototype.hasOwnProperty.call(vars, name) ? vars[name] : placeholder;
	});
}
