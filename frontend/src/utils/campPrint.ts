import type { CampDetail, CampActivity } from '../types';
import { buildActivityNumbers } from './campNumbering';
import { formatMinuteOfDay, formatDuration } from './campTime';

// Escapes text for safe insertion into the generated print HTML.
function esc(s: string): string {
	return String(s)
		.replace(/&/g, '&amp;')
		.replace(/</g, '&lt;')
		.replace(/>/g, '&gt;')
		.replace(/"/g, '&quot;');
}

function dayLabel(iso: string): string {
	const d = new Date(iso + 'T00:00:00');
	const wd = ['So', 'Mo', 'Di', 'Mi', 'Do', 'Fr', 'Sa'][d.getDay()];
	return `${wd} ${d.getDate()}.${d.getMonth() + 1}.${d.getFullYear()}`;
}

function daysInPeriod(startDate: string, endDate: string): string[] {
	const out: string[] = [];
	const start = new Date(startDate + 'T00:00:00');
	const end = new Date(endDate + 'T00:00:00');
	for (const d = new Date(start); d <= end; d.setDate(d.getDate() + 1)) {
		out.push(
			d.getFullYear() + '-' +
			String(d.getMonth() + 1).padStart(2, '0') + '-' +
			String(d.getDate()).padStart(2, '0'),
		);
	}
	return out;
}

// Builds a standalone, print-optimized HTML document for the whole camp program
// ("PDF / Drucken"). Renders each period day-by-day with times, categories
// and responsibles, then triggers the browser print dialog.
// resolveName maps a user id (or free-text) to a display name, matching the
// activity "Verantwortlich" system. Defaults to identity when not supplied.
export function buildCampPrintHtml(
	camp: CampDetail,
	resolveName: (idOrText: string) => string = (s) => s,
): string {
	const numbers = buildActivityNumbers(camp.activities, camp.categories);
	const catById = Object.fromEntries(camp.categories.map((c) => [c.id, c]));
	const collabById = Object.fromEntries(
		camp.collaborations.map((c) => [c.id, c.abbreviation || c.display_name]),
	);

	function respString(a: CampActivity): string {
		const users = (a.responsible ?? []).map(resolveName);
		const fns = a.responsible_collaboration_ids
			.map((id) => collabById[id])
			.filter(Boolean);
		return [...users, ...fns].filter(Boolean).join(', ');
	}

	let body = '';
	for (const period of camp.periods) {
		body += `<h2>${esc(period.description || 'Lagerabschnitt')}</h2>`;
		body += `<p class="sub">${esc(period.start_date)} – ${esc(period.end_date)}</p>`;
		const days = daysInPeriod(period.start_date, period.end_date);

		days.forEach((day, dayIdx) => {
			// Day responsibles
			const dayResp = camp.day_responsibles
				.filter((d) => d.period_id === period.id && d.day_offset === dayIdx)
				.map((d) => collabById[d.collaboration_id])
				.filter(Boolean)
				.join(', ');

			// Activities scheduled on this day, sorted by start time.
			const rows = camp.activities
				.flatMap((a) =>
					a.schedule_entries
						.filter(
							(s) =>
								s.period_id === period.id &&
								Math.floor(s.period_offset / 1440) === dayIdx,
						)
						.map((s) => ({ a, minute: s.period_offset - dayIdx * 1440, len: s.length })),
				)
				.sort((x, y) => x.minute - y.minute);

			body += `<h3>${esc(dayLabel(day))}${dayResp ? ` <span class="resp">— ${esc(dayResp)}</span>` : ''}</h3>`;
			if (rows.length === 0) {
				body += `<p class="empty">Keine Programmpunkte.</p>`;
				return;
			}
			body += '<table><thead><tr><th>Zeit</th><th>Dauer</th><th>Nr.</th><th>Titel</th><th>Ort</th><th>Verantwortlich</th></tr></thead><tbody>';
			for (const r of rows) {
				const cat = r.a.category_id ? catById[r.a.category_id] : undefined;
				const num = numbers[r.a.id] ?? '';
				const color = cat?.color ?? '#888';
				body += `<tr>`;
				body += `<td>${formatMinuteOfDay(r.minute)}–${formatMinuteOfDay(r.minute + r.len)}</td>`;
				body += `<td>${esc(formatDuration(r.len))}</td>`;
				body += `<td><span class="cat" style="background:${esc(color)}">${esc(num)}</span></td>`;
				body += `<td>${esc(r.a.title)}</td>`;
				body += `<td>${esc(r.a.location)}</td>`;
				body += `<td>${esc(respString(r.a))}</td>`;
				body += `</tr>`;
			}
			body += '</tbody></table>';
		});
	}

	return `<!doctype html><html lang="de"><head><meta charset="utf-8">
<title>${esc(camp.title)} — Programm</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: -apple-system, Segoe UI, Roboto, sans-serif; color: #111827; margin: 24px; }
  h1 { font-size: 22px; margin: 0 0 2px; }
  .motto { font-style: italic; color: #6b7280; margin: 0 0 16px; }
  h2 { font-size: 17px; margin: 22px 0 0; border-bottom: 2px solid #0080ff; padding-bottom: 4px; }
  h3 { font-size: 14px; margin: 16px 0 6px; }
  .sub { color: #6b7280; font-size: 12px; margin: 2px 0 0; }
  .resp { color: #0080ff; font-weight: normal; font-size: 12px; }
  .empty { color: #9ca3af; font-size: 12px; margin: 2px 0 8px; }
  table { width: 100%; border-collapse: collapse; font-size: 12px; margin-bottom: 8px; }
  th { text-align: left; border-bottom: 1px solid #d1d5db; padding: 4px 6px; font-size: 10px; text-transform: uppercase; color: #6b7280; }
  td { padding: 5px 6px; border-bottom: 1px solid #e5e7eb; vertical-align: top; }
  .cat { display: inline-block; color: #fff; font-weight: 700; font-size: 10px; padding: 1px 6px; border-radius: 4px; }
  @media print { body { margin: 0; } h2 { page-break-after: avoid; } table { page-break-inside: auto; } tr { page-break-inside: avoid; } }
</style></head>
<body>
  <h1>${esc(camp.title)}</h1>
  ${camp.motto ? `<p class="motto">«${esc(camp.motto)}»</p>` : ''}
  ${body || '<p class="empty">Noch kein Programm erfasst.</p>'}
</body></html>`;
}

// Opens the print document in a new window and triggers the print dialog.
export function printCamp(
	camp: CampDetail,
	resolveName: (idOrText: string) => string = (s) => s,
): void {
	const html = buildCampPrintHtml(camp, resolveName);
	const w = window.open('', '_blank');
	if (!w) return;
	w.document.open();
	w.document.write(html);
	w.document.close();
	w.focus();
	// Give the new document a tick to lay out before printing.
	setTimeout(() => w.print(), 250);
}
