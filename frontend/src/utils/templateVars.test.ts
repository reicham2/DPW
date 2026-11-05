import { describe, expect, it } from 'vitest';
import { replaceTemplateVarsPlain } from './templateVars';

describe('plain-text title variables', () => {
	const vars = {
		titel: 'Spiel & Spass <2026>',
		datum_kurz: '15.07.2026',
		ort: 'Wald & Wiese',
		tn_material: 'Flasche, Regenschutz',
		absender_name: 'Anna & Ben',
	};
	const formUrl = 'https://dpw.example/forms/signup?a=1&b=$&';

	it('expands repeated and case-insensitive variables without HTML encoding', () => {
		expect(replaceTemplateVarsPlain('{{TITEL}} / {{titel}} am {{datum_kurz}} in {{ort}}', vars, formUrl))
			.toBe('Spiel & Spass <2026> / Spiel & Spass <2026> am 15.07.2026 in Wald & Wiese');
	});

	it('keeps literal markup and entities as plain text', () => {
		expect(replaceTemplateVarsPlain('<Pfadi> &amp; {{absender_name}}', vars, formUrl))
			.toBe('<Pfadi> &amp; Anna & Ben');
	});

	it('uses a literal URL, not anchor markup or custom labels, for form links', () => {
		expect(replaceTemplateVarsPlain('{{formular_link}} / {{FORMULAR_LINK|Anmelden}}', vars, formUrl))
			.toBe(`${formUrl} / ${formUrl}`);
	});

	it('uses plain comma-separated material instead of an HTML list', () => {
		expect(replaceTemplateVarsPlain('{{tn_material}}', vars, formUrl)).toBe('Flasche, Regenschutz');
	});

	it('leaves unsupported variables and non-link labels untouched', () => {
		expect(replaceTemplateVarsPlain('{{unbekannt}} {{titel|Text}} {{constructor}}', vars, formUrl))
			.toBe('{{unbekannt}} {{titel|Text}} {{constructor}}');
	});

	it('does not recursively expand variable values', () => {
		expect(replaceTemplateVarsPlain('{{titel}}', { titel: '{{ort}}', ort: 'Wald' }, formUrl))
			.toBe('{{ort}}');
	});

	it('preserves the caller-specific missing form URL behavior', () => {
		expect(replaceTemplateVarsPlain('{{formular_link}}', vars, '')).toBe('');
		expect(replaceTemplateVarsPlain('{{formular_link}}', vars, '#')).toBe('#');
	});
});
