import { createRouter, createWebHistory } from 'vue-router';
import IndexPage from '../pages/IndexPage.vue';
import CreatePage from '../pages/CreatePage.vue';
import DetailPage from '../pages/DetailPage.vue';
import ProfilePage from '../pages/ProfilePage.vue';
import MailComposerPage from '../pages/MailComposerPage.vue';
import AdminPage from '../pages/AdminPage.vue';
import StatsPage from '../pages/StatsPage.vue';
import FormPublicPage from '../pages/FormPublicPage.vue';
import SharedActivityPage from '../pages/SharedActivityPage.vue';
import ActivityFormsPage from '../pages/ActivityFormsPage.vue';
import TemplatesPage from '../pages/TemplatesPage.vue';
import IdeaBoxPage from '../pages/IdeaBoxPage.vue';
import SetupPage from '../pages/SetupPage.vue';
import MaintenancePage from '../pages/MaintenancePage.vue';
import LegalInfoPage from '../pages/LegalInfoPage.vue';
import { user, authLoading } from '../composables/useAuth';
import { ensureSetupStatus } from '../composables/useSetupAuthConfig';

export const router = createRouter({
	history: createWebHistory(),
	routes: [
		{ path: '/setup', component: SetupPage, meta: { setup: true } },
		{ path: '/maintenance', component: MaintenancePage, meta: { public: true } },
		{ path: '/impressum', component: LegalInfoPage, props: { kind: 'impressum' }, meta: { public: true } },
		{ path: '/datenschutz', component: LegalInfoPage, props: { kind: 'datenschutz' }, meta: { public: true } },
		{ path: '/', component: IndexPage },
		{ path: '/activities/new', component: CreatePage },
		{ path: '/activities/:id', component: DetailPage, meta: { loginInPlace: true } },
		{ path: '/activities/:id/mail', component: MailComposerPage },
		{ path: '/activities/:id/forms', component: ActivityFormsPage },
		{ path: '/templates', component: TemplatesPage },
		{ path: '/ideas', component: IdeaBoxPage },
		{ path: '/vorlagen', redirect: '/templates' },
		{ path: '/ideenkiste', redirect: '/ideas' },
		{ path: '/mail-templates', redirect: '/templates' },
		{ path: '/form-templates', redirect: '/templates' },
		{ path: '/stats', component: StatsPage },
		{ path: '/profile', component: ProfilePage },
		{ path: '/admin', component: AdminPage },
		// Public form page — no auth required
		{ path: '/forms/:slug', component: FormPublicPage, meta: { public: true } },
		// Public shared activity — no auth required
		{
			path: '/shared/:token',
			component: SharedActivityPage,
			meta: { public: true },
		},
	],
});

router.beforeEach(async (to) => {
	if (to.meta.public) {
		return true;
	}

	const needsSetup = await ensureSetupStatus();

	// While setup is required, only /setup is allowed.
	if (needsSetup) {
		if (to.path !== '/setup') return '/setup';
		return true;
	}

	// If setup is complete and we're on setup page, redirect away.
	if (to.path === '/setup') return '/';

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

	// App.vue keeps protected content unmounted until login; retain activity deep links.
	if (!user.value && !to.meta.loginInPlace && to.path !== '/') {
		return '/';
	}

	return true;
});
