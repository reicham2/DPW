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
const positive = ref('');
const improvements = ref('');
const actionItems = ref('');
const notes = ref('');
const loading = ref(false);
const saving = ref(false);
const error = ref<string | null>(null);
const saved = ref(false);

function applyEvaluation(data: EvaluationResponse) {
	evaluation.value = data;
	actualParticipants.value = data.actual_participants === null ? '' : String(data.actual_participants);
	positive.value = data.mine?.positive ?? '';
	improvements.value = data.mine?.improvements ?? '';
	actionItems.value = data.mine?.action_items ?? '';
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

	saving.value = true;
	try {
		const response = await apiFetch(`/api/activities/${props.activityId}/evaluation`, {
			method: 'PUT',
			body: JSON.stringify({
				actual_participants: participantCount,
				positive: positive.value,
				improvements: improvements.value,
				action_items: actionItems.value,
				notes: notes.value,
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

			<form v-if="canEdit" class="evaluation-form" @submit.prevent="saveEvaluation">
				<label>
					Was lief gut?
					<textarea v-model="positive" maxlength="4000" rows="3" />
				</label>
				<label>
					Was kann verbessert werden?
					<textarea v-model="improvements" maxlength="4000" rows="3" />
				</label>
				<label>
					Welche Massnahmen nehmen wir mit?
					<textarea v-model="actionItems" maxlength="4000" rows="3" />
				</label>
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
						<div v-if="entry.positive">
							<dt>Was lief gut?</dt>
							<dd>{{ entry.positive }}</dd>
						</div>
						<div v-if="entry.improvements">
							<dt>Verbesserungspunkte</dt>
							<dd>{{ entry.improvements }}</dd>
						</div>
						<div v-if="entry.action_items">
							<dt>Massnahmen</dt>
							<dd>{{ entry.action_items }}</dd>
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
.evaluation-form label {
	font-weight: 600;
}

.evaluation-count input,
.evaluation-form textarea {
	box-sizing: border-box;
	font: inherit;
	padding: 0.55rem 0.65rem;
	width: 100%;
}

.evaluation-form textarea {
	display: block;
	font-weight: 400;
	margin-top: 0.35rem;
	resize: vertical;
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
	white-space: pre-wrap;
	overflow-wrap: anywhere;
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
}
</style>
