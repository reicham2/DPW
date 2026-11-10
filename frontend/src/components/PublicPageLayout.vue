<template>
	<div class="public-page">
		<PublicPageHeader :label="label" />
		<main class="public-page-content">
			<slot />
		</main>
	</div>
</template>

<script setup lang="ts">
import PublicPageHeader from './PublicPageHeader.vue';

defineProps<{ label: string }>();
</script>

<style scoped>
.public-page {
	min-height: 100vh;
	background: var(--bg-base);
	color: var(--text-primary);
	line-height: 1.6;
}

.public-page-content {
	max-width: 860px;
	margin: 0 auto;
	padding: 32px 24px;
}

.public-page-content :deep(.loading-state),
.public-page-content :deep(.error-state),
.public-page-content :deep(.success-state) {
	display: flex;
	flex-direction: column;
	align-items: center;
	gap: 12px;
	padding: 48px 24px;
	background: var(--card-bg);
	border: 1px solid var(--card-border);
	border-radius: 16px;
	color: var(--text-primary);
	text-align: center;
}

@media (max-width: 599px) {
	.public-page-content {
		padding: 20px 16px;
	}
}
</style>
