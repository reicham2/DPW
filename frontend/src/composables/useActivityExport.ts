import {
	AlignmentType,
	BorderStyle,
	Document,
	HeadingLevel,
	Packer,
	Paragraph,
	Table,
	TableCell,
	TableRow,
	TextRun,
	WidthType,
} from 'docx';
import type { Activity } from '../types';
import { formatDuration } from '../utils/time';

function formatDate(d: string): string {
	return new Date(d + 'T00:00:00').toLocaleDateString('de-DE', {
		weekday: 'long',
		day: 'numeric',
		month: 'long',
		year: 'numeric',
	});
}

function label(text: string): Paragraph {
	return new Paragraph({
		children: [new TextRun({ text, bold: true, size: 20 })],
		spacing: { before: 160, after: 40 },
	});
}

function value(text: string): Paragraph {
	return new Paragraph({
		children: [new TextRun({ text: text || '—', size: 20 })],
		spacing: { after: 80 },
	});
}

function heading2(text: string): Paragraph {
	return new Paragraph({
		text,
		heading: HeadingLevel.HEADING_2,
		spacing: { before: 300, after: 120 },
	});
}

const noBorder = {
	top: { style: BorderStyle.NONE, size: 0, color: 'auto' },
	bottom: { style: BorderStyle.NONE, size: 0, color: 'auto' },
	left: { style: BorderStyle.NONE, size: 0, color: 'auto' },
	right: { style: BorderStyle.NONE, size: 0, color: 'auto' },
};

function metaTable(rows: [string, string][]): Table {
	return new Table({
		width: { size: 100, type: WidthType.PERCENTAGE },
		borders: {
			top: { style: BorderStyle.NONE, size: 0, color: 'auto' },
			bottom: { style: BorderStyle.NONE, size: 0, color: 'auto' },
			left: { style: BorderStyle.NONE, size: 0, color: 'auto' },
			right: { style: BorderStyle.NONE, size: 0, color: 'auto' },
			insideHorizontal: { style: BorderStyle.NONE, size: 0, color: 'auto' },
			insideVertical: { style: BorderStyle.NONE, size: 0, color: 'auto' },
		},
		rows: rows.map(
			([k, v]) =>
				new TableRow({
					children: [
						new TableCell({
							width: { size: 25, type: WidthType.PERCENTAGE },
							borders: noBorder,
							children: [
								new Paragraph({
									children: [new TextRun({ text: k, bold: true, size: 20 })],
								}),
							],
						}),
						new TableCell({
							width: { size: 75, type: WidthType.PERCENTAGE },
							borders: noBorder,
							children: [
								new Paragraph({
									children: [new TextRun({ text: v || '—', size: 20 })],
								}),
							],
						}),
					],
				}),
		),
	});
}

function programsTable(activity: Activity): Table {
	const headerRow = new TableRow({
		tableHeader: true,
		children: ['Dauer', 'Titel', 'Beschreibung', 'Verantwortlich'].map(
			(h) =>
				new TableCell({
					children: [
						new Paragraph({
							children: [new TextRun({ text: h, bold: true, size: 20 })],
							alignment: AlignmentType.LEFT,
						}),
					],
					shading: { fill: 'E8E8E8' },
				}),
		),
	});

	const dataRows = activity.programs.map(
		(p) =>
			new TableRow({
				children: [
					new TableCell({
						children: [
							new Paragraph({
								children: [
									new TextRun({
										text: formatDuration(p.duration_minutes),
										size: 20,
									}),
								],
							}),
						],
					}),
					new TableCell({
						children: [
							new Paragraph({
								children: [new TextRun({ text: p.title || '—', size: 20 })],
							}),
						],
					}),
					new TableCell({
						children: [
							new Paragraph({
								children: [
									new TextRun({ text: p.description || '—', size: 20 }),
								],
							}),
						],
					}),
					new TableCell({
						children: [
							new Paragraph({
								children: [
									new TextRun({
										text: p.responsible.join(', ') || '—',
										size: 20,
									}),
								],
							}),
						],
					}),
				],
			}),
	);

	return new Table({
		width: { size: 100, type: WidthType.PERCENTAGE },
		rows: [headerRow, ...dataRows],
	});
}

export function useActivityExport() {
	async function exportToWord(activity: Activity): Promise<void> {
		const children: (Paragraph | Table)[] = [];

		// Title
		children.push(
			new Paragraph({
				text: activity.title,
				heading: HeadingLevel.HEADING_1,
				spacing: { after: 200 },
			}),
		);

		// Meta table: date / time / location / department / responsible / participants
		const metaRows: [string, string][] = [
			['Datum:', formatDate(activity.date)],
			['Zeit:', `${activity.start_time} – ${activity.end_time}`],
			['Ort:', activity.location || '—'],
		];
		if (activity.department) metaRows.push(['Stufe:', activity.department]);
		metaRows.push(['Verantwortlich:', activity.responsible.join(', ') || '—']);
		if (
			activity.planned_participants_estimate !== null &&
			activity.planned_participants_estimate !== undefined
		) {
			metaRows.push([
				'Teilnehmende (gesch.):',
				String(activity.planned_participants_estimate),
			]);
		}

		children.push(metaTable(metaRows));

		// Goal
		if (activity.goal) {
			children.push(heading2('Ziel'));
			children.push(value(activity.goal));
		}

		// Programs
		if (activity.programs.length > 0) {
			children.push(heading2('Programm'));
			children.push(programsTable(activity));
		}

		// Material
		if (activity.material.length > 0) {
			children.push(heading2('Material'));
			for (const item of activity.material) {
				const resp =
					item.responsible && item.responsible.length > 0
						? ` (${item.responsible.join(', ')})`
						: '';
				children.push(
					new Paragraph({
						bullet: { level: 0 },
						children: [new TextRun({ text: item.name + resp, size: 20 })],
					}),
				);
			}
		}

		// Safety concept
		if (activity.siko_text) {
			children.push(heading2('Sicherheitskonzept'));
			children.push(value(activity.siko_text));
		}

		// Bad weather
		if (activity.bad_weather_info) {
			children.push(heading2('Schlechtwetteralternative'));
			children.push(value(activity.bad_weather_info));
		}

		const doc = new Document({
			sections: [{ children }],
		});

		const blob = await Packer.toBlob(doc);
		const url = URL.createObjectURL(blob);
		const a = document.createElement('a');
		const safeTitle =
			activity.title.replace(/[^a-zA-Z0-9äöüÄÖÜß\s-]/g, '').trim() ||
			'Aktivitaet';
		a.href = url;
		a.download = `${safeTitle}.docx`;
		document.body.appendChild(a);
		a.click();
		document.body.removeChild(a);
		URL.revokeObjectURL(url);
	}

	return { exportToWord };
}
