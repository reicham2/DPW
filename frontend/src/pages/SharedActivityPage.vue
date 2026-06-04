<template>
	<div class="shared-page">
		<!-- Loading -->
		<div v-if="loading" class="loading-state">
			<span class="spinner" />
			Aktivität wird geladen…
		</div>

		<!-- Not found -->
		<div v-else-if="notFound" class="error-state">
			<div class="error-icon"><Search :size="28" aria-hidden="true" /></div>
			<h2>Nicht gefunden</h2>
			<p>Diese Aktivität existiert nicht oder der Link ist nicht mehr gültig.</p>
		</div>

		<!-- Activity view -->
		<template v-else-if="activity">
			<main class="main">
				<div class="detail-view">
					<!-- Hero -->
					<div class="detail-section">
						<div class="detail-hero">
							<div>
								<h2 class="detail-hero-title">{{ activity.title }}</h2>
								<p class="detail-hero-time">
									{{ formatDate(activity.date) }} &middot;
									{{ activity.start_time }}–{{ activity.end_time }}
								</p>
							</div>
						</div>
					</div>

					<!-- Ort / Verantwortlich / Stufe -->
					<div class="detail-section">
						<div class="detail-grid detail-grid--3">
							<div class="detail-field">
								<span class="detail-label">Ort</span>
								<span class="detail-value">{{ activity.location || '—' }}</span>
							</div>
							<div class="detail-field">
								<span class="detail-label">Verantwortlich</span>
								<span class="detail-value">{{ activity.responsible.length ? activity.responsible.join(', ') : '—' }}</span>
							</div>
							<div class="detail-field">
								<span class="detail-label">Stufe</span>
								<span class="detail-value">
									<DepartmentBadge v-if="activity.department" :department="activity.department" />
									<template v-else>—</template>
								</span>
							</div>
						</div>
					</div>

					<!-- Programmpunkte -->
					<div class="detail-section">
						<p class="detail-section-title">Programmpunkte</p>
						<div v-if="activity.programs.length" class="program-timeline">
							<div
								v-for="(prog, pi) in activity.programs"
								:key="prog.id"
								class="program-item"
							>
								<div class="program-marker" aria-hidden="true">
									<span class="program-marker-dot" />
								</div>
								<div class="program-body">
									<div class="program-meta">
										<span class="program-time">{{ programLabel(pi) }}</span>
										<span v-if="programSecondaryLabel(pi)" class="program-time-secondary">{{ programSecondaryLabel(pi) }}</span>
									</div>
									<div class="program-header">
										<p class="program-title">{{ prog.title }}</p>
									</div>
									<p v-if="prog.responsible.length" class="program-resp">Leitung: {{ prog.responsible.join(', ') }}</p>
									<div v-if="prog.description" class="program-desc" v-html="sanitizeHtml(prog.description)" />
								</div>
							</div>
						</div>
						<span v-else class="detail-value detail-value--muted">—</span>
					</div>

					<!-- Material -->
					<div class="detail-section">
						<p class="detail-section-title">Material</p>
						<ul v-if="activity.material.length" class="material-list-view">
							<li v-for="(m, i) in activity.material" :key="i" class="material-list-item">
								<span class="material-list-name">{{ m.name }}</span>
								<span v-if="m.responsible?.length" class="material-list-resp">{{ m.responsible.join(', ') }}</span>
							</li>
						</ul>
						<span v-else class="detail-value detail-value--muted">—</span>
					</div>

					<!-- SiKo -->
					<div class="detail-section">
						<p class="detail-section-title">Sicherheitskonzept</p>
						<p class="detail-value detail-value--multiline">
							{{ activity.siko_text || '—' }}
						</p>
					</div>

					<!-- Ziel + Schlechtwetter -->
					<div class="detail-section">
						<div class="detail-grid">
							<div>
								<p class="detail-section-title">Ziel</p>
								<p class="detail-value detail-value--multiline">
									{{ activity.goal || '—' }}
								</p>
							</div>
							<div>
								<p class="detail-section-title">Schlechtwetter-Info</p>
								<p class="detail-value detail-value--multiline">
									{{ activity.bad_weather_info || '—' }}
								</p>
							</div>
						</div>
					</div>
				</div>
			</main>
		</template>
	</div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { useRoute } from 'vue-router';
import type { Activity } from '../types';
import { Search } from 'lucide-vue-next';
import DepartmentBadge from '../components/DepartmentBadge.vue';
import { sanitizeHtml } from '../utils/sanitizeHtml';
import { addMinutesToClock, formatDuration } from '../utils/time';

const route = useRoute();
const token = route.params.token as string;

const activity = ref<Activity | null>(null);
const loading = ref(true);
const notFound = ref(false);

function formatDate(iso: string): string {
	const d = new Date(iso + 'T00:00:00');
	const days = ['So', 'Mo', 'Di', 'Mi', 'Do', 'Fr', 'Sa'];
	return `${days[d.getDay()]}, ${d.getDate()}.${d.getMonth() + 1}.${d.getFullYear()}`;
}

function programLabel(index: number): string {
	if (!activity.value) return '';
	const progs = activity.value.programs;
	return formatDuration(progs[index].duration_minutes || 0);
}

function programSecondaryLabel(index: number): string {
	if (!activity.value) return '';
	const progs = activity.value.programs;
	let acc = 0;
	for (let i = 0; i < index; i++) acc += progs[i].duration_minutes || 0;
	const start = addMinutesToClock(activity.value.start_time, acc);
	const end = addMinutesToClock(activity.value.start_time, acc + (progs[index].duration_minutes || 0));
	return start && end ? `${start} – ${end}` : start || '—';
}

onMounted(async () => {
	try {
		const res = await fetch(`/api/shared/${token}`);
		if (res.status === 404) {
			notFound.value = true;
			return;
		}
		if (!res.ok) {
			notFound.value = true;
			return;
		}
		activity.value = await res.json();
		if (activity.value) {
			document.title = `${activity.value.title} – Geteilte Aktivität`;
		}
	} catch {
		notFound.value = true;
	} finally {
		loading.value = false;
	}
});
</script>
