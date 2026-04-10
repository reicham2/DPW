import { createApp } from 'vue';
import { router } from './router';
import App from './App.vue';
import './assets/main.css';
import './composables/useDebugInfo'; // install global error/request interceptors early
import { installVueErrorHandler } from './composables/useDebugInfo';

const app = createApp(App);
installVueErrorHandler(app);
app.use(router).mount('#app');
