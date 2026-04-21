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
				<button class="avatar-dropdown-item" @click="openNotificationsPopup">Benachrichtigungen</button>
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
							<div class="avatar-notification-title">{{ n.title }}</div>
							<div class="avatar-notification-message">{{ n.message }}</div>
							<div class="avatar-notification-meta">Datum: {{ notificationDate(n) }}</div>
							<div class="avatar-notification-meta">Empfänger: {{ notificationRecipients(n) }}</div>
							<div v-if="n.link" class="avatar-notification-link">{{ n.link }}</div>
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
		if (n.category === 'mail_own_activity') return user.value.notify_mail_own_activity;
		if (n.category === 'mail_department') return user.value.notify_mail_department;
		return true;
	}),
);

const initials = computed(() => {
	if (!user.value) return '';
	return user.value.display_name
		.split(' ')
		.filter(Boolean)
		.slice(0, 2)
		.map((w) => w[0].toUpperCase())
		.join('');
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

function notificationRecipients(n: NotificationRecord): string {
	const p = n.payload as Record<string, unknown>;
	const candidates = [p.recipients, p.assigned_users, p.to];
	for (const c of candidates) {
		if (Array.isArray(c) && c.length > 0) {
			return c
				.filter((v) => typeof v === 'string' && v.trim().length > 0)
				.join(', ');
		}
	}
	return '-';
}

function notificationDate(n: NotificationRecord): string {
	const p = n.payload as Record<string, unknown>;
	if (typeof p.activity_date_display === 'string' && p.activity_date_display.trim()) {
		return p.activity_date_display;
	}
	const d = new Date(n.created_at);
	if (Number.isNaN(d.getTime())) return n.created_at;
	const dd = String(d.getDate()).padStart(2, '0');
	const mm = String(d.getMonth() + 1).padStart(2, '0');
	const yyyy = String(d.getFullYear());
	return `${dd}.${mm}.${yyyy}`;
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
	background: #e8f0fe;
	color: #1a56db;
	border: 2px solid #cce0ff;
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
	background: #1a56db;
	color: #fff;
}

.avatar-dropdown {
	position: absolute;
	top: calc(100% + 10px);
	right: 0;
	min-width: 200px;
	background: #fff;
	border-radius: 10px;
	box-shadow: 0 4px 24px rgba(0, 0, 0, 0.13);
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
	color: #1a202c;
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
	color: #6b7280;
	margin-top: 2px;
}

.avatar-dropdown-divider {
	height: 1px;
	background: #f0f0f0;
	margin: 0;
}

.avatar-dropdown-item {
	display: block;
	width: 100%;
	padding: 11px 16px;
	font-size: 0.875rem;
	color: #374151;
	text-decoration: none;
	background: none;
	border: none;
	text-align: left;
	cursor: pointer;
	transition: background 0.1s;
}
.avatar-dropdown-item:hover {
	background: #f5f7ff;
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
	border: 1px solid #e5e7eb;
	border-radius: 12px;
}

.avatar-notifications-empty {
	padding: 14px 16px;
	font-size: 0.85rem;
	color: #6b7280;
}

.avatar-notification-item {
	display: block;
	width: 100%;
	text-align: left;
	border: none;
	border-top: 1px solid #f3f4f6;
	background: #fff;
	padding: 9px 16px;
	cursor: pointer;
}

.avatar-notification-item--unread {
	background: #eff6ff;
}

.avatar-notification-title {
	font-size: 0.82rem;
	font-weight: 700;
	color: #1f2937;
}

.avatar-notification-message {
	margin-top: 4px;
	font-size: 0.78rem;
	color: #4b5563;
}

.avatar-notification-meta {
	margin-top: 4px;
	font-size: 0.74rem;
	color: #6b7280;
}

.avatar-notification-link {
	margin-top: 4px;
	font-size: 0.72rem;
	color: #1a56db;
	word-break: break-all;
}

.avatar-notifications-footer {
	margin-top: 14px;
}
</style>
