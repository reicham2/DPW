<script setup lang="ts">
import { ref, computed, watch, nextTick, onUnmounted, onMounted } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useActivities } from '../composables/useActivities';
import { useUsers } from '../composables/useUsers';
import { usePermissions } from '../composables/usePermissions';
import { user } from '../composables/useAuth';
import { wsSend, wsRegister, wsJoin, wsLeave } from '../composables/useWebSocket';
import { useForms } from '../composables/useForms';
import { useEventTemplates } from '../composables/useEventTemplates';
import { apiFetch } from '../composables/useApi';
import { useIdeenkiste } from '../composables/useIdeenkiste';
import { ArrowLeft, ClipboardList, Mail, Share2, Pencil, Eye, Check, Save, Users, Lock, X, TriangleAlert, Info, Globe, CheckCircle2, FileDown, Upload, ExternalLink, Trash2, Sun, CloudSun, Cloud, CloudRain, Snowflake, BookMarked } from 'lucide-vue-next';
import type { Activity, Attachment, Department, ProgramInput, EditSection, SectionLock, MaterialItem, FormStats, ActivityExpectedWeather, EventPublication } from '../types';
import type { FormType } from '../types';
import ErrorAlert from '../components/ErrorAlert.vue';
import DepartmentBadge from '../components/DepartmentBadge.vue';
import BadgeSelect from '../components/BadgeSelect.vue';


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
const { myPermissions, fetchMyPermissions, writableDepts, canReadActivity, canForms: canFormsHelper } = usePermissions();

const { fetchForm } = useForms();
const { fetchTemplate: fetchEventTemplate, fetchPublication, publishEvent, unpublishEvent } = useEventTemplates();

const activity = ref<Activity | null>(null);
const activityMissingConfirmed = ref(false);
const loading = ref(true);
const showNoFormDialog = ref(false);
const showNoFormForEventDialog = ref(false);
const showUnpublishConfirm = ref(false);
const eventUnpublishing = ref(false);
const eventFormUrl = ref('');
const liveParticipantsLoading = ref(false);
const liveExpectedParticipants = ref<number | null>(null);
const liveRegistrationCount = ref<number | null>(null);
const liveDeregistrationCount = ref<number | null>(null);
const liveFormType = ref<FormType | null>(null);
const liveHasForm = ref<boolean | null>(null);
const midataLoading = ref(false);
const midataConfigured = ref(false);
const midataChildrenCount = ref<number | null>(null);
const midataError = ref<string | null>(null);
const expectedWeatherLoading = ref(false);
const expectedWeather = ref<ActivityExpectedWeather | null>(null);
const weatherLocationInput = ref('');
const weatherLocationSaved = ref<string | null>(null);
const weatherLocationEditing = ref(true);
const weatherLocationSaving = ref(false);
let expectedWeatherRequestController: AbortController | null = null;
let expectedWeatherRequestSeq = 0;
const WEATHER_EXPECTED_TIMEOUT_MS = 2500;
const WEATHER_EXPECTED_CACHE_TTL_MS = 10 * 60 * 1000;
const mode = ref<'view' | 'edit'>('view');
const isStatsDrawerOpen = ref(false);
const activityActionsMenuOpen = ref(false);
const activityActionsMenuRef = ref<HTMLElement | null>(null);
const activityActionsMenuPanelRef = ref<HTMLElement | null>(null);
const activityActionsMenuPanelStyle = ref<Record<string, string>>({});
const shareToken = ref<string | null>(null);
const shareUrl = ref<string | null>(null);
const shareLoading = ref(false);
const shareCopied = ref(false);
const eventPublication = ref<EventPublication | null>(null);
const showEventPreview = ref(false);
const eventPreviewTitle = ref('');
const eventPreviewBody = ref('');
const eventPublishing = ref(false);
const eventPreviewEditorRef = ref<HTMLElement | null>(null);
const evtPreviewToolbar = ref<{ bold: boolean; italic: boolean; underline: boolean; ul: boolean; ol: boolean; font: string; size: string; color: string; bgColor: string } | null>(null);
let evtPreviewSavedSelection: Range | null = null;
let evtPreviewLinkSavedRange: Range | null = null;
const showEvtPreviewLinkDialog = ref(false);
const evtPreviewLinkUrl = ref('');
const evtPreviewLinkInputRef = ref<HTMLInputElement | null>(null);

function configuredPublicBaseUrl(): string | null {
	const normalized = (config.PUBLIC_BASE_URL || '').trim().replace(/\/+$/, '');
	return normalized || null;
}

const canShareActivity = computed(() => !!configuredPublicBaseUrl())

const shareButtonTitle = computed(() => {
	if (!canShareActivity.value) return 'Öffentliche Basis-URL ist nicht konfiguriert'
	if (shareLoading.value) return 'Share-Link wird erstellt'
	if (!shareToken.value) return 'Share-Link nicht verfügbar'
	return 'Share-Link kopieren'
})

function onEvtPreviewInput() {
	updateEvtPreviewToolbar();
}

function updateEvtPreviewToolbar() {
	const el = eventPreviewEditorRef.value;
	const sel = window.getSelection();
	if (!sel || !sel.rangeCount || !el) return;
	const node = sel.anchorNode?.nodeType === 3 ? sel.anchorNode.parentElement : sel.anchorNode as HTMLElement;
	if (!node || !el.contains(node)) return;
	const cs = window.getComputedStyle(node);
	evtPreviewToolbar.value = {
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

function evtPreviewSaveSelection() {
	const sel = window.getSelection();
	if (sel?.rangeCount && eventPreviewEditorRef.value?.contains(sel.anchorNode)) {
		evtPreviewSavedSelection = sel.getRangeAt(0).cloneRange();
	}
}

function evtPreviewExecCmd(cmd: string, value?: string) {
	if (evtPreviewSavedSelection) {
		const sel = window.getSelection();
		if (sel) { sel.removeAllRanges(); sel.addRange(evtPreviewSavedSelection); }
		evtPreviewSavedSelection = null;
	}
	document.execCommand(cmd, false, value);
	eventPreviewEditorRef.value?.focus();
	onEvtPreviewInput();
}

function evtPreviewSetFontSize(size: string) {
	if (evtPreviewSavedSelection) {
		const sel = window.getSelection();
		if (sel) { sel.removeAllRanges(); sel.addRange(evtPreviewSavedSelection); }
		evtPreviewSavedSelection = null;
	}
	const sel = window.getSelection();
	if (sel && sel.rangeCount && sel.getRangeAt(0).collapsed) {
		const span = document.createElement('span');
		span.style.fontSize = size + 'px';
		span.textContent = '\u200B';
		const range = sel.getRangeAt(0);
		range.insertNode(span);
		const newRange = document.createRange();
		newRange.setStart(span.firstChild!, 1);
		newRange.collapse(true);
		sel.removeAllRanges();
		sel.addRange(newRange);
		eventPreviewEditorRef.value?.focus();
		if (evtPreviewToolbar.value) evtPreviewToolbar.value.size = size;
		onEvtPreviewInput();
		return;
	}
	document.execCommand('fontSize', false, '7');
	const fontEls = eventPreviewEditorRef.value?.querySelectorAll('font[size="7"]');
	fontEls?.forEach(fe => {
		const span = document.createElement('span');
		span.style.fontSize = size + 'px';
		span.innerHTML = fe.innerHTML;
		fe.replaceWith(span);
	});
	eventPreviewEditorRef.value?.focus();
	onEvtPreviewInput();
}

function evtPreviewAdjustFontSize(delta: number) {
	const current = parseInt(evtPreviewToolbar.value?.size ?? '12') || 12;
	const newSize = Math.max(8, Math.min(72, current + delta));
	evtPreviewSetFontSize(String(newSize));
}

function openEvtPreviewLinkDialog() {
	evtPreviewLinkSavedRange = evtPreviewSavedSelection;
	evtPreviewSavedSelection = null;
	const sel = window.getSelection();
	const anchor = sel?.focusNode?.parentElement?.closest('a') as HTMLAnchorElement | null;
	evtPreviewLinkUrl.value = anchor?.getAttribute('href') ?? '';
	showEvtPreviewLinkDialog.value = true;
	nextTick(() => evtPreviewLinkInputRef.value?.focus());
}

function confirmEvtPreviewLink() {
	showEvtPreviewLinkDialog.value = false;
	if (!evtPreviewLinkSavedRange) return;
	const sel = window.getSelection();
	if (sel) { sel.removeAllRanges(); sel.addRange(evtPreviewLinkSavedRange); }
	if (evtPreviewLinkUrl.value.trim()) {
		document.execCommand('createLink', false, evtPreviewLinkUrl.value.trim());
	} else {
		document.execCommand('unlink');
	}
	eventPreviewEditorRef.value?.focus();
	evtPreviewLinkSavedRange = null;
}

function cancelEvtPreviewLink() {
	showEvtPreviewLinkDialog.value = false;
	evtPreviewLinkSavedRange = null;
}

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

function openStatsDrawer() {
	isStatsDrawerOpen.value = true;
}

function closeStatsDrawer() {
	isStatsDrawerOpen.value = false;
}

function toggleStatsDrawer() {
	isStatsDrawerOpen.value = !isStatsDrawerOpen.value;
}

function closeActivityActionsMenu() {
	activityActionsMenuOpen.value = false;
	activityActionsMenuPanelStyle.value = {};
}

function toggleActivityActionsMenu() {
	activityActionsMenuOpen.value = !activityActionsMenuOpen.value;
}

function updateActivityActionsMenuPosition() {
	if (!activityActionsMenuOpen.value || !activityActionsMenuPanelRef.value) return;
	const rect = activityActionsMenuPanelRef.value.getBoundingClientRect();
	const viewportPadding = 8;
	let shiftX = 0;
	if (rect.right > window.innerWidth - viewportPadding) {
		shiftX -= rect.right - (window.innerWidth - viewportPadding);
	}
	if (rect.left + shiftX < viewportPadding) {
		shiftX += viewportPadding - (rect.left + shiftX);
	}
	activityActionsMenuPanelStyle.value = shiftX
		? { transform: `translateX(${Math.round(shiftX)}px)` }
		: {};
}

function onWindowResize() {
	if (!activityActionsMenuOpen.value) return;
	updateActivityActionsMenuPosition();
}

function onDocumentPointerDown(event: PointerEvent) {
	if (!activityActionsMenuOpen.value) return;
	const target = event.target as Node | null;
	if (!target || !activityActionsMenuRef.value?.contains(target)) {
		closeActivityActionsMenu();
	}
}

function onStatsDrawerKeydown(event: KeyboardEvent) {
	if (event.key !== 'Escape') return;
	closeStatsDrawer();
	closeActivityActionsMenu();
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

interface EditSnapshot {
	title: string;
	date: string;
	startTime: string;
	endTime: string;
	goal: string;
	location: string;
	responsible: string[];
	department: Department | '';
	material: MaterialItem[];
	sikoText: string;
	badWeather: string;
	programs: ProgramInput[];
}

interface LocalEditDraft {
	version: 1;
	activityId: string;
	savedAt: number;
	data: EditSnapshot;
}

const editSnapshot = ref<EditSnapshot | null>(null);
const pendingLocalDraft = ref<LocalEditDraft | null>(null);
const localDraftRestoredAt = ref<number | null>(null);
const LOCAL_DRAFT_WRITE_DEBOUNCE_MS = 350;
let localDraftWriteTimer: ReturnType<typeof setTimeout> | null = null;

function cloneSnapshotMaterial(material: MaterialItem[]): MaterialItem[] {
	return material.map((m) => ({ name: m.name, responsible: m.responsible ? [...m.responsible] : [] }));
}

function cloneSnapshotPrograms(programs: ProgramInput[]): ProgramInput[] {
	return programs.map((p) => ({
		duration_minutes: p.duration_minutes,
		title: p.title,
		description: p.description,
		responsible: [...p.responsible],
	}));
}

function applyEditSnapshot(snapshot: EditSnapshot) {
	editTitle.value = snapshot.title;
	editDate.value = snapshot.date;
	editStartTime.value = snapshot.startTime;
	editEndTime.value = snapshot.endTime;
	editGoal.value = snapshot.goal;
	editLocation.value = snapshot.location;
	editResponsible.value = [...snapshot.responsible];
	editDepartment.value = snapshot.department;
	editMaterial.value = cloneSnapshotMaterial(snapshot.material);
	editSikoText.value = snapshot.sikoText;
	editBadWeather.value = snapshot.badWeather;
	editPrograms.value = cloneSnapshotPrograms(snapshot.programs);
}

function buildCurrentEditSnapshot(): EditSnapshot {
	return {
		title: editTitle.value,
		date: editDate.value,
		startTime: editStartTime.value,
		endTime: editEndTime.value,
		goal: editGoal.value,
		location: editLocation.value,
		responsible: [...editResponsible.value],
		department: editDepartment.value,
		material: cloneSnapshotMaterial(editMaterial.value),
		sikoText: editSikoText.value,
		badWeather: editBadWeather.value,
		programs: cloneSnapshotPrograms(editPrograms.value),
	};
}

function localDraftStorageKey() {
	return `dpw:activity-edit-draft:${id}`;
}

function readLocalDraft(): LocalEditDraft | null {
	try {
		const raw = window.localStorage.getItem(localDraftStorageKey());
		if (!raw) return null;
		const parsed = JSON.parse(raw) as Partial<LocalEditDraft>;
		if (parsed.version !== 1) return null;
		if (parsed.activityId !== id) return null;
		if (!parsed.data || typeof parsed.savedAt !== 'number') return null;
		return parsed as LocalEditDraft;
	} catch {
		return null;
	}
}

function clearLocalDraft() {
	if (localDraftWriteTimer) {
		clearTimeout(localDraftWriteTimer);
		localDraftWriteTimer = null;
	}
	try {
		window.localStorage.removeItem(localDraftStorageKey());
	} catch {
		/* ignore unavailable storage */
	}
}

function writeLocalDraftNow() {
	if (mode.value !== 'edit') return;
	const draft: LocalEditDraft = {
		version: 1,
		activityId: id,
		savedAt: Date.now(),
		data: buildCurrentEditSnapshot(),
	};
	try {
		window.localStorage.setItem(localDraftStorageKey(), JSON.stringify(draft));
	} catch {
		/* ignore unavailable storage */
	}
}

function scheduleLocalDraftWrite() {
	if (mode.value !== 'edit' || suppressDirtyTracking || applyingRemote) return;
	if (localDraftWriteTimer) clearTimeout(localDraftWriteTimer);
	localDraftWriteTimer = setTimeout(() => {
		writeLocalDraftNow();
	}, LOCAL_DRAFT_WRITE_DEBOUNCE_MS);
}

function loadPendingLocalDraft() {
	const draft = readLocalDraft();
	if (!draft) {
		pendingLocalDraft.value = null;
		return;
	}
	const currentSnapshot = JSON.stringify(buildCurrentEditSnapshot());
	const draftSnapshot = JSON.stringify(draft.data);
	if (currentSnapshot === draftSnapshot) {
		clearLocalDraft();
		pendingLocalDraft.value = null;
		return;
	}
	pendingLocalDraft.value = draft;
}

function applyLocalDraft() {
	if (!pendingLocalDraft.value) return;
	suppressDirtyTracking = true;
	applyEditSnapshot(pendingLocalDraft.value.data);
	initProgEditors();
	pendingLocalDraft.value = null;
	localDraftRestoredAt.value = Date.now();
	nextTick(() => {
		suppressDirtyTracking = false;
		scheduleLocalDraftWrite();
		scheduleAutoSave();
	});
}

function discardLocalDraft() {
	clearLocalDraft();
	pendingLocalDraft.value = null;
	localDraftRestoredAt.value = null;
}

function captureEditSnapshot() {
	editSnapshot.value = buildCurrentEditSnapshot();
}

function restoreEditSnapshot() {
	if (!editSnapshot.value) return;
	applyEditSnapshot(editSnapshot.value);
}

// ---- Attachments state -----------------------------------------------------
const attachments = ref<Attachment[]>([]);
const uploadingAttachment = ref(false);
const editAttachmentIdsSnapshot = ref<string[]>([]);
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
	return canReadActivity(activity.value, user.value.display_name, user.value.department);
});

function canEditByScope(scope: 'none' | 'own' | 'same_dept' | 'all') {
	if (!user.value || !activity.value) return false;
	const isOwnResponsible = () => {
		if (!user.value || !activity.value) return false;
		const normalize = (value: string) => value.trim().toLowerCase();
		const userName = normalize(user.value.display_name ?? '');
		const userEmail = normalize(user.value.email ?? '');
		const userEmailLocal = userEmail.includes('@') ? userEmail.split('@')[0] : '';
		return activity.value.responsible.some((entry) => {
			const candidate = normalize(entry ?? '');
			return !!candidate && (candidate === userName || candidate === userEmail || (!!userEmailLocal && candidate === userEmailLocal));
		});
	};
	if (scope === 'all') return true;
	if (scope === 'same_dept') {
		return (!!activity.value.department && activity.value.department === user.value.department) || isOwnResponsible();
	}
	if (scope === 'own') {
		return isOwnResponsible();
	}
	return false;
}

const canEdit = computed(() => {
	const scope = myPermissions.value?.activity_edit_scope ?? 'none';
	return canEditByScope(scope);
});

const canDelete = computed(() => {
	const scope = myPermissions.value?.activity_edit_scope ?? 'none';
	return canEditByScope(scope);
});

const canMail = computed(() => {
	if (!user.value || !activity.value) return false;
	const p = myPermissions.value;
	if (!p) return false;
	const normalize = (value: string) => value.trim().toLowerCase();
	const userName = normalize(user.value.display_name ?? '');
	const userEmail = normalize(user.value.email ?? '');
	const userEmailLocal = userEmail.includes('@') ? userEmail.split('@')[0] : '';
	const isOwnResponsible = activity.value.responsible.some((entry) => {
		const candidate = normalize(entry ?? '');
		return !!candidate && (candidate === userName || candidate === userEmail || (!!userEmailLocal && candidate === userEmailLocal));
	});
	if (p.mail_send_scope === 'all') return true;
	if (p.mail_send_scope === 'same_dept' && activity.value.department === user.value.department) return true;
	if (p.mail_send_scope === 'own' && isOwnResponsible) return true;
	return false;
});

async function handleMailClick() {
	const existingForm = await fetchForm(id);
	if (existingForm) {
		router.push(`/activities/${id}/mail`);
	} else {
		showNoFormDialog.value = true;
	}
}

const canForms = computed(() => {
	if (!user.value || !activity.value) return false;
	return canFormsHelper(activity.value, user.value.display_name, user.value.department);
});

const canPublishEvent = computed(() => {
	if (!user.value || !activity.value) return false;
	if (!config.WP_PUBLISHING_ENABLED) return false;
	const p = myPermissions.value;
	if (!p) return false;
	if (p.event_publish_scope === 'all') return true;
	if (p.event_publish_scope === 'own_dept' && activity.value.department && activity.value.department === user.value.department) return true;
	if (p.event_publish_scope === 'own' && user.value.display_name && activity.value.responsible.includes(user.value.display_name)) return true;
	return false;
});

function evtFormatDateLong(d: string): string {
	return new Date(d + 'T00:00:00').toLocaleDateString('de-DE', {
		weekday: 'long', day: 'numeric', month: 'long', year: 'numeric'
	});
}
function evtFormatDateShort(d: string): string {
	return new Date(d + 'T00:00:00').toLocaleDateString('de-DE', {
		day: '2-digit', month: '2-digit', year: 'numeric'
	});
}
function evtFormatPrograms(act: Activity): string {
	if (!act.programs.length) return '';
	return act.programs.map(p => {
		const dur = p.duration_minutes ? `${p.duration_minutes} min` : '';
		const resp = p.responsible && p.responsible.length ? ' (' + p.responsible.join(', ') + ')' : '';
		const desc = p.description ? ': ' + p.description : '';
		return `${dur} – ${p.title}${resp}${desc}`;
	}).join('\n');
}

function evtSubstituteVarsPlain(text: string, act: Activity, formUrl = ''): string {
	// Handle {{formular_link}} and {{formular_link|Text}} → plain URL (strip custom label)
	text = text.replace(/\{\{formular_link(?:\|[^}]*)?\}\}/gi, formUrl);
	const vars: Record<string, string> = {
		titel: act.title, datum: evtFormatDateLong(act.date), datum_kurz: evtFormatDateShort(act.date),
		startzeit: act.start_time, endzeit: act.end_time, ort: act.location,
		verantwortlich: act.responsible.join(', '), abteilung: act.department ?? '',
		ziel: act.goal, material: act.material.map(m => m.name).join(', ') || '',
		schlechtwetter: act.bad_weather_info ?? '', programm: evtFormatPrograms(act),
	};
	return text.replace(/\{\{(\w+)\}\}/gi, (m, key) => {
		const lk = key.toLowerCase();
		return lk in vars ? vars[lk] : m;
	});
}

function evtSubstituteVarsHtml(text: string, act: Activity, formUrl = ''): string {
	const container = document.createElement('div');
	container.innerHTML = text;
	const vars: Record<string, string> = {
		titel: act.title, datum: evtFormatDateLong(act.date), datum_kurz: evtFormatDateShort(act.date),
		startzeit: act.start_time, endzeit: act.end_time, ort: act.location,
		verantwortlich: act.responsible.join(', '), abteilung: act.department ?? '',
		ziel: act.goal, material: act.material.map(m => m.name).join(', ') || '',
		schlechtwetter: act.bad_weather_info ?? '', programm: evtFormatPrograms(act),
	};
	const replacer = (m: string, key: string) => { const lk = key.toLowerCase(); return lk in vars ? vars[lk] : m; };
	const walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT);
	const textNodes: Text[] = [];
	while (walker.nextNode()) textNodes.push(walker.currentNode as Text);
	for (const node of textNodes) {
		const original = node.nodeValue ?? '';
		// Handle {{formular_link}} or {{formular_link|Eigener Text}} → <a> element
		if (/\{\{formular_link(?:\|[^}]*)?\}\}/i.test(original)) {
			const parts = original.split(/\{\{formular_link(?:\|([^}]*))?\}\}/i);
			const parent = node.parentNode!;
			for (let i = 0; i < parts.length; i++) {
				if (i % 2 === 0) {
					const partText = parts[i].replace(/\{\{(\w+)\}\}/gi, replacer);
					if (partText) parent.insertBefore(document.createTextNode(partText), node);
				} else {
					const a = document.createElement('a');
					a.href = formUrl || '#';
					a.textContent = parts[i] || 'Zum Formular';
					parent.insertBefore(a, node);
				}
			}
			parent.removeChild(node);
			continue;
		}
		const replaced = original.replace(/\{\{(\w+)\}\}/gi, replacer);
		if (replaced !== original) node.nodeValue = replaced;
	}
	// Handle {{formular_link}} in href attributes
	for (const el of Array.from(container.querySelectorAll('[href]'))) {
		const href = el.getAttribute('href') ?? '';
		const replaced = href.replace(/\{\{formular_link(?:\|[^}]*)?\}\}/gi, formUrl)
			.replace(/\{\{(\w+)\}\}/gi, replacer);
		if (replaced !== href) el.setAttribute('href', replaced);
	}
	return container.innerHTML;
}

// Cache for the event template so we only fetch it once
let cachedEventTemplate: { dept: string; tpl: import('../types').EventTemplate | null } | null = null;

function setEvtPreviewEditorHtml(html: string) {
	nextTick(() => {
		if (eventPreviewEditorRef.value) eventPreviewEditorRef.value.innerHTML = html;
	});
}

async function openEventPreviewModal() {
	if (!activity.value) return;
	eventPreviewTitle.value = activity.value.title;
	showEventPreview.value = true;
	setEvtPreviewEditorHtml('');

	const dept = activity.value.department;
	if (!dept) return;

	let tpl: import('../types').EventTemplate | null = null;
	if (cachedEventTemplate && cachedEventTemplate.dept === dept) {
		tpl = cachedEventTemplate.tpl;
	} else {
		tpl = await fetchEventTemplate(dept);
		cachedEventTemplate = { dept, tpl };
	}

	if (tpl && (tpl.title || tpl.body) && activity.value) {
		eventPreviewTitle.value = evtSubstituteVarsPlain(tpl.title, activity.value, eventFormUrl.value);
		setEvtPreviewEditorHtml(evtSubstituteVarsHtml(tpl.body, activity.value, eventFormUrl.value));
	}
}

async function handlePublishEventClick() {
	if (!activity.value) return;

	// If already published, show existing publication data
	if (eventPublication.value) {
		eventPreviewTitle.value = eventPublication.value.title;
		showEventPreview.value = true;
		setEvtPreviewEditorHtml(eventPublication.value.body_html);
		return;
	}

	// Check if a form exists for this activity
	const existingForm = await fetchForm(id);
	if (!existingForm) {
		showNoFormForEventDialog.value = true;
		return;
	}
	const baseUrl = configuredPublicBaseUrl();
	if (!baseUrl) {
		error.value = 'Öffentliche Basis-URL ist nicht konfiguriert.';
		return;
	}
	eventFormUrl.value = `${baseUrl}/forms/${existingForm.public_slug}`;
	await openEventPreviewModal();
}

async function publishWithoutForm() {
	showNoFormForEventDialog.value = false;
	eventFormUrl.value = '';
	await openEventPreviewModal();
}

async function confirmPublishEvent() {
	if (!activity.value) return;
	// Sync editor content before publishing
	if (eventPreviewEditorRef.value) eventPreviewBody.value = eventPreviewEditorRef.value.innerHTML;
	eventPublishing.value = true;
	const result = await publishEvent(id, eventPreviewTitle.value, eventPreviewBody.value);
	eventPublishing.value = false;
	if (result) {
		eventPublication.value = result;
		showEventPreview.value = false;
	}
}

async function confirmUnpublishEvent() {
	eventUnpublishing.value = true;
	await unpublishEvent(id);
	eventUnpublishing.value = false;
	eventPublication.value = null;
	showUnpublishConfirm.value = false;
	showEventPreview.value = false;
}

let liveParticipantsRefreshTimer: ReturnType<typeof setInterval> | null = null;
let detailDeferredLoadTimer: ReturnType<typeof setTimeout> | null = null;

function isPastActivityDate(date: string | undefined): boolean {
	if (!date) return false;
	const today = new Date();
	const yyyy = today.getUTCFullYear();
	const mm = String(today.getUTCMonth() + 1).padStart(2, '0');
	const dd = String(today.getUTCDate()).padStart(2, '0');
	return date < `${yyyy}-${mm}-${dd}`;
}

async function refreshLiveParticipants() {
	liveParticipantsLoading.value = true;
	try {
		const formRes = await apiFetch(`/api/activities/${id}/form`);
		if (!formRes.ok) {
			liveHasForm.value = false;
			liveFormType.value = null;
			liveExpectedParticipants.value = null;
			liveRegistrationCount.value = null;
			liveDeregistrationCount.value = null;
			return;
		}
		type LiveFormPayload = { exists?: boolean; form?: { id?: string; form_type?: FormType } | null };
		type LiveFormData = { id?: string; form_type?: FormType };
		const formPayload = (await formRes.json()) as LiveFormPayload | LiveFormData | null;
		const formData: LiveFormData | null = (
			formPayload &&
			typeof formPayload === 'object' &&
			'exists' in formPayload
		)
			? (formPayload.exists && formPayload.form ? formPayload.form : null)
			: (formPayload as LiveFormData | null);

		if (!formData?.id) {
			liveHasForm.value = false;
			liveFormType.value = null;
			liveExpectedParticipants.value = null;
			liveRegistrationCount.value = null;
			liveDeregistrationCount.value = null;
			return;
		}

		liveHasForm.value = true;
		const statsRes = await apiFetch(`/api/activities/${id}/form/stats`);
		if (!statsRes.ok) {
			liveFormType.value = formData?.form_type ?? null;
			liveExpectedParticipants.value = null;
			liveRegistrationCount.value = null;
			liveDeregistrationCount.value = null;
			return;
		}
		type LiveStatsPayload = { exists?: boolean; stats?: FormStats | null };
		const statsPayload = (await statsRes.json()) as LiveStatsPayload | FormStats | null;
		const statsData: FormStats | null = (
			statsPayload &&
			typeof statsPayload === 'object' &&
			'exists' in statsPayload
		)
			? (statsPayload.exists && statsPayload.stats ? statsPayload.stats : null)
			: (statsPayload as FormStats | null);

		if (!statsData) {
			const fallbackType = formData.form_type ?? null;
			liveFormType.value = fallbackType;
			if (fallbackType === 'registration') {
				liveRegistrationCount.value = 0;
				liveDeregistrationCount.value = null;
				liveExpectedParticipants.value = 0;
			} else if (fallbackType === 'deregistration') {
				liveRegistrationCount.value = null;
				liveDeregistrationCount.value = 0;
				liveExpectedParticipants.value = null;
			} else {
				liveRegistrationCount.value = 0;
				liveDeregistrationCount.value = 0;
				liveExpectedParticipants.value = 0;
			}
			return;
		}

		const formType: FormType | null = statsData.form_type ?? formData.form_type ?? null;
		liveFormType.value = formType;
		const registration = statsData.registration_count ?? statsData.by_mode?.registration ?? 0;
		const deregistration = statsData.deregistration_count ?? statsData.by_mode?.deregistration ?? 0;

		if (formType === 'registration') {
			liveRegistrationCount.value = registration;
			liveDeregistrationCount.value = null;
			liveExpectedParticipants.value = registration;
		} else if (formType === 'deregistration') {
			liveRegistrationCount.value = null;
			liveDeregistrationCount.value = deregistration;
			liveExpectedParticipants.value = null;
		} else {
			liveRegistrationCount.value = registration;
			liveDeregistrationCount.value = deregistration;
			liveExpectedParticipants.value =
				typeof statsData.expected_current === 'number'
					? statsData.expected_current
					: registration - deregistration;
		}
	} catch {
		liveHasForm.value = null;
		liveFormType.value = null;
		liveExpectedParticipants.value = null;
		liveRegistrationCount.value = null;
		liveDeregistrationCount.value = null;
	} finally {
		liveParticipantsLoading.value = false;
	}
}

const registrationsExceedMidata = computed(() => {
	if (!config.MIDATA_ENABLED) return false;
	if (liveFormType.value !== 'registration') return false;
	if (typeof liveRegistrationCount.value !== 'number') return false;
	if (typeof midataChildrenCount.value !== 'number') return false;
	return liveRegistrationCount.value > midataChildrenCount.value;
});

const liveActivitySumDisplay = computed(() => {
	if (liveParticipantsLoading.value) return 'Lädt…';
	if (liveHasForm.value === false) return 'kein Formular';
	if (liveFormType.value === 'registration') {
		return String(liveRegistrationCount.value ?? 0);
	}
	if (liveFormType.value === 'deregistration') {
		if (typeof midataChildrenCount.value !== 'number') {
			return String(liveDeregistrationCount.value ?? 0);
		}
		return String(midataChildrenCount.value - (liveDeregistrationCount.value ?? 0));
	}
	if (typeof liveExpectedParticipants.value === 'number') {
		return String(liveExpectedParticipants.value);
	}
	return '—';
});

async function refreshActivityMidataChildren() {
	if (!config.MIDATA_ENABLED) {
		midataConfigured.value = false;
		midataChildrenCount.value = null;
		midataError.value = null;
		midataLoading.value = false;
		return;
	}
	midataLoading.value = true;
	try {
		const res = await apiFetch(`/api/activities/${id}/midata/children-count`);
		if (!res.ok) {
			midataConfigured.value = false;
			midataChildrenCount.value = null;
			midataError.value = null;
			return;
		}
		const data = (await res.json()) as { configured?: boolean; children_count?: number | null; error?: string };
		midataConfigured.value = !!data.configured;
		midataChildrenCount.value = typeof data.children_count === 'number' ? data.children_count : null;
		midataError.value = data.error ?? null;
	} catch {
		midataConfigured.value = false;
		midataChildrenCount.value = null;
		midataError.value = null;
	} finally {
		midataLoading.value = false;
	}
}

async function refreshExpectedWeather() {
	let location = weatherLocationInput.value.trim();
	if (!location && weatherLocationSaved.value) {
		location = weatherLocationSaved.value.trim();
	}
	const pastActivity = isPastActivityDate(activity.value?.date);
	if (!location && !pastActivity) {
		expectedWeather.value = null;
		expectedWeatherLoading.value = false;
		return;
	}

	const locationKey = location.toLowerCase();
	const cacheKey = `dpw.weatherExpected:${id}:${pastActivity ? 'past' : 'future'}:${locationKey}`;
	try {
		const raw = sessionStorage.getItem(cacheKey);
		if (raw) {
			const parsed = JSON.parse(raw) as { ts?: number; data?: ActivityExpectedWeather };
			if (
				typeof parsed?.ts === 'number' &&
				parsed.data &&
				Date.now() - parsed.ts < WEATHER_EXPECTED_CACHE_TTL_MS
			) {
				expectedWeather.value = parsed.data;
				expectedWeatherLoading.value = false;
				return;
			}
		}
	} catch {
		/* ignore cache issues */
	}

	expectedWeatherRequestSeq += 1;
	const requestSeq = expectedWeatherRequestSeq;
	if (expectedWeatherRequestController) {
		expectedWeatherRequestController.abort();
	}
	const controller = new AbortController();
	expectedWeatherRequestController = controller;
	const timeout = setTimeout(() => controller.abort(), WEATHER_EXPECTED_TIMEOUT_MS);
	expectedWeatherLoading.value = true;
	try {
		const query = location ? `?location=${encodeURIComponent(location)}` : '';
		const res = await apiFetch(`/api/activities/${id}/weather-expected${query}`, {
			signal: controller.signal,
		});
		if (requestSeq !== expectedWeatherRequestSeq) return;
		if (!res.ok) return;
		const data = (await res.json()) as ActivityExpectedWeather;
		expectedWeather.value = data;
		try {
			sessionStorage.setItem(
				cacheKey,
				JSON.stringify({ ts: Date.now(), data }),
			);
		} catch {
			/* ignore cache issues */
		}
	} catch (e) {
		if (!(e instanceof Error) || e.name !== 'AbortError') {
			throw e;
		}
	} finally {
		clearTimeout(timeout);
		if (expectedWeatherRequestController === controller) {
			expectedWeatherRequestController = null;
		}
		if (requestSeq === expectedWeatherRequestSeq) {
			expectedWeatherLoading.value = false;
		}
	}
}

async function fetchWeatherLocation() {
	try {
		const res = await apiFetch(`/api/activities/${id}/weather-location`);
		if (!res.ok) {
			weatherLocationSaved.value = null;
			weatherLocationEditing.value = true;
			return;
		}
		const data = (await res.json()) as { location?: string | null };
		const location = (data.location ?? '').trim();
		if (location) {
			weatherLocationSaved.value = location;
			weatherLocationInput.value = location;
			weatherLocationEditing.value = false;
		} else {
			weatherLocationSaved.value = null;
			weatherLocationEditing.value = true;
		}
		void refreshExpectedWeather();
	} catch {
		weatherLocationSaved.value = null;
		weatherLocationEditing.value = true;
	}
}

async function saveWeatherLocation() {
	const location = weatherLocationInput.value.trim();
	if (!location) return;
	weatherLocationSaving.value = true;
	try {
		const res = await apiFetch(`/api/activities/${id}/weather-location`, {
			method: 'PUT',
			body: JSON.stringify({ location }),
		});
		if (!res.ok) return;
		weatherLocationSaved.value = location;
		weatherLocationEditing.value = false;
		void refreshExpectedWeather();
	} finally {
		weatherLocationSaving.value = false;
	}
}

function startWeatherLocationEdit() {
	weatherLocationInput.value = weatherLocationSaved.value ?? weatherLocationInput.value;
	weatherLocationEditing.value = true;
}

const weatherSymbolIcon = computed(() => {
	switch (expectedWeather.value?.weather_symbol) {
		case 'sun':
			return Sun;
		case 'partly-cloudy':
			return CloudSun;
		case 'cloud':
			return Cloud;
		case 'rain':
			return CloudRain;
		case 'snow':
			return Snowflake;
		default:
			return CloudSun;
	}
});

const weatherTemperatureDisplay = computed(() => {
	if (!expectedWeather.value?.available) return '—';
	if (
		typeof expectedWeather.value.temperature_min_c === 'number'
		&& typeof expectedWeather.value.temperature_max_c === 'number'
	) {
		return `${Math.round(expectedWeather.value.temperature_min_c)}° | ${Math.round(expectedWeather.value.temperature_max_c)}°`;
	}
	return `${(expectedWeather.value.temperature_c ?? 0).toFixed(1)} °C`;
});

const weatherRainProbabilityDisplay = computed(() => {
	if (!expectedWeather.value?.available) return '—';
	return typeof expectedWeather.value.rain_probability_percent === 'number'
		? `${expectedWeather.value.rain_probability_percent} %`
		: '—';
});

const weatherChartSamples = computed(() => {
	const samples = expectedWeather.value?.hourly_temps ?? [];
	if (!Array.isArray(samples)) return [] as Array<{ ts_unix: number; temperature_c: number }>;
	return samples.filter((p) => Number.isFinite(p.ts_unix) && Number.isFinite(p.temperature_c));
});

type WeatherChartPoint = {
	x: number;
	y: number;
	temperatureC: number;
	timeLabel: string;
	tempLabel: string;
};

const weatherChartWidth = 320;
const weatherChartHeight = 84;
const weatherChartPlotLeft = 42;
const weatherChartPlotRight = 312;
const weatherChartPlotTop = 10;
const weatherChartPlotBottom = 74;

const weatherChartStats = computed(() => {
	const points = weatherChartSamples.value;
	if (!points.length) {
		return {
			min: 0,
			max: 0,
			span: 1,
		};
	}
	const min = Math.min(...points.map((p) => p.temperature_c));
	const max = Math.max(...points.map((p) => p.temperature_c));
	return {
		min,
		max,
		span: Math.max(0.1, max - min),
	};
});

const weatherChartPoints = computed<WeatherChartPoint[]>(() => {
	const samples = weatherChartSamples.value;
	if (samples.length < 2) return [];
	const plotWidth = weatherChartPlotRight - weatherChartPlotLeft;
	const plotHeight = weatherChartPlotBottom - weatherChartPlotTop;
	const { min, span } = weatherChartStats.value;
	return samples.map((sample, i) => {
		const x = weatherChartPlotLeft + (i / (samples.length - 1)) * plotWidth;
		const y = weatherChartPlotBottom - ((sample.temperature_c - min) / span) * plotHeight;
		return {
			x,
			y,
			temperatureC: sample.temperature_c,
			timeLabel: new Date(sample.ts_unix * 1000).toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' }),
			tempLabel: `${sample.temperature_c.toFixed(1)} °C`,
		};
	});
});

const weatherChartYTicks = computed(() => {
	const { min, max } = weatherChartStats.value;
	const values = [max, (max + min) / 2, min];
	const uniqueValues = values.filter((value, idx) => values.findIndex((v) => Math.abs(v - value) < 0.05) === idx);
	return uniqueValues.map((value) => {
		const ratio = (value - min) / weatherChartStats.value.span;
		const y = weatherChartPlotBottom - ratio * (weatherChartPlotBottom - weatherChartPlotTop);
		return {
			value,
			y,
			label: `${Math.round(value)}°`,
		};
	});
});

const weatherHoveredPointIndex = ref<number | null>(null);

const weatherHoveredPoint = computed(() => {
	if (weatherHoveredPointIndex.value === null) return null;
	return weatherChartPoints.value[weatherHoveredPointIndex.value] ?? null;
});

function onWeatherChartMouseMove(event: MouseEvent) {
	const points = weatherChartPoints.value;
	if (!points.length) {
		weatherHoveredPointIndex.value = null;
		return;
	}
	const chart = event.currentTarget as SVGSVGElement | null;
	if (!chart) return;
	const rect = chart.getBoundingClientRect();
	if (!rect.width) return;
	const x = ((event.clientX - rect.left) / rect.width) * weatherChartWidth;
	let nearest = 0;
	let bestDist = Number.POSITIVE_INFINITY;
	for (let i = 0; i < points.length; i += 1) {
		const dist = Math.abs(points[i].x - x);
		if (dist < bestDist) {
			bestDist = dist;
			nearest = i;
		}
	}
	weatherHoveredPointIndex.value = nearest;
}

function onWeatherChartMouseLeave() {
	weatherHoveredPointIndex.value = null;
}

const weatherChartPath = computed(() => {
	const points = weatherChartPoints.value;
	if (points.length < 2) return '';
	return points
		.map((p, i) => {
			return `${i === 0 ? 'M' : 'L'} ${p.x.toFixed(1)} ${p.y.toFixed(1)}`;
		})
		.join(' ');
});

const weatherChartStartLabel = computed(() => {
	const first = weatherChartSamples.value[0];
	if (!first) return '';
	return new Date(first.ts_unix * 1000).toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' });
});

const weatherChartEndLabel = computed(() => {
	const samples = weatherChartSamples.value;
	const last = samples[samples.length - 1];
	if (!last) return '';
	return new Date(last.ts_unix * 1000).toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' });
});

function parseTimeToMinutes(time: string): number | null {
	if (!time) return null;
	const parts = time.split(':');
	if (parts.length < 2) return null;
	const h = Number(parts[0]);
	const m = Number(parts[1]);
	if (!Number.isFinite(h) || !Number.isFinite(m)) return null;
	return h * 60 + m;
}

type DayBucket = 'saturday' | 'weekday' | 'sunday' | 'unknown';

function getDayBucket(dateStr: string): DayBucket {
	if (!dateStr) return 'unknown';
	const d = new Date(`${dateStr}T00:00:00`);
	if (Number.isNaN(d.getTime())) return 'unknown';
	const day = d.getDay();
	if (day === 6) return 'saturday';
	if (day >= 1 && day <= 5) return 'weekday';
	if (day === 0) return 'sunday';
	return 'unknown';
}

function getSimilarityWindowStart(dateStr: string): number | null {
	if (!dateStr) return null;
	const d = new Date(`${dateStr}T00:00:00`);
	if (Number.isNaN(d.getTime())) return null;
	d.setFullYear(d.getFullYear() - 1);
	d.setMonth(d.getMonth() - 1);
	return d.getTime();
}

const similarActivities = computed(() => {
	if (!activity.value?.department) return [] as Activity[];
	const current = activity.value;
	const currentDate = Date.parse(current.date);
	const windowStartDate = getSimilarityWindowStart(current.date);
	const currentBucket = getDayBucket(current.date);
	const currentStart = parseTimeToMinutes(current.start_time);

	return activities.value
		.filter((a) => a.id !== current.id)
		.filter((a) => a.department === current.department)
		.filter((a) => {
			if (Number.isNaN(currentDate)) return true;
			const candidateDate = Date.parse(a.date);
			if (Number.isNaN(candidateDate)) return false;
			if (candidateDate > currentDate) return false;
			if (windowStartDate !== null && candidateDate < windowStartDate) return false;
			return true;
		})
		.filter((a) => getDayBucket(a.date) === currentBucket)
		.sort((a, b) => {
			const aStart = parseTimeToMinutes(a.start_time);
			const bStart = parseTimeToMinutes(b.start_time);
			const aDiff = currentStart === null || aStart === null ? Number.POSITIVE_INFINITY : Math.abs(aStart - currentStart);
			const bDiff = currentStart === null || bStart === null ? Number.POSITIVE_INFINITY : Math.abs(bStart - currentStart);
			if (aDiff !== bDiff) return aDiff - bDiff;
			return Date.parse(b.date) - Date.parse(a.date);
		});
});

const similarActivityStats = ref<Record<string, number>>({});
const similarActivityEstimateLoading = ref(false);
let similarActivityEstimateRequestId = 0;

function expectedParticipantsFromStats(stats: FormStats): number | null {
	if (typeof stats.expected_current === 'number') return stats.expected_current;
	const registration = stats.registration_count ?? stats.by_mode?.registration ?? 0;
	const deregistration = stats.deregistration_count ?? stats.by_mode?.deregistration ?? 0;
	return registration - deregistration;
}

async function refreshEstimatedParticipantsFromSimilar() {
	const requestId = ++similarActivityEstimateRequestId;
	const candidates = similarActivities.value.slice(0, 8);
	if (!candidates.length) {
		similarActivityStats.value = {};
		similarActivityEstimateLoading.value = false;
		return;
	}

	similarActivityEstimateLoading.value = true;
	const nextStats: Record<string, number> = {};

	for (const candidate of candidates) {
		if (requestId !== similarActivityEstimateRequestId) return;
		try {
			const res = await apiFetch(`/api/activities/${candidate.id}/form/stats`);
			if (!res.ok) continue;
			const payload = await res.json();
			if (payload && typeof payload === 'object' && 'exists' in payload) {
				if (!payload.exists || !payload.stats) continue;
			}
			const stats = (payload && typeof payload === 'object' && 'stats' in payload
				? payload.stats
				: payload) as FormStats;
			const expected = expectedParticipantsFromStats(stats);
			if (typeof expected !== 'number' || !Number.isFinite(expected) || expected < 0) continue;
			nextStats[candidate.id] = Math.round(expected);
		} catch {
			/* ignore invalid candidates */
		}
	}

	if (requestId !== similarActivityEstimateRequestId) return;
	similarActivityStats.value = nextStats;
	similarActivityEstimateLoading.value = false;
}

const similarActivitiesKey = computed(() => {
	const ids = similarActivities.value.slice(0, 8).map((a) => a.id).join('|');
	return `${activity.value?.id ?? ''}|${ids}`;
});

watch(similarActivitiesKey, () => {
	void refreshEstimatedParticipantsFromSimilar();
}, { immediate: true });

watch(mode, () => {
	closeStatsDrawer();
});

const estimatedParticipantsFromSimilar = computed(() => {
	const sample = similarActivities.value
		.map((a) => similarActivityStats.value[a.id])
		.filter((value): value is number => typeof value === 'number')
		.slice(0, 5);
	if (!sample.length) return null;
	const avg = sample.reduce((sum, value) => sum + value, 0) / sample.length;
	return Math.round(avg);
});

const estimatedParticipantsDisplay = computed(() => {
	if (typeof estimatedParticipantsFromSimilar.value === 'number') return String(estimatedParticipantsFromSimilar.value);
	if (similarActivityEstimateLoading.value) return 'Berechnung…';
	return '—';
});

const estimateSourceActivities = computed(() => {
	return similarActivities.value
		.filter((a) => typeof similarActivityStats.value[a.id] === 'number')
		.slice(0, 2);
});

onMounted(async () => {
	const permissionTask = fetchMyPermissions().catch(() => null);
	try {
		await Promise.race([
			permissionTask,
			new Promise<null>((resolve) => setTimeout(() => resolve(null), 1200)),
		]);

		const fetchedActivity = await fetchActivity(id);
		activity.value = fetchedActivity;

		if (!activity.value && error.value) {
			const retryActivity = await fetchActivity(id);
			if (retryActivity) {
				activity.value = retryActivity;
			}
		}

		activityMissingConfirmed.value = !activity.value && !error.value;

		if (activity.value) {
			if (myPermissions.value && !canView.value) {
				router.replace('/');
				return;
			}

			document.title = `${activity.value.title} – DPWeb`;
		}
	} finally {
		loading.value = false;
	}

	void permissionTask.then(() => {
		if (activity.value && myPermissions.value && !canView.value) {
			router.replace('/');
		}
	});

	if (activity.value) {
		void fetchAttachments(id).then((items) => {
			attachments.value = items;
		});
		if (canShareActivity.value) {
			void fetchShareLink();
		}
		if (config.WP_PUBLISHING_ENABLED) {
			void fetchPublication(id).then(pub => { eventPublication.value = pub; });
		}
		const preloadTasks: Promise<unknown>[] = [];
		if (departments.value.length === 0) preloadTasks.push(fetchDepartments());
		if (predefinedLocations.value.length === 0) preloadTasks.push(fetchLocations());
		if (activities.value.length === 0) preloadTasks.push(fetchActivities());
		if (preloadTasks.length > 0) {
			void Promise.allSettled(preloadTasks);
		}

		detailDeferredLoadTimer = setTimeout(() => {
			void refreshLiveParticipants();
			void refreshActivityMidataChildren();
			void fetchWeatherLocation();

			liveParticipantsRefreshTimer = setInterval(() => {
				void refreshLiveParticipants();
				void refreshActivityMidataChildren();
				void refreshExpectedWeather();
			}, config.MIDATA_WEATHER_REFRESH_INTERVAL);
		}, 250);
	}

	// Register identity and join this activity for collaborative editing
	if (user.value) {
		wsRegister(user.value.display_name, user.value.microsoft_oid);
		wsJoin(id);
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

onMounted(() => {
	window.addEventListener('keydown', onStatsDrawerKeydown);
	document.addEventListener('pointerdown', onDocumentPointerDown);
	window.addEventListener('resize', onWindowResize);
});

watch(activityActionsMenuOpen, async (open) => {
	if (!open) {
		activityActionsMenuPanelStyle.value = {};
		return;
	}
	await nextTick();
	updateActivityActionsMenuPosition();
});

onUnmounted(() => {
	expectedWeatherRequestSeq += 1;
	if (expectedWeatherRequestController) {
		expectedWeatherRequestController.abort();
		expectedWeatherRequestController = null;
	}
	similarActivityEstimateRequestId += 1;
	if (mode.value === 'edit') writeLocalDraftNow();
	if (localDraftWriteTimer) {
		clearTimeout(localDraftWriteTimer);
		localDraftWriteTimer = null;
	}
	if (autoSaveTimer) clearTimeout(autoSaveTimer);
	if (savedTimer) clearTimeout(savedTimer);
	stopAutoSaveInterval();
	if (detailDeferredLoadTimer) {
		clearTimeout(detailDeferredLoadTimer);
		detailDeferredLoadTimer = null;
	}
	if (liveParticipantsRefreshTimer) {
		clearInterval(liveParticipantsRefreshTimer);
		liveParticipantsRefreshTimer = null;
	}
	window.removeEventListener('keydown', onStatsDrawerKeydown);
	document.removeEventListener('pointerdown', onDocumentPointerDown);
	window.removeEventListener('resize', onWindowResize);
	// Leave the activity and unlock any held section
	wsLeave();
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
		duration_minutes: p.duration_minutes,
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
				duration_minutes: p.duration_minutes,
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
	localDraftRestoredAt.value = null;
	syncEditFields(activity.value);
	captureEditSnapshot();
	loadPendingLocalDraft();
	editAttachmentIdsSnapshot.value = attachments.value.map(a => a.id);
	error.value = null;
	mode.value = 'edit';
	void fetchUsers();
	startAutoSaveInterval();
	initProgEditors();
	nextTick(() => {
		suppressDirtyTracking = false;
		dirtyFields.clear();
		savedFields.value = {};
	});
}

async function cancelEdit() {
	if (autoSaveTimer) {
		clearTimeout(autoSaveTimer);
		autoSaveTimer = null;
	}
	hasPendingChanges = false;
	stopAutoSaveInterval();
	suppressDirtyTracking = true;

	// Roll back attachments that were uploaded during this edit session.
	const snapshot = new Set(editAttachmentIdsSnapshot.value);
	const addedDuringEdit = attachments.value.filter(a => !snapshot.has(a.id));
	for (const att of addedDuringEdit) {
		await deleteAttachment(att.id);
	}
	attachments.value = attachments.value.filter(a => snapshot.has(a.id));

	restoreEditSnapshot();
	initProgEditors();
	dirtyFields.clear();
	savedFields.value = {};
	await doSave();
	if (!error.value) {
		mode.value = 'view';
	}
	nextTick(() => {
		suppressDirtyTracking = false;
		dirtyFields.clear();
		savedFields.value = {};
	});
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
const editWritableDepts = computed(() => writableDepts(user.value?.department));
const deptFieldDisabled = computed(() => editWritableDepts.value.length <= 1);
const editDeptItems = computed(() =>
	editWritableDepts.value.map((d) => ({ value: d })),
);

// ---- Programs --------------------------------------------------------------
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

const timeDisplayMode = computed<'minutes' | 'clock'>(
	() => user.value?.time_display_mode === 'clock' ? 'clock' : 'minutes'
);

function programLabelFor(a: Activity | null, index: number): string {
	if (!a) return '';
	const progs = a.programs;
	if (timeDisplayMode.value === 'clock') {
		let acc = 0;
		for (let i = 0; i < index; i++) acc += progs[i].duration_minutes || 0;
		const start = addMinutesToClock(a.start_time, acc);
		const end = addMinutesToClock(a.start_time, acc + (progs[index].duration_minutes || 0));
		return start && end ? `${start} – ${end}` : start || '—';
	}
	return formatDuration(progs[index].duration_minutes || 0);
}

function programSecondaryLabelFor(a: Activity | null, index: number): string {
	if (!a) return '';
	const progs = a.programs;
	if (timeDisplayMode.value === 'clock') {
		return formatDuration(progs[index].duration_minutes || 0);
	}
	let acc = 0;
	for (let i = 0; i < index; i++) acc += progs[i].duration_minutes || 0;
	const start = addMinutesToClock(a.start_time, acc);
	const end = addMinutesToClock(a.start_time, acc + (progs[index].duration_minutes || 0));
	return start && end ? `${start} – ${end}` : start || '—';
}

function viewProgramLabel(index: number): string {
	return programLabelFor(activity.value, index);
}

function viewProgramSecondaryLabel(index: number): string {
	return programSecondaryLabelFor(activity.value, index);
}

function previewProgramLabel(index: number): string {
	return programLabelFor(previewActivity.value, index);
}

function editProgramLabel(index: number): string {
	const start = editStartTime.value;
	if (!start) return '';
	if (timeDisplayMode.value === 'clock') {
		let acc = 0;
		for (let i = 0; i < index; i++) acc += Number(editPrograms.value[i].duration_minutes) || 0;
		const s = addMinutesToClock(start, acc);
		const e = addMinutesToClock(start, acc + (Number(editPrograms.value[index].duration_minutes) || 0));
		const clock = s && e ? `${s} – ${e}` : '';
		const dur = formatDuration(Number(editPrograms.value[index].duration_minutes) || 0);
		return clock ? `${clock}  · ${dur}` : '';
	}
	let acc = 0;
	for (let i = 0; i < index; i++) acc += Number(editPrograms.value[i].duration_minutes) || 0;
	const s = addMinutesToClock(start, acc);
	const e = addMinutesToClock(start, acc + (Number(editPrograms.value[index].duration_minutes) || 0));
	return s && e ? `${s} – ${e}` : '';
}

function addProgram() {
	editPrograms.value.push({
		duration_minutes: 0,
		title: '',
		description: '',
		responsible: editResponsible.value.length ? [editResponsible.value[0]] : [],
	});
}
function removeProgram(i: number) {
	editPrograms.value.splice(i, 1);
}

// ---- Ideenkiste integration --------------------------------------------------
const { items: ideenkisteItems, fetchItems: fetchIdeenkiste, createItem: createIdeenkisteItem } = useIdeenkiste();
const showIdeenDropdown = ref(false);
const ideenSearch = ref('');
const ideenSaving = ref<number | null>(null);

const ideenFiltered = computed(() => {
	const q = ideenSearch.value.toLowerCase();
	return q ? ideenkisteItems.value.filter(it =>
		it.title.toLowerCase().includes(q) || it.description.toLowerCase().includes(q)
	) : ideenkisteItems.value;
});

function addProgramFromIdee(item: typeof ideenkisteItems.value[0]) {
	editPrograms.value.push({
		duration_minutes: item.duration_minutes,
		title: item.title,
		description: item.description,
		responsible: [],
	});
	showIdeenDropdown.value = false;
	ideenSearch.value = '';
}

async function saveToIdeenkiste(i: number) {
	const prog = editPrograms.value[i];
	ideenSaving.value = i;
	await createIdeenkisteItem({
		title: prog.title,
		duration_minutes: prog.duration_minutes,
		description: prog.description,
		department: activity.value?.department ?? null,
	});
	ideenSaving.value = null;
}

function openIdeenDropdown() {
	showIdeenDropdown.value = true;
	fetchIdeenkiste();
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
let progLinkSavedRange: Range | null = null;
const showProgLinkDialog = ref(false);
const progLinkUrl = ref('');
const progLinkInputRef = ref<HTMLInputElement | null>(null);

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

function openProgLinkDialog() {
	progLinkSavedRange = progSavedSelection;
	progSavedSelection = null;
	const sel = window.getSelection();
	const anchor = sel?.focusNode?.parentElement?.closest('a') as HTMLAnchorElement | null;
	progLinkUrl.value = anchor?.getAttribute('href') ?? '';
	showProgLinkDialog.value = true;
	nextTick(() => progLinkInputRef.value?.focus());
}

function confirmProgLink() {
	showProgLinkDialog.value = false;
	if (!progLinkSavedRange) return;
	const sel = window.getSelection();
	if (sel) { sel.removeAllRanges(); sel.addRange(progLinkSavedRange); }
	if (progLinkUrl.value.trim()) {
		document.execCommand('createLink', false, progLinkUrl.value.trim());
	} else {
		document.execCommand('unlink');
	}
	const idx = progToolbar.value?.idx ?? 0;
	progEditorRefs.value[idx]?.focus();
	onProgDescInput(idx);
	progLinkSavedRange = null;
}

function cancelProgLink() {
	showProgLinkDialog.value = false;
	progLinkSavedRange = null;
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
const locationWarningOpen = ref(false);

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
const autosaveInterval = () => config.AUTOSAVE_INTERVAL;
const autosaveDebounce = () => config.AUTOSAVE_DEBOUNCE;

let autoSaveTimer: ReturnType<typeof setTimeout> | null = null;
let autoSaveInterval: ReturnType<typeof setInterval> | null = null;
let hasPendingChanges = false;

function scheduleAutoSave() {
	if (mode.value !== 'edit') return;
	if (suppressDirtyTracking) return;
	if (applyingRemote) return; // ignore watcher firings caused by remote WS updates
	scheduleLocalDraftWrite();

	if (autosaveDebounce()) {
		// Debounce: restart timer on every change, save after idle
		if (autoSaveTimer) clearTimeout(autoSaveTimer);
		autoSaveTimer = setTimeout(() => doSave({ skipFuzzyCheck: true }), autosaveInterval());
	} else {
		// Interval: just mark dirty, the interval will pick it up
		hasPendingChanges = true;
	}
}

function startAutoSaveInterval() {
	if (!autosaveDebounce() && !autoSaveInterval) {
		autoSaveInterval = setInterval(() => {
			if (hasPendingChanges && mode.value === 'edit') {
				hasPendingChanges = false;
				doSave({ skipFuzzyCheck: true });
			}
		}, autosaveInterval());
	}
}

function stopAutoSaveInterval() {
	if (autoSaveInterval) {
		clearInterval(autoSaveInterval);
		autoSaveInterval = null;
	}
	hasPendingChanges = false;
}

function flushAutoSave() {
	if (autoSaveTimer) {
		clearTimeout(autoSaveTimer);
		autoSaveTimer = null;
		doSave();
	}
	if (hasPendingChanges) {
		hasPendingChanges = false;
		doSave();
	}
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

// ---- Fuzzy location overlap confirmation dialog ----------------------------
const showFuzzyDialog = ref(false);
const fuzzyDialogOverlaps = ref<OverlapInfo[]>([]);
let pendingFuzzyAcknowledged = false;

function confirmFuzzyAndSave() {
	showFuzzyDialog.value = false;
	pendingFuzzyAcknowledged = true;
	doSave();
}

function cancelFuzzyDialog() {
	showFuzzyDialog.value = false;
	pendingFuzzyAcknowledged = false;
}

// ---- Core save (used by auto-save and the button) --------------------------
async function doSave(opts: { skipFuzzyCheck?: boolean } = {}) {
	if (!activity.value) return;
	error.value = null;
	writeLocalDraftNow();

	// When there are fuzzy (but not exact) overlaps, ask for confirmation unless already acknowledged
	if (!opts.skipFuzzyCheck && !pendingFuzzyAcknowledged && fuzzyOverlaps.value.length > 0) {
		fuzzyDialogOverlaps.value = fuzzyOverlaps.value;
		showFuzzyDialog.value = true;
		return;
	}
	pendingFuzzyAcknowledged = false;

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

	if (!error.value) {
		showSavedIndicator();
		clearLocalDraft();
		pendingLocalDraft.value = null;
		localDraftRestoredAt.value = null;
	}
}

// ---- Delete ----------------------------------------------------------------
async function doDelete() {
	if (!confirm(`Aktivität "${activity.value?.title || id}" wirklich löschen?`))
		return;
	await deleteActivity(id);
	router.push('/');
}

// ---- Share link ------------------------------------------------------------
async function fetchShareLink() {
	if (!canShareActivity.value) {
		shareToken.value = null;
		shareUrl.value = null;
		shareLoading.value = false;
		return;
	}
	shareLoading.value = true;
	try {
		const res = await apiFetch(`/api/activities/${id}/share`);
		if (res.ok) {
			const data = await res.json();
			if (data.share_token) {
				shareToken.value = data.share_token;
				shareUrl.value = data.share_url || null;
				return;
			}
		}

		// Backfill old activities: create a share link once when none exists yet.
		const createRes = await apiFetch(`/api/activities/${id}/share`, { method: 'POST' });
		if (createRes.ok) {
			const created = await createRes.json();
			shareToken.value = created.share_token || null;
			shareUrl.value = created.share_url || null;
		}
	} catch (e) {
		error.value = e instanceof Error ? e.message : 'Share-Link konnte nicht geladen werden.';
	}
	finally {
		shareLoading.value = false;
	}
}

function copyShareLink() {
	if (!canShareActivity.value || !shareToken.value) return;
	const baseUrl = configuredPublicBaseUrl();
	const url = shareUrl.value || (baseUrl ? `${baseUrl}/shared/${shareToken.value}` : '');
	if (!url) return;
	navigator.clipboard.writeText(url);
	shareCopied.value = true;
	setTimeout(() => { shareCopied.value = false; }, 2000);
}
</script>

<template>
	<header class="header">
		<button class="btn-back" @click="flushAutoSave(); router.push('/')">
			<ArrowLeft class="btn-icon" :size="16" aria-hidden="true" />
			Zurück
		</button>
		<div class="header-right">
			<div v-if="activity && mode === 'view'" ref="activityActionsMenuRef" class="activity-actions-mobile">
				<button
					type="button"
					class="activity-actions-mobile-toggle"
					:aria-expanded="activityActionsMenuOpen"
					aria-label="Aktionen öffnen"
					@click="toggleActivityActionsMenu"
				>
					<span class="activity-actions-mobile-icon" aria-hidden="true">
						<span />
						<span />
						<span />
					</span>
					<span>Aktionen</span>
				</button>
				<div
					v-if="activityActionsMenuOpen"
					ref="activityActionsMenuPanelRef"
					class="activity-actions-mobile-menu"
					:style="activityActionsMenuPanelStyle"
				>
					<router-link
						v-if="canForms"
						:to="`/activities/${id}/forms`"
						class="activity-actions-mobile-item"
						@click="closeActivityActionsMenu"
					>
						<ClipboardList class="btn-icon" :size="16" aria-hidden="true" />
						<span>Formular</span>
					</router-link>
					<button v-else type="button" class="activity-actions-mobile-item" disabled>
						<ClipboardList class="btn-icon" :size="16" aria-hidden="true" />
						<span>Formular</span>
					</button>
					<button
						v-if="canMail"
						type="button"
						class="activity-actions-mobile-item"
						@click="closeActivityActionsMenu(); handleMailClick()"
					>
						<Mail class="btn-icon" :size="16" aria-hidden="true" />
						<span>Mail</span>
					</button>
					<button v-else type="button" class="activity-actions-mobile-item" disabled>
						<Mail class="btn-icon" :size="16" aria-hidden="true" />
						<span>Mail</span>
					</button>
					<button
						v-if="config.WP_PUBLISHING_ENABLED && !eventPublication"
						type="button"
						class="activity-actions-mobile-item"
						:disabled="!canPublishEvent"
						@click="closeActivityActionsMenu(); handlePublishEventClick()"
					>
						<Globe class="btn-icon" :size="16" aria-hidden="true" />
						<span>Veröffentlichen</span>
					</button>
					<button
						v-else-if="config.WP_PUBLISHING_ENABLED && eventPublication"
						type="button"
						class="activity-actions-mobile-item activity-actions-mobile-item--active"
						:disabled="!canPublishEvent"
						@click="closeActivityActionsMenu(); handlePublishEventClick()"
					>
						<CheckCircle2 class="btn-icon" :size="16" aria-hidden="true" />
						<span>Veröffentlicht (WP)</span>
					</button>
					<button
						type="button"
						class="activity-actions-mobile-item"
						:class="{ 'activity-actions-mobile-item--active': shareCopied }"
						:disabled="!canShareActivity || shareLoading || !shareToken"
						@click="closeActivityActionsMenu(); copyShareLink()"
					>
						<Check v-if="shareCopied" class="btn-icon" :size="16" aria-hidden="true" />
						<Share2 v-else class="btn-icon" :size="16" aria-hidden="true" />
						<span>{{ shareCopied ? 'Kopiert' : (shareLoading ? 'Teilen…' : 'Teilen') }}</span>
					</button>
				</div>
			</div>
			<div class="activity-actions-desktop">
				<router-link
					v-if="activity && mode === 'view' && canForms"
					:to="`/activities/${id}/forms`"
					class="btn-mail"
					title="Formular verwalten"
					><ClipboardList class="btn-icon" :size="16" aria-hidden="true" />Formular</router-link
				>
				<button
					v-else-if="activity && mode === 'view'"
					class="btn-mail"
					disabled
					title="Kein Zugriff auf Formulare"
				>
					<ClipboardList class="btn-icon" :size="16" aria-hidden="true" />
					Formular
				</button>
				<button
					v-if="activity && mode === 'view' && canMail"
					class="btn-mail"
					title="Mail senden"
					@click="handleMailClick"
				>
					<Mail class="btn-icon" :size="16" aria-hidden="true" />
					Mail
				</button>
				<button
					v-else-if="activity && mode === 'view'"
					class="btn-mail"
					disabled
					title="Kein Zugriff auf Mailversand"
				>
					<Mail class="btn-icon" :size="16" aria-hidden="true" />
					Mail
				</button>
				<button
					v-if="activity && mode === 'view' && config.WP_PUBLISHING_ENABLED && !eventPublication"
					class="btn-mail"
					:disabled="!canPublishEvent"
					:title="canPublishEvent ? 'Als Event veröffentlichen' : 'Keine Berechtigung zum Veröffentlichen'"
					@click="handlePublishEventClick"
				>
					<Globe class="btn-icon" :size="16" aria-hidden="true" />
					Veröffentlichen
				</button>
				<button
					v-else-if="activity && mode === 'view' && config.WP_PUBLISHING_ENABLED && eventPublication"
					class="btn-mail btn-mail--active"
					:disabled="!canPublishEvent"
					:title="!canPublishEvent ? 'Keine Berechtigung zum Bearbeiten' : 'Veröffentlicht + WordPress synchronisiert'"
					@click="handlePublishEventClick"
				>
					<CheckCircle2 class="btn-icon" :size="16" aria-hidden="true" />
					Veröffentlicht (WP)
				</button>
				<div v-if="activity && mode === 'view'" class="share-link-wrap">
					<button
						class="btn-mail"
						:class="{ 'btn-mail--active': shareCopied }"
						:title="shareButtonTitle"
						:disabled="!canShareActivity || shareLoading || !shareToken"
						@click="copyShareLink"
					>
						<Check v-if="shareCopied" class="btn-icon" :size="16" aria-hidden="true" />
						<Share2 v-else class="btn-icon" :size="16" aria-hidden="true" />
						{{ shareCopied ? 'Kopiert' : (shareLoading ? 'Teilen…' : 'Teilen') }}
					</button>
				</div>
			</div>
			<button
				v-if="activity && mode === 'view'"
				class="btn-toggle"
				:class="'btn-primary'"
				:disabled="!canEdit"
				:title="canEdit ? 'Aktivität bearbeiten' : 'Keine Berechtigung zum Bearbeiten'"
				@click="mode === 'view' ? enterEdit() : (mode = 'view')"
			>
				<Pencil class="btn-icon" :size="16" aria-hidden="true" />
				Bearbeiten
			</button>
			<button
				v-else-if="activity && mode === 'edit'"
				class="btn-toggle btn-mail"
				@click="mode = 'view'"
			>
				<Eye class="btn-icon" :size="16" aria-hidden="true" />
				Ansicht
			</button>
		</div>
	</header>

	<button
		v-if="activity"
		type="button"
		class="detail-stats-drawer-handle"
		:class="{ 'is-open': isStatsDrawerOpen }"
		:aria-expanded="isStatsDrawerOpen"
		title="Statistik ein-/ausblenden"
		@click="toggleStatsDrawer"
	>
		{{ isStatsDrawerOpen ? 'Statistik schliessen' : 'Statistik öffnen' }}
	</button>

	<main class="main">
		<p v-if="loading" class="loading">Laden…</p>
		<ErrorAlert v-else-if="!activity && !!error" :error="error" />
		<p v-else-if="activityMissingConfirmed" class="error">Aktivität nicht gefunden.</p>
		<p v-else-if="!activity" class="loading">Aktivität wird geladen…</p>

		<!-- ================================================================ VIEW -->
		<div v-else-if="mode === 'view'" class="detail-view">
			<div class="detail-view-layout">
				<div class="detail-view-main">
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
								<span
									class="detail-value"
									:class="{ 'location-has-warning': exactOverlaps.length || fuzzyOverlaps.length }"
									@click="(exactOverlaps.length || fuzzyOverlaps.length) && (locationWarningOpen = !locationWarningOpen)"
								>
									{{ activity.location || '—' }}
									<span v-if="exactOverlaps.length" class="location-warn-icon" title="Überschneidung"><TriangleAlert :size="12" aria-hidden="true" /></span>
									<span v-else-if="fuzzyOverlaps.length" class="location-warn-icon" title="Möglicherweise ähnlicher Ort"><Info :size="12" aria-hidden="true" /></span>
								</span>
								<template v-if="locationWarningOpen">
									<div v-if="exactOverlaps.length" class="field-warning" style="margin-top: 6px">
										<TriangleAlert :size="12" aria-hidden="true" /> Überschneidung:
										<ul class="overlap-list">
											<li v-for="o in exactOverlaps" :key="o.id">
												<a href="#" class="overlap-link" @click.prevent="openActivityPreview(o.id)">{{ o.title }}</a>
												({{ o.start }}–{{ o.end }}<template v-if="o.department">, {{ o.department }}</template>)
											</li>
										</ul>
									</div>
									<div v-if="fuzzyOverlaps.length" class="field-hint" style="margin-top: 6px">
										<Info :size="12" aria-hidden="true" /> Möglicherweise ähnlicher Ort:
										<ul class="overlap-list">
											<li v-for="o in fuzzyOverlaps" :key="o.id">
												<a href="#" class="overlap-link" @click.prevent="openActivityPreview(o.id)">{{ o.title }}</a>
												– «{{ o.location }}» ({{ o.start }}–{{ o.end }}<template v-if="o.department">, {{ o.department }}</template>)
											</li>
										</ul>
									</div>
								</template>
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
										<span class="program-time">{{ viewProgramLabel(pi) }}</span>
										<span v-if="viewProgramSecondaryLabel(pi)" class="program-time-secondary">{{ viewProgramSecondaryLabel(pi) }}</span>
									</div>
									<div class="program-header">
										<p class="program-title">{{ prog.title }}</p>
							</div>
									<p v-if="prog.responsible.length" class="program-resp">Leitung: {{ prog.responsible.join(', ') }}</p>
									<div v-if="prog.description" class="program-desc" v-html="prog.description" />
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
							<FileDown :size="14" aria-hidden="true" />
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

				<div class="detail-stats-popout" :class="{ 'is-open': isStatsDrawerOpen }" @click.self="closeStatsDrawer">
					<div class="detail-stats-popout__panel">
						<button type="button" class="detail-stats-popout__close" @click="closeStatsDrawer" aria-label="Schliessen"><X :size="16" aria-hidden="true" /></button>
				<aside class="detail-stats-sidebar">
					<div class="detail-stats-card">
						<p class="detail-stats-card-title">Aktuelle Informationen</p>
						<div class="detail-stats-list">
							<div v-if="config.MIDATA_ENABLED" class="detail-stats-row">
								<span class="detail-label">MiData Teilnehmende</span>
								<span class="detail-value">
									{{ midataLoading ? 'Lädt…' : (!midataConfigured ? 'Nicht konfiguriert' : (midataChildrenCount ?? '—')) }}
								</span>
							</div>
							<div class="detail-stats-row" v-if="liveFormType === 'registration'">
								<span class="detail-label">Anmeldungen</span>
								<span class="detail-value" :class="{ 'detail-value--warn': registrationsExceedMidata }">{{ liveParticipantsLoading ? 'Lädt…' : (liveRegistrationCount ?? 0) }}</span>
							</div>
							<div class="detail-stats-row" v-if="liveFormType === 'deregistration'">
								<span class="detail-label">Abmeldungen</span>
								<span class="detail-value">{{ liveParticipantsLoading ? 'Lädt…' : (liveDeregistrationCount ?? 0) }}</span>
							</div>
							<div class="detail-stats-row">
								<span class="detail-label">Total Teilnehmende</span>
								<span class="detail-value" :class="{ 'detail-value--warn': registrationsExceedMidata }">{{ liveActivitySumDisplay }}</span>
							</div>
							<div class="detail-stats-row" v-if="config.MIDATA_ENABLED && registrationsExceedMidata">
								<span class="detail-label">Hinweis</span>
								<span class="detail-value detail-value--warn">Mehr Anmeldungen als MiData Kinder</span>
							</div>
							<div class="detail-stats-row" v-if="config.MIDATA_ENABLED && midataError">
								<span class="detail-label">MiData Fehler</span>
								<span class="detail-value">{{ midataError }}</span>
							</div>
						</div>
					</div>

					<div class="detail-stats-card">
						<p class="detail-stats-card-title">Erwartete Teilnehmende</p>
						<div class="detail-stats-list">
							<div class="detail-stats-row">
								<span class="detail-label">Schätzwert ähnliche Aktivitäten</span>
								<span class="detail-value">{{ estimatedParticipantsDisplay }}</span>
							</div>
						</div>
						<div class="detail-stats-sources" v-if="estimateSourceActivities.length">
							<p class="detail-stats-sources__title">Quellen</p>
							<div class="detail-stats-sources__list">
								<a
									v-for="source in estimateSourceActivities"
									:key="source.id"
									:href="`/activities/${source.id}`"
									@click="flushAutoSave"
									class="detail-stats-sources__link"
								>
									{{ source.title }}
								</a>
							</div>
						</div>
					</div>

					<div class="detail-stats-card">
						<p class="detail-stats-card-title">Erwartetes Wetter</p>
						<div class="detail-stats-list">
							<div class="detail-stats-row detail-stats-row--weather-input">
								<span class="detail-label">Ort</span>
								<div class="detail-weather-input-wrap">
									<span class="detail-value">{{ weatherLocationSaved ?? '—' }}</span>
								</div>
							</div>
							<div class="detail-stats-row">
								<span class="detail-label">Vorhersage</span>
								<div class="detail-weather-value">
									<span v-if="expectedWeather?.available" class="detail-value detail-weather-symbol" aria-hidden="true">
										<component :is="weatherSymbolIcon" :size="18" />
									</span>
									<span v-else class="detail-value">—</span>
									<span v-if="expectedWeatherLoading" class="detail-inline-spinner" aria-label="Wetterdaten werden aktualisiert" role="status" />
								</div>
							</div>
							<div class="detail-stats-row">
								<span class="detail-label">Temperatur</span>
								<div class="detail-weather-value">
									<span class="detail-value">{{ weatherTemperatureDisplay }}</span>
									<span v-if="expectedWeatherLoading" class="detail-inline-spinner" aria-label="Wetterdaten werden aktualisiert" role="status" />
								</div>
							</div>
							<div class="detail-stats-row detail-stats-row--weather-chart" v-if="expectedWeather?.available && weatherChartSamples.length > 1">
								<span class="detail-label">Verlauf</span>
								<div class="weather-mini-chart">
									<svg
										:viewBox="`0 0 ${weatherChartWidth} ${weatherChartHeight}`"
										preserveAspectRatio="none"
										class="weather-mini-chart__svg"
										aria-label="Temperaturverlauf"
										@mousemove="onWeatherChartMouseMove"
										@mouseleave="onWeatherChartMouseLeave"
									>
										<line
											v-for="tick in weatherChartYTicks"
											:key="`grid-view-${tick.label}`"
											:x1="weatherChartPlotLeft"
											:y1="tick.y"
											:x2="weatherChartPlotRight"
											:y2="tick.y"
											class="weather-mini-chart__grid"
										/>
										<text
											v-for="tick in weatherChartYTicks"
											:key="`label-view-${tick.label}`"
											x="6"
											:y="tick.y + 3"
											class="weather-mini-chart__tick-label"
										>
											{{ tick.label }}
										</text>
										<path :d="weatherChartPath" class="weather-mini-chart__line" />
										<circle
											v-for="(point, idx) in weatherChartPoints"
											:key="`point-view-${idx}`"
											:cx="point.x"
											:cy="point.y"
											:r="weatherHoveredPointIndex === idx ? 3.2 : 2"
											class="weather-mini-chart__point"
											:class="{ 'weather-mini-chart__point--active': weatherHoveredPointIndex === idx }"
											@mouseenter="weatherHoveredPointIndex = idx"
										/>
									</svg>
									<div v-if="weatherHoveredPoint" class="weather-mini-chart__hover-value">
										{{ weatherHoveredPoint.timeLabel }} · {{ weatherHoveredPoint.tempLabel }}
									</div>
									<div class="weather-mini-chart__labels">
										<span>{{ weatherChartStartLabel }}</span>
										<span>{{ weatherChartEndLabel }}</span>
									</div>
								</div>
							</div>
							<div class="detail-stats-row">
								<span class="detail-label">Regenwahrscheinlichkeit</span>
								<div class="detail-weather-value">
									<span class="detail-value">{{ weatherRainProbabilityDisplay }}</span>
									<span v-if="expectedWeatherLoading" class="detail-inline-spinner" aria-label="Wetterdaten werden aktualisiert" role="status" />
								</div>
							</div>
							<div class="detail-stats-row" v-if="expectedWeather?.mode === 'seasonal-average' && expectedWeather?.season">
								<span class="detail-label">Saison</span>
								<span class="detail-value">{{ expectedWeather.season }}</span>
							</div>
							<div class="detail-stats-row" v-if="expectedWeather?.point_name">
								<span class="detail-label">MeteoSwiss Punkt</span>
								<span class="detail-value">{{ expectedWeather.point_name }}</span>
							</div>
							<div class="detail-stats-row" v-if="expectedWeather?.source">
								<span class="detail-label">Quelle</span>
								<span class="detail-value">{{ expectedWeather.source }}</span>
							</div>
						</div>
					</div>
				</aside>
					</div>
				</div>
			</div>
		</div>

		<!-- =============================================================== EDIT -->
		<div v-else class="detail-form">
			<div v-if="pendingLocalDraft" class="editors-banner" style="margin-bottom: 12px; gap: 10px; flex-wrap: wrap;">
				<span class="editors-banner-icon"><Save :size="16" aria-hidden="true" /></span>
				<span>Ungespeicherter Entwurf gefunden ({{ new Date(pendingLocalDraft.savedAt).toLocaleString('de-DE') }}).</span>
				<button type="button" class="btn-secondary" @click="applyLocalDraft">Wiederherstellen</button>
				<button type="button" class="btn-secondary" @click="discardLocalDraft">Verwerfen</button>
			</div>
			<div v-else-if="localDraftRestoredAt" class="editors-banner" style="margin-bottom: 12px;">
				<span class="editors-banner-icon"><Check :size="16" aria-hidden="true" /></span>
				<span>Lokaler Entwurf wurde wiederhergestellt.</span>
			</div>

			<!-- Active editors indicator -->
			<div v-if="activeEditors.length" class="editors-banner">
				<span class="editors-banner-icon"><Users :size="16" aria-hidden="true" /></span>
				<span>{{ activeEditors.join(', ') }} {{ activeEditors.length === 1 ? 'bearbeitet' : 'bearbeiten' }} ebenfalls</span>
			</div>

			<!-- Titel -->
			<div class="form-group lock-wrapper" :class="{ 'is-locked': isLockedByOther('title') }"
				@focusin="lockSection('title')" @focusout="unlockSection('title', $event)">
				<div v-if="lockedBy('title')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('title') }}</div>
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
					<span v-if="savedFields['title']" class="field-saved-icon" :key="savedFields['title']"><Save :size="12" aria-hidden="true" /></span>
				</div>
			</div>

			<!-- Datum + Zeiten -->
			<div class="form-row form-row--3 lock-wrapper" :class="{ 'is-locked': isLockedByOther('datetime') }"
				@focusin="lockSection('datetime')" @focusout="unlockSection('datetime', $event)">
				<div v-if="lockedBy('datetime')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('datetime') }}</div>
				<div class="form-group">
					<label for="edit-date">Datum</label>
					<div class="input-save-wrap">
						<input id="edit-date" v-model="editDate" type="date" :disabled="isLockedByOther('datetime')" />
						<span v-if="savedFields['date']" class="field-saved-icon" :key="savedFields['date']"><Save :size="12" aria-hidden="true" /></span>
					</div>
				</div>
				<div class="form-group">
					<label for="edit-start">Startzeit</label>
					<div class="input-save-wrap">
						<input id="edit-start" v-model="editStartTime" type="time" :disabled="isLockedByOther('datetime')" />
						<span v-if="savedFields['start_time']" class="field-saved-icon" :key="savedFields['start_time']"><Save :size="12" aria-hidden="true" /></span>
					</div>
				</div>
				<div class="form-group">
					<label for="edit-end">Endzeit</label>
					<div class="input-save-wrap">
						<input id="edit-end" v-model="editEndTime" type="time" :disabled="isLockedByOther('datetime')" />
						<span v-if="savedFields['end_time']" class="field-saved-icon" :key="savedFields['end_time']"><Save :size="12" aria-hidden="true" /></span>
					</div>
				</div>
			</div>

			<!-- Ort + Verantwortlich + Stufe -->
			<div class="form-row form-row--3 lock-wrapper" :class="{ 'is-locked': isLockedByOther('location') }"
				@focusin="lockSection('location')" @focusout="unlockSection('location', $event)">
				<div v-if="lockedBy('location')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('location') }}</div>
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
										<TriangleAlert :size="12" aria-hidden="true" /> {{ locationOverlapsFor(loc).length }} Überschneidung{{ locationOverlapsFor(loc).length > 1 ? 'en' : '' }}
									</span>
								</div>
							</div>
						</div>
						<span v-if="savedFields['location']" class="field-saved-icon" :key="savedFields['location']"><Save :size="12" aria-hidden="true" /></span>
					</div>
					<div v-if="exactOverlaps.length" class="field-warning">
						<TriangleAlert :size="12" aria-hidden="true" /> Überschneidung am selben Ort:
						<ul class="overlap-list">
							<li v-for="o in exactOverlaps" :key="o.id">
								<a href="#" class="overlap-link" @click.prevent="openActivityPreview(o.id)">{{ o.title }}</a>
								({{ o.start }}–{{ o.end }}<template v-if="o.department">, {{ o.department }}</template>)
							</li>
						</ul>
					</div>
					<div v-if="fuzzyOverlaps.length" class="field-hint">
						<Info :size="12" aria-hidden="true" /> Möglicherweise ähnlicher Ort:
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
							<button type="button" class="user-chip-remove" @click="removeResponsible(i)" :disabled="isLockedByOther('location')" aria-label="Verantwortliche Person entfernen"><X :size="12" aria-hidden="true" /></button>
						</span>
					</div>
				</div>
				<div class="form-group">
					<label>Stufe</label>
					<BadgeSelect
						kind="department"
						:items="editDeptItems"
						placeholder="Stufe wählen…"
						:disabled="isLockedByOther('location') || deptFieldDisabled"
						:model-value="editDepartment || null"
						@update:model-value="(v) => { editDepartment = (v ?? '') as Department | ''; markDirty('department'); }"
					/>
					<span v-if="savedFields['department']" class="field-saved-icon field-saved-icon--inline" :key="savedFields['department']"><Save :size="12" aria-hidden="true" /></span>
				</div>
			</div>

			<!-- Programmpunkte -->
			<div class="form-section">
				<p class="form-section-title">Programmpunkte</p>
				<div style="display: flex; flex-direction: column; gap: 10px">
					<div v-for="(prog, i) in editPrograms" :key="i" class="program-card lock-wrapper"
						:class="{ 'is-locked': isLockedByOther(`program_${i}`) }"
						@focusin="lockSection(`program_${i}`)" @focusout="unlockSection(`program_${i}`, $event)">
						<div v-if="lockedBy(`program_${i}`)" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy(`program_${i}`) }}</div>
						<div class="program-card__top-actions">
							<button
								type="button"
								class="program-card__save-idee"
								@click="saveToIdeenkiste(i)"
								:disabled="isLockedByOther(`program_${i}`) || ideenSaving === i || !prog.title"
								:title="ideenSaving === i ? 'Gespeichert!' : 'Zur Ideenkiste'"
							>
								<BookMarked :size="14" aria-hidden="true" />
							</button>
							<button
								type="button"
								class="program-card__remove"
								@click="removeProgram(i)"
								:disabled="isLockedByOther(`program_${i}`)"
							>
								<X :size="14" aria-hidden="true" />
							</button>
						</div>
						<div class="program-card__fields">
							<div class="form-group">
								<label>Dauer (Minuten)</label>
								<div class="input-save-wrap">
									<input
										type="number"
										min="0"
										step="5"
										placeholder="z.B. 30"
										:value="prog.duration_minutes"
										@input="prog.duration_minutes = Math.max(0, parseInt(($event.target as HTMLInputElement).value, 10) || 0); markDirty(`prog_${i}_time`)"
										:disabled="isLockedByOther(`program_${i}`)"
									/>
									<span v-if="savedFields[`prog_${i}_time`]" class="field-saved-icon" :key="savedFields[`prog_${i}_time`]"><Save :size="12" aria-hidden="true" /></span>
								</div>
								<p v-if="editProgramLabel(i)" style="margin: 4px 0 0; font-size: 0.78rem; color: #6b7280">{{ editProgramLabel(i) }}</p>
							</div>
							<div class="form-group">
								<label>Titel</label>
								<div class="input-save-wrap">
									<input v-model="prog.title" type="text" placeholder="Titel" :disabled="isLockedByOther(`program_${i}`)" @input="markDirty(`prog_${i}_title`)" />
									<span v-if="savedFields[`prog_${i}_title`]" class="field-saved-icon" :key="savedFields[`prog_${i}_title`]"><Save :size="12" aria-hidden="true" /></span>
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
										<button type="button" class="user-chip-remove" @click="removeProgResponsible(i, ri)" :disabled="isLockedByOther(`program_${i}`)" aria-label="Programm-Verantwortliche Person entfernen"><X :size="12" aria-hidden="true" /></button>
									</span>
								</div>
								<span v-if="savedFields[`prog_${i}_resp`]" class="field-saved-icon field-saved-icon--inline" :key="savedFields[`prog_${i}_resp`]"><Save :size="12" aria-hidden="true" /></span>
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
										<button type="button" @mousedown.prevent @click="progExecCmd(i, 'removeFormat')" title="Formatierung entfernen"><X :size="12" aria-hidden="true" /> Format</button>
										<span class="toolbar-sep"></span>
										<button type="button" @mousedown="progSaveSelection" @click="openProgLinkDialog" title="Link einfügen">🔗 Link</button>
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
									<span v-if="savedFields[`prog_${i}_desc`]" class="field-saved-icon field-saved-icon--textarea" :key="savedFields[`prog_${i}_desc`]"><Save :size="12" aria-hidden="true" /></span>
								</div>
							</div>
						</div>
					</div>
					<div class="btn-add-split">
						<button type="button" class="btn-add btn-add-split__main" @click="addProgram">
							+ Programmpunkt
						</button>
						<div class="btn-add-split__dropdown-wrap">
							<button type="button" class="btn-add btn-add-split__caret" @click="openIdeenDropdown" title="Aus Ideenkiste">
								<BookMarked :size="14" aria-hidden="true" />
							</button>
							<div v-if="showIdeenDropdown" class="ideen-dropdown-overlay" @click="showIdeenDropdown = false; ideenSearch = ''" />
							<div v-if="showIdeenDropdown" class="ideen-dropdown">
								<div class="ideen-dropdown__header">
									<input
										v-model="ideenSearch"
										class="ideen-dropdown__search"
										type="search"
										placeholder="Suchen…"
										autofocus
										@keydown.escape="showIdeenDropdown = false; ideenSearch = ''"
									/>
									<button type="button" class="ideen-dropdown__close" @click="showIdeenDropdown = false; ideenSearch = ''">
										<X :size="14" aria-hidden="true" />
									</button>
								</div>
								<div class="ideen-dropdown__list">
									<p v-if="ideenFiltered.length === 0" class="ideen-dropdown__empty">Keine Einträge gefunden.</p>
									<button
										v-for="item in ideenFiltered"
										:key="item.id"
										type="button"
										class="ideen-dropdown__item"
										@click="addProgramFromIdee(item)"
									>
										<span class="ideen-dropdown__item-title">{{ item.title }}</span>
										<span v-if="item.duration_minutes" class="ideen-dropdown__item-meta">{{ item.duration_minutes }} min</span>
									</button>
								</div>
							</div>
						</div>
					</div>
				</div>
			</div>

			<!-- Material -->
			<div class="form-section">
				<p class="form-section-title">Material</p>
				<div class="material-list">
					<div v-for="(_, i) in editMaterial" :key="i" class="material-card lock-wrapper"
						:class="{ 'is-locked': isLockedByOther(`material_${i}`) }"
						@focusin="lockSection(`material_${i}`)" @focusout="unlockSection(`material_${i}`, $event)">
						<div v-if="lockedBy(`material_${i}`)" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy(`material_${i}`) }}</div>
						<button
							v-if="i < editMaterial.length - 1"
							type="button"
							class="program-card__remove"
							@click="removeMaterial(i)"
							:disabled="isLockedByOther(`material_${i}`)"
						>
							<X :size="14" aria-hidden="true" />
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
									<span v-if="savedFields[`mat_${i}`] && editMaterial[i].name.trim()" class="field-saved-icon" :key="savedFields[`mat_${i}`]"><Save :size="12" aria-hidden="true" /></span>
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
										<button type="button" class="user-chip-remove" @click="clearMaterialResp(i, ri)" :disabled="isLockedByOther(`material_${i}`)" aria-label="Material-Verantwortliche Person entfernen"><X :size="12" aria-hidden="true" /></button>
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
				<div v-if="lockedBy('siko')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('siko') }}</div>
				<p class="form-section-title">Sicherheitskonzept</p>
				<div class="form-group">
					<div class="input-save-wrap">
						<textarea
							v-model="editSikoText"
							rows="3"
							placeholder="Sicherheitskonzept (optional)"
							:disabled="isLockedByOther('siko')"
						/>
						<span v-if="savedFields['siko_text']" class="field-saved-icon field-saved-icon--textarea" :key="savedFields['siko_text']"><Save :size="12" aria-hidden="true" /></span>
					</div>
				</div>
			</div>

			<!-- Ziel + Schlechtwetter -->
			<div class="form-row lock-wrapper" :class="{ 'is-locked': isLockedByOther('goal_weather') }"
				@focusin="lockSection('goal_weather')" @focusout="unlockSection('goal_weather', $event)">
				<div v-if="lockedBy('goal_weather')" class="lock-badge"><Lock :size="12" aria-hidden="true" /> {{ lockedBy('goal_weather') }}</div>
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
						<span v-if="savedFields['goal']" class="field-saved-icon field-saved-icon--textarea" :key="savedFields['goal']"><Save :size="12" aria-hidden="true" /></span>
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
						<span v-if="savedFields['bad_weather']" class="field-saved-icon field-saved-icon--textarea" :key="savedFields['bad_weather']"><Save :size="12" aria-hidden="true" /></span>
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
					<Upload class="drop-zone__icon" :size="20" aria-hidden="true" />
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
							<FileDown :size="14" aria-hidden="true" />
						</button>
						<button type="button" class="user-chip-remove" @click.stop="removeAttachment(att.id)" title="Löschen" aria-label="Anhang löschen"><X :size="12" aria-hidden="true" /></button>
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
				<button type="button" class="btn-secondary" @click="cancelEdit">
					Abbrechen
				</button>
				<button type="button" class="btn-secondary" @click="mode = 'view'">
					Schliessen
				</button>
			</div>

			<div class="detail-stats-popout" :class="{ 'is-open': isStatsDrawerOpen }" @click.self="closeStatsDrawer">
				<div class="detail-stats-popout__panel">
					<button type="button" class="detail-stats-popout__close" @click="closeStatsDrawer" aria-label="Schliessen"><X :size="16" aria-hidden="true" /></button>
			<aside class="detail-stats-sidebar detail-stats-sidebar--edit">
				<div class="detail-stats-card">
					<p class="detail-stats-card-title">Aktuelle Informationen</p>
					<div class="detail-stats-list">
						<div v-if="config.MIDATA_ENABLED" class="detail-stats-row">
							<span class="detail-label">MiData Teilnehmende</span>
							<span class="detail-value">{{ midataLoading ? 'Lädt…' : (!midataConfigured ? 'Nicht konfiguriert' : (midataChildrenCount ?? '—')) }}</span>
						</div>
						<div class="detail-stats-row" v-if="liveFormType === 'registration'">
							<span class="detail-label">Anmeldungen</span>
							<span class="detail-value" :class="{ 'detail-value--warn': registrationsExceedMidata }">{{ liveParticipantsLoading ? 'Lädt…' : (liveRegistrationCount ?? 0) }}</span>
						</div>
						<div class="detail-stats-row" v-if="liveFormType === 'deregistration'">
							<span class="detail-label">Abmeldungen</span>
							<span class="detail-value">{{ liveParticipantsLoading ? 'Lädt…' : (liveDeregistrationCount ?? 0) }}</span>
						</div>
						<div class="detail-stats-row">
							<span class="detail-label">Total Teilnehmende</span>
							<span class="detail-value" :class="{ 'detail-value--warn': registrationsExceedMidata }">{{ liveActivitySumDisplay }}</span>
						</div>
						<div class="detail-stats-row" v-if="config.MIDATA_ENABLED && registrationsExceedMidata">
							<span class="detail-label">Hinweis</span>
							<span class="detail-value detail-value--warn">Mehr Anmeldungen als MiData Kinder</span>
						</div>
						<div class="detail-stats-row" v-if="config.MIDATA_ENABLED && midataError">
							<span class="detail-label">MiData Fehler</span>
							<span class="detail-value">{{ midataError }}</span>
						</div>
					</div>
				</div>

				<div class="detail-stats-card">
					<p class="detail-stats-card-title">Erwartete Teilnehmende</p>
					<div class="detail-stats-list">
						<div class="detail-stats-row">
							<span class="detail-label">Schätzwert ähnliche Aktivitäten</span>
							<span class="detail-value">{{ estimatedParticipantsDisplay }}</span>
						</div>
					</div>
					<div class="detail-stats-sources" v-if="estimateSourceActivities.length">
						<p class="detail-stats-sources__title">Quellen</p>
						<div class="detail-stats-sources__list">
							<a
								v-for="source in estimateSourceActivities"
								:key="source.id"
								:href="`/activities/${source.id}`"
								@click="flushAutoSave"
								class="detail-stats-sources__link"
							>
								{{ source.title }}
							</a>
						</div>
					</div>
				</div>

				<div class="detail-stats-card">
					<p class="detail-stats-card-title">Erwartetes Wetter</p>
					<div class="detail-stats-list">
						<div class="detail-stats-row detail-stats-row--weather-input">
							<span class="detail-label">Ort</span>
							<div class="detail-weather-input-wrap">
								<template v-if="weatherLocationEditing">
									<input
										v-model="weatherLocationInput"
										type="text"
										class="form-input detail-weather-input"
										placeholder="z. B. 6300 Zug"
										@keyup.enter="saveWeatherLocation"
									/>
									<button type="button" class="btn-secondary" :disabled="weatherLocationSaving" @click="saveWeatherLocation">Speichern</button>
								</template>
								<template v-else>
									<span class="detail-value">{{ weatherLocationSaved }}</span>
									<button type="button" class="detail-icon-btn" title="Ort bearbeiten" @click="startWeatherLocationEdit">✎</button>
								</template>
							</div>
						</div>
						<div class="detail-stats-row">
							<span class="detail-label">Vorhersage</span>
							<div class="detail-weather-value">
								<span v-if="expectedWeather?.available" class="detail-value detail-weather-symbol" aria-hidden="true">
									<component :is="weatherSymbolIcon" :size="18" />
								</span>
								<span v-else class="detail-value">—</span>
								<span v-if="expectedWeatherLoading" class="detail-inline-spinner" aria-label="Wetterdaten werden aktualisiert" role="status" />
							</div>
						</div>
						<div class="detail-stats-row">
							<span class="detail-label">Temperatur</span>
							<div class="detail-weather-value">
								<span class="detail-value">{{ weatherTemperatureDisplay }}</span>
								<span v-if="expectedWeatherLoading" class="detail-inline-spinner" aria-label="Wetterdaten werden aktualisiert" role="status" />
							</div>
						</div>
						<div class="detail-stats-row detail-stats-row--weather-chart" v-if="expectedWeather?.available && weatherChartSamples.length > 1">
							<span class="detail-label">Verlauf</span>
							<div class="weather-mini-chart">
								<svg
									:viewBox="`0 0 ${weatherChartWidth} ${weatherChartHeight}`"
									preserveAspectRatio="none"
									class="weather-mini-chart__svg"
									aria-label="Temperaturverlauf"
									@mousemove="onWeatherChartMouseMove"
									@mouseleave="onWeatherChartMouseLeave"
								>
									<line
										v-for="tick in weatherChartYTicks"
										:key="`grid-edit-${tick.label}`"
										:x1="weatherChartPlotLeft"
										:y1="tick.y"
										:x2="weatherChartPlotRight"
										:y2="tick.y"
										class="weather-mini-chart__grid"
									/>
									<text
										v-for="tick in weatherChartYTicks"
										:key="`label-edit-${tick.label}`"
										x="6"
										:y="tick.y + 3"
										class="weather-mini-chart__tick-label"
									>
										{{ tick.label }}
									</text>
									<path :d="weatherChartPath" class="weather-mini-chart__line" />
									<circle
										v-for="(point, idx) in weatherChartPoints"
										:key="`point-edit-${idx}`"
										:cx="point.x"
										:cy="point.y"
										:r="weatherHoveredPointIndex === idx ? 3.2 : 2"
										class="weather-mini-chart__point"
										:class="{ 'weather-mini-chart__point--active': weatherHoveredPointIndex === idx }"
										@mouseenter="weatherHoveredPointIndex = idx"
									/>
								</svg>
								<div v-if="weatherHoveredPoint" class="weather-mini-chart__hover-value">
									{{ weatherHoveredPoint.timeLabel }} · {{ weatherHoveredPoint.tempLabel }}
								</div>
								<div class="weather-mini-chart__labels">
									<span>{{ weatherChartStartLabel }}</span>
									<span>{{ weatherChartEndLabel }}</span>
								</div>
							</div>
						</div>
						<div class="detail-stats-row">
							<span class="detail-label">Regenwahrscheinlichkeit</span>
							<div class="detail-weather-value">
								<span class="detail-value">{{ weatherRainProbabilityDisplay }}</span>
								<span v-if="expectedWeatherLoading" class="detail-inline-spinner" aria-label="Wetterdaten werden aktualisiert" role="status" />
							</div>
						</div>
						<div class="detail-stats-row" v-if="expectedWeather?.mode === 'seasonal-average' && expectedWeather?.season">
							<span class="detail-label">Saison</span>
							<span class="detail-value">{{ expectedWeather.season }}</span>
						</div>
						<div class="detail-stats-row" v-if="expectedWeather?.point_name">
							<span class="detail-label">MeteoSwiss Punkt</span>
							<span class="detail-value">{{ expectedWeather.point_name }}</span>
						</div>
						<div class="detail-stats-row" v-if="expectedWeather?.source">
							<span class="detail-label">Quelle</span>
							<span class="detail-value">{{ expectedWeather.source }}</span>
						</div>
					</div>
				</div>
			</aside>
				</div>
			</div>
		</div>
	</main>

	<!-- Fuzzy Location Overlap Confirmation Dialog -->
	<div v-if="showFuzzyDialog" class="preview-overlay" @click.self="cancelFuzzyDialog">
		<div class="duplicate-dialog">
			<p class="duplicate-dialog__title"><TriangleAlert :size="16" aria-hidden="true" /> Mögliche Ortsüberschneidung</p>
			<p class="duplicate-dialog__text">
				Der eingetragene Ort <strong>«{{ editLocation }}»</strong> ähnelt dem Ort einer anderen Aktivität zur gleichen Zeit.
				Ist dies derselbe Ort? Die Aktivitäten könnten sich überschneiden.
			</p>
			<ul class="overlap-list" style="margin: 8px 0 12px">
				<li v-for="o in fuzzyDialogOverlaps" :key="o.id">
					<a href="#" class="overlap-link" @click.prevent="openActivityPreview(o.id)">{{ o.title }}</a>
					– Ort: «{{ o.location }}» ({{ o.start }}–{{ o.end }}<template v-if="o.department">, {{ o.department }}</template>)
				</li>
			</ul>
			<div class="duplicate-dialog__actions">
				<button type="button" class="btn-secondary" @click="cancelFuzzyDialog">Abbrechen</button>
				<button type="button" class="btn-primary" @click="confirmFuzzyAndSave">Ja, trotzdem speichern</button>
			</div>
		</div>
	</div>

	<!-- Duplicate File Dialog -->
	<div v-if="showDuplicateDialog" class="preview-overlay" @click.self="onDuplicateSkip">		<div class="duplicate-dialog">
			<p class="duplicate-dialog__title">Datei bereits vorhanden</p>
			<p class="duplicate-dialog__text">Die Datei <strong>{{ duplicateFile?.name }}</strong> existiert bereits. Möchtest du sie ersetzen?</p>
			<div class="duplicate-dialog__actions">
				<button type="button" class="btn-secondary" @click="onDuplicateSkip">Überspringen</button>
				<button type="button" class="btn-primary" @click="onDuplicateReplace">Ersetzen</button>
			</div>
		</div>
	</div>

	<!-- Event publish preview modal -->
	<div v-if="showEventPreview" class="modal-backdrop" @click.self="showEventPreview = false">
		<div class="modal" style="max-width: 640px;">
			<div style="display: flex; align-items: center; justify-content: space-between;">
				<h2 class="modal-title" style="margin: 0;">Event-Vorschau</h2>
				<a
					v-if="config.WP_PUBLISHING_ENABLED && eventPublication?.wp_event_id && config.WP_URL"
					:href="`${config.WP_URL}/wp-admin/post.php?post=${eventPublication.wp_event_id}&action=edit`"
					target="_blank"
					rel="noopener"
					class="wp-id-link"
					title="In WordPress bearbeiten"
				>WP #{{ eventPublication.wp_event_id }}</a>
				<span
					v-else-if="config.WP_PUBLISHING_ENABLED && eventPublication?.wp_event_id"
					class="wp-id-link"
				>WP #{{ eventPublication.wp_event_id }}</span>
			</div>
			<div class="event-preview-form">
				<!-- Read-only meta info -->
				<div class="event-preview-meta">
					<DepartmentBadge v-if="activity?.department" :department="activity.department" />
					<span v-if="activity?.date" class="event-preview-meta-item">{{ evtFormatDateLong(activity.date) }}</span>
					<span v-if="activity?.start_time || activity?.end_time" class="event-preview-meta-item">{{ activity.start_time }}{{ activity.end_time ? ' – ' + activity.end_time : '' }}</span>
				</div>
				<label class="event-preview-label">Titel</label>
				<input
					v-model="eventPreviewTitle"
					class="event-preview-input"
					placeholder="Event-Titel"
				/>
				<label class="event-preview-label" style="margin-top: 12px;">Beschreibung</label>
				<div class="rich-editor-toolbar">
					<select class="toolbar-select" :value="evtPreviewToolbar?.font ?? 'Arial'" @change="evtPreviewExecCmd('fontName', ($event.target as HTMLSelectElement).value)" title="Schriftart">
						<option value="" disabled>Schriftart</option>
						<option value="Arial" style="font-family:Arial">Arial</option>
						<option value="Helvetica" style="font-family:Helvetica">Helvetica</option>
						<option value="Georgia" style="font-family:Georgia">Georgia</option>
						<option value="Times New Roman" style="font-family:'Times New Roman'">Times New Roman</option>
						<option value="Courier New" style="font-family:'Courier New'">Courier New</option>
						<option value="Verdana" style="font-family:Verdana">Verdana</option>
						<option value="Trebuchet MS" style="font-family:'Trebuchet MS'">Trebuchet MS</option>
					</select>
					<input type="text" class="toolbar-select toolbar-select--narrow" :value="evtPreviewToolbar?.size ?? '12'" @mousedown="evtPreviewSaveSelection" @change="evtPreviewSetFontSize(($event.target as HTMLInputElement).value)" @keydown.enter.prevent="evtPreviewSetFontSize(($event.target as HTMLInputElement).value)" title="Schriftgrösse" />
					<button type="button" class="toolbar-btn-sm" @mousedown.prevent @click="evtPreviewAdjustFontSize(-2)" title="Kleiner">A−</button>
					<button type="button" class="toolbar-btn-sm" @mousedown.prevent @click="evtPreviewAdjustFontSize(2)" title="Grösser">A+</button>
					<span class="toolbar-sep"></span>
					<button type="button" :class="{'toolbar-btn--active': evtPreviewToolbar?.bold}" @mousedown.prevent @click="evtPreviewExecCmd('bold')" title="Fett"><b>B</b></button>
					<button type="button" :class="{'toolbar-btn--active': evtPreviewToolbar?.italic}" @mousedown.prevent @click="evtPreviewExecCmd('italic')" title="Kursiv"><i>I</i></button>
					<button type="button" :class="{'toolbar-btn--active': evtPreviewToolbar?.underline}" @mousedown.prevent @click="evtPreviewExecCmd('underline')" title="Unterstrichen"><u>U</u></button>
					<span class="toolbar-sep"></span>
					<label class="toolbar-color" title="Schriftfarbe" @mousedown="evtPreviewSaveSelection">
						A
						<input type="color" :value="evtPreviewToolbar?.color ?? '#000000'" @input="evtPreviewExecCmd('foreColor', ($event.target as HTMLInputElement).value)" />
					</label>
					<label class="toolbar-color toolbar-color--bg" title="Hintergrundfarbe" @mousedown="evtPreviewSaveSelection">
						<span class="toolbar-color-icon">A</span>
						<input type="color" :value="evtPreviewToolbar?.bgColor ?? '#ffffff'" @input="evtPreviewExecCmd('hiliteColor', ($event.target as HTMLInputElement).value)" />
					</label>
					<span class="toolbar-sep"></span>
					<button type="button" :class="{'toolbar-btn--active': evtPreviewToolbar?.ul}" @mousedown.prevent @click="evtPreviewExecCmd('insertUnorderedList')" title="Aufzählung">• Liste</button>
					<button type="button" :class="{'toolbar-btn--active': evtPreviewToolbar?.ol}" @mousedown.prevent @click="evtPreviewExecCmd('insertOrderedList')" title="Nummerierte Liste">1. Liste</button>
					<span class="toolbar-sep"></span>
					<button type="button" @mousedown.prevent @click="evtPreviewExecCmd('removeFormat')" title="Formatierung entfernen">✕ Format</button>
					<span class="toolbar-sep"></span>
					<button type="button" @mousedown="evtPreviewSaveSelection" @click="openEvtPreviewLinkDialog" title="Link einfügen">🔗 Link</button>
				</div>
				<div
					ref="eventPreviewEditorRef"
					class="rich-editor"
					contenteditable="true"
					@input="onEvtPreviewInput"
					@focus="updateEvtPreviewToolbar"
					@mouseup="updateEvtPreviewToolbar"
					@keyup="updateEvtPreviewToolbar"
					data-placeholder="Beschreibung…"
				></div>
			</div>
			<div class="modal-actions">
				<button class="btn-cancel" @click="showEventPreview = false">Abbrechen</button>
				<button
					v-if="eventPublication"
					class="btn-danger"
					:disabled="eventPublishing || eventUnpublishing"
					@click="showUnpublishConfirm = true"
				>
					<Trash2 class="btn-icon" :size="16" aria-hidden="true" />
					Löschen
				</button>
				<button
					class="btn-primary"
					:disabled="eventPublishing || eventUnpublishing || !eventPreviewTitle.trim()"
					@click="confirmPublishEvent"
				>
					{{ eventPublishing ? 'Wird veröffentlicht…' : (eventPublication ? 'Aktualisieren' : 'Veröffentlichen') }}
				</button>
			</div>
		</div>
	</div>

	<!-- No-form dialog (Mail flow) -->
	<div v-if="showNoFormDialog" class="modal-backdrop" @click.self="showNoFormDialog = false">
		<div class="modal modal--info">
			<h2 class="modal-title modal-title--info">Kein Formular vorhanden</h2>
			<p class="modal-warning">
				Für diese Aktivität wurde noch kein Anmeldeformular erstellt.
				Möchtest du zuerst ein Formular erstellen?
			</p>
			<div class="modal-actions modal-actions--equal">
				<button class="btn-cancel" @click="showNoFormDialog = false">Abbrechen</button>
				<button class="btn-cancel" @click="showNoFormDialog = false; router.push(`/activities/${id}/mail`)">Mail erstellen</button>
				<button class="btn-info" @click="showNoFormDialog = false; router.push(`/activities/${id}/forms`)">Formular erstellen</button>
			</div>
		</div>
	</div>

	<!-- No-form dialog (Event publish flow) -->
	<div v-if="showNoFormForEventDialog" class="modal-backdrop" @click.self="showNoFormForEventDialog = false">
		<div class="modal modal--info">
			<h2 class="modal-title modal-title--info">Kein Formular vorhanden</h2>
			<p class="modal-warning">
				Für diese Aktivität wurde noch kein Anmeldeformular erstellt.
				Die Variable <code v-pre>{{formular_link}}</code> wird im Event-Text nicht ersetzt.
				Möchtest du zuerst ein Formular erstellen?
			</p>
			<div class="modal-actions">
				<button class="btn-cancel" @click="showNoFormForEventDialog = false">Abbrechen</button>
				<button class="btn-cancel" @click="showNoFormForEventDialog = false; router.push(`/activities/${id}/forms`)">Formular erstellen</button>
				<button class="btn-primary" @click="publishWithoutForm">Trotzdem veröffentlichen</button>
			</div>
		</div>
	</div>

	<!-- Unpublish confirmation dialog -->
	<div v-if="showUnpublishConfirm" class="modal-backdrop" @click.self="showUnpublishConfirm = false">
		<div class="modal modal--danger">
			<h2 class="modal-title modal-title--danger">Event löschen?</h2>
			<p class="modal-warning">
				Das Event wird aus dem DPW entfernt{{ config.WP_PUBLISHING_ENABLED && eventPublication?.wp_event_id ? ' und aus WordPress gelöscht' : '' }}.
				Diese Aktion kann nicht rückgängig gemacht werden.
			</p>
			<div class="modal-actions">
				<button class="btn-cancel" @click="showUnpublishConfirm = false">Abbrechen</button>
				<button class="btn-danger" :disabled="eventUnpublishing" @click="confirmUnpublishEvent">
					{{ eventUnpublishing ? 'Wird gelöscht…' : 'Endgültig löschen' }}
				</button>
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
						<ExternalLink :size="16" aria-hidden="true" />
					</a>
					<button type="button" @click="closeActivityPreview" title="Schliessen" aria-label="Schliessen"><X :size="14" aria-hidden="true" /></button>
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
					<span>
						<DepartmentBadge v-if="previewActivity.department" :department="previewActivity.department" />
						<template v-else>—</template>
					</span>
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
						<li v-for="(prog, pi) in previewActivity.programs" :key="prog.id">
							<strong>{{ previewProgramLabel(pi) }}</strong> {{ prog.title }}
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
						<FileDown :size="16" aria-hidden="true" />
					</button>
					<button type="button" @click="closePreview" title="Schliessen" aria-label="Schliessen"><X :size="14" aria-hidden="true" /></button>
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

	<div v-if="showEvtPreviewLinkDialog" class="modal-backdrop" @click.self="cancelEvtPreviewLink">
		<div class="modal link-modal">
			<h2 class="modal-title">Link einfügen</h2>
			<input
				ref="evtPreviewLinkInputRef"
				v-model="evtPreviewLinkUrl"
				type="url"
				class="link-modal-input"
				placeholder="https://"
				@keydown.enter.prevent="confirmEvtPreviewLink"
				@keydown.escape.prevent="cancelEvtPreviewLink"
			/>
			<div class="modal-actions">
				<button class="btn-cancel" @mousedown.prevent @click="cancelEvtPreviewLink">Abbrechen</button>
				<button v-if="evtPreviewLinkUrl" class="btn-secondary" @mousedown.prevent @click="() => { evtPreviewLinkUrl = ''; confirmEvtPreviewLink() }">Link entfernen</button>
				<button class="btn-primary" @mousedown.prevent @click="confirmEvtPreviewLink">{{ evtPreviewLinkUrl ? 'Einfügen' : 'OK' }}</button>
			</div>
		</div>
	</div>

	<div v-if="showProgLinkDialog" class="modal-backdrop" @click.self="cancelProgLink">
		<div class="modal link-modal">
			<h2 class="modal-title">Link einfügen</h2>
			<input
				ref="progLinkInputRef"
				v-model="progLinkUrl"
				type="url"
				class="link-modal-input"
				placeholder="https://"
				@keydown.enter.prevent="confirmProgLink"
				@keydown.escape.prevent="cancelProgLink"
			/>
			<div class="modal-actions">
				<button class="btn-cancel" @mousedown.prevent @click="cancelProgLink">Abbrechen</button>
				<button v-if="progLinkUrl" class="btn-secondary" @mousedown.prevent @click="() => { progLinkUrl = ''; confirmProgLink() }">Link entfernen</button>
				<button class="btn-primary" @mousedown.prevent @click="confirmProgLink">{{ progLinkUrl ? 'Einfügen' : 'OK' }}</button>
			</div>
		</div>
	</div>
</template>
