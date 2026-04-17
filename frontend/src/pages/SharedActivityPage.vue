<template>
	<div class="shared-page">
		<!-- Loading -->
		<div v-if="loading" class="loading-state">
			<span class="spinner" />
			Aktivität wird geladen…
		</div>

		<!-- Not found -->
		<div v-else-if="notFound" class="error-state">
			<div class="error-icon">🔍</div>
			<h2>Nicht gefunden</h2>
			<p>Diese Aktivität existiert nicht oder der Link ist nicht mehr gültig.</p>
		</div>

		<!-- Activity view -->
		<template v-else-if="activity">
			<header class="shared-header">
				<span class="shared-badge">📎 Geteilte Aktivität</span>
			</header>

			<main class="shared-main">
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
								<span class="detail-value">{{ activity.department || '—' }}</span>
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
								<div class="program-time-col">
									<span class="program-time">{{ programLabel(pi) }}</span>
									<span class="program-time-secondary">{{ programSecondaryLabel(pi) }}</span>
								</div>
								<div class="program-body">
									<div class="program-header">
										<p class="program-title">{{ prog.title }}</p>
										<span v-if="prog.responsible.length" class="program-resp">{{
											prog.responsible.join(', ')
										}}</span>
									</div>
									<p v-if="prog.description" class="program-desc" v-html="sanitizeHtml(prog.description)"></p>
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

function sanitizeHtml(html: string): string {
	const div = document.createElement('div');
	div.innerHTML = html;
	div.querySelectorAll('script,iframe,object,embed,form').forEach(el => el.remove());
	return div.innerHTML;
}

// ---- Time helpers (same as DetailPage, default to minutes mode) ----
function addMinutesToClock(start: string, minutes: number): string {
	const m = /^(\d{1,2}):(\d{2})$/.exec(start.trim());
	if (!m) return '';
	const total = parseInt(m[1], 10) * 60 + parseInt(m[2], 10) + minutes;
	const h = ((Math.floor(total / 60) % 24) + 24) % 24;
	const mm = ((total % 60) + 60) % 60;
	return `${String(h).padStart(2, '0')}:${String(mm).padStart(2, '0')}`;
}

function formatDuration(min: number): string {
	if (!Number.isFinite(min) || min <= 0) return '0 min';
	if (min < 60) return `${min} min`;
	const h = Math.floor(min / 60);
	const m = min % 60;
	return m === 0 ? `${h} h` : `${h} h ${m} min`;
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
