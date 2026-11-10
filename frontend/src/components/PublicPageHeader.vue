<template>
	<header class="public-page-header">
		<div class="public-page-header-inner">
			<a :href="homepageUrl" class="public-brand" aria-label="DPWeb-Startseite">
				<img src="/logo.png" alt="" width="44" height="44" />
				<span>DPWeb</span>
			</a>
			<p class="public-page-label">{{ label }}</p>
		</div>
	</header>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { publicUrl } from '../utils/publicUrl';

defineProps<{ label: string }>();

const homepageUrl = computed(() => publicUrl('/'));
</script>

<style scoped>
.public-page-header {
	background: var(--nav-bg);
	border-bottom: 1px solid var(--nav-border);
}

.public-page-header-inner {
	max-width: 860px;
	margin: 0 auto;
	padding: 16px 24px;
	display: flex;
	align-items: center;
	justify-content: space-between;
	gap: 16px;
}

.public-brand {
	display: inline-flex;
	align-items: center;
	gap: 10px;
	color: var(--text-primary);
	font-size: 1.25rem;
	font-weight: 800;
	text-decoration: none;
	border-radius: 8px;
	flex-shrink: 0;
}

.public-brand img {
	object-fit: contain;
}

.public-brand:focus-visible {
	outline: 2px solid var(--text-primary);
	outline-offset: 4px;
}

.public-page-label {
	color: var(--text-secondary);
	font-size: 0.9rem;
	margin: 0;
	text-align: right;
}

@media (max-width: 599px) {
	.public-page-header-inner {
		padding: 12px 16px;
		gap: 12px;
	}

	.public-page-label {
		font-size: 0.8rem;
	}
}
</style>
