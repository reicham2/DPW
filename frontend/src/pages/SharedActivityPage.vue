<template>
	<PublicPageLayout label="Geteilte Aktivität">
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

		<div v-else-if="error" class="error-state" role="alert">
			<TriangleAlert :size="28" aria-hidden="true" />
			<h2>Aktivität konnte nicht geladen werden</h2>
			<p>{{ error }}</p>
		</div>

		<!-- Activity view -->
		<template v-else-if="activity">
			<article class="shared-page shared-main">
				<div class="detail-view">
					<!-- Hero -->
					<div class="detail-section">
						<div class="detail-hero">
							<div>
								<h1 class="detail-hero-title">{{ activity.title }}</h1>
								<p class="detail-hero-time">
									{{ formatDate(activity.date) }} &middot;
									{{ activity.start_time }}–{{ activity.end_time }}
								</p>
							</div>
							<a :href="activityUrl" class="shared-open">
								<ExternalLink :size="18" aria-hidden="true" />
								In DPWeb öffnen
							</a>
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

					<div class="detail-section">
						<h2 class="detail-section-title">Roter Faden</h2>
						<p class="detail-value detail-value--multiline">{{ activity.guiding_thread || '—' }}</p>
					</div>

					<!-- Programmpunkte -->
					<div class="detail-section">
						<h2 class="detail-section-title">Programmpunkte</h2>
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
									<ProgramDescription v-if="prog.description" :html="prog.description" :shared-token="token" />
								</div>
							</div>
						</div>
						<span v-else class="detail-value detail-value--muted">—</span>
					</div>

					<!-- Material -->
					<div class="detail-section">
						<h2 class="detail-section-title">Material</h2>
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
						<h2 class="detail-section-title">Sicherheitskonzept</h2>
						<p class="detail-value detail-value--multiline">
							{{ activity.siko_text || '—' }}
						</p>
					</div>

					<!-- Ziel + Schlechtwetter -->
					<div class="detail-section">
						<div class="detail-grid">
							<div>
								<h2 class="detail-section-title">Ziel</h2>
								<p class="detail-value detail-value--multiline">
									{{ activity.goal || '—' }}
								</p>
							</div>
							<div>
								<h2 class="detail-section-title">Schlechtwetter-Info</h2>
								<p class="detail-value detail-value--multiline">
									{{ activity.bad_weather_info || '—' }}
								</p>
							</div>
						</div>
					</div>
				</div>
			</article>
		</template>
	</PublicPageLayout>
</template>

<script setup lang="ts">
import { computed, ref, onMounted } from 'vue';
import { useRoute } from 'vue-router';
import type { Activity } from '../types';
import { ExternalLink, Search, TriangleAlert } from 'lucide-vue-next';
import DepartmentBadge from '../components/DepartmentBadge.vue';
import ProgramDescription from '../components/ProgramDescription.vue';
import PublicPageLayout from '../components/PublicPageLayout.vue';
import { addMinutesToClock, formatDuration } from '../utils/time';
import { publicUrl } from '../utils/publicUrl';

const route = useRoute();
const token = route.params.token as string;

const activity = ref<Activity | null>(null);
const loading = ref(true);
const notFound = ref(false);
const error = ref<string | null>(null);
const activityUrl = computed(() =>
	activity.value ? publicUrl(`/activities/${encodeURIComponent(activity.value.id)}`) : '',
);

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
		const res = await fetch(`/api/shared/${encodeURIComponent(token)}`);
		if (res.status === 404) {
			notFound.value = true;
			return;
		}
		if (!res.ok) {
			throw new Error(`Aktivität konnte nicht geladen werden (HTTP ${res.status}).`);
		}
		activity.value = await res.json();
		if (activity.value) {
			document.title = `${activity.value.title} – Geteilte Aktivität`;
		}
	} catch (e) {
		error.value = e instanceof Error && e.message.trim() ? e.message : 'Bitte versuche es später erneut.';
	} finally {
		loading.value = false;
	}
});
</script>
