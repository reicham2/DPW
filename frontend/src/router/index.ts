import { createRouter, createWebHistory } from 'vue-router';
import IndexPage from '../pages/IndexPage.vue';
import CreatePage from '../pages/CreatePage.vue';
import DetailPage from '../pages/DetailPage.vue';
import ProfilePage from '../pages/ProfilePage.vue';
import MailComposerPage from '../pages/MailComposerPage.vue';
import MailTemplatePage from '../pages/MailTemplatePage.vue';
import AdminPage from '../pages/AdminPage.vue';
import { user, authLoading } from '../composables/useAuth';

export const router = createRouter({
	history: createWebHistory(),
	routes: [
		{ path: '/', component: IndexPage },
		{ path: '/activities/new', component: CreatePage },
		{ path: '/activities/:id', component: DetailPage },
		{ path: '/activities/:id/mail', component: MailComposerPage },
		{ path: '/mail-templates', component: MailTemplatePage },
		{ path: '/profile', component: ProfilePage },
		{ path: '/admin', component: AdminPage },
	],
});

router.beforeEach(async (to) => {
	// Wait for auth to initialise before any navigation decision
	if (authLoading.value) {
		await new Promise<void>((resolve) => {
			const stop = setInterval(() => {
				if (!authLoading.value) {
					clearInterval(stop);
					resolve();
				}
			}, 50);
		});
	}
	// Redirect to home if not logged in and trying to access a protected route
	if (!user.value && to.path !== '/') {
		return '/';
	}
	return true;
});
