import { createApp } from 'vue';
import { router } from './router';
import App from './App.vue';
import { registerSW } from 'virtual:pwa-register';
import './assets/main.css';
import './composables/useDebugInfo'; // install global error/request interceptors early
import { installVueErrorHandler } from './composables/useDebugInfo';

registerSW({ immediate: true });

const app = createApp(App);
installVueErrorHandler(app);
app.use(router).mount('#app');
