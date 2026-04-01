import { createRouter, createWebHistory } from 'vue-router'
import IndexPage  from '../pages/IndexPage.vue'
import CreatePage from '../pages/CreatePage.vue'
import DetailPage from '../pages/DetailPage.vue'

export const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: '/',                component: IndexPage  },
    { path: '/activities/new',  component: CreatePage },
    { path: '/activities/:id',  component: DetailPage },
  ]
})
