<script setup lang="ts">
import { ref, computed, watch, nextTick, onUnmounted, onMounted } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useActivities } from '../composables/useActivities';
import { useUsers } from '../composables/useUsers';
import { user } from '../composables/useAuth';
import { wsSend, wsRegister } from '../composables/useWebSocket';
import type { Activity, Department, ProgramInput, EditSection, SectionLock, MaterialItem } from '../types';

const route = useRoute();
const router = useRouter();
const id = route.params.id as string;

const {
	fetchActivity,
	fetchDepartments,
	updateActivity,
	deleteActivity,
	error,
	lastUpdatedActivity,
	departments,
} = useActivities();
const { users, fetchUsers } = useUsers();

const activity = ref<Activity | null>(null);
const loading = ref(true);
const mode = ref<'view' | 'edit'>('view');
const saving = ref(false);

// ---- Edit state ------------------------------------------------------------
const editTitle = ref('');
const editDate = ref('');
const editStartTime = ref('');
const editEndTime = ref('');
const editGoal = ref('');
const editLocation = ref('');
const editResponsible = ref<string[]>([]);
const editDepartment = ref<Department | ''>('');
const editMaterial = ref<MaterialItem[]>([{ name: '', responsible: '' }]);
const editNeedsSiko = ref(false);
const editSikoFile = ref<File | null>(null);
const editSikoBase64 = ref<string | null>(null);
const editBadWeather = ref('');
const editPrograms = ref<ProgramInput[]>([]);

// ---- Collaborative editing state -------------------------------------------
const activeEditors = ref<string[]>([]);
const sectionLocks = ref<Map<EditSection, string>>(new Map());
const myLockedSection = ref<EditSection | null>(null);

function isLockedByOther(section: EditSection): boolean {
	const locker = sectionLocks.value.get(section);
	return !!locker && locker !== user.value?.display_name;
}
function lockedBy(section: EditSection): string | null {
	const locker = sectionLocks.value.get(section);
	return locker && locker !== user.value?.display_name ? locker : null;
}

function lockSection(section: EditSection) {
	if (isLockedByOther(section)) return;
	if (myLockedSection.value === section) return;
	// Unlock previous section if switching
	if (myLockedSection.value) {
		wsSend({ type: 'unlock', activity_id: id, section: myLockedSection.value });
	}
	myLockedSection.value = section;
	wsSend({ type: 'lock', activity_id: id, section });
}

let unlockTimer: ReturnType<typeof setTimeout> | null = null;
function unlockSection(section: EditSection, event?: FocusEvent) {
	if (myLockedSection.value !== section) return;
	// Check if focus is moving to another element within the same wrapper
	if (event?.relatedTarget instanceof HTMLElement) {
		const wrapper = (event.currentTarget as HTMLElement);
		if (wrapper?.contains(event.relatedTarget)) return;
	}
	// Small delay so rapid focus changes within the same section don't trigger unlock
	if (unlockTimer) clearTimeout(unlockTimer);
	unlockTimer = setTimeout(() => {
		if (myLockedSection.value === section) {
			myLockedSection.value = null;
			wsSend({ type: 'unlock', activity_id: id, section });
		}
	}, 100);
}

// ---- Load ------------------------------------------------------------------
const canEdit = computed(() => {
	if (!user.value || !activity.value) return false;
	const role = user.value.role;
	if (role === 'admin') return true;
	if (role === 'Stufenleiter') {
		if (!!user.value.department && activity.value.department === user.value.department) return true;
		return activity.value.responsible.includes(user.value.display_name);
	}
	// Leiter and Pio: only if verantwortlich (Pio also limited to own dept — enforced by backend)
	return activity.value.responsible.includes(user.value.display_name);
});

const canDelete = computed(() => {
	if (!user.value || !activity.value) return false;
	const role = user.value.role;
	if (role === 'admin') return true;
	if (role === 'Stufenleiter') {
		if (!!user.value.department && activity.value.department === user.value.department) return true;
		return activity.value.responsible.includes(user.value.display_name);
	}
	// Leiter and Pio: only if verantwortlich
	return activity.value.responsible.includes(user.value.display_name);
});

const canMail = computed(() => {
	if (!user.value || !activity.value) return false;
	const role = user.value.role;
	if (role === 'admin') return true;
	if (role === 'Stufenleiter') {
		if (!!user.value.department && activity.value.department === user.value.department) return true;
		return activity.value.responsible.includes(user.value.display_name);
	}
	// Leiter and Pio: only if verantwortlich
	return activity.value.responsible.includes(user.value.display_name);
});

onMounted(async () => {
	await Promise.all([fetchDepartments(), fetchUsers()]);
	activity.value = await fetchActivity(id);
	loading.value = false;

	// Register identity and join this activity for collaborative editing
	if (user.value) {
		wsRegister(user.value.display_name, user.value.microsoft_oid);
		wsSend({ type: 'join', activity_id: id });
	}
});

onUnmounted(() => {
	if (autoSaveTimer) clearTimeout(autoSaveTimer);
	// Leave the activity and unlock any held section
	wsSend({ type: 'leave', activity_id: id });
});

// ---- Sync edit fields from an activity (used by enterEdit + WS) ------------
function syncEditFields(a: typeof activity.value) {
	if (!a) return;
	editTitle.value = a.title;
	editDate.value = a.date;
	editStartTime.value = a.start_time;
	editEndTime.value = a.end_time;
	editGoal.value = a.goal;
	editLocation.value = a.location;
	editResponsible.value = [...a.responsible];
	editDepartment.value = a.department ?? '';
	editMaterial.value = [...a.material.map(m => ({ name: m.name, responsible: m.responsible ?? '' })), { name: '', responsible: '' }]; // trailing empty = sentinel input
	editNeedsSiko.value = a.needs_siko;
	editBadWeather.value = a.bad_weather_info ?? '';
	editPrograms.value = a.programs.map((p) => ({
		time: p.time,
		title: p.title,
		description: p.description,
		responsible: p.responsible,
	}));
}

// ---- WS live update — field-level merge in edit mode -----------------------
// Compare incoming update against last known saved state (prev).
// Only fields that changed remotely are patched into the edit form,
// so Tab B's own uncommitted edits in other fields are never overwritten.
watch(lastUpdatedActivity, (updated) => {
	if (!updated || updated.id !== id) return;

	const prev = activity.value; // last known saved snapshot
	activity.value = updated; // always advance source-of-truth

	if (mode.value === 'edit' && prev) {
		applyingRemote = true; // prevent the field watchers from triggering auto-save
		if (updated.title !== prev.title) editTitle.value = updated.title;
		if (updated.date !== prev.date) editDate.value = updated.date;
		if (updated.start_time !== prev.start_time)
			editStartTime.value = updated.start_time;
		if (updated.end_time !== prev.end_time)
			editEndTime.value = updated.end_time;
		if (updated.goal !== prev.goal) editGoal.value = updated.goal;
		if (updated.location !== prev.location)
			editLocation.value = updated.location;
		if (JSON.stringify(updated.responsible) !== JSON.stringify(prev.responsible))
			editResponsible.value = [...updated.responsible];
		if (updated.department !== prev.department)
			editDepartment.value = updated.department ?? '';
		if (updated.needs_siko !== prev.needs_siko)
			editNeedsSiko.value = updated.needs_siko;
		if (updated.bad_weather_info !== prev.bad_weather_info)
			editBadWeather.value = updated.bad_weather_info ?? '';
		if (JSON.stringify(updated.material) !== JSON.stringify(prev.material))
			editMaterial.value = [...updated.material.map(m => ({ name: m.name, responsible: m.responsible ?? '' })), { name: '', responsible: '' }];
		if (JSON.stringify(updated.programs) !== JSON.stringify(prev.programs))
			editPrograms.value = updated.programs.map((p) => ({
				time: p.time,
				title: p.title,
				description: p.description,
				responsible: p.responsible,
			}));
		nextTick(() => {
			applyingRemote = false;
		}); // reset after Vue has flushed all watchers
	}
});

// ---- WS lock/presence handlers -------------------------------------------
import { useWebSocket } from '../composables/useWebSocket';

useWebSocket((e) => {
	if (e.event === 'lock' && e.activity_id === id) {
		sectionLocks.value.set(e.section, e.user);
		sectionLocks.value = new Map(sectionLocks.value); // trigger reactivity
	} else if (e.event === 'unlock' && e.activity_id === id) {
		sectionLocks.value.delete(e.section);
		sectionLocks.value = new Map(sectionLocks.value);
	} else if (e.event === 'editors' && e.activity_id === id) {
		activeEditors.value = e.users.filter((u: string) => u !== user.value?.display_name);
	} else if (e.event === 'locks_state' && e.activity_id === id) {
		const m = new Map<EditSection, string>();
		for (const l of e.locks) {
			m.set(l.section, l.user);
		}
		sectionLocks.value = m;
	}
});

// ---- Helpers ---------------------------------------------------------------
function formatDate(d: string): string {
	return new Date(d + 'T00:00:00').toLocaleDateString('de-DE', {
		weekday: 'long',
		day: 'numeric',
		month: 'long',
		year: 'numeric',
	});
}

// ---- Enter edit mode -------------------------------------------------------
function enterEdit() {
	if (!activity.value || !canEdit.value) return;
	syncEditFields(activity.value);
	editSikoFile.value = null;
	editSikoBase64.value = null;
	error.value = null;
	mode.value = 'edit';
}

// ---- Material --------------------------------------------------------------
// Sentinel pattern: array always ends with one empty string.
// @input  → if user typed in the last (sentinel) field, grow the list
// @blur   → if a non-sentinel field is empty when leaving, remove it
function onMaterialInput(i: number) {
	const isLast = i === editMaterial.value.length - 1;
	if (isLast && editMaterial.value[i].name !== '') {
		editMaterial.value.push({ name: '', responsible: '' });
	}
}
function onMaterialBlur(i: number) {
	const isLast = i === editMaterial.value.length - 1;
	if (!isLast && editMaterial.value[i].name === '') {
		editMaterial.value.splice(i, 1);
	}
}

// ---- Material responsible search -------------------------------------------
const materialRespSearch = ref<Record<number, string>>({});
const materialRespDropdown = ref<number | null>(null);

function materialRespFiltered(i: number) {
	const q = (materialRespSearch.value[i] ?? '').toLowerCase();
	return users.value.filter(u => q === '' || u.display_name.toLowerCase().includes(q));
}
function setMaterialResp(i: number, name: string) {
	editMaterial.value[i].responsible = name;
	materialRespSearch.value[i] = '';
	materialRespDropdown.value = null;
}
function clearMaterialResp(i: number) {
	editMaterial.value[i].responsible = '';
}
function onMaterialRespBlur(i: number) {
	setTimeout(() => {
		materialRespDropdown.value = null;
		delete materialRespSearch.value[i];
	}, 200);
}

// ---- Responsible search ----------------------------------------------------
const responsibleSearch = ref('');
const showResponsibleDropdown = ref(false);

const filteredResponsibleUsers = computed(() => {
	const q = responsibleSearch.value.toLowerCase();
	return users.value.filter(
		(u) =>
			!editResponsible.value.includes(u.display_name) &&
			(q === '' || u.display_name.toLowerCase().includes(q)),
	);
});

function addResponsible(name: string) {
	if (!editResponsible.value.includes(name)) {
		editResponsible.value.push(name);
	}
	responsibleSearch.value = '';
	showResponsibleDropdown.value = false;
}

function removeResponsible(i: number) {
	editResponsible.value.splice(i, 1);
}

function onResponsibleBlur() {
	setTimeout(() => {
		showResponsibleDropdown.value = false;
		responsibleSearch.value = '';
	}, 200);
}

// ---- Programs --------------------------------------------------------------
function addProgram() {
	editPrograms.value.push({
		time: '',
		title: '',
		description: '',
		responsible: editResponsible.value[0] ?? '',
	});
}
function removeProgram(i: number) {
	editPrograms.value.splice(i, 1);
}

// ---- SiKo file change ------------------------------------------------------
async function onSikoFileChange(e: Event) {
	const file = (e.target as HTMLInputElement).files?.[0];
	if (!file) {
		editSikoFile.value = null;
		editSikoBase64.value = null;
		return;
	}
	editSikoFile.value = file;
	const buf = await file.arrayBuffer();
	const bytes = new Uint8Array(buf);
	let binary = '';
	for (let i = 0; i < bytes.length; i++)
		binary += String.fromCharCode(bytes[i]);
	editSikoBase64.value = btoa(binary);
	scheduleAutoSave(); // file upload triggers save like any other field change
}

// ---- Auto-save (debounced 1.5 s) -------------------------------------------
let autoSaveTimer: ReturnType<typeof setTimeout> | null = null;
let applyingRemote = false; // plain boolean — set while WS update patches edit fields

function scheduleAutoSave() {
	if (mode.value !== 'edit') return;
	if (applyingRemote) return; // ignore watcher firings caused by remote WS updates
	if (autoSaveTimer) clearTimeout(autoSaveTimer);
	autoSaveTimer = setTimeout(doSave, 1500);
}

// onUnmounted is handled above (combined with WS leave)

watch(
	[
		editTitle,
		editDate,
		editStartTime,
		editEndTime,
		editGoal,
		editLocation,
		editResponsible,
		editDepartment,
		editMaterial,
		editNeedsSiko,
		editBadWeather,
		editPrograms,
	],
	scheduleAutoSave,
	{ deep: true },
);

// ---- Core save (used by auto-save and the button) --------------------------
async function doSave() {
	if (!activity.value) return;
	saving.value = true;
	error.value = null;

	await updateActivity(id, {
		title: editTitle.value.trim(),
		date: editDate.value,
		start_time: editStartTime.value,
		end_time: editEndTime.value,
		goal: editGoal.value.trim(),
		location: editLocation.value.trim(),
		responsible: editResponsible.value,
		department: editDepartment.value || null,
		material: editMaterial.value.filter((m) => m.name.trim()).map(m => ({ name: m.name.trim(), ...(m.responsible?.trim() ? { responsible: m.responsible.trim() } : {}) })),
		needs_siko: editNeedsSiko.value,
		siko_base64: editSikoBase64.value ?? undefined,
		bad_weather_info: editBadWeather.value.trim() || null,
		programs: editPrograms.value,
	});

	saving.value = false;
}

// ---- Delete ----------------------------------------------------------------
async function doDelete() {
	if (!confirm(`Aktivität "${activity.value?.title || id}" wirklich löschen?`))
		return;
	await deleteActivity(id);
	router.push('/');
}
</script>

<template>
	<header class="header">
		<button class="btn-back" @click="router.push('/')">← Zurück</button>
		<div class="header-right">
			<span v-if="saving" class="saving-badge">Speichert…</span>
			<router-link
				v-if="activity && mode === 'view' && canMail"
				:to="`/activities/${id}/mail`"
				class="btn-mail"
				title="Mail senden"
				>📧 Mail</router-link
			>
			<button
				v-if="activity && canEdit"
				class="btn-toggle"
				:class="mode === 'edit' ? 'btn-secondary' : 'btn-primary'"
				@click="mode === 'view' ? enterEdit() : (mode = 'view')"
			>
				{{ mode === 'view' ? 'Bearbeiten' : 'Ansicht' }}
			</button>
		</div>
	</header>

	<main class="main">
		<p v-if="loading" class="loading">Laden…</p>
		<p v-else-if="!activity" class="error">Aktivität nicht gefunden.</p>

		<!-- ================================================================ VIEW -->
		<div v-else-if="mode === 'view'" class="detail-view">
			<!-- Hero: Titel + Datum + Abteilung -->
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

			<!-- Ort / Verantwortlich -->
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
						<span class="detail-label">Abteilung</span>
						<span class="detail-value">{{ activity.department || '—' }}</span>
					</div>
				</div>
			</div>

			<!-- Programmpunkte -->
			<div class="detail-section">
				<p class="detail-section-title">Programmpunkte</p>
				<div v-if="activity.programs.length" class="program-timeline">
					<div
						v-for="prog in activity.programs"
						:key="prog.id"
						class="program-item"
					>
						<span class="program-time">{{
							prog.time ? prog.time + ' min' : '—'
						}}</span>
						<div class="program-body">
							<div class="program-header">
								<p class="program-title">{{ prog.title }}</p>
								<span v-if="prog.responsible" class="program-resp">{{
									prog.responsible
								}}</span>
							</div>
							<p v-if="prog.description" class="program-desc">
								{{ prog.description }}
							</p>
						</div>
					</div>
				</div>
				<span v-else class="detail-value detail-value--muted">—</span>
			</div>

			<!-- Material -->
			<div class="detail-section">
				<p class="detail-section-title">Material</p>
				<ul v-if="activity.material.length" class="material-list-view">
					<li
						v-for="(m, i) in activity.material"
						:key="i"
						class="material-list-item"
					>
						<span class="material-list-name">{{ m.name }}</span>
						<span v-if="m.responsible" class="material-list-resp">{{ m.responsible }}</span>
					</li>
				</ul>
				<span v-else class="detail-value detail-value--muted">—</span>
			</div>

			<!-- SiKo -->
			<div class="detail-section">
				<p class="detail-section-title">Sicherheitskonzept</p>
				<div class="detail-grid">
					<div class="detail-field">
						<span class="detail-label">Benötigt</span>
						<span class="detail-value">{{
							activity.needs_siko ? 'Ja' : 'Nein'
						}}</span>
					</div>
					<div class="detail-field">
						<span class="detail-label">Datei</span>
						<a
							v-if="activity.has_siko"
							:href="`/api/activities/${activity.id}/siko`"
							download
							class="siko-link"
							>📄 SiKo herunterladen</a
						>
						<span v-else class="detail-value detail-value--muted">—</span>
					</div>
				</div>
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

			<!-- Meta -->
			<div class="detail-meta">
				<span
					>Erstellt:
					{{ new Date(activity.created_at).toLocaleString('de-DE') }}</span
				>
				<span
					>Geändert:
					{{ new Date(activity.updated_at).toLocaleString('de-DE') }}</span
				>
			</div>
		</div>

		<!-- =============================================================== EDIT -->
		<div v-else class="detail-form">
			<!-- Active editors indicator -->
			<div v-if="activeEditors.length" class="editors-banner">
				<span class="editors-banner-icon">👥</span>
				<span>{{ activeEditors.join(', ') }} {{ activeEditors.length === 1 ? 'bearbeitet' : 'bearbeiten' }} ebenfalls</span>
			</div>

			<!-- Titel -->
			<div class="form-group lock-wrapper" :class="{ 'is-locked': isLockedByOther('title') }"
				@focusin="lockSection('title')" @focusout="unlockSection('title', $event)">
				<div v-if="lockedBy('title')" class="lock-badge">🔒 {{ lockedBy('title') }}</div>
				<label for="edit-title">Titel</label>
				<input
					id="edit-title"
					v-model="editTitle"
					type="text"
					placeholder="Titel der Aktivität"
					autofocus
					:disabled="isLockedByOther('title')"
				/>
			</div>

			<!-- Datum + Zeiten -->
			<div class="form-row form-row--3 lock-wrapper" :class="{ 'is-locked': isLockedByOther('datetime') }"
				@focusin="lockSection('datetime')" @focusout="unlockSection('datetime', $event)">
				<div v-if="lockedBy('datetime')" class="lock-badge">🔒 {{ lockedBy('datetime') }}</div>
				<div class="form-group">
					<label for="edit-date">Datum</label>
					<input id="edit-date" v-model="editDate" type="date" :disabled="isLockedByOther('datetime')" />
				</div>
				<div class="form-group">
					<label for="edit-start">Startzeit</label>
					<input id="edit-start" v-model="editStartTime" type="time" :disabled="isLockedByOther('datetime')" />
				</div>
				<div class="form-group">
					<label for="edit-end">Endzeit</label>
					<input id="edit-end" v-model="editEndTime" type="time" :disabled="isLockedByOther('datetime')" />
				</div>
			</div>

			<!-- Ort + Verantwortlich + Abteilung -->
			<div class="form-row form-row--3 lock-wrapper" :class="{ 'is-locked': isLockedByOther('location') }"
				@focusin="lockSection('location')" @focusout="unlockSection('location', $event)">
				<div v-if="lockedBy('location')" class="lock-badge">🔒 {{ lockedBy('location') }}</div>
				<div class="form-group">
					<label for="edit-location">Ort</label>
					<input
						id="edit-location"
						v-model="editLocation"
						type="text"
						placeholder="Veranstaltungsort"
						:disabled="isLockedByOther('location')"
					/>
				</div>
				<div class="form-group user-search-group">
					<label>Verantwortlich</label>
					<div class="user-chips" v-if="editResponsible.length">
						<span v-for="(name, i) in editResponsible" :key="name" class="user-chip">
							{{ name }}
							<button type="button" class="user-chip-remove" @click="removeResponsible(i)" :disabled="isLockedByOther('location')">✕</button>
						</span>
					</div>
					<div class="user-search-wrapper">
						<input
							type="text"
							v-model="responsibleSearch"
							placeholder="Person suchen…"
							@focus="showResponsibleDropdown = true"
							@blur="onResponsibleBlur"
							:disabled="isLockedByOther('location')"
						/>
						<div v-if="showResponsibleDropdown && filteredResponsibleUsers.length" class="user-dropdown">
							<div
								v-for="u in filteredResponsibleUsers"
								:key="u.id"
								class="user-dropdown-item"
								@mousedown.prevent="addResponsible(u.display_name)"
							>
								{{ u.display_name }}
							</div>
						</div>
					</div>
				</div>
				<div class="form-group">
					<label for="edit-department">Abteilung</label>
					<select id="edit-department" v-model="editDepartment" :disabled="isLockedByOther('location')">
						<option value="">Bitte wählen</option>
						<option v-for="dep in departments" :key="dep" :value="dep">
							{{ dep }}
						</option>
					</select>
				</div>
			</div>

			<!-- Programmpunkte -->
			<div class="form-section">
				<p class="form-section-title">Programmpunkte</p>
				<div style="display: flex; flex-direction: column; gap: 10px">
					<div v-for="(prog, i) in editPrograms" :key="i" class="program-card lock-wrapper"
						:class="{ 'is-locked': isLockedByOther(`program_${i}`) }"
						@focusin="lockSection(`program_${i}`)" @focusout="unlockSection(`program_${i}`, $event)">
						<div v-if="lockedBy(`program_${i}`)" class="lock-badge">🔒 {{ lockedBy(`program_${i}`) }}</div>
						<button
							type="button"
							class="program-card__remove"
							@click="removeProgram(i)"
							:disabled="isLockedByOther(`program_${i}`)"
						>
							✕
						</button>
						<div class="program-card__fields">
							<div class="form-group">
								<label>Minuten</label>
								<input
									type="number"
									min="0"
									placeholder="30"
									:value="prog.time"
									@input="prog.time = ($event.target as HTMLInputElement).value"
									:disabled="isLockedByOther(`program_${i}`)"
								/>
							</div>
							<div class="form-group">
								<label>Titel</label>
								<input v-model="prog.title" type="text" placeholder="Titel" :disabled="isLockedByOther(`program_${i}`)" />
							</div>
							<div class="form-group">
								<label>Verantwortlich</label>
								<select v-model="prog.responsible" :disabled="isLockedByOther(`program_${i}`)">
									<option value="" disabled>Bitte wählen</option>
									<option
										v-for="u in users"
										:key="u.id"
										:value="u.display_name"
									>
										{{ u.display_name }}
									</option>
								</select>
							</div>
							<div class="form-group program-card__full">
								<label>Beschreibung</label>
								<textarea
									v-model="prog.description"
									rows="2"
									placeholder="Beschreibung…"
									:disabled="isLockedByOther(`program_${i}`)"
								/>
							</div>
						</div>
					</div>
					<button type="button" class="btn-add" @click="addProgram">
						+ Programmpunkt
					</button>
				</div>
			</div>

			<!-- Material -->
			<div class="form-section">
				<p class="form-section-title">Material</p>
				<div class="material-list">
					<div v-for="(_, i) in editMaterial" :key="i" class="material-row lock-wrapper"
						:class="{ 'is-locked': isLockedByOther(`material_${i}`) }"
						@focusin="lockSection(`material_${i}`)" @focusout="unlockSection(`material_${i}`, $event)">
						<div v-if="lockedBy(`material_${i}`)" class="lock-badge">🔒 {{ lockedBy(`material_${i}`) }}</div>
						<input
							v-model="editMaterial[i].name"
							type="text"
							placeholder="Material…"
							class="material-row__name"
							@input="onMaterialInput(i)"
							@blur="onMaterialBlur(i)"
							:disabled="isLockedByOther(`material_${i}`)"
						/>
						<div class="material-row__responsible user-search-wrapper">
							<template v-if="editMaterial[i].responsible">
								<span class="material-resp-chip">
									{{ editMaterial[i].responsible }}
									<button type="button" class="user-chip-remove" @click="clearMaterialResp(i)" :disabled="isLockedByOther(`material_${i}`)">✕</button>
								</span>
							</template>
							<template v-else>
								<input
									type="text"
									:value="materialRespSearch[i] ?? ''"
									@input="materialRespSearch[i] = ($event.target as HTMLInputElement).value"
									placeholder="Verantwortlich (optional)"
									@focus="materialRespDropdown = i"
									@blur="onMaterialRespBlur(i)"
									:disabled="isLockedByOther(`material_${i}`)"
								/>
								<div v-if="materialRespDropdown === i && materialRespFiltered(i).length" class="user-dropdown">
									<div
										v-for="u in materialRespFiltered(i)"
										:key="u.id"
										class="user-dropdown-item"
										@mousedown.prevent="setMaterialResp(i, u.display_name)"
									>
										{{ u.display_name }}
									</div>
								</div>
							</template>
						</div>
					</div>
				</div>
			</div>

			<!-- SiKo -->
			<div class="form-section lock-wrapper" :class="{ 'is-locked': isLockedByOther('siko') }"
				@focusin="lockSection('siko')" @focusout="unlockSection('siko', $event)">
				<div v-if="lockedBy('siko')" class="lock-badge">🔒 {{ lockedBy('siko') }}</div>
				<p class="form-section-title">Sicherheitskonzept</p>
				<label class="form-check">
					<input type="checkbox" v-model="editNeedsSiko" :disabled="isLockedByOther('siko')" />
					<span>Sicherheitskonzept benötigt?</span>
				</label>
				<div v-if="editNeedsSiko" style="margin-top: 12px">
					<div class="form-group">
						<label for="edit-siko"
							>SiKo (PDF)
							<span v-if="activity.has_siko" class="file-hint">
								— aktuell vorhanden</span
							>
						</label>
						<input
							id="edit-siko"
							type="file"
							accept=".pdf"
							@change="onSikoFileChange"
							:disabled="isLockedByOther('siko')"
						/>
						<span v-if="editSikoFile" class="file-name">{{
							editSikoFile.name
						}}</span>
					</div>
				</div>
			</div>

			<!-- Ziel + Schlechtwetter -->
			<div class="form-row lock-wrapper" :class="{ 'is-locked': isLockedByOther('goal_weather') }"
				@focusin="lockSection('goal_weather')" @focusout="unlockSection('goal_weather', $event)">
				<div v-if="lockedBy('goal_weather')" class="lock-badge">🔒 {{ lockedBy('goal_weather') }}</div>
				<div class="form-group">
					<label for="edit-goal">Ziel</label>
					<textarea
						id="edit-goal"
						v-model="editGoal"
						rows="3"
						placeholder="Was soll erreicht werden?"
						:disabled="isLockedByOther('goal_weather')"
					/>
				</div>
				<div class="form-group">
					<label for="edit-weather">Schlechtwetter-Info</label>
					<textarea
						id="edit-weather"
						v-model="editBadWeather"
						rows="3"
						placeholder="Was passiert bei schlechtem Wetter?"
						:disabled="isLockedByOther('goal_weather')"
					/>
				</div>
			</div>

			<!-- Error -->
			<p v-if="error" class="error">{{ error }}</p>

			<!-- Actions -->
			<div class="form-actions">
				<button
					type="button"
					class="btn-danger"
					v-if="canDelete"
					@click="doDelete"
				>
					Löschen
				</button>
				<button type="button" class="btn-secondary" @click="mode = 'view'">
					Schliessen
				</button>
			</div>
		</div>
	</main>
</template>
