<script setup lang="ts">
import { ChevronRight } from 'lucide-vue-next'
import type { Component } from 'vue'

// eCamp-style sidebar list item (SidebarListItem): leading icon or colour dot,
// title + optional subtitle, trailing chevron, active state. DPWeb tokens.
defineProps<{
  title: string
  subtitle?: string
  icon?: Component
  dot?: string
  active?: boolean
  hideChevron?: boolean
}>()

defineEmits<{ (e: 'click'): void }>()
</script>

<template>
  <button
    class="csi"
    :class="{ 'csi--active': active }"
    @click="$emit('click')"
  >
    <span class="csi-lead">
      <span v-if="dot" class="csi-dot" :style="{ background: dot }" />
      <component :is="icon" v-else-if="icon" :size="18" class="csi-ico" />
    </span>
    <span class="csi-text">
      <span class="csi-title">{{ title }}</span>
      <span v-if="subtitle" class="csi-subtitle">{{ subtitle }}</span>
    </span>
    <ChevronRight v-if="!hideChevron" :size="15" class="csi-chevron" />
  </button>
</template>

<style scoped>
.csi {
  display: flex;
  align-items: center;
  gap: 10px;
  width: 100%;
  text-align: left;
  background: transparent;
  border: none;
  border-radius: 8px;
  padding: 9px 10px;
  cursor: pointer;
  color: var(--text-secondary);
  transition: background 0.12s, color 0.12s;
}
.csi:hover { background: var(--bg-hover); color: var(--accent); }
.csi--active { background: var(--accent-bg); color: var(--accent); }
.csi-lead { display: inline-flex; align-items: center; justify-content: center; width: 20px; flex-shrink: 0; }
.csi-ico { color: var(--text-muted); }
.csi--active .csi-ico { color: var(--accent); }
.csi-dot { width: 12px; height: 12px; border-radius: 50%; }
.csi-text { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 1px; }
.csi-title {
  font-size: 0.88rem; font-weight: 600; line-height: 1.2;
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.csi-subtitle {
  font-size: 0.74rem; color: var(--text-subtle); line-height: 1.2;
  overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
}
.csi--active .csi-subtitle { color: var(--accent); opacity: 0.8; }
.csi-chevron { color: var(--text-subtle); flex-shrink: 0; }
.csi--active .csi-chevron { color: var(--accent); }
</style>
