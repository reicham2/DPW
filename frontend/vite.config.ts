/// <reference types="vitest" />
import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';
import { VitePWA } from 'vite-plugin-pwa';

export default defineConfig({
	test: {
		environment: 'happy-dom',
		include: ['src/**/*.test.ts'],
	},
	plugins: [
		vue(),
		VitePWA({
			registerType: 'autoUpdate',
			includeAssets: ['logo.svg'],
			manifest: {
				name: 'DPWeb Aktivitäten',
				short_name: 'DPWeb',
				description: 'Aktivitätenplanung für Pfadfinder',
				start_url: '/',
				display: 'standalone',
				background_color: '#ffffff',
				theme_color: '#0080ff',
				icons: [
					{
						src: '/logo.svg',
						sizes: 'any',
						type: 'image/svg+xml',
						purpose: 'any',
					},
					{
						src: '/logo.svg',
						sizes: 'any',
						type: 'image/svg+xml',
						purpose: 'maskable',
					},
				],
			},
			workbox: {
				// Cache the app shell and all static assets
				globPatterns: ['**/*.{js,css,html,svg,png,woff2}'],
				// Network-first for API calls, cache-first for assets
				runtimeCaching: [
					{
						urlPattern: /^\/api\//,
						handler: 'NetworkOnly',
					},
					{
						urlPattern: /^\/ws/,
						handler: 'NetworkOnly',
					},
				],
			},
		}),
	],
	server: {
		proxy: {
			'/api': {
				target: 'http://localhost:8080',
				rewrite: (path) => path.replace(/^\/api/, ''),
			},
			'/ws': {
				target: 'ws://localhost:8080',
				ws: true,
			},
		},
	},
});
