<template>
	<div class="user-avatar-wrap" ref="wrapRef">
		<template v-if="user">
			<button
				class="avatar-circle"
				@click="open = !open"
				:title="user.display_name"
			>
				{{ initials }}
				<span v-if="unreadCount > 0" class="avatar-notification-badge">{{ unreadCount }}</span>
			</button>

			<div v-if="open" class="avatar-dropdown">
				<div class="avatar-dropdown-header">
					<span class="avatar-dropdown-name">{{ user.display_name }}</span>
					<div class="avatar-dropdown-badges">
						<RoleBadge :role="user.role" />
						<DepartmentBadge :department="user.department" />
					</div>
					<span class="avatar-dropdown-email">{{ user.email }}</span>
				</div>
				<div class="avatar-dropdown-divider" />
				<button class="avatar-dropdown-item" @click="openNotificationsPopup">
						Benachrichtigungen
						<span v-if="unreadCount > 0" class="avatar-dropdown-badge">{{ unreadCount }}</span>
					</button>
				<router-link
					class="avatar-dropdown-item"
					to="/profile"
					@click="open = false"
				>
					Profil bearbeiten
				</router-link>
				<button
					class="avatar-dropdown-item avatar-dropdown-logout"
					@click="handleLogout"
				>
					Abmelden
				</button>
			</div>

			<div v-if="notificationsPopupOpen" class="modal-backdrop" @click.self="notificationsPopupOpen = false">
				<div class="modal modal--info avatar-notifications-modal">
					<h2 class="modal-title modal-title--info">Benachrichtigungen</h2>
					<p class="modal-warning avatar-notifications-subtitle">Alle verfügbaren Meldungen zu deinen Aktivitäten.</p>

					<div class="avatar-notifications-block">
						<div v-if="visibleNotifications.length === 0" class="avatar-notifications-empty">Keine Benachrichtigungen</div>
						<button
							v-for="n in visibleNotifications"
							:key="n.id"
							class="avatar-notification-item"
							:class="{ 'avatar-notification-item--unread': !n.is_read }"
							@click="openNotification(n.id, n.link)"
						>
							<div class="avatar-notification-headline">
								<div class="avatar-notification-headline-main">
									<span class="avatar-notification-category" :class="notificationCategoryClass(n)">
										{{ notificationCategoryLabel(n) }}
									</span>
									<div class="avatar-notification-title">{{ n.title }}</div>
								</div>
								<div class="avatar-notification-headline-side">
									<div class="avatar-notification-date-inline">{{ activityDate(n) }}</div>
									<div v-if="headlineRecipients(n) !== '-'" class="avatar-notification-recipients-inline">{{ headlineRecipients(n) }}</div>
								</div>
							</div>
							<div class="avatar-notification-message">{{ notificationSummary(n) }}</div>
							<div class="avatar-notification-grid">
								<div class="avatar-notification-meta"><strong>Aktivität</strong><span>{{ activityTitle(n) }}</span></div>
								<div v-if="triggeredBy(n) !== '-'" class="avatar-notification-meta"><strong>Ausgelöst von</strong><span>{{ triggeredBy(n) }}</span></div>
								<div v-if="mailRecipients(n) !== '-'" class="avatar-notification-meta avatar-notification-meta--wide"><strong>Mail-Empfänger</strong><span>{{ mailRecipients(n) }}</span></div>
								<div v-if="mailCcRecipients(n) !== '-'" class="avatar-notification-meta avatar-notification-meta--wide"><strong>Mail-CC</strong><span>{{ mailCcRecipients(n) }}</span></div>
							</div>
							<div v-if="n.link" class="avatar-notification-link-row">
								<span class="avatar-notification-link-label">Link</span>
								<span class="avatar-notification-link">{{ compactLink(n.link) }}</span>
							</div>
						</button>
					</div>

					<div class="modal-actions avatar-notifications-footer">
						<button class="btn-info" @click="handleMarkAllRead">Alle gelesen</button>
						<button class="btn-cancel" @click="notificationsPopupOpen = false">Schließen</button>
					</div>
				</div>
			</div>
		</template>
	</div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { user, logout } from '../composables/useAuth';
import { useNotifications, requestBrowserNotificationPermission, syncPushSubscription } from '../composables/useNotifications';
import RoleBadge from './RoleBadge.vue';
import DepartmentBadge from './DepartmentBadge.vue';
import type { NotificationRecord } from '../types';

const open = ref(false);
const notificationsPopupOpen = ref(false);
const wrapRef = ref<HTMLElement | null>(null);
const { notifications, unreadCount, ensureNotifications, markRead, markAllRead } = useNotifications();

const visibleNotifications = computed(() =>
	notifications.value.filter((n) => {
		if (!user.value) return false;
		if (n.category === 'material_assigned') return user.value.notify_material_assigned;
		if (n.category === 'activity_assigned') return user.value.notify_activity_assigned;
		if (n.category === 'program_assigned') return user.value.notify_program_assigned;
		if (n.category === 'mail_own_activity') return user.value.notify_mail_own_activity;
		if (n.category === 'mail_department') return user.value.notify_mail_department;
		return true;
	}),
);

const initials = computed(() => {
	if (!user.value) return '';
	const words = user.value.display_name.split(' ').filter(Boolean);
	if (words.length === 1) return user.value.display_name.trim().slice(0, 2).toUpperCase();
	return words.slice(0, 2).map((w) => w[0].toUpperCase()).join('');
});

async function handleLogout() {
	open.value = false;
	await logout();
}

async function openNotification(id: string, link: string | null) {
	await markRead(id).catch(() => {});
	notificationsPopupOpen.value = false;
	open.value = false;
	if (link) window.location.href = link;
}

async function openNotificationsPopup() {
	notificationsPopupOpen.value = true;
	await ensureNotifications().catch(() => {});
}

async function handleMarkAllRead() {
	await markAllRead().catch(() => {});
}

function onClickOutside(e: MouseEvent) {
	if (wrapRef.value && !wrapRef.value.contains(e.target as Node)) {
		open.value = false;
	}
}

function payloadOf(n: NotificationRecord): Record<string, unknown> {
	return (n.payload ?? {}) as Record<string, unknown>;
}

function valueString(v: unknown): string {
	return typeof v === 'string' && v.trim().length > 0 ? v.trim() : '';
}

function displayPersonLabel(value: string): string {
	const trimmed = value.trim();
	if (!trimmed) return '';
	const mailMatch = trimmed.match(/^([^@]+)@/);
	if (!mailMatch) return trimmed;
	return mailMatch[1]
		.split(/[._-]+/)
		.filter(Boolean)
		.map((part) => part.charAt(0).toUpperCase() + part.slice(1))
		.join(' ');
}

function listString(v: unknown): string {
	if (!Array.isArray(v)) return '-';
	const out = v.filter((entry): entry is string => typeof entry === 'string' && entry.trim().length > 0);
	return out.length ? out.join(', ') : '-';
}

function listDisplayNames(v: unknown): string {
	if (!Array.isArray(v)) return '-';
	const out = v
		.filter((entry): entry is string => typeof entry === 'string' && entry.trim().length > 0)
		.map((entry) => displayPersonLabel(entry))
		.filter(Boolean);
	return out.length ? out.join(', ') : '-';
}

function activityTitle(n: NotificationRecord): string {
	const p = payloadOf(n);
	return valueString(p.activity_title) || '-';
}

function activityDate(n: NotificationRecord): string {
	const p = payloadOf(n);
	const explicit = valueString(p.activity_date_display);
	if (explicit) return explicit;

	const iso = valueString(p.activity_date);
	if (iso.length >= 10 && iso[4] === '-' && iso[7] === '-') {
		return `${iso.slice(8, 10)}.${iso.slice(5, 7)}.${iso.slice(0, 4)}`;
	}
	const d = new Date(n.created_at);
	if (Number.isNaN(d.getTime())) return n.created_at;
	const dd = String(d.getDate()).padStart(2, '0');
	const mm = String(d.getMonth() + 1).padStart(2, '0');
	const yyyy = String(d.getFullYear());
	return `${dd}.${mm}.${yyyy}`;
}

function triggeredBy(n: NotificationRecord): string {
	const p = payloadOf(n);
	const name = valueString(p.triggered_by_name);
	const email = valueString(p.triggered_by_email);
	if (name && email) return `${name} (${email})`;
	return name || email || '-';
}

function notificationRecipient(n: NotificationRecord): string {
	const p = payloadOf(n);
	const name = valueString(p.notification_recipient_name);
	const email = valueString(p.notification_recipient_email);
	return name || displayPersonLabel(email) || '-';
}

function showNotificationRecipient(n: NotificationRecord): boolean {
	return n.category === 'material_assigned' && notificationRecipient(n) !== '-';
}

function mailRecipients(n: NotificationRecord): string {
	const p = payloadOf(n);
	return listString(p.to);
}

function mailRecipientNames(n: NotificationRecord): string {
	const p = payloadOf(n);
	return listDisplayNames(p.to);
}

function mailCcRecipients(n: NotificationRecord): string {
	const p = payloadOf(n);
	return listString(p.cc);
}

function notificationCategoryLabel(n: NotificationRecord): string {
	if (n.category === 'material_assigned') return 'Material';
	if (n.category === 'activity_assigned') return 'Aktivität';
	if (n.category === 'program_assigned') return 'Programm';
	if (n.category === 'mail_own_activity') return 'Mail Aktivität';
	if (n.category === 'mail_department') return 'Mail Stufe';
	return 'Info';
}

function notificationCategoryClass(n: NotificationRecord): string {
	if (n.category === 'material_assigned') return 'avatar-notification-category--material';
	if (n.category === 'activity_assigned') return 'avatar-notification-category--activity';
	if (n.category === 'program_assigned') return 'avatar-notification-category--program';
	if (n.category === 'mail_own_activity') return 'avatar-notification-category--activity-mail';
	if (n.category === 'mail_department') return 'avatar-notification-category--department-mail';
	return '';
}

function compactLink(link: string | null): string {
	if (!link) return '';
	try {
		const url = new URL(link, window.location.origin);
		return `${url.pathname}${url.search}`;
	} catch {
		return link;
	}
}

function headlineRecipients(n: NotificationRecord): string {
	if (n.category === 'material_assigned') return notificationRecipient(n);
	if (n.category === 'activity_assigned' || n.category === 'program_assigned') return notificationRecipient(n);
	return mailRecipientNames(n);
}

function notificationSummary(n: NotificationRecord): string {
	if (n.category === 'material_assigned') return 'Neue Materialverantwortung';
	if (n.category === 'activity_assigned') return 'Du wurdest zur Aktivität hinzugefügt';
	if (n.category === 'program_assigned') {
		const progTitle = n.payload?.program_title as string | undefined;
		return progTitle ? `Programmblock "${progTitle}" dir zugewiesen` : 'Programmblock dir zugewiesen';
	}
	if (n.category === 'mail_own_activity') return 'Mail für deine Aktivität wurde versendet';
	if (n.category === 'mail_department') return 'Mail in deiner Stufe wurde versendet';
	return n.message;
}

onMounted(() => document.addEventListener('mousedown', onClickOutside));
onUnmounted(() => document.removeEventListener('mousedown', onClickOutside));

onMounted(() => {
	ensureNotifications().catch(() => {});
});

onMounted(() => {
	if (user.value?.notify_channel_websocket) {
		requestBrowserNotificationPermission().catch(() => {});
		syncPushSubscription(true).catch(() => {});
	}
});
</script>

<style scoped>
.user-avatar-wrap {
	position: relative;
	display: flex;
	align-items: center;
}

.avatar-circle {
	position: relative;
	width: 36px;
	height: 36px;
	border-radius: 50%;
	background: var(--bg-elevated);
	color: var(--accent);
	border: 2px solid var(--border-strong);
	font-size: 0.8rem;
	font-weight: 700;
	cursor: pointer;
	display: flex;
	align-items: center;
	justify-content: center;
	letter-spacing: 0.5px;
	transition:
		background 0.15s,
		color 0.15s;
}

.avatar-notification-badge {
	position: absolute;
	top: -4px;
	right: -3px;
	min-width: 18px;
	height: 18px;
	border-radius: 999px;
	background: #dc2626;
	color: #fff;
	font-size: 0.68rem;
	font-weight: 700;
	display: inline-flex;
	align-items: center;
	justify-content: center;
	padding: 0 5px;
	line-height: 1;
}
.avatar-circle:hover {
	background: var(--accent);
	color: var(--btn-primary-color);
}

.avatar-dropdown {
	position: absolute;
	top: calc(100% + 10px);
	right: 0;
	min-width: 200px;
	background: var(--dropdown-bg);
	border-radius: 10px;
	box-shadow: var(--shadow-lg);
	z-index: 1000;
	overflow: hidden;
	animation: dd-in 0.12s ease;
}
@keyframes dd-in {
	from {
		opacity: 0;
		transform: translateY(-6px);
	}
	to {
		opacity: 1;
		transform: translateY(0);
	}
}

.avatar-dropdown-header {
	padding: 14px 16px 10px;
	display: flex;
	flex-direction: column;
	gap: 2px;
}
.avatar-dropdown-name {
	font-weight: 700;
	font-size: 0.95rem;
	color: var(--text-primary);
}
.avatar-dropdown-badges {
	display: flex;
	gap: 6px;
	align-items: center;
	margin-top: 2px;
	flex-wrap: wrap;
}
.avatar-dropdown-email {
	font-size: 0.78rem;
	color: var(--text-muted);
	margin-top: 2px;
}

.avatar-dropdown-divider {
	height: 1px;
	background: var(--border);
	margin: 0;
}

.avatar-dropdown-item {
	display: flex;
	align-items: center;
	justify-content: space-between;
	width: 100%;
	padding: 11px 16px;
	font-size: 0.875rem;
	color: var(--text-secondary);
	text-decoration: none;
	background: none;
	border: none;
	text-align: left;
	cursor: pointer;
	transition: background 0.1s;
}
.avatar-dropdown-badge {
	min-width: 18px;
	height: 18px;
	border-radius: 999px;
	background: #dc2626;
	color: #fff;
	font-size: 0.68rem;
	font-weight: 700;
	display: inline-flex;
	align-items: center;
	justify-content: center;
	padding: 0 5px;
	line-height: 1;
	margin-left: 8px;
}
.avatar-dropdown-item:hover {
	background: var(--dropdown-hover);
}
.avatar-dropdown-logout {
	color: #dc2626;
}
.avatar-dropdown-logout:hover {
	background: #fff5f5;
}

.avatar-notifications-modal {
	max-width: 760px;
	max-height: min(82vh, 760px);
	display: flex;
	flex-direction: column;
	padding: 24px;
}

.avatar-notifications-subtitle {
	margin-bottom: 14px;
}

.avatar-notifications-block {
	overflow: auto;
	border: 1px solid var(--border);
	border-radius: 12px;
	background: var(--bg-elevated);
	padding: 8px;
}

.avatar-notifications-empty {
	padding: 14px 16px;
	font-size: 0.85rem;
	color: var(--text-muted);
}

.avatar-notification-item {
	display: block;
	width: 100%;
	text-align: left;
	border: none;
	background: var(--bg-surface);
	padding: 12px 13px;
	cursor: pointer;
	border-radius: 12px;
	margin-top: 8px;
	border: 1px solid var(--border);
	transition:
		background 0.14s ease,
		box-shadow 0.14s ease,
		border-color 0.14s ease;
}

.avatar-notification-item:hover {
	background: var(--bg-elevated);
	border-color: #dbeafe;
	box-shadow: 0 8px 18px rgba(148, 163, 184, 0.12);
}

.avatar-notification-item:first-child {
	margin-top: 0;
}

.avatar-notification-item--unread {
	background: linear-gradient(180deg, #eff6ff 0%, #f8fbff 100%);
	border-color: #bfdbfe;
}

.avatar-notification-headline {
	display: flex;
	align-items: flex-start;
	justify-content: space-between;
	gap: 12px;
}

.avatar-notification-headline-main {
	display: flex;
	align-items: center;
	gap: 8px;
	min-width: 0;
	flex-wrap: wrap;
}

.avatar-notification-headline-side {
	display: flex;
	flex-direction: column;
	align-items: flex-end;
	gap: 2px;
	flex: 0 0 auto;
}

.avatar-notification-category {
	display: inline-flex;
	align-items: center;
	padding: 3px 8px;
	border-radius: 999px;
	font-size: 0.67rem;
	font-weight: 800;
	letter-spacing: 0.04em;
	text-transform: uppercase;
	white-space: nowrap;
	background: var(--chip-bg);
	color: var(--chip-color);
}

.avatar-notification-category--material {
	background: #dcfce7;
	color: #166534;
}

.avatar-notification-category--activity {
	background: #ede9fe;
	color: #6d28d9;
}

.avatar-notification-category--program {
	background: #ccfbf1;
	color: #0f766e;
}

.avatar-notification-category--activity-mail {
	background: #dbeafe;
	color: #1d4ed8;
}

.avatar-notification-category--department-mail {
	background: #ffedd5;
	color: #c2410c;
}

.avatar-notification-title {
	font-size: 0.84rem;
	font-weight: 700;
	color: var(--text-primary);
	line-height: 1.25;
}

.avatar-notification-date-inline {
	font-size: 0.69rem;
	font-weight: 600;
	color: var(--text-muted);
	white-space: nowrap;
}

.avatar-notification-recipients-inline {
	max-width: 240px;
	font-size: 0.68rem;
	font-weight: 600;
	line-height: 1.2;
	color: var(--text-secondary);
	text-align: right;
}

.avatar-notification-message {
	margin-top: 6px;
	font-size: 0.73rem;
	font-weight: 600;
	color: var(--text-secondary);
	line-height: 1.35;
}

.avatar-notification-grid {
	margin-top: 8px;
	display: grid;
	grid-template-columns: repeat(2, minmax(0, 1fr));
	gap: 6px;
}

.avatar-notification-meta {
	display: flex;
	flex-direction: column;
	gap: 2px;
	padding: 7px 9px;
	font-size: 0.71rem;
	color: var(--text-muted);
	background: var(--bg-elevated);
	border: 1px solid var(--border);
	border-radius: 9px;
}

.avatar-notification-meta strong {
	font-size: 0.62rem;
	font-weight: 800;
	letter-spacing: 0.03em;
	text-transform: uppercase;
	color: var(--text-muted);
}

.avatar-notification-meta span {
	color: var(--text-secondary);
	line-height: 1.35;
	word-break: break-word;
}

.avatar-notification-meta--wide {
	grid-column: 1 / -1;
}

.avatar-notification-link-row {
	margin-top: 8px;
	display: flex;
	align-items: center;
	gap: 8px;
	padding-top: 7px;
	border-top: 1px dashed var(--border);
}

.avatar-notification-link-label {
	flex: 0 0 auto;
	font-size: 0.67rem;
	font-weight: 800;
	letter-spacing: 0.04em;
	text-transform: uppercase;
	color: var(--text-muted);
}

.avatar-notification-link {
	font-size: 0.75rem;
	color: var(--accent);
	word-break: break-all;
}

.avatar-notifications-footer {
	margin-top: 14px;
}

@media (max-width: 640px) {
	.avatar-notification-headline {
		flex-direction: column;
		gap: 6px;
	}

	.avatar-notification-headline-side {
		align-items: flex-start;
	}

	.avatar-notification-recipients-inline {
		max-width: none;
		text-align: left;
	}

	.avatar-notification-grid {
		grid-template-columns: 1fr;
	}
}
</style>
