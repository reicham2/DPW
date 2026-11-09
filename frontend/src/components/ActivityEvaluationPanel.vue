<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { apiFetch, formatApiError } from '../composables/useApi';

interface FeedbackFields {
	rating: number | null;
	positive: string;
	improvements: string;
	action_items: string;
	notes: string;
}

interface FeedbackEntry extends FeedbackFields {
	author: string;
	created_at: string;
	updated_at: string;
}

interface EvaluationResponse {
	actual_participants: number | null;
	actual_start_time: string | null;
	actual_end_time: string | null;
	can_edit_actuals: boolean;
	feedback: FeedbackEntry[];
	mine: FeedbackFields | null;
}

const props = defineProps<{
	activityId: string;
	plannedStartTime?: string;
	plannedEndTime?: string;
}>();

const ratingScale = Array.from({ length: 11 }, (_, index) => index);

const evaluation = ref<EvaluationResponse | null>(null);
const actualParticipants = ref('');
const actualStartTime = ref('');
const actualEndTime = ref('');
const rating = ref<number | null>(null);
const positiveItems = ref(['']);
const improvementItems = ref(['']);
const actionItems = ref(['']);
const notes = ref('');
const expanded = ref<Set<number>>(new Set());
const loading = ref(false);
const savingActuals = ref(false);
const savingFeedback = ref(false);
const actualsError = ref<string | null>(null);
const feedbackError = ref<string | null>(null);
const error = ref<string | null>(null);
const actualsSaved = ref(false);
const feedbackSaved = ref(false);

const averageRating = computed(() => {
	const rated = (evaluation.value?.feedback ?? []).filter((entry) => entry.rating !== null);
	if (!rated.length) return null;
	const sum = rated.reduce((total, entry) => total + (entry.rating ?? 0), 0);
	return (sum / rated.length).toFixed(1).replace('.', ',');
});

function parseFeedbackItems(value: string | null | undefined): string[] {
	return value ? value.split('\n') : [''];
}

function serializeFeedbackItems(items: string[]): string {
	return items.filter((item) => item.trim().length > 0).join('\n');
}

function displayFeedbackItems(value: string): string[] {
	return value.split('\n').filter((item) => item.trim().length > 0);
}

function addItem(items: string[]) {
	items.push('');
}

function removeItem(items: string[], index: number) {
	items.splice(index, 1);
	if (items.length === 0) items.push('');
}

function truncate(value: string, length = 90): string {
	const characters = Array.from(value.trim());
	return characters.length > length ? `${characters.slice(0, length - 1).join('')}…` : characters.join('');
}

// A few short verbatim quotes: first point of each answered category, then the general feedback.
function excerpts(entry: FeedbackEntry): string[] {
	const quotes = [entry.positive, entry.improvements, entry.action_items]
		.map((value) => displayFeedbackItems(value)[0])
		.filter((value): value is string => Boolean(value));
	if (entry.notes.trim()) quotes.push(entry.notes.trim());
	return quotes.slice(0, 2).map((quote) => truncate(quote));
}

function ratingHue(value: number): number {
	return Math.round((value / 10) * 120);
}

function ratingStyle(value: number | null): Record<string, string> {
	return value === null
		? {}
		: { '--rating-bg': `hsl(${ratingHue(value)} 65% 28%)` };
}

function ratingLabel(value: number | null): string {
	return value === null ? 'Keine Bewertung abgegeben' : `Bewertung ${value} von 10`;
}

function toggleEntry(index: number) {
	const next = new Set(expanded.value);
	if (next.has(index)) next.delete(index);
	else next.add(index);
	expanded.value = next;
}

function toggleAll() {
	const total = evaluation.value?.feedback.length ?? 0;
	expanded.value = expanded.value.size === total
		? new Set()
		: new Set(Array.from({ length: total }, (_, index) => index));
}

function applyActuals(data: EvaluationResponse) {
	actualParticipants.value = data.actual_participants === null ? '' : String(data.actual_participants);
	actualStartTime.value = data.actual_start_time ?? '';
	actualEndTime.value = data.actual_end_time ?? '';
}

function applyMine(data: EvaluationResponse) {
	rating.value = data.mine?.rating ?? null;
	positiveItems.value = parseFeedbackItems(data.mine?.positive);
	improvementItems.value = parseFeedbackItems(data.mine?.improvements);
	actionItems.value = parseFeedbackItems(data.mine?.action_items);
	notes.value = data.mine?.notes ?? '';
}

async function loadEvaluation() {
	loading.value = true;
	error.value = null;
	try {
		const response = await apiFetch(`/api/activities/${props.activityId}/evaluation`);
		if (!response.ok) throw new Error(await response.text());
		const data = await response.json() as EvaluationResponse;
		evaluation.value = data;
		applyActuals(data);
		applyMine(data);
	} catch (cause) {
		error.value = formatApiError(cause);
	} finally {
		loading.value = false;
	}
}

async function saveActuals() {
	actualsError.value = null;
	actualsSaved.value = false;
	let participantCount: number | null = null;
	if (actualParticipants.value !== '') {
		if (!/^\d+$/.test(actualParticipants.value)) {
			actualsError.value = 'Bitte eine ganze, nicht negative Teilnehmendenzahl eingeben.';
			return;
		}
		participantCount = Number(actualParticipants.value);
		if (!Number.isSafeInteger(participantCount) || participantCount > 2147483647) {
			actualsError.value = 'Die Teilnehmendenzahl ist zu gross.';
			return;
		}
	}

	savingActuals.value = true;
	try {
		const response = await apiFetch(`/api/activities/${props.activityId}/evaluation/actuals`, {
			method: 'PUT',
			body: JSON.stringify({
				actual_participants: participantCount,
				actual_start_time: actualStartTime.value || null,
				actual_end_time: actualEndTime.value || null,
			}),
		});
		if (!response.ok) throw new Error(await response.text());
		const data = await response.json() as EvaluationResponse;
		evaluation.value = data;
		applyActuals(data);
		actualsSaved.value = true;
	} catch (cause) {
		actualsError.value = formatApiError(cause);
	} finally {
		savingActuals.value = false;
	}
}

async function submitFeedback(clear = false) {
	feedbackError.value = null;
	feedbackSaved.value = false;
	const fields: FeedbackFields = clear
		? { rating: null, positive: '', improvements: '', action_items: '', notes: '' }
		: {
			rating: rating.value,
			positive: serializeFeedbackItems(positiveItems.value),
			improvements: serializeFeedbackItems(improvementItems.value),
			action_items: serializeFeedbackItems(actionItems.value),
			notes: notes.value,
		};
	const hasText = [fields.positive, fields.improvements, fields.action_items, fields.notes]
		.some((value) => value.trim().length > 0);
	if (!clear && fields.rating === null && hasText) {
		feedbackError.value = 'Bitte zuerst eine Bewertung von 0 bis 10 wählen.';
		return;
	}
	if ([fields.positive, fields.improvements, fields.action_items, fields.notes]
		.some((value) => Array.from(value).length > 4000)) {
		feedbackError.value = 'Feedback darf höchstens 4000 Zeichen pro Bereich enthalten.';
		return;
	}

	savingFeedback.value = true;
	try {
		const response = await apiFetch(`/api/activities/${props.activityId}/evaluation`, {
			method: 'PUT',
			body: JSON.stringify(fields),
		});
		if (!response.ok) throw new Error(await response.text());
		const data = await response.json() as EvaluationResponse;
		evaluation.value = data;
		expanded.value = new Set();
		applyMine(data);
		feedbackSaved.value = true;
	} catch (cause) {
		feedbackError.value = formatApiError(cause);
	} finally {
		savingFeedback.value = false;
	}
}

watch(() => props.activityId, () => { void loadEvaluation(); });
onMounted(() => { void loadEvaluation(); });

const categories = [
	{ key: 'positive', title: 'Was lief gut?', tone: 'good', items: positiveItems },
	{ key: 'improvement', title: 'Was kann verbessert werden?', tone: 'improve', items: improvementItems },
	{ key: 'action', title: 'Welche Massnahmen nehmen wir mit?', tone: 'action', items: actionItems },
] as const;
</script>

<template>
	<section class="evaluation-panel detail-section" aria-labelledby="evaluation-title">
		<h2 id="evaluation-title" class="detail-section-title">Auswertung</h2>

		<p v-if="loading" class="evaluation-muted" role="status">Auswertung wird geladen…</p>
		<p v-if="error" class="evaluation-error" role="alert">{{ error }}</p>
		<template v-if="evaluation">
			<form
				v-if="evaluation.can_edit_actuals"
				class="evaluation-card evaluation-actuals"
				aria-labelledby="actuals-title"
				@submit.prevent="saveActuals"
			>
				<h3 id="actuals-title">Tatsächlicher Ablauf <span class="evaluation-badge">Für Verantwortliche</span></h3>
				<div class="evaluation-actuals-grid">
					<label class="evaluation-field">
						<span>Teilnehmende (Ist)</span>
						<input
							id="actual-participants"
							v-model="actualParticipants"
							class="evaluation-input evaluation-input--compact"
							type="number"
							min="0"
							max="2147483647"
							step="1"
							inputmode="numeric"
						>
					</label>
					<label class="evaluation-field">
						<span>Tatsächlicher Start</span>
						<input id="actual-start" v-model="actualStartTime" class="evaluation-input" type="time">
					</label>
					<label class="evaluation-field">
						<span>Tatsächliches Ende</span>
						<input id="actual-end" v-model="actualEndTime" class="evaluation-input" type="time">
					</label>
				</div>
				<p class="evaluation-muted">
					Getrennt von der geplanten Teilnehmendenzahl<template v-if="plannedStartTime && plannedEndTime">
						und der geplanten Zeit ({{ plannedStartTime }}–{{ plannedEndTime }})</template>.
				</p>
				<p v-if="actualsError" class="evaluation-error" role="alert">{{ actualsError }}</p>
				<div class="evaluation-actions">
					<button class="btn-primary" type="submit" :disabled="savingActuals || loading">
						{{ savingActuals ? 'Speichern…' : 'Ist-Werte speichern' }}
					</button>
					<span v-if="actualsSaved" class="evaluation-saved" role="status">Gespeichert</span>
				</div>
			</form>
			<dl v-else class="evaluation-card evaluation-actuals-readonly">
				<div>
					<dt>Teilnehmende (Ist)</dt>
					<dd>{{ evaluation.actual_participants ?? 'Noch nicht erfasst' }}</dd>
				</div>
				<div>
					<dt>Tatsächlicher Ablauf</dt>
					<dd v-if="evaluation.actual_start_time || evaluation.actual_end_time">
						{{ evaluation.actual_start_time ?? '–' }}–{{ evaluation.actual_end_time ?? '–' }}
					</dd>
					<dd v-else>Noch nicht erfasst</dd>
				</div>
			</dl>

			<form class="evaluation-form" aria-labelledby="feedback-title" @submit.prevent="submitFeedback(false)">
				<h3 id="feedback-title">Deine Rückmeldung</h3>
				<fieldset class="evaluation-card evaluation-rating">
					<legend>Wie gut war die Aktivität?</legend>
					<div class="evaluation-rating-scale" role="radiogroup" aria-label="Bewertung von 0 bis 10">
						<label
							v-for="value in ratingScale"
							:key="value"
							class="evaluation-rating-option"
							:class="{ 'is-selected': rating === value }"
							:style="ratingStyle(value)"
						>
							<input v-model="rating" type="radio" name="activity-rating" :value="value">
							<span>{{ value }}</span>
						</label>
					</div>
					<p class="evaluation-muted evaluation-scale-hint"><span>0 = gar nicht gut</span><span>10 = ausgezeichnet</span></p>
				</fieldset>

				<fieldset
					v-for="category in categories"
					:key="category.key"
					class="evaluation-card evaluation-group"
					:class="`evaluation-group--${category.tone}`"
				>
					<legend>{{ category.title }}</legend>
					<div v-for="(item, index) in category.items.value" :key="`${category.key}-${index}`" class="evaluation-item-row">
						<input
							:id="`${category.key}-${index}`"
							v-model="category.items.value[index]"
							class="evaluation-input"
							type="text"
							maxlength="4000"
							:aria-label="`${category.title} Punkt ${index + 1}`"
						>
						<button
							class="btn-secondary evaluation-remove-button"
							type="button"
							:aria-label="`Punkt ${index + 1} bei ${category.title} entfernen`"
							@click="removeItem(category.items.value, index)"
						>✕</button>
					</div>
					<button class="btn-secondary evaluation-add-button" type="button" @click="addItem(category.items.value)">
						+ Weiteren Punkt hinzufügen
					</button>
				</fieldset>

				<div class="evaluation-card">
					<label class="evaluation-field">
						<span class="evaluation-card-title">Weitere Rückmeldung (optional)</span>
						<textarea v-model="notes" class="evaluation-input" maxlength="4000" rows="3" />
					</label>
				</div>

				<p v-if="feedbackError" class="evaluation-error" role="alert">{{ feedbackError }}</p>
				<div class="evaluation-actions">
					<button class="btn-primary" type="submit" :disabled="savingFeedback || loading">
						{{ savingFeedback ? 'Speichern…' : 'Rückmeldung speichern' }}
					</button>
					<button
						v-if="evaluation.mine"
						class="btn-secondary"
						type="button"
						:disabled="savingFeedback || loading"
						@click="submitFeedback(true)"
					>Meine Rückmeldung löschen</button>
					<span v-if="feedbackSaved" class="evaluation-saved" role="status">Gespeichert</span>
				</div>
			</form>

			<div class="evaluation-submissions">
				<div class="evaluation-submissions-head">
					<h3>Gesammelte Rückmeldungen</h3>
					<p class="evaluation-summary">
						{{ evaluation.feedback.length }} {{ evaluation.feedback.length === 1 ? 'Rückmeldung' : 'Rückmeldungen' }} gesammelt<template v-if="averageRating">
							· Ø {{ averageRating }} / 10</template>
					</p>
					<button
						v-if="evaluation.feedback.length"
						class="btn-secondary"
						type="button"
						@click="toggleAll"
					>{{ expanded.size === evaluation.feedback.length ? 'Alle zuklappen' : 'Alle aufklappen' }}</button>
				</div>
				<p v-if="!evaluation.feedback.length" class="evaluation-muted">
					Noch keine Rückmeldungen erfasst.
				</p>
				<div v-else class="evaluation-table-wrap" role="region" aria-label="Gesammelte Rückmeldungen" tabindex="0">
					<table class="evaluation-table">
						<thead>
							<tr>
								<th scope="col">Person</th>
								<th scope="col">Bewertung</th>
								<th scope="col">Auszug</th>
								<th scope="col"><span class="visually-hidden">Details</span></th>
							</tr>
						</thead>
						<tbody>
							<template v-for="(entry, index) in evaluation.feedback" :key="`${entry.author}-${index}`">
								<tr class="evaluation-row" :class="{ 'is-open': expanded.has(index) }" @click="toggleEntry(index)">
									<th scope="row">{{ entry.author }}</th>
									<td>
										<span
											class="evaluation-score"
											:class="{ 'evaluation-score--none': entry.rating === null }"
											:style="ratingStyle(entry.rating)"
											:aria-label="ratingLabel(entry.rating)"
										>{{ entry.rating ?? '–' }}</span>
									</td>
									<td class="evaluation-excerpt">
										<q v-for="(quote, quoteIndex) in excerpts(entry)" :key="quoteIndex">{{ quote }}</q>
										<span v-if="!excerpts(entry).length" class="evaluation-muted">Kein Text</span>
									</td>
									<td class="evaluation-toggle-cell">
										<button
											class="btn-secondary evaluation-toggle"
											type="button"
											:aria-expanded="expanded.has(index)"
											:aria-controls="`evaluation-details-${index}`"
											:aria-label="`Details von ${entry.author} ${expanded.has(index) ? 'zuklappen' : 'aufklappen'}`"
											@click.stop="toggleEntry(index)"
										>{{ expanded.has(index) ? '▲' : '▼' }}</button>
									</td>
								</tr>
								<tr v-if="expanded.has(index)" :id="`evaluation-details-${index}`" class="evaluation-details">
									<td colspan="4">
										<dl class="evaluation-entry">
											<div v-if="displayFeedbackItems(entry.positive).length">
												<dt>Was lief gut?</dt>
												<dd><ul><li v-for="(item, itemIndex) in displayFeedbackItems(entry.positive)" :key="itemIndex">{{ item }}</li></ul></dd>
											</div>
											<div v-if="displayFeedbackItems(entry.improvements).length">
												<dt>Verbesserungspunkte</dt>
												<dd><ul><li v-for="(item, itemIndex) in displayFeedbackItems(entry.improvements)" :key="itemIndex">{{ item }}</li></ul></dd>
											</div>
											<div v-if="displayFeedbackItems(entry.action_items).length">
												<dt>Massnahmen</dt>
												<dd><ul><li v-for="(item, itemIndex) in displayFeedbackItems(entry.action_items)" :key="itemIndex">{{ item }}</li></ul></dd>
											</div>
											<div v-if="entry.notes">
												<dt>Weitere Rückmeldung</dt>
												<dd>{{ entry.notes }}</dd>
											</div>
										</dl>
										<p class="evaluation-muted">Zuletzt geändert: {{ new Date(entry.updated_at).toLocaleString('de-DE') }}</p>
									</td>
								</tr>
							</template>
						</tbody>
					</table>
				</div>
			</div>
		</template>
	</section>
</template>

<style scoped>
.evaluation-panel {
	display: grid;
	gap: 1rem;
}

.evaluation-panel h3 {
	font-size: 1rem;
	margin: 0;
}

.evaluation-muted {
	color: var(--text-muted, #6b7280);
	font-size: 0.88rem;
	margin: 0;
}

.evaluation-error {
	color: var(--error-color, #991b1b);
	margin: 0;
}

.evaluation-card {
	background: var(--bg-elevated, #f9fafb);
	border: 1px solid var(--card-border, #e5e7eb);
	border-radius: 10px;
	display: grid;
	gap: 0.65rem;
	margin: 0;
	min-width: 0;
	padding: 0.9rem 1rem;
}

.evaluation-card legend,
.evaluation-card-title {
	font-weight: 600;
	padding: 0 0.25rem;
}

.evaluation-badge {
	background: var(--chip-bg, #dbeafe);
	border-radius: 999px;
	color: var(--chip-color, #1e40af);
	font-size: 0.72rem;
	font-weight: 600;
	margin-left: 0.4rem;
	padding: 0.1rem 0.5rem;
}

.evaluation-form {
	display: grid;
	gap: 0.75rem;
}

.evaluation-actuals-grid {
	display: flex;
	flex-wrap: wrap;
	gap: 0.75rem 1rem;
}

.evaluation-field {
	display: grid;
	font-size: 0.88rem;
	gap: 0.3rem;
}

.evaluation-field > span:first-child {
	color: var(--text-secondary, #374151);
	font-weight: 600;
}

.evaluation-input {
	background: var(--input-bg, #fff);
	border: 1.5px solid var(--input-border, #d1d5db);
	border-radius: 6px;
	box-sizing: border-box;
	color: var(--input-color, #1a202c);
	font: inherit;
	padding: 0.45rem 0.6rem;
	width: 100%;
}

.evaluation-input--compact {
	width: 6.5rem;
}

.evaluation-actuals-grid .evaluation-input:not(.evaluation-input--compact) {
	width: 8.5rem;
}

.evaluation-input:focus {
	border-color: var(--focus-border, #0080ff);
	box-shadow: var(--focus-ring, 0 0 0 3px rgba(0, 128, 255, 0.15));
	outline: none;
}

textarea.evaluation-input {
	display: block;
	resize: vertical;
}

.evaluation-actuals-readonly {
	grid-template-columns: repeat(auto-fit, minmax(10rem, 1fr));
}

.evaluation-actuals-readonly dt {
	color: var(--text-muted, #6b7280);
	font-size: 0.82rem;
}

.evaluation-actuals-readonly dd {
	font-size: 1.05rem;
	font-weight: 600;
	margin: 0.15rem 0 0;
}

.evaluation-rating-scale {
	display: grid;
	gap: 0.35rem;
	grid-template-columns: repeat(11, minmax(0, 1fr));
}

.evaluation-rating-option {
	cursor: pointer;
	display: block;
	position: relative;
}

.evaluation-rating-option input {
	height: 1px;
	opacity: 0;
	position: absolute;
	width: 1px;
}

.evaluation-rating-option span {
	background: var(--bg-surface, #fff);
	border: 2px solid var(--rating-bg, var(--input-border, #d1d5db));
	border-radius: 8px;
	display: block;
	font-weight: 600;
	padding: 0.45rem 0;
	text-align: center;
}

.evaluation-rating-option.is-selected span {
	background: var(--rating-bg);
	color: #fff;
}

.evaluation-rating-option input:focus-visible + span {
	box-shadow: var(--focus-ring, 0 0 0 3px rgba(0, 128, 255, 0.3));
	outline: 2px solid var(--focus-border, #0080ff);
	outline-offset: 2px;
}

.evaluation-scale-hint {
	display: flex;
	justify-content: space-between;
}

.evaluation-group {
	border-left-width: 5px;
}

.evaluation-group--good { border-left-color: #16a34a; }
.evaluation-group--improve { border-left-color: #d97706; }
.evaluation-group--action { border-left-color: var(--accent, #0080ff); }

.evaluation-item-row {
	display: grid;
	gap: 0.5rem;
	grid-template-columns: minmax(0, 1fr) auto;
}

.evaluation-add-button,
.evaluation-remove-button {
	font: inherit;
	padding: 0.4rem 0.65rem;
}

.evaluation-add-button {
	justify-self: start;
}

.evaluation-actions {
	align-items: center;
	display: flex;
	flex-wrap: wrap;
	gap: 0.75rem;
}

.evaluation-saved {
	color: var(--success-color, #065f46);
}

.evaluation-submissions {
	display: grid;
	gap: 0.75rem;
}

.evaluation-submissions-head {
	align-items: center;
	display: flex;
	flex-wrap: wrap;
	gap: 0.5rem 1rem;
}

.evaluation-summary {
	color: var(--text-secondary, #374151);
	flex: 1;
	font-size: 0.9rem;
	margin: 0;
}

.evaluation-table-wrap {
	border: 1px solid var(--card-border, #e5e7eb);
	border-radius: 10px;
	overflow-x: auto;
}

.evaluation-table {
	border-collapse: collapse;
	width: 100%;
}

.evaluation-table th,
.evaluation-table td {
	padding: 0.6rem 0.75rem;
	text-align: left;
	vertical-align: top;
}

.evaluation-table thead th {
	background: var(--bg-elevated, #f9fafb);
	font-size: 0.82rem;
}

.evaluation-row {
	border-top: 1px solid var(--card-border, #e5e7eb);
	cursor: pointer;
}

.evaluation-row:hover,
.evaluation-row.is-open {
	background: var(--bg-hover, #f3f4f6);
}

.evaluation-score {
	background: var(--rating-bg, #6b7280);
	border-radius: 999px;
	color: #fff;
	display: inline-block;
	font-weight: 700;
	min-width: 2.2rem;
	padding: 0.15rem 0.5rem;
	text-align: center;
}

.evaluation-excerpt q {
	display: block;
	overflow-wrap: anywhere;
}

.evaluation-excerpt q + q {
	margin-top: 0.2rem;
}

.evaluation-toggle-cell {
	text-align: right !important;
	width: 1%;
}

.evaluation-toggle {
	padding: 0.25rem 0.55rem;
}

.evaluation-details td {
	background: var(--bg-elevated, #f9fafb);
}

.evaluation-entry {
	display: grid;
	gap: 0.5rem;
	margin: 0 0 0.5rem;
}

.evaluation-entry dt {
	font-weight: 600;
}

.evaluation-entry dd {
	margin: 0.2rem 0 0;
	overflow-wrap: anywhere;
}

.evaluation-entry ul {
	margin: 0;
	padding-left: 1.25rem;
}

.visually-hidden {
	clip: rect(0 0 0 0);
	height: 1px;
	overflow: hidden;
	position: absolute;
	white-space: nowrap;
	width: 1px;
}

@media (max-width: 640px) {
	.evaluation-rating-scale {
		grid-template-columns: repeat(6, minmax(0, 1fr));
	}

	.evaluation-actions button {
		width: 100%;
	}
}
</style>
