<template>
	<div v-if="stats" class="form-stats">
		<!-- Summary -->
		<div class="stats-summary">
			<div class="stat-card">
				<span class="stat-value">{{ stats.total }}</span>
				<span class="stat-label">Antworten gesamt</span>
			</div>
			<div class="stat-card">
				<span class="stat-value mode-badge" :data-type="stats.form_type">
					{{ stats.form_type === 'registration' ? 'Anmeldung' : 'Abmeldung' }}
				</span>
				<span class="stat-label">Aktueller Modus</span>
			</div>
			<template v-if="stats.by_mode">
				<div v-for="(count, mode) in stats.by_mode" :key="mode" class="stat-card small">
					<span class="stat-value">{{ count }}</span>
					<span class="stat-label">{{ mode === 'registration' ? 'Anmeldungen' : 'Abmeldungen' }}</span>
				</div>
			</template>
		</div>

		<!-- Per-question breakdown -->
		<div
			v-for="(qstat, qid) in stats.questions"
			:key="qid"
			class="question-stat"
		>
			<div class="question-stat-header">
				<span class="question-stat-title">{{ qstat.question_text }}</span>
				<span class="question-stat-type">{{ typeLabel(qstat.question_type) }}</span>
			</div>

			<!-- Text questions: just list answers -->
			<template v-if="qstat.question_type === 'text_input'">
				<div class="text-answers">
					<div
						v-for="(count, val) in qstat.answers"
						:key="val"
						class="text-answer-row"
					>
						<span class="text-answer-val">{{ val }}</span>
						<span class="text-answer-count">{{ count }}×</span>
					</div>
				</div>
			</template>

			<!-- Choice questions: bar chart -->
			<template v-else>
				<div class="bar-chart">
					<div
						v-for="(count, val) in sortedAnswers(qstat.answers)"
						:key="val"
						class="bar-row"
					>
						<span class="bar-label">{{ optionLabel(qid as string, val as string) }}</span>
						<div class="bar-track">
							<div
								class="bar-fill"
								:style="{ width: barWidth(count as number, qstat.answers) + '%' }"
							/>
						</div>
						<span class="bar-count">{{ count }} ({{ barPct(count as number, qstat.answers) }}%)</span>
					</div>
				</div>
			</template>
		</div>

		<div v-if="Object.keys(stats.questions).length === 0 && stats.total > 0" class="no-questions">
			Keine Fragen-Statistiken verfügbar.
		</div>
	</div>

	<div v-else class="no-stats">
		Noch keine Statistiken verfügbar.
	</div>
</template>

<script setup lang="ts">
import type { FormStats, SignupForm } from '../types';

const props = defineProps<{
	stats: FormStats | null;
	form?: SignupForm | null;
}>();

function typeLabel(type: string): string {
	const m: Record<string, string> = {
		text_input: 'Freitext',
		single_choice: 'Single Choice',
		multiple_choice: 'Multiple Choice',
		dropdown: 'Dropdown',
	};
	return m[type] ?? type;
}

function sortedAnswers(answers: Record<string, number>): Record<string, number> {
	return Object.fromEntries(
		Object.entries(answers).sort(([, a], [, b]) => b - a),
	);
}

function total(answers: Record<string, number>): number {
	return Object.values(answers).reduce((s, v) => s + v, 0);
}

function barWidth(count: number, answers: Record<string, number>): number {
	const t = total(answers);
	return t === 0 ? 0 : Math.round((count / t) * 100);
}

function barPct(count: number, answers: Record<string, number>): number {
	return barWidth(count, answers);
}

function optionLabel(questionId: string, optionId: string): string {
	if (!props.form) return optionId;
	const q = props.form.questions.find((q) => q.id === questionId);
	if (!q?.metadata.choices) return optionId;
	const opt = q.metadata.choices.find((o) => o.id === optionId);
	return opt?.label ?? optionId;
}
</script>

<style scoped>
.form-stats {
	display: flex;
	flex-direction: column;
	gap: 1.25rem;
}

.stats-summary {
	display: flex;
	gap: 0.75rem;
	flex-wrap: wrap;
}

.stat-card {
	background: #f9fafb;
	border: 1px solid #e5e7eb;
	border-radius: 0.5rem;
	padding: 0.75rem 1.25rem;
	display: flex;
	flex-direction: column;
	align-items: center;
	min-width: 100px;
}

.stat-value {
	font-size: 1.5rem;
	font-weight: 700;
	color: #111827;
}

.stat-label {
	font-size: 0.72rem;
	color: #6b7280;
	text-transform: uppercase;
	letter-spacing: 0.04em;
	margin-top: 0.2rem;
}

.mode-badge {
	font-size: 0.9rem;
	font-weight: 600;
	padding: 0.15rem 0.6rem;
	border-radius: 9999px;
	background: #d1fae5;
	color: #065f46;
}
.mode-badge[data-type="deregistration"] {
	background: #fee2e2;
	color: #991b1b;
}

.stat-card.small .stat-value { font-size: 1.1rem; }

.question-stat {
	background: #fff;
	border: 1px solid #e5e7eb;
	border-radius: 0.5rem;
	padding: 1rem 1.25rem;
}

.question-stat-header {
	display: flex;
	justify-content: space-between;
	align-items: center;
	margin-bottom: 0.75rem;
}

.question-stat-title {
	font-weight: 600;
	font-size: 0.9rem;
	color: #111827;
}

.question-stat-type {
	font-size: 0.72rem;
	color: #6b7280;
	background: #f3f4f6;
	padding: 0.1rem 0.4rem;
	border-radius: 9999px;
}

.bar-chart {
	display: flex;
	flex-direction: column;
	gap: 0.4rem;
}

.bar-row {
	display: grid;
	grid-template-columns: 10rem 1fr 5rem;
	align-items: center;
	gap: 0.5rem;
}

.bar-label {
	font-size: 0.85rem;
	color: #374151;
	white-space: nowrap;
	overflow: hidden;
	text-overflow: ellipsis;
}

.bar-track {
	height: 1.1rem;
	background: #e5e7eb;
	border-radius: 9999px;
	overflow: hidden;
}

.bar-fill {
	height: 100%;
	background: #6366f1;
	border-radius: 9999px;
	transition: width 0.3s ease;
}

.bar-count {
	font-size: 0.78rem;
	color: #6b7280;
	text-align: right;
}

.text-answers {
	display: flex;
	flex-direction: column;
	gap: 0.25rem;
}

.text-answer-row {
	display: flex;
	justify-content: space-between;
	font-size: 0.85rem;
	padding: 0.25rem 0.5rem;
	background: #f9fafb;
	border-radius: 0.25rem;
}

.text-answer-val { color: #374151; }
.text-answer-count { color: #6b7280; }

.no-stats, .no-questions {
	text-align: center;
	color: #9ca3af;
	font-size: 0.875rem;
	padding: 2rem;
}
</style>
