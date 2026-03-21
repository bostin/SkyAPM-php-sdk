import { createRouter, createWebHistory } from 'vue-router'
import TraceList from '../views/TraceList.vue'
import TraceDetail from '../views/TraceDetail.vue'

const routes = [
  { path: '/', component: TraceList },
  { path: '/trace/:traceId', name: 'trace-detail', component: TraceDetail },
  { path: '/about', component: () => import('../views/About.vue') }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

export default router
