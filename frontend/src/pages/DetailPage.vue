<script setup lang="ts">
import { ref, computed, watch, nextTick, onUnmounted, onMounted } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useActivities } from '../composables/useActivities';
import { useUsers } from '../composables/useUsers';
import { usePermissions } from '../composables/usePermissions';
import { user } from '../composables/useAuth';
import { wsSend, wsRegister } from '../composables/useWebSocket';
import type { Activity, Attachment, Department, ProgramInput, EditSection, SectionLock, MaterialItem } from '../types';
import ErrorAlert from '../components/ErrorAlert.vue';


const route = useRoute();
const router = useRouter();
const id = route.params.id as string;

const {
	activities,
	fetchActivities,
	fetchActivity,
	fetchDepartments,
	fetchLocations,
	updateActivity,
	deleteActivity,
	fetchAttachments,
	uploadAttachment,
	deleteAttachment,
	getAttachmentBlobUrl,
	error,
	lastUpdatedActivity,
	departments,
	predefinedLocations,
} = useActivities();
const { users, fetchUsers } = useUsers();
const { myPermissions, fetchMyPermissions, canWriteDept, writableDepts, canReadDept } = usePermissions();

const activity = ref<Activity | null>(null);
const loading = ref(true);
const mode = ref<'view' | 'edit'>('view');
const dirtyFields = new Set<string>();
const savedFields = ref<Record<string, number>>({});
let savedTimer: ReturnType<typeof setTimeout> | null = null;
let suppressDirtyTracking = true;
let applyingRemote = false; // plain boolean — set while WS update patches edit fields

function markDirty(...fields: string[]) {
	if (suppressDirtyTracking || applyingRemote) return;
	if (mode.value !== 'edit') return;
	for (const f of fields) dirtyFields.add(f);
}

function showSavedIndicator() {
	if (savedTimer) clearTimeout(savedTimer);
	const snap: Record<string, number> = {};
	for (const f of dirtyFields) snap[f] = Date.now();
	dirtyFields.clear();
	savedFields.value = snap;
	savedTimer = setTimeout(() => { savedFields.value = {}; }, 2000);
}

// ---- Edit state ------------------------------------------------------------
const editTitle = ref('');
const editDate = ref('');
const editStartTime = ref('');
const editEndTime = ref('');
const editGoal = ref('');
const editLocation = ref('');
const editResponsible = ref<string[]>([]);
const editDepartment = ref<Department | ''>('');
const editMaterial = ref<MaterialItem[]>([{ name: '', responsible: [] }]);
const editSikoText = ref('');
const editBadWeather = ref('');
const editPrograms = ref<ProgramInput[]>([]);

// ---- Attachments state -----------------------------------------------------
const attachments = ref<Attachment[]>([]);
const uploadingAttachment = ref(false);
const previewAttachment = ref<Attachment | null>(null);
const previewBlobUrl = ref<string | null>(null);
const previewLoading = ref(false);

function isPreviewable(att: Attachment): boolean {
	return att.content_type.startsWith('image/') || att.content_type === 'application/pdf';
}

async function openPreview(att: Attachment) {
	if (!isPreviewable(att)) return;
	previewAttachment.value = att;
	previewLoading.value = true;
	const url = await getAttachmentBlobUrl(att.id);
	previewBlobUrl.value = url;
	previewLoading.value = false;
}

function closePreview() {
	if (previewBlobUrl.value) {
		URL.revokeObjectURL(previewBlobUrl.value);
	}
	previewAttachment.value = null;
	previewBlobUrl.value = null;
}

async function downloadAttachment(att: Attachment) {
	const url = await getAttachmentBlobUrl(att.id);
	if (!url) return;
	const a = document.createElement('a');
	a.href = url;
	a.download = att.filename;
	document.body.appendChild(a);
	a.click();
	document.body.removeChild(a);
	URL.revokeObjectURL(url);
}

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
const canView = computed(() => {
	if (!user.value || !activity.value) return true; // don't block during loading
	const dept = activity.value.department;
	if (!dept) return true; // activities without department are visible to all
	return canReadDept(dept, user.value.department);
});

const canEdit = computed(() => {
	if (!user.value || !activity.value) return false;
	const dept = activity.value.department;
	if (dept && canWriteDept(dept, user.value.department)) return true;
	return activity.value.responsible.includes(user.value.display_name) && myPermissions.value?.can_write_own_dept;
});

const canDelete = computed(() => {
	if (!user.value || !activity.value) return false;
	const dept = activity.value.department;
	if (dept && canWriteDept(dept, user.value.department)) return true;
	return activity.value.responsible.includes(user.value.display_name) && myPermissions.value?.can_write_own_dept;
});

const canMail = computed(() => {
	if (!user.value || !activity.value) return false;
	const p = myPermissions.value;
	if (!p) return false;
	if (p.mail_send_scope === 'all') return true;
	if (p.mail_send_scope === 'same_dept' && activity.value.department === user.value.department) return true;
	if (p.mail_send_scope === 'own' && activity.value.responsible.includes(user.value.display_name)) return true;
	return false;
});

onMounted(async () => {
	await Promise.all([fetchDepartments(), fetchUsers(), fetchLocations(), fetchActivities(), fetchMyPermissions()]);
	activity.value = await fetchActivity(id);
	if (activity.value) {
		// Check read permission
		if (!canView.value) {
			router.replace('/');
			return;
		}
		document.title = `${activity.value.title} – DPWeb`;
		attachments.value = await fetchAttachments(id);
	}
	loading.value = false;

	// Register identity and join this activity for collaborative editing
	if (user.value) {
		wsRegister(user.value.display_name, user.value.microsoft_oid);
		wsSend({ type: 'join', activity_id: id });
	}

	// Scroll to focused element (e.g. material) via ?focus=material_2
	const focusTarget = route.query.focus as string | undefined;
	if (focusTarget) {
		await nextTick();
		setTimeout(() => {
			const el = document.getElementById(`section-${focusTarget}`);
			if (el) {
				el.scrollIntoView({ behavior: 'smooth', block: 'center' });
				el.classList.add('section-highlight');
				setTimeout(() => el.classList.remove('section-highlight'), 2000);
			}
		}, 200);
	}
});

onUnmounted(() => {
	if (autoSaveTimer) clearTimeout(autoSaveTimer);
	if (savedTimer) clearTimeout(savedTimer);
	stopAutoSaveInterval();
	// Leave the activity and unlock any held section
	wsSend({ type: 'leave', activity_id: id });
	document.title = 'DPWeb Aktivitäten';
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
	editMaterial.value = [...a.material.map(m => ({ name: m.name, responsible: m.responsible ?? [] })), { name: '', responsible: [] }]; // trailing empty = sentinel input
	editSikoText.value = a.siko_text ?? '';
	editBadWeather.value = a.bad_weather_info ?? '';
	editPrograms.value = a.programs.map((p) => ({
		time: p.time,
		title: p.title,
		description: p.description,
		responsible: [...p.responsible],
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
		if (updated.siko_text !== prev.siko_text)
			editSikoText.value = updated.siko_text ?? '';
		if (updated.bad_weather_info !== prev.bad_weather_info)
			editBadWeather.value = updated.bad_weather_info ?? '';
		if (JSON.stringify(updated.material) !== JSON.stringify(prev.material))
			editMaterial.value = [...updated.material.map(m => ({ name: m.name, responsible: m.responsible ?? [] })), { name: '', responsible: [] }];
		if (JSON.stringify(updated.programs) !== JSON.stringify(prev.programs)) {
			editPrograms.value = updated.programs.map((p) => ({
				time: p.time,
				title: p.title,
				description: p.description,
				responsible: [...p.responsible],
			}));
			initProgEditors();
		}
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
	suppressDirtyTracking = true;
	syncEditFields(activity.value);
	error.value = null;
	mode.value = 'edit';
	startAutoSaveInterval();
	initProgEditors();
	nextTick(() => { suppressDirtyTracking = false; dirtyFields.clear(); });
}

// ---- Material --------------------------------------------------------------
// Sentinel pattern: array always ends with one empty string.
// @input  → if user typed in the last (sentinel) field, grow the list
// @blur   → if a non-sentinel field is empty when leaving, remove it
function onMaterialInput(i: number) {
	const isLast = i === editMaterial.value.length - 1;
	if (isLast && editMaterial.value[i].name !== '') {
		editMaterial.value.push({ name: '', responsible: [] });
	}
}
function onMaterialBlur(i: number) {
	const isLast = i === editMaterial.value.length - 1;
	if (!isLast && editMaterial.value[i].name === '') {
		editMaterial.value.splice(i, 1);
	}
}
function removeMaterial(i: number) {
	editMaterial.value.splice(i, 1);
	if (editMaterial.value.length === 0 || editMaterial.value[editMaterial.value.length - 1].name !== '') {
		editMaterial.value.push({ name: '', responsible: [] });
	}
	markDirty('material');
}

// ---- Material responsible search -------------------------------------------
const materialRespSearch = ref<Record<number, string>>({});
const materialRespDropdown = ref<number | null>(null);

function materialRespFiltered(i: number) {
	const q = (materialRespSearch.value[i] ?? '').toLowerCase();
	const current = editMaterial.value[i].responsible ?? [];
	return users.value.filter(u => !current.includes(u.display_name) && (q === '' || u.display_name.toLowerCase().includes(q)));
}
function setMaterialResp(i: number, name: string) {
	if (!editMaterial.value[i].responsible) editMaterial.value[i].responsible = [];
	if (!editMaterial.value[i].responsible!.includes(name)) {
		editMaterial.value[i].responsible!.push(name);
	}
	materialRespSearch.value[i] = '';
	materialRespDropdown.value = null;
	(document.activeElement as HTMLElement)?.blur();
}
function addMaterialRespFreeText(i: number) {
	const text = (materialRespSearch.value[i] ?? '').trim();
	if (!text) return;
	if (!editMaterial.value[i].responsible) editMaterial.value[i].responsible = [];
	if (!editMaterial.value[i].responsible!.includes(text)) {
		editMaterial.value[i].responsible!.push(text);
	}
	materialRespSearch.value[i] = '';
	materialRespDropdown.value = null;
	(document.activeElement as HTMLElement)?.blur();
}
function clearMaterialResp(i: number, idx?: number) {
	if (idx !== undefined) {
		editMaterial.value[i].responsible?.splice(idx, 1);
	} else {
		editMaterial.value[i].responsible = [];
	}
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
	(document.activeElement as HTMLElement)?.blur();
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

// ---- Location dropdown -----------------------------------------------------
const showLocationDropdown = ref(false);

const filteredLocations = computed(() => {
	const q = editLocation.value.toLowerCase();
	return predefinedLocations.value.filter(
		(loc) => q === '' || loc.toLowerCase().includes(q),
	);
});

function selectLocation(loc: string) {
	editLocation.value = loc;
	showLocationDropdown.value = false;
	markDirty('location');
	(document.activeElement as HTMLElement)?.blur();
}

function locationOverlapsFor(loc: string): OverlapInfo[] {
	const currentDate = editDate.value;
	const currentStart = editStartTime.value;
	const currentEnd = editEndTime.value;
	if (!currentDate || !currentStart || !currentEnd) return [];
	return activities.value.filter(a =>
		a.id !== id &&
		a.location.toLowerCase() === loc.toLowerCase() &&
		a.date === currentDate &&
		a.start_time < currentEnd &&
		a.end_time > currentStart
	).map(a => ({
		id: a.id, title: a.title, start: a.start_time, end: a.end_time,
		department: a.department, location: a.location, kind: 'exact' as const,
	}));
}

function onLocationBlur() {
	setTimeout(() => {
		showLocationDropdown.value = false;
	}, 200);
}

// ---- Department dropdown ----------------------------------------------------
const departmentSearch = ref('');
const showDepartmentDropdown = ref(false);

const editWritableDepts = computed(() => writableDepts(user.value?.department));
const deptFieldDisabled = computed(() => editWritableDepts.value.length <= 1);

const filteredDepartments = computed(() => {
	const q = departmentSearch.value.toLowerCase();
	return editWritableDepts.value.filter(
		(dep) => q === '' || dep.toLowerCase().includes(q),
	);
});

function selectDepartment(dep: string) {
	editDepartment.value = dep as Department;
	departmentSearch.value = '';
	showDepartmentDropdown.value = false;
	(document.activeElement as HTMLElement)?.blur();
}

function onDepartmentBlur() {
	setTimeout(() => {
		showDepartmentDropdown.value = false;
		departmentSearch.value = '';
	}, 200);
}

// ---- Programs --------------------------------------------------------------
function addProgram() {
	editPrograms.value.push({
		time: '',
		title: '',
		description: '',
		responsible: editResponsible.value.length ? [editResponsible.value[0]] : [],
	});
}
function removeProgram(i: number) {
	editPrograms.value.splice(i, 1);
}

// ---- Program responsible search (multi-select + free text) ------------------
const progRespSearch = ref<Record<number, string>>({});
const progRespDropdown = ref<number | null>(null);

function progRespFiltered(i: number) {
	const q = (progRespSearch.value[i] ?? '').toLowerCase();
	const current = editPrograms.value[i].responsible;
	return users.value.filter(u => !current.includes(u.display_name) && (q === '' || u.display_name.toLowerCase().includes(q)));
}
function addProgResponsible(i: number, name: string) {
	if (!editPrograms.value[i].responsible.includes(name)) {
		editPrograms.value[i].responsible.push(name);
	}
	progRespSearch.value[i] = '';
	progRespDropdown.value = null;
	markDirty(`prog_${i}_resp`);
	(document.activeElement as HTMLElement)?.blur();
}
function addProgResponsibleFreeText(i: number) {
	const text = (progRespSearch.value[i] ?? '').trim();
	if (text && !editPrograms.value[i].responsible.includes(text)) {
		editPrograms.value[i].responsible.push(text);
	}
	progRespSearch.value[i] = '';
	progRespDropdown.value = null;
	markDirty(`prog_${i}_resp`);
}
function removeProgResponsible(i: number, idx: number) {
	editPrograms.value[i].responsible.splice(idx, 1);
	markDirty(`prog_${i}_resp`);
}
function onProgRespBlur(i: number) {
	setTimeout(() => {
		progRespDropdown.value = null;
	}, 200);
}

// ---- Program description rich editor ----------------------------------------
const progEditorRefs = ref<Record<number, HTMLElement>>({});
const progToolbar = ref<{ idx: number; bold: boolean; italic: boolean; underline: boolean; ul: boolean; ol: boolean; font: string; size: string; color: string; bgColor: string } | null>(null);

function setProgEditorRef(el: any, i: number) {
	if (el) progEditorRefs.value[i] = el as HTMLElement;
	else delete progEditorRefs.value[i];
}

function onProgDescInput(i: number) {
	const el = progEditorRefs.value[i];
	if (el) editPrograms.value[i].description = el.innerHTML;
	markDirty(`prog_${i}_desc`);
	updateProgToolbar(i);
}

function updateProgToolbar(i: number) {
	const el = progEditorRefs.value[i];
	const sel = window.getSelection();
	if (!sel || !sel.rangeCount || !el) return;
	const node = sel.anchorNode?.nodeType === 3 ? sel.anchorNode.parentElement : sel.anchorNode as HTMLElement;
	if (!node || !el.contains(node)) return;
	const cs = window.getComputedStyle(node);
	progToolbar.value = {
		idx: i,
		bold: document.queryCommandState('bold'),
		italic: document.queryCommandState('italic'),
		underline: document.queryCommandState('underline'),
		ul: document.queryCommandState('insertUnorderedList'),
		ol: document.queryCommandState('insertOrderedList'),
		font: cs.fontFamily.replace(/["']/g, '').split(',')[0].trim() || 'Arial',
		size: parseInt(cs.fontSize).toString() || '12',
		color: rgbToHex(cs.color) || '#000000',
		bgColor: (cs.backgroundColor === 'rgba(0, 0, 0, 0)' || cs.backgroundColor === 'transparent') ? '#ffffff' : (rgbToHex(cs.backgroundColor) || '#ffffff'),
	};
}

function rgbToHex(rgb: string): string {
	const m = rgb.match(/^rgb\((\d+),\s*(\d+),\s*(\d+)\)$/);
	if (!m) return rgb;
	return '#' + [m[1], m[2], m[3]].map(x => parseInt(x).toString(16).padStart(2, '0')).join('');
}

let progSavedSelection: Range | null = null;

function progSaveSelection() {
	const sel = window.getSelection();
	const activeIdx = progToolbar.value?.idx;
	if (activeIdx == null) return;
	const el = progEditorRefs.value[activeIdx];
	if (sel?.rangeCount && el?.contains(sel.anchorNode)) {
		progSavedSelection = sel.getRangeAt(0).cloneRange();
	}
}

function progExecCmd(i: number, cmd: string, value?: string) {
	if (progSavedSelection) {
		const sel = window.getSelection();
		if (sel) { sel.removeAllRanges(); sel.addRange(progSavedSelection); }
		progSavedSelection = null;
	}
	document.execCommand(cmd, false, value);
	progEditorRefs.value[i]?.focus();
	onProgDescInput(i);
}

function progSetFontSize(i: number, size: string) {
	if (progSavedSelection) {
		const sel = window.getSelection();
		if (sel) { sel.removeAllRanges(); sel.addRange(progSavedSelection); }
		progSavedSelection = null;
	}
	const sel = window.getSelection();
	if (sel && sel.rangeCount && sel.getRangeAt(0).collapsed) {
		// Collapsed cursor: insert a zero-width space inside a styled span
		const span = document.createElement('span');
		span.style.fontSize = size + 'px';
		span.textContent = '\u200B';
		const range = sel.getRangeAt(0);
		range.insertNode(span);
		// Place cursor after the ZWS inside the span
		const newRange = document.createRange();
		newRange.setStart(span.firstChild!, 1);
		newRange.collapse(true);
		sel.removeAllRanges();
		sel.addRange(newRange);
		const el = progEditorRefs.value[i];
		el?.focus();
		if (progToolbar.value) progToolbar.value.size = size;
		onProgDescInput(i);
		return;
	}
	document.execCommand('fontSize', false, '7');
	const el = progEditorRefs.value[i];
	const fontEls = el?.querySelectorAll('font[size="7"]');
	fontEls?.forEach(fe => {
		const span = document.createElement('span');
		span.style.fontSize = size + 'px';
		span.innerHTML = fe.innerHTML;
		fe.replaceWith(span);
	});
	el?.focus();
	onProgDescInput(i);
}

function progAdjustFontSize(i: number, delta: number) {
	const current = parseInt(progToolbar.value?.size ?? '12') || 12;
	const newSize = Math.max(8, Math.min(72, current + delta));
	progSetFontSize(i, String(newSize));
	if (progToolbar.value) progToolbar.value.size = String(newSize);
}

function onProgEditorFocus(i: number) {
	progToolbar.value = { idx: i, bold: false, italic: false, underline: false, ul: false, ol: false, font: 'Arial', size: '12', color: '#000000', bgColor: '#ffffff' };
	updateProgToolbar(i);
}

function initProgEditors() {
	nextTick(() => {
		for (let i = 0; i < editPrograms.value.length; i++) {
			const el = progEditorRefs.value[i];
			if (el && el.innerHTML !== editPrograms.value[i].description) {
				el.innerHTML = editPrograms.value[i].description;
			}
		}
	});
}

// ---- Location overlap types & helpers ---------------------------------------
interface OverlapInfo {
	id: string;
	title: string;
	start: string;
	end: string;
	department: string | null;
	location: string;
	kind: 'exact' | 'fuzzy';
}

function normalizeLocation(s: string): string {
	return s.toLowerCase().replace(/[^a-z0-9äöüéèêàâ]/g, '');
}

function locationsSimilar(a: string, b: string): boolean {
	if (!a || !b) return false;
	const na = normalizeLocation(a);
	const nb = normalizeLocation(b);
	if (na === nb) return true;
	if (na.includes(nb) || nb.includes(na)) return true;
	// Simple character-level similarity (Sørensen-Dice on bigrams)
	if (na.length < 2 || nb.length < 2) return false;
	const bigrams = (s: string): Set<string> => {
		const set = new Set<string>();
		for (let i = 0; i < s.length - 1; i++) set.add(s.slice(i, i + 2));
		return set;
	};
	const ba = bigrams(na);
	const bb = bigrams(nb);
	let intersection = 0;
	for (const b of ba) if (bb.has(b)) intersection++;
	const dice = (2 * intersection) / (ba.size + bb.size);
	return dice > 0.6;
}

// ---- Attachment helpers -----------------------------------------------------
function attachmentIcon(contentType: string): string {
	if (contentType.startsWith('image/')) return '🖼️';
	if (contentType === 'application/pdf') return '📄';
	return '📁';
}

// ---- Attachment upload ------------------------------------------------------
const duplicateFile = ref<File | null>(null);
const duplicateAttId = ref<string | null>(null);
const showDuplicateDialog = ref(false);
let pendingUploadFiles: File[] = [];
let pendingUploadIndex = 0;

const draggingOver = ref(false);
let dragLeaveTimer: ReturnType<typeof setTimeout> | null = null;

function onDragOver(e: DragEvent) {
	e.preventDefault();
	if (dragLeaveTimer) { clearTimeout(dragLeaveTimer); dragLeaveTimer = null; }
	draggingOver.value = true;
}

function onDragLeave() {
	dragLeaveTimer = setTimeout(() => { draggingOver.value = false; }, 50);
}

async function onDrop(e: DragEvent) {
	e.preventDefault();
	draggingOver.value = false;
	const files = e.dataTransfer?.files;
	if (!files || !files.length) return;
	await startUpload(Array.from(files));
}

async function onAttachmentUpload(e: Event) {
	const files = (e.target as HTMLInputElement).files;
	if (!files || !files.length) return;
	await startUpload(Array.from(files));
	(e.target as HTMLInputElement).value = '';
}

async function startUpload(files: File[]) {
	pendingUploadFiles = files;
	pendingUploadIndex = 0;
	uploadingAttachment.value = true;
	await processNextUpload();
}

async function processNextUpload() {
	while (pendingUploadIndex < pendingUploadFiles.length) {
		const file = pendingUploadFiles[pendingUploadIndex];
		const existing = attachments.value.find(a => a.filename === file.name);
		if (existing) {
			duplicateFile.value = file;
			duplicateAttId.value = existing.id;
			showDuplicateDialog.value = true;
			return; // wait for user decision
		}
		const att = await uploadAttachment(id, file);
		if (att) attachments.value.push(att);
		pendingUploadIndex++;
	}
	uploadingAttachment.value = false;
}

async function onDuplicateReplace() {
	if (duplicateAttId.value) {
		await deleteAttachment(duplicateAttId.value);
		attachments.value = attachments.value.filter(a => a.id !== duplicateAttId.value);
	}
	if (duplicateFile.value) {
		const att = await uploadAttachment(id, duplicateFile.value);
		if (att) attachments.value.push(att);
	}
	showDuplicateDialog.value = false;
	duplicateFile.value = null;
	duplicateAttId.value = null;
	pendingUploadIndex++;
	await processNextUpload();
}

function onDuplicateSkip() {
	showDuplicateDialog.value = false;
	duplicateFile.value = null;
	duplicateAttId.value = null;
	pendingUploadIndex++;
	processNextUpload();
}
async function removeAttachment(attId: string) {
	if (await deleteAttachment(attId)) {
		attachments.value = attachments.value.filter(a => a.id !== attId);
	}
}

// ---- Location overlap warning -----------------------------------------------
const locationOverlaps = computed<OverlapInfo[]>(() => {
	if (!activity.value) return [];
	const loc = mode.value === 'edit' ? editLocation.value.trim() : activity.value.location;
	if (!loc) return [];
	const currentDate = mode.value === 'edit' ? editDate.value : activity.value.date;
	const currentStart = mode.value === 'edit' ? editStartTime.value : activity.value.start_time;
	const currentEnd = mode.value === 'edit' ? editEndTime.value : activity.value.end_time;
	if (!currentDate || !currentStart || !currentEnd) return [];
	const timeOverlaps = activities.value.filter(a =>
		a.id !== id &&
		a.date === currentDate &&
		a.start_time < currentEnd &&
		a.end_time > currentStart
	);
	const results: OverlapInfo[] = [];
	const seenIds = new Set<string>();
	// Exact matches first
	for (const a of timeOverlaps) {
		if (a.location.toLowerCase() === loc.toLowerCase()) {
			results.push({ id: a.id, title: a.title, start: a.start_time, end: a.end_time, department: a.department, location: a.location, kind: 'exact' });
			seenIds.add(a.id);
		}
	}
	// Fuzzy matches (only for non-empty free-text)
	for (const a of timeOverlaps) {
		if (seenIds.has(a.id)) continue;
		if (!a.location) continue;
		if (locationsSimilar(loc, a.location)) {
			results.push({ id: a.id, title: a.title, start: a.start_time, end: a.end_time, department: a.department, location: a.location, kind: 'fuzzy' });
		}
	}
	return results;
});

const exactOverlaps = computed(() => locationOverlaps.value.filter(o => o.kind === 'exact'));
const fuzzyOverlaps = computed(() => locationOverlaps.value.filter(o => o.kind === 'fuzzy'));

// ---- Activity preview popup ------------------------------------------------
const previewActivity = ref<Activity | null>(null);
const previewActivityLoading = ref(false);

async function openActivityPreview(activityId: string) {
	previewActivityLoading.value = true;
	previewActivity.value = null;
	const a = await fetchActivity(activityId);
	previewActivity.value = a;
	previewActivityLoading.value = false;
}

function closeActivityPreview() {
	previewActivity.value = null;
}

// ---- Auto-save --------------------------------------------------------------
import { config } from '../config';
const AUTOSAVE_INTERVAL = config.AUTOSAVE_INTERVAL;
const AUTOSAVE_DEBOUNCE = config.AUTOSAVE_DEBOUNCE;

let autoSaveTimer: ReturnType<typeof setTimeout> | null = null;
let autoSaveInterval: ReturnType<typeof setInterval> | null = null;
let hasPendingChanges = false;

function scheduleAutoSave() {
	if (mode.value !== 'edit') return;
	if (applyingRemote) return; // ignore watcher firings caused by remote WS updates

	if (AUTOSAVE_DEBOUNCE) {
		// Debounce: restart timer on every change, save after idle
		if (autoSaveTimer) clearTimeout(autoSaveTimer);
		autoSaveTimer = setTimeout(doSave, AUTOSAVE_INTERVAL);
	} else {
		// Interval: just mark dirty, the interval will pick it up
		hasPendingChanges = true;
	}
}

function startAutoSaveInterval() {
	if (!AUTOSAVE_DEBOUNCE && !autoSaveInterval) {
		autoSaveInterval = setInterval(() => {
			if (hasPendingChanges && mode.value === 'edit') {
				hasPendingChanges = false;
				doSave();
			}
		}, AUTOSAVE_INTERVAL);
	}
}

function stopAutoSaveInterval() {
	if (autoSaveInterval) {
		clearInterval(autoSaveInterval);
		autoSaveInterval = null;
	}
	hasPendingChanges = false;
}

watch(mode, (val) => {
	if (val !== 'edit') stopAutoSaveInterval();
});

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
		editSikoText,
		editBadWeather,
		editPrograms,
	],
	scheduleAutoSave,
	{ deep: true },
);

// Per-field dirty tracking
watch([editTitle], () => markDirty('title'));
watch([editDate], () => markDirty('date'));
watch([editStartTime], () => markDirty('start_time'));
watch([editEndTime], () => markDirty('end_time'));
watch([editLocation], () => markDirty('location'));
watch([editResponsible], () => markDirty('responsible'), { deep: true });
watch([editDepartment], () => markDirty('department'));
watch([editSikoText], () => markDirty('siko_text'));
watch([editGoal], () => markDirty('goal'));
watch([editBadWeather], () => markDirty('bad_weather'));

// ---- Core save (used by auto-save and the button) --------------------------
async function doSave() {
	if (!activity.value) return;
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
		material: editMaterial.value.filter((m) => m.name.trim()).map(m => ({ name: m.name.trim(), ...(m.responsible?.length ? { responsible: m.responsible } : {}) })),
		siko_text: editSikoText.value.trim() || null,
		bad_weather_info: editBadWeather.value.trim() || null,
		programs: editPrograms.value,
	});

	if (!error.value) showSavedIndicator();
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
			<router-link
				v-if="activity && mode === 'view' && canMail"
				:to="`/activities/${id}/mail`"
				class="btn-mail"
				title="Mail senden"
				>📧 Mail</router-link
			>
			<button
				v-else-if="activity && mode === 'view'"
				class="btn-mail"
				disabled
				title="Kein Zugriff auf Mailversand"
			>
				📧 Mail
			</button>
			<button
				v-if="activity && canEdit"
				class="btn-toggle"
				:class="mode === 'edit' ? 'btn-mail' : 'btn-primary'"
				@click="mode === 'view' ? enterEdit() : (mode = 'view')"
			>
				{{ mode === 'view' ? '✏️ Bearbeiten' : '👁️ Ansicht' }}
			</button>
		</div>
	</header>

	<main class="main">
		<p v-if="loading" class="loading">Laden…</p>
		<p v-else-if="!activity" class="error">Aktivität nicht gefunden.</p>

		<!-- ================================================================ VIEW -->
		<div v-else-if="mode === 'view'" class="detail-view">
			<!-- Hero: Titel + Datum + Stufe -->
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
						<div v-if="exactOverlaps.length" class="field-warning" style="margin-top: 6px">
							⚠️ Überschneidung:
							<ul class="overlap-list">
								<li v-for="o in exactOverlaps" :key="o.id">
									<a href="#" class="overlap-link" @click.prevent="openActivityPreview(o.id)">{{ o.title }}</a>
									({{ o.start }}–{{ o.end }}<template v-if="o.department">, {{ o.department }}</template>)
								</li>
							</ul>
						</div>
						<div v-if="fuzzyOverlaps.length" class="field-hint" style="margin-top: 6px">
							ℹ️ Möglicherweise ähnlicher Ort:
							<ul class="overlap-list">
								<li v-for="o in fuzzyOverlaps" :key="o.id">
									<a href="#" class="overlap-link" @click.prevent="openActivityPreview(o.id)">{{ o.title }}</a>
									– «{{ o.location }}» ({{ o.start }}–{{ o.end }}<template v-if="o.department">, {{ o.department }}</template>)
								</li>
							</ul>
						</div>
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
						v-for="prog in activity.programs"
						:key="prog.id"
						class="program-item"
					>
						<span class="program-time">{{
							prog.time || '—'
						}}</span>
						<div class="program-body">
							<div class="program-header">
								<p class="program-title">{{ prog.title }}</p>
								<span v-if="prog.responsible.length" class="program-resp">{{
									prog.responsible.join(', ')
								}}</span>
							</div>
							<p v-if="prog.description" class="program-desc" v-html="prog.description">
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
						:id="`section-material_${i}`"
						class="material-list-item"
					>
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

			<!-- Anhänge -->
			<div class="detail-section">
				<p class="detail-section-title">Anhänge</p>
				<div v-if="attachments.length" class="user-chips">
					<span
						v-for="att in attachments"
						:key="att.id"
						class="user-chip attachment-chip"
						:class="{ 'attachment-chip--clickable': isPreviewable(att) }"
						@click="isPreviewable(att) && openPreview(att)"
						:title="isPreviewable(att) ? 'Vorschau öffnen' : att.filename"
					>
						<span class="attachment-chip__icon">{{ attachmentIcon(att.content_type) }}</span>
						<span class="attachment-chip__name">{{ att.filename }}</span>
						<button type="button" class="attachment-chip__download" @click.stop="downloadAttachment(att)" title="Herunterladen">
							<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="12" y1="18" x2="12" y2="12"/><polyline points="9 15 12 18 15 15"/></svg>
						</button>
					</span>
				</div>
				<span v-else class="detail-value detail-value--muted">—</span>
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
				<div class="input-save-wrap">
					<input
						id="edit-title"
						v-model="editTitle"
						type="text"
						placeholder="Titel der Aktivität"
						autofocus
						:disabled="isLockedByOther('title')"
					/>
					<span v-if="savedFields['title']" class="field-saved-icon" :key="savedFields['title']">💾</span>
				</div>
			</div>

			<!-- Datum + Zeiten -->
			<div class="form-row form-row--3 lock-wrapper" :class="{ 'is-locked': isLockedByOther('datetime') }"
				@focusin="lockSection('datetime')" @focusout="unlockSection('datetime', $event)">
				<div v-if="lockedBy('datetime')" class="lock-badge">🔒 {{ lockedBy('datetime') }}</div>
				<div class="form-group">
					<label for="edit-date">Datum</label>
					<div class="input-save-wrap">
						<input id="edit-date" v-model="editDate" type="date" :disabled="isLockedByOther('datetime')" />
						<span v-if="savedFields['date']" class="field-saved-icon" :key="savedFields['date']">💾</span>
					</div>
				</div>
				<div class="form-group">
					<label for="edit-start">Startzeit</label>
					<div class="input-save-wrap">
						<input id="edit-start" v-model="editStartTime" type="time" :disabled="isLockedByOther('datetime')" />
						<span v-if="savedFields['start_time']" class="field-saved-icon" :key="savedFields['start_time']">💾</span>
					</div>
				</div>
				<div class="form-group">
					<label for="edit-end">Endzeit</label>
					<div class="input-save-wrap">
						<input id="edit-end" v-model="editEndTime" type="time" :disabled="isLockedByOther('datetime')" />
						<span v-if="savedFields['end_time']" class="field-saved-icon" :key="savedFields['end_time']">💾</span>
					</div>
				</div>
			</div>

			<!-- Ort + Verantwortlich + Stufe -->
			<div class="form-row form-row--3 lock-wrapper" :class="{ 'is-locked': isLockedByOther('location') }"
				@focusin="lockSection('location')" @focusout="unlockSection('location', $event)">
				<div v-if="lockedBy('location')" class="lock-badge">🔒 {{ lockedBy('location') }}</div>
				<div class="form-group">
					<label for="edit-location">Ort</label>
					<div class="input-save-wrap">
						<div class="user-search-wrapper">
							<input
								id="edit-location"
								v-model="editLocation"
								type="text"
								placeholder="Veranstaltungsort"
								@focus="showLocationDropdown = true"
								@blur="onLocationBlur"
								@input="showLocationDropdown = true; markDirty('location')"
								:disabled="isLockedByOther('location')"
							/>
							<div v-if="showLocationDropdown && filteredLocations.length" class="user-dropdown">
								<div
									v-for="loc in filteredLocations"
									:key="loc"
									class="user-dropdown-item"
									:class="{ 'user-dropdown-item--warn': locationOverlapsFor(loc).length }"
									@mousedown.prevent="selectLocation(loc)"
								>
									{{ loc }}
									<span v-if="locationOverlapsFor(loc).length" class="dropdown-warn-hint">
										⚠️ {{ locationOverlapsFor(loc).length }} Überschneidung{{ locationOverlapsFor(loc).length > 1 ? 'en' : '' }}
									</span>
								</div>
							</div>
						</div>
						<span v-if="savedFields['location']" class="field-saved-icon" :key="savedFields['location']">💾</span>
					</div>
					<div v-if="exactOverlaps.length" class="field-warning">
						⚠️ Überschneidung am selben Ort:
						<ul class="overlap-list">
							<li v-for="o in exactOverlaps" :key="o.id">
								<a href="#" class="overlap-link" @click.prevent="openActivityPreview(o.id)">{{ o.title }}</a>
								({{ o.start }}–{{ o.end }}<template v-if="o.department">, {{ o.department }}</template>)
							</li>
						</ul>
					</div>
					<div v-if="fuzzyOverlaps.length" class="field-hint">
						ℹ️ Möglicherweise ähnlicher Ort:
						<ul class="overlap-list">
							<li v-for="o in fuzzyOverlaps" :key="o.id">
								<a href="#" class="overlap-link" @click.prevent="openActivityPreview(o.id)">{{ o.title }}</a>
								– Ort: «{{ o.location }}» ({{ o.start }}–{{ o.end }}<template v-if="o.department">, {{ o.department }}</template>)
							</li>
						</ul>
					</div>
				</div>
				<div class="form-group user-search-group">
					<label>Verantwortlich</label>
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
					<div class="user-chips" v-if="editResponsible.length">
						<span v-for="(name, i) in editResponsible" :key="name" class="user-chip">
							{{ name }}
							<button type="button" class="user-chip-remove" @click="removeResponsible(i)" :disabled="isLockedByOther('location')">✕</button>
						</span>
					</div>
				</div>
				<div class="form-group">
					<label>Stufe</label>
					<div class="user-search-wrapper">
						<input
							type="text"
							:value="editDepartment || departmentSearch"
							@input="departmentSearch = ($event.target as HTMLInputElement).value; editDepartment = '' as any; showDepartmentDropdown = true; markDirty('department')"
							@focus="showDepartmentDropdown = true"
							@blur="onDepartmentBlur"
							placeholder="Stufe wählen…"
							:disabled="isLockedByOther('location') || deptFieldDisabled"
						/>
						<div v-if="showDepartmentDropdown && filteredDepartments.length" class="user-dropdown">
							<div
								v-for="dep in filteredDepartments"
								:key="dep"
								class="user-dropdown-item"
								@mousedown.prevent="selectDepartment(dep)"
							>
								{{ dep }}
							</div>
						</div>
					</div>
					<span v-if="savedFields['department']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['department']">💾</span>
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
								<label>Zeit</label>
								<div class="input-save-wrap">
									<input
										type="text"
										placeholder="z.B. 13:30 bis 14:30"
										:value="prog.time"
										@input="prog.time = ($event.target as HTMLInputElement).value; markDirty(`prog_${i}_time`)"
										:disabled="isLockedByOther(`program_${i}`)"
									/>
									<span v-if="savedFields[`prog_${i}_time`]" class="field-saved-icon" :key="savedFields[`prog_${i}_time`]">💾</span>
								</div>
							</div>
							<div class="form-group">
								<label>Titel</label>
								<div class="input-save-wrap">
									<input v-model="prog.title" type="text" placeholder="Titel" :disabled="isLockedByOther(`program_${i}`)" @input="markDirty(`prog_${i}_title`)" />
									<span v-if="savedFields[`prog_${i}_title`]" class="field-saved-icon" :key="savedFields[`prog_${i}_title`]">💾</span>
								</div>
							</div>
							<div class="form-group user-search-group">
								<label>Verantwortlich</label>
								<div class="user-search-wrapper">
									<input
										type="text"
										:value="progRespSearch[i] ?? ''"
										@input="progRespSearch[i] = ($event.target as HTMLInputElement).value"
										placeholder="Person suchen oder eingeben…"
										@focus="progRespDropdown = i"
										@blur="onProgRespBlur(i)"
										@keydown.enter.prevent="addProgResponsibleFreeText(i)"
										:disabled="isLockedByOther(`program_${i}`)"
									/>
									<div v-if="progRespDropdown === i && progRespFiltered(i).length" class="user-dropdown">
										<div
											v-for="u in progRespFiltered(i)"
											:key="u.id"
											class="user-dropdown-item"
											@mousedown.prevent="addProgResponsible(i, u.display_name)"
										>
											{{ u.display_name }}
										</div>
									</div>
								</div>
								<div class="user-chips" v-if="prog.responsible.length">
									<span v-for="(name, ri) in prog.responsible" :key="name" class="user-chip">
										{{ name }}
										<button type="button" class="user-chip-remove" @click="removeProgResponsible(i, ri)" :disabled="isLockedByOther(`program_${i}`)">✕</button>
									</span>
								</div>
								<span v-if="savedFields[`prog_${i}_resp`]" class="field-saved-icon field-saved-icon--inline" :key="savedFields[`prog_${i}_resp`]">💾</span>
							</div>
							<div class="form-group program-card__full">
								<label>Beschreibung</label>
								<div class="input-save-wrap">
									<div class="rich-editor-toolbar rich-editor-toolbar--compact" v-if="progToolbar && progToolbar.idx === i">
										<select class="toolbar-select" :value="progToolbar.font" @change="progExecCmd(i, 'fontName', ($event.target as HTMLSelectElement).value)" title="Schriftart">
											<option value="Arial" style="font-family:Arial">Arial</option>
											<option value="Helvetica" style="font-family:Helvetica">Helvetica</option>
											<option value="Georgia" style="font-family:Georgia">Georgia</option>
											<option value="Times New Roman" style="font-family:'Times New Roman'">Times New Roman</option>
											<option value="Courier New" style="font-family:'Courier New'">Courier New</option>
											<option value="Verdana" style="font-family:Verdana">Verdana</option>
										</select>
										<input type="text" class="toolbar-select toolbar-select--narrow" :value="progToolbar.size" @mousedown="progSaveSelection" @change="progSetFontSize(i, ($event.target as HTMLInputElement).value)" @keydown.enter.prevent="progSetFontSize(i, ($event.target as HTMLInputElement).value)" title="Schriftgrösse" />
										<button type="button" class="toolbar-btn-sm" @mousedown.prevent @click="progAdjustFontSize(i, -2)" title="Kleiner">A−</button>
										<button type="button" class="toolbar-btn-sm" @mousedown.prevent @click="progAdjustFontSize(i, 2)" title="Grösser">A+</button>
										<span class="toolbar-sep"></span>
										<button type="button" :class="{'toolbar-btn--active': progToolbar.bold}" @mousedown.prevent @click="progExecCmd(i, 'bold')" title="Fett"><b>B</b></button>
										<button type="button" :class="{'toolbar-btn--active': progToolbar.italic}" @mousedown.prevent @click="progExecCmd(i, 'italic')" title="Kursiv"><i>I</i></button>
										<button type="button" :class="{'toolbar-btn--active': progToolbar.underline}" @mousedown.prevent @click="progExecCmd(i, 'underline')" title="Unterstrichen"><u>U</u></button>
										<span class="toolbar-sep"></span>
										<label class="toolbar-color" title="Schriftfarbe" @mousedown="progSaveSelection">
											A
											<input type="color" :value="progToolbar.color" @input="progExecCmd(i, 'foreColor', ($event.target as HTMLInputElement).value)" />
										</label>
										<label class="toolbar-color toolbar-color--bg" title="Hintergrundfarbe" @mousedown="progSaveSelection">
											<span class="toolbar-color-icon">A</span>
											<input type="color" :value="progToolbar.bgColor" @input="progExecCmd(i, 'hiliteColor', ($event.target as HTMLInputElement).value)" />
										</label>
										<span class="toolbar-sep"></span>
										<button type="button" :class="{'toolbar-btn--active': progToolbar.ul}" @mousedown.prevent @click="progExecCmd(i, 'insertUnorderedList')" title="Aufzählung">• Liste</button>
										<button type="button" :class="{'toolbar-btn--active': progToolbar.ol}" @mousedown.prevent @click="progExecCmd(i, 'insertOrderedList')" title="Nummerierte Liste">1. Liste</button>
										<span class="toolbar-sep"></span>
										<button type="button" @mousedown.prevent @click="progExecCmd(i, 'removeFormat')" title="Formatierung entfernen">✕ Format</button>
									</div>
									<div
										:ref="(el) => setProgEditorRef(el, i)"
										class="rich-editor rich-editor--compact"
										:contenteditable="!isLockedByOther(`program_${i}`)"
										@input="onProgDescInput(i)"
										@focus="onProgEditorFocus(i)"
										@mouseup="updateProgToolbar(i)"
										@keyup="updateProgToolbar(i)"
										data-placeholder="Beschreibung…"
									></div>
									<span v-if="savedFields[`prog_${i}_desc`]" class="field-saved-icon field-saved-icon--textarea" :key="savedFields[`prog_${i}_desc`]">💾</span>
								</div>
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
					<div v-for="(_, i) in editMaterial" :key="i" class="material-card lock-wrapper"
						:class="{ 'is-locked': isLockedByOther(`material_${i}`) }"
						@focusin="lockSection(`material_${i}`)" @focusout="unlockSection(`material_${i}`, $event)">
						<div v-if="lockedBy(`material_${i}`)" class="lock-badge">🔒 {{ lockedBy(`material_${i}`) }}</div>
						<button
							v-if="i < editMaterial.length - 1"
							type="button"
							class="program-card__remove"
							@click="removeMaterial(i)"
							:disabled="isLockedByOther(`material_${i}`)"
						>
							✕
						</button>
						<div class="material-card__fields">
							<div class="form-group material-card__name">
								<label>Material</label>
								<div class="input-save-wrap">
									<input
										v-model="editMaterial[i].name"
										type="text"
										placeholder="Material…"
										@input="onMaterialInput(i); markDirty(`mat_${i}`)"
										@blur="onMaterialBlur(i)"
										:disabled="isLockedByOther(`material_${i}`)"
									/>
									<span v-if="savedFields[`mat_${i}`] && editMaterial[i].name.trim()" class="field-saved-icon" :key="savedFields[`mat_${i}`]">💾</span>
								</div>
							</div>
							<div class="form-group material-card__resp user-search-group">
								<label>Verantwortlich</label>
								<div class="user-search-wrapper">
									<input
										type="text"
										:value="materialRespSearch[i] ?? ''"
										@input="materialRespSearch[i] = ($event.target as HTMLInputElement).value"
										placeholder="Person suchen oder eingeben…"
										@focus="materialRespDropdown = i"
										@blur="onMaterialRespBlur(i)"
										@keydown.enter.prevent="addMaterialRespFreeText(i)"
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
								</div>
								<div class="user-chips" v-if="editMaterial[i].responsible?.length">
									<span v-for="(name, ri) in editMaterial[i].responsible" :key="name" class="user-chip">
										{{ name }}
										<button type="button" class="user-chip-remove" @click="clearMaterialResp(i, ri)" :disabled="isLockedByOther(`material_${i}`)">✕</button>
									</span>
								</div>
							</div>
						</div>
					</div>
				</div>
			</div>

			<!-- SiKo -->
			<div class="form-section lock-wrapper" :class="{ 'is-locked': isLockedByOther('siko') }"
				@focusin="lockSection('siko')" @focusout="unlockSection('siko', $event)">
				<div v-if="lockedBy('siko')" class="lock-badge">🔒 {{ lockedBy('siko') }}</div>
				<p class="form-section-title">Sicherheitskonzept</p>
				<div class="form-group">
					<div class="input-save-wrap">
						<textarea
							v-model="editSikoText"
							rows="3"
							placeholder="Sicherheitskonzept (optional)"
							:disabled="isLockedByOther('siko')"
						/>
						<span v-if="savedFields['siko_text']" class="field-saved-icon field-saved-icon--textarea" :key="savedFields['siko_text']">💾</span>
					</div>
				</div>
			</div>

			<!-- Ziel + Schlechtwetter -->
			<div class="form-row lock-wrapper" :class="{ 'is-locked': isLockedByOther('goal_weather') }"
				@focusin="lockSection('goal_weather')" @focusout="unlockSection('goal_weather', $event)">
				<div v-if="lockedBy('goal_weather')" class="lock-badge">🔒 {{ lockedBy('goal_weather') }}</div>
				<div class="form-group">
					<label for="edit-goal">Ziel</label>
					<div class="input-save-wrap">
						<textarea
							id="edit-goal"
							v-model="editGoal"
							rows="3"
							placeholder="Was soll erreicht werden?"
							:disabled="isLockedByOther('goal_weather')"
						/>
						<span v-if="savedFields['goal']" class="field-saved-icon field-saved-icon--textarea" :key="savedFields['goal']">💾</span>
					</div>
				</div>
				<div class="form-group">
					<label for="edit-weather">Schlechtwetter-Info</label>
					<div class="input-save-wrap">
						<textarea
							id="edit-weather"
							v-model="editBadWeather"
							rows="3"
							placeholder="Was passiert bei schlechtem Wetter?"
							:disabled="isLockedByOther('goal_weather')"
						/>
						<span v-if="savedFields['bad_weather']" class="field-saved-icon field-saved-icon--textarea" :key="savedFields['bad_weather']">💾</span>
					</div>
				</div>
			</div>

			<!-- Anhänge -->
			<div class="form-section">
				<p class="form-section-title">Anhänge</p>
				<div
					class="drop-zone"
					:class="{ 'drop-zone--active': draggingOver, 'drop-zone--disabled': uploadingAttachment }"
					@dragover="onDragOver"
					@dragleave="onDragLeave"
					@drop="onDrop"
				>
					<svg class="drop-zone__icon" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
					<span class="drop-zone__text">Dateien hierher ziehen oder
						<label class="drop-zone__browse">
							auswählen
							<input
								id="edit-attachment"
								type="file"
								multiple
								@change="onAttachmentUpload"
								:disabled="uploadingAttachment"
								style="display: none"
							/>
						</label>
					</span>
					<span v-if="uploadingAttachment" class="drop-zone__status">Wird hochgeladen…</span>
				</div>
				<div v-if="attachments.length" class="user-chips" style="margin-top: 10px">
					<span
						v-for="att in attachments"
						:key="att.id"
						class="user-chip attachment-chip"
						:class="{ 'attachment-chip--clickable': isPreviewable(att) }"
						@click="isPreviewable(att) && openPreview(att)"
						:title="isPreviewable(att) ? 'Vorschau öffnen' : att.filename"
					>
						<span class="attachment-chip__icon">{{ attachmentIcon(att.content_type) }}</span>
						<span class="attachment-chip__name">{{ att.filename }}</span>
						<button type="button" class="attachment-chip__download" @click.stop="downloadAttachment(att)" title="Herunterladen">
							<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="12" y1="18" x2="12" y2="12"/><polyline points="9 15 12 18 15 15"/></svg>
						</button>
						<button type="button" class="user-chip-remove" @click.stop="removeAttachment(att.id)" title="Löschen">✕</button>
					</span>
				</div>
			</div>

			<!-- Error -->
			<ErrorAlert :error="error" />

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

	<!-- Duplicate File Dialog -->
	<div v-if="showDuplicateDialog" class="preview-overlay" @click.self="onDuplicateSkip">
		<div class="duplicate-dialog">
			<p class="duplicate-dialog__title">Datei bereits vorhanden</p>
			<p class="duplicate-dialog__text">Die Datei <strong>{{ duplicateFile?.name }}</strong> existiert bereits. Möchtest du sie ersetzen?</p>
			<div class="duplicate-dialog__actions">
				<button type="button" class="btn-secondary" @click="onDuplicateSkip">Überspringen</button>
				<button type="button" class="btn-primary" @click="onDuplicateReplace">Ersetzen</button>
			</div>
		</div>
	</div>

	<!-- Activity Preview Popup -->
	<div v-if="previewActivity || previewActivityLoading" class="preview-overlay" @click.self="closeActivityPreview">
		<div class="activity-preview-popup">
			<div class="activity-preview-popup__header">
				<span v-if="previewActivity">{{ previewActivity.title }}</span>
				<span v-else>Laden…</span>
				<div class="preview-modal__actions">
					<a v-if="previewActivity" :href="`/activities/${previewActivity.id}`" target="_blank" title="In neuem Tab öffnen" class="preview-modal__open-link">
						<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/></svg>
					</a>
					<button type="button" @click="closeActivityPreview" title="Schliessen">✕</button>
				</div>
			</div>
			<div v-if="previewActivityLoading" class="activity-preview-popup__loading">Laden…</div>
			<div v-else-if="previewActivity" class="activity-preview-popup__body">
				<div class="activity-preview-popup__row">
					<span class="detail-label">Datum</span>
					<span>{{ formatDate(previewActivity.date) }}, {{ previewActivity.start_time }}–{{ previewActivity.end_time }}</span>
				</div>
				<div class="activity-preview-popup__row">
					<span class="detail-label">Ort</span>
					<span>{{ previewActivity.location || '—' }}</span>
				</div>
				<div class="activity-preview-popup__row">
					<span class="detail-label">Stufe</span>
					<span>{{ previewActivity.department || '—' }}</span>
				</div>
				<div class="activity-preview-popup__row">
					<span class="detail-label">Verantwortlich</span>
					<span>{{ previewActivity.responsible.length ? previewActivity.responsible.join(', ') : '—' }}</span>
				</div>
				<div v-if="previewActivity.goal" class="activity-preview-popup__row">
					<span class="detail-label">Ziel</span>
					<span>{{ previewActivity.goal }}</span>
				</div>
				<div v-if="previewActivity.programs.length" class="activity-preview-popup__row activity-preview-popup__row--full">
					<span class="detail-label">Programmpunkte</span>
					<ul class="overlap-list" style="margin-top: 4px">
						<li v-for="prog in previewActivity.programs" :key="prog.id">
							<strong>{{ prog.time || '—' }}</strong> {{ prog.title }}
							<span v-if="prog.responsible.length"> – {{ prog.responsible.join(', ') }}</span>
						</li>
					</ul>
				</div>
			</div>
		</div>
	</div>

	<!-- Preview Modal -->
	<div v-if="previewAttachment" class="preview-overlay" @click.self="closePreview">
		<div class="preview-modal">
			<div class="preview-modal__header">
				<span>{{ attachmentIcon(previewAttachment.content_type) }} {{ previewAttachment.filename }}</span>
				<div class="preview-modal__actions">
					<button type="button" @click="downloadAttachment(previewAttachment!)" title="Herunterladen">
						<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="12" y1="18" x2="12" y2="12"/><polyline points="9 15 12 18 15 15"/></svg>
					</button>
					<button type="button" @click="closePreview" title="Schliessen">✕</button>
				</div>
			</div>
			<div class="preview-modal__body">
				<div v-if="previewLoading" class="preview-modal__loading">Laden…</div>
				<template v-else-if="previewBlobUrl">
					<img
						v-if="previewAttachment.content_type.startsWith('image/')"
						:src="previewBlobUrl"
						:alt="previewAttachment.filename"
						class="preview-modal__image"
					/>
					<iframe
						v-else-if="previewAttachment.content_type === 'application/pdf'"
						:src="previewBlobUrl"
						class="preview-modal__pdf"
					></iframe>
				</template>
			</div>
		</div>
	</div>
</template>
