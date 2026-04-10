<template>
	<div class="user-avatar-wrap" ref="wrapRef">
		<template v-if="user">
			<button
				class="avatar-circle"
				@click="open = !open"
				:title="user.display_name"
			>
				{{ initials }}
			</button>

			<div v-if="open" class="avatar-dropdown">
				<div class="avatar-dropdown-header">
					<span class="avatar-dropdown-name">{{ user.display_name }}</span>
					<span v-if="user.department" class="avatar-dropdown-dept">{{
						user.department
					}}</span>
					<span class="avatar-dropdown-email">{{ user.email }}</span>
				</div>
				<div class="avatar-dropdown-divider" />
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
		</template>
	</div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { user, login, logout } from '../composables/useAuth';

const open = ref(false);
const loggingIn = ref(false);
const wrapRef = ref<HTMLElement | null>(null);

const initials = computed(() => {
	if (!user.value) return '';
	return user.value.display_name
		.split(' ')
		.filter(Boolean)
		.slice(0, 2)
		.map((w) => w[0].toUpperCase())
		.join('');
});

async function handleLogin() {
	loggingIn.value = true;
	try {
		await login();
	} finally {
		loggingIn.value = false;
	}
}

async function handleLogout() {
	open.value = false;
	await logout();
}

function onClickOutside(e: MouseEvent) {
	if (wrapRef.value && !wrapRef.value.contains(e.target as Node)) {
		open.value = false;
	}
}

onMounted(() => document.addEventListener('mousedown', onClickOutside));
onUnmounted(() => document.removeEventListener('mousedown', onClickOutside));
</script>

<style scoped>
.user-avatar-wrap {
	position: relative;
	display: flex;
	align-items: center;
}

.avatar-circle {
	width: 36px;
	height: 36px;
	border-radius: 50%;
	background: #fff;
	color: #1a56db;
	border: 2px solid rgba(255, 255, 255, 0.6);
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
.avatar-dropdown-dept {
	display: inline-block;
	font-size: 0.72rem;
	font-weight: 600;
	background: #e8f0fe;
	color: #1a56db;
	border-radius: 4px;
	padding: 1px 7px;
	margin-top: 2px;
	width: fit-content;
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
</style>
