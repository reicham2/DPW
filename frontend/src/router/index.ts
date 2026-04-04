import { createRouter, createWebHistory } from 'vue-router'
import IndexPage   from '../pages/IndexPage.vue'
import CreatePage  from '../pages/CreatePage.vue'
import DetailPage  from '../pages/DetailPage.vue'
import ProfilePage from '../pages/ProfilePage.vue'
import { user, authLoading } from '../composables/useAuth'

export const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/',                component: IndexPage   },
    { path: '/activities/new',  component: CreatePage  },
    { path: '/activities/:id',  component: DetailPage  },
    { path: '/profile',         component: ProfilePage },
  ]
})

router.beforeEach(async () => {
  // Wait for auth to initialise before any navigation decision
  if (authLoading.value) {
    await new Promise<void>(resolve => {
      const stop = setInterval(() => {
        if (!authLoading.value) { clearInterval(stop); resolve() }
      }, 50)
    })
  }
  // If not logged in, App.vue shows the login overlay — no redirect needed here
  return true
})
