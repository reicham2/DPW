<script setup lang="ts">
import { onMounted, ref, watch } from 'vue';
import { apiFetch, formatApiError } from '../composables/useApi';

interface FeedbackEntry {
	author: string;
	positive: string;
	improvements: string;
	action_items: string;
	notes: string;
	created_at: string;
	updated_at: string;
}

interface EvaluationResponse {
	actual_participants: number | null;
	feedback: FeedbackEntry[];
	mine: Omit<FeedbackEntry, 'author' | 'created_at' | 'updated_at'> | null;
}

const props = defineProps<{
	activityId: string;
	canEdit: boolean;
}>();

const evaluation = ref<EvaluationResponse | null>(null);
const actualParticipants = ref('');
const positiveItems = ref(['']);
const improvementItems = ref(['']);
const actionItems = ref(['']);
const notes = ref('');
const loading = ref(false);
const saving = ref(false);
const error = ref<string | null>(null);
const saved = ref(false);

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

function applyEvaluation(data: EvaluationResponse) {
	evaluation.value = data;
	actualParticipants.value = data.actual_participants === null ? '' : String(data.actual_participants);
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
		applyEvaluation(await response.json() as EvaluationResponse);
	} catch (cause) {
		error.value = formatApiError(cause);
	} finally {
		loading.value = false;
	}
}

async function saveEvaluation() {
	error.value = null;
	saved.value = false;
	let participantCount: number | null = null;
	if (actualParticipants.value !== '') {
		if (!/^\d+$/.test(actualParticipants.value)) {
			error.value = 'Bitte eine ganze, nicht negative Teilnehmendenzahl eingeben.';
			return;
		}
		participantCount = Number(actualParticipants.value);
		if (!Number.isSafeInteger(participantCount) || participantCount > 2147483647) {
			error.value = 'Die Teilnehmendenzahl ist zu gross.';
			return;
		}
	}

	const feedbackFields = {
		positive: serializeFeedbackItems(positiveItems.value),
		improvements: serializeFeedbackItems(improvementItems.value),
		action_items: serializeFeedbackItems(actionItems.value),
		notes: notes.value,
	};
	if (Object.values(feedbackFields).some((value) => Array.from(value).length > 4000)) {
		error.value = 'Feedback darf höchstens 4000 Zeichen pro Bereich enthalten.';
		return;
	}

	saving.value = true;
	try {
		const response = await apiFetch(`/api/activities/${props.activityId}/evaluation`, {
			method: 'PUT',
			body: JSON.stringify({
				actual_participants: participantCount,
				...feedbackFields,
			}),
		});
		if (!response.ok) throw new Error(await response.text());
		applyEvaluation(await response.json() as EvaluationResponse);
		saved.value = true;
	} catch (cause) {
		error.value = formatApiError(cause);
	} finally {
		saving.value = false;
	}
}

watch(() => props.activityId, () => { void loadEvaluation(); });
onMounted(() => { void loadEvaluation(); });
</script>

<template>
	<section class="evaluation-panel detail-section" aria-labelledby="evaluation-title">
		<h2 id="evaluation-title" class="detail-section-title">Auswertung</h2>
		<p class="evaluation-intro">
			Erfasst die tatsächliche Teilnehmendenzahl und sammelt Rückmeldungen zu dieser Aktivität.
		</p>

		<p v-if="loading" class="evaluation-muted" role="status">Auswertung wird geladen…</p>
		<p v-if="error" class="evaluation-error" role="alert">{{ error }}</p>
		<template v-if="evaluation">
			<div class="evaluation-count">
				<label v-if="canEdit" for="actual-participants">Tatsächliche Anzahl Teilnehmende</label>
				<span v-else class="evaluation-label">Tatsächliche Anzahl Teilnehmende</span>
				<input
					v-if="canEdit"
					id="actual-participants"
					class="evaluation-count-input"
					v-model="actualParticipants"
					type="number"
					min="0"
					max="2147483647"
					step="1"
					inputmode="numeric"
					placeholder="Noch nicht erfasst"
				>
				<strong v-else>{{ evaluation.actual_participants ?? 'Noch nicht erfasst' }}</strong>
				<p class="evaluation-muted">Getrennt von der geplanten Teilnehmendenzahl.</p>
			</div>
			<p class="evaluation-summary">
				{{ evaluation.feedback.length }} {{ evaluation.feedback.length === 1 ? 'Rückmeldung' : 'Rückmeldungen' }} gesammelt
			</p>

			<form v-if="canEdit" class="evaluation-form" @submit.prevent="saveEvaluation">
				<fieldset class="evaluation-group">
					<legend>Was lief gut?</legend>
					<div v-for="(item, index) in positiveItems" :key="`positive-${index}`" class="evaluation-item-row">
						<label class="evaluation-item-label" :for="`positive-${index}`">Punkt {{ index + 1 }}</label>
						<input
							:id="`positive-${index}`"
							v-model="positiveItems[index]"
							class="evaluation-item-input"
							type="text"
							maxlength="4000"
						>
						<button
							class="btn-secondary evaluation-remove-button"
							type="button"
							:aria-label="`Punkt ${index + 1} bei Was lief gut entfernen`"
							@click="removeItem(positiveItems, index)"
						>Entfernen</button>
					</div>
					<button class="btn-secondary evaluation-add-button" type="button" @click="addItem(positiveItems)">
						Weiteren Punkt hinzufügen
					</button>
				</fieldset>
				<fieldset class="evaluation-group">
					<legend>Was kann verbessert werden?</legend>
					<div v-for="(item, index) in improvementItems" :key="`improvement-${index}`" class="evaluation-item-row">
						<label class="evaluation-item-label" :for="`improvement-${index}`">Punkt {{ index + 1 }}</label>
						<input
							:id="`improvement-${index}`"
							v-model="improvementItems[index]"
							class="evaluation-item-input"
							type="text"
							maxlength="4000"
						>
						<button
							class="btn-secondary evaluation-remove-button"
							type="button"
							:aria-label="`Punkt ${index + 1} bei Was kann verbessert werden entfernen`"
							@click="removeItem(improvementItems, index)"
						>Entfernen</button>
					</div>
					<button class="btn-secondary evaluation-add-button" type="button" @click="addItem(improvementItems)">
						Weiteren Punkt hinzufügen
					</button>
				</fieldset>
				<fieldset class="evaluation-group">
					<legend>Welche Massnahmen nehmen wir mit?</legend>
					<div v-for="(item, index) in actionItems" :key="`action-${index}`" class="evaluation-item-row">
						<label class="evaluation-item-label" :for="`action-${index}`">Punkt {{ index + 1 }}</label>
						<input
							:id="`action-${index}`"
							v-model="actionItems[index]"
							class="evaluation-item-input"
							type="text"
							maxlength="4000"
						>
						<button
							class="btn-secondary evaluation-remove-button"
							type="button"
							:aria-label="`Punkt ${index + 1} bei Welche Massnahmen nehmen wir mit entfernen`"
							@click="removeItem(actionItems, index)"
						>Entfernen</button>
					</div>
					<button class="btn-secondary evaluation-add-button" type="button" @click="addItem(actionItems)">
						Weiteren Punkt hinzufügen
					</button>
				</fieldset>
				<label>
					Weitere Rückmeldung (optional)
					<textarea v-model="notes" maxlength="4000" rows="3" />
				</label>
				<div class="evaluation-actions">
					<button class="btn-primary" type="submit" :disabled="saving || loading">
						{{ saving ? 'Speichern…' : 'Auswertung speichern' }}
					</button>
					<span v-if="saved" class="evaluation-saved" role="status">Gespeichert</span>
				</div>
			</form>

			<div class="evaluation-submissions">
				<h3>Gesammelte Rückmeldungen</h3>
				<p v-if="!evaluation.feedback.length" class="evaluation-muted">
					Noch keine Rückmeldungen erfasst.
				</p>
				<article v-for="(entry, index) in evaluation.feedback" :key="`${entry.author}-${index}`" class="evaluation-entry">
					<h4>{{ entry.author }}</h4>
					<dl>
						<div v-if="displayFeedbackItems(entry.positive).length">
							<dt>Was lief gut?</dt>
							<dd>
								<ul>
									<li v-for="(item, itemIndex) in displayFeedbackItems(entry.positive)" :key="itemIndex">
										{{ item }}
									</li>
								</ul>
							</dd>
						</div>
						<div v-if="displayFeedbackItems(entry.improvements).length">
							<dt>Verbesserungspunkte</dt>
							<dd>
								<ul>
									<li v-for="(item, itemIndex) in displayFeedbackItems(entry.improvements)" :key="itemIndex">
										{{ item }}
									</li>
								</ul>
							</dd>
						</div>
						<div v-if="displayFeedbackItems(entry.action_items).length">
							<dt>Massnahmen</dt>
							<dd>
								<ul>
									<li v-for="(item, itemIndex) in displayFeedbackItems(entry.action_items)" :key="itemIndex">
										{{ item }}
									</li>
								</ul>
							</dd>
						</div>
						<div v-if="entry.notes">
							<dt>Weitere Rückmeldung</dt>
							<dd>{{ entry.notes }}</dd>
						</div>
					</dl>
					<p class="evaluation-muted">Zuletzt geändert: {{ new Date(entry.updated_at).toLocaleString('de-DE') }}</p>
				</article>
			</div>
		</template>
	</section>
</template>

<style scoped>
.evaluation-panel {
	display: grid;
	gap: 1rem;
}

.evaluation-intro,
.evaluation-muted {
	color: var(--text-muted, #6b7280);
	margin: 0;
}

.evaluation-error {
	color: var(--color-danger, #b91c1c);
	margin: 0;
}

.evaluation-count,
.evaluation-form,
.evaluation-submissions {
	display: grid;
	gap: 0.75rem;
}

.evaluation-count label,
.evaluation-label,
.evaluation-group legend,
.evaluation-form > label {
	font-weight: 600;
}

.evaluation-count-input {
	max-width: 100%;
	width: 9rem;
}

.evaluation-count-input,
.evaluation-item-input,
.evaluation-form textarea {
	box-sizing: border-box;
	border: 1.5px solid var(--input-border, #d1d5db);
	border-radius: 6px;
	background: var(--input-bg, #fff);
	color: var(--input-color, #1a202c);
	font: inherit;
	padding: 0.45rem 0.6rem;
}

.evaluation-count-input:focus,
.evaluation-item-input:focus,
.evaluation-form textarea:focus {
	border-color: var(--focus-border, #0080ff);
	box-shadow: var(--focus-ring, 0 0 0 3px rgba(0, 128, 255, 0.15));
	outline: none;
}

.evaluation-form textarea {
	display: block;
	font-weight: 400;
	margin-top: 0.35rem;
	resize: vertical;
	width: 100%;
}

.evaluation-summary {
	color: var(--text-secondary, #374151);
	font-size: 0.9rem;
	margin: 0;
}

.evaluation-group {
	border: 0;
	display: grid;
	gap: 0.6rem;
	margin: 0;
	min-width: 0;
	padding: 0;
}

.evaluation-group legend {
	margin-bottom: 0.35rem;
	padding: 0;
}

.evaluation-item-row {
	align-items: center;
	display: grid;
	gap: 0.5rem;
	grid-template-columns: auto minmax(0, 1fr) auto;
}

.evaluation-item-label {
	color: var(--text-muted, #6b7280);
	font-size: 0.82rem;
	font-weight: 500;
	white-space: nowrap;
}

.evaluation-item-input {
	min-width: 0;
	width: 100%;
}

.evaluation-add-button,
.evaluation-remove-button {
	font: inherit;
	padding: 0.4rem 0.65rem;
}

.evaluation-add-button {
	justify-self: start;
}

.evaluation-submissions h3,
.evaluation-entry h4 {
	margin: 0;
}

.evaluation-entry {
	border-top: 1px solid var(--border-color, #d1d5db);
	padding-top: 0.75rem;
}

.evaluation-entry dl {
	display: grid;
	gap: 0.5rem;
	margin: 0.75rem 0;
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

.evaluation-entry li + li {
	margin-top: 0.2rem;
}

.evaluation-actions {
	align-items: center;
	display: flex;
	flex-wrap: wrap;
	gap: 0.75rem;
}

.evaluation-saved {
	color: var(--color-success, #15803d);
}

@media (max-width: 640px) {
	.evaluation-actions button {
		width: 100%;
	}

	.evaluation-item-row {
		grid-template-columns: minmax(0, 1fr) auto;
	}

	.evaluation-item-label {
		grid-column: 1 / -1;
	}
}
</style>
