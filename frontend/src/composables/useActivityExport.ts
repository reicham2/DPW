import {
	AlignmentType,
	BorderStyle,
	Document,
	HeadingLevel,
	ImageRun,
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
import { useUserResolver } from './useUserResolver';
import { ref } from 'vue';
import { apiFetch, formatApiError } from './useApi';
import { PROGRAM_IMAGE_TYPES, programImageId, sanitizeProgramDescription } from '../utils/programImages';

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

function multilineValue(text: string): Paragraph {
	return new Paragraph({
		children: text.split(/\r\n|\r|\n/).map((line, index) =>
			new TextRun({ text: line, size: 20, break: index > 0 ? 1 : 0 }),
		),
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

async function descriptionImage(img: HTMLImageElement): Promise<ImageRun | TextRun> {
	const id = programImageId(img.getAttribute('src') ?? '');
	if (!id) return new TextRun({ text: img.alt || '[Bild]', size: 20 });
	const response = await apiFetch(`/api/program-images/${id}`);
	if (!response.ok) throw new Error('Ein Programmbild konnte nicht exportiert werden. Bitte Berechtigung und Verbindung prüfen.');
	let blob = await response.blob();
	if (!PROGRAM_IMAGE_TYPES.includes(blob.type)) throw new Error('Der Server hat kein unterstütztes Programmbild geliefert.');
	const url = URL.createObjectURL(blob);
	try {
		const image = new Image();
		await new Promise<void>((resolve, reject) => {
			image.onload = () => resolve();
			image.onerror = () => reject(new Error('Ein Programmbild konnte nicht gelesen werden.'));
			image.src = url;
		});
		if (!image.naturalWidth || !image.naturalHeight) throw new Error('Ein Programmbild hat ungültige Abmessungen.');
		if (blob.type === 'image/webp') {
			const canvas = document.createElement('canvas');
			canvas.width = image.naturalWidth;
			canvas.height = image.naturalHeight;
			const context = canvas.getContext('2d');
			if (!context) throw new Error('Ein WebP-Bild konnte nicht für Word umgewandelt werden.');
			context.drawImage(image, 0, 0);
			blob = await new Promise<Blob>((resolve, reject) => canvas.toBlob(
				result => result ? resolve(result) : reject(new Error('Ein WebP-Bild konnte nicht für Word umgewandelt werden.')),
				'image/png',
			));
		}
		const type = blob.type === 'image/jpeg' ? 'jpg' : blob.type === 'image/gif' ? 'gif' : 'png';
		const scale = Math.min(1, 160 / image.naturalWidth, 240 / image.naturalHeight);
		return new ImageRun({
			type,
			data: new Uint8Array(await blob.arrayBuffer()),
			transformation: { width: image.naturalWidth * scale, height: image.naturalHeight * scale },
			altText: { title: img.alt, description: img.alt, name: img.alt },
		});
	} finally {
		URL.revokeObjectURL(url);
	}
}

export async function programDescriptionParagraphs(html: string): Promise<Paragraph[]> {
	const template = document.createElement('template');
	template.innerHTML = sanitizeProgramDescription(html);
	const paragraphs: Paragraph[] = [];
	let runs: (TextRun | ImageRun)[] = [];
	const flush = () => {
		if (!runs.length) return;
		paragraphs.push(new Paragraph({ children: runs }));
		runs = [];
	};
	async function walk(node: Node, bold = false, italics = false): Promise<void> {
		if (node.nodeType === Node.TEXT_NODE) {
			runs.push(new TextRun({ text: node.textContent ?? '', size: 20, bold, italics }));
			return;
		}
		if (!(node instanceof HTMLElement)) return;
		if (node.tagName === 'IMG') {
			runs.push(await descriptionImage(node as HTMLImageElement));
			return;
		}
		if (node.tagName === 'BR') {
			runs.push(new TextRun({ break: 1 }));
			return;
		}
		const block = ['P', 'DIV', 'LI', 'UL', 'OL'].includes(node.tagName);
		if (block) flush();
		for (const child of Array.from(node.childNodes)) {
			await walk(child, bold || node.tagName === 'B' || node.tagName === 'STRONG',
				italics || node.tagName === 'I' || node.tagName === 'EM');
		}
		if (block) flush();
	}
	for (const child of Array.from(template.content.childNodes)) await walk(child);
	flush();
	return paragraphs.length ? paragraphs : [new Paragraph({ children: [new TextRun({ text: '—', size: 20 })] })];
}

async function programsTable(activity: Activity, resolveResponsibleName: (id: string) => string): Promise<Table> {
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

	const dataRows = await Promise.all(activity.programs.map(
		async (p) =>
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
						children: await programDescriptionParagraphs(p.description),
					}),
					new TableCell({
						children: [
							new Paragraph({
								children: [
									new TextRun({
										text: p.responsible.map(r => resolveResponsibleName(r)).join(', ') || '—',
										size: 20,
									}),
								],
							}),
						],
					}),
				],
			}),
	));

	return new Table({
		width: { size: 100, type: WidthType.PERCENTAGE },
		rows: [headerRow, ...dataRows],
	});
}

export function useActivityExport() {
	const { resolveResponsibleName } = useUserResolver();
	const error = ref<string | null>(null);

	async function exportToWord(activity: Activity): Promise<void> {
		error.value = null;
		try {
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
			metaRows.push(['Verantwortlich:', activity.responsible.map(r => resolveResponsibleName(r)).join(', ') || '—']);
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

			if (activity.guiding_thread) {
				children.push(heading2('Roter Faden'));
				children.push(multilineValue(activity.guiding_thread));
			}

			// Programs
			if (activity.programs.length > 0) {
				children.push(heading2('Programm'));
				children.push(await programsTable(activity, resolveResponsibleName));
			}

			// Material
			if (activity.material.length > 0) {
				children.push(heading2('Material'));
				for (const item of activity.material) {
					const resp =
						item.responsible && item.responsible.length > 0
							? ` (${item.responsible.map(r => resolveResponsibleName(r)).join(', ')})`
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
		} catch (e) {
			error.value = formatApiError(e);
		}
	}

	return { exportToWord, error };
}
