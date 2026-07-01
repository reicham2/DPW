<script setup lang="ts">
import { ref } from 'vue'
import { ChevronLeft, ChevronRight } from 'lucide-vue-next'
import type { Component } from 'vue'

// eCamp-style contextual left sidebar (the `aside` slot in eCamp's shell),
// dressed in DPWeb design tokens. Header with an icon + title, a divider, then
// the section's navigation/controls in the default slot. Collapsible to a slim
// rail on desktop; on narrow screens it renders as a full-width block above the
// main content (handled by the parent grid).
defineProps<{
  title: string
  icon?: Component
}>()

const collapsed = ref(false)
</script>

<template>
  <aside class="camp-sidebar" :class="{ 'camp-sidebar--collapsed': collapsed }">
    <div class="camp-sidebar-head">
      <button
        v-if="collapsed"
        class="camp-sidebar-rail-btn"
        :title="title"
        @click="collapsed = false"
      >
        <component :is="icon" v-if="icon" :size="18" />
      </button>
      <template v-else>
        <span class="camp-sidebar-title">
          <component :is="icon" v-if="icon" :size="16" class="camp-sidebar-title-ico" />
          {{ title }}
        </span>
        <button class="camp-sidebar-collapse" title="Einklappen" @click="collapsed = true">
          <ChevronLeft :size="16" />
        </button>
      </template>
    </div>
    <div v-if="!collapsed" class="camp-sidebar-body">
      <slot />
    </div>
    <button
      v-else
      class="camp-sidebar-expand"
      title="Ausklappen"
      @click="collapsed = false"
    >
      <ChevronRight :size="16" />
    </button>
  </aside>
</template>

<style scoped>
.camp-sidebar {
  flex: 0 0 244px;
  align-self: flex-start;
  position: sticky;
  top: 12px;
  max-height: calc(100vh - 84px);
  display: flex;
  flex-direction: column;
  background: var(--bg-surface);
  border: 1px solid var(--border);
  border-radius: 12px;
  overflow: hidden;
}
.camp-sidebar--collapsed {
  flex-basis: 46px;
  align-items: center;
}
.camp-sidebar-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 6px;
  padding: 10px 12px;
  border-bottom: 1px solid var(--border);
  min-height: 44px;
}
.camp-sidebar--collapsed .camp-sidebar-head {
  padding: 6px;
  border-bottom: none;
  justify-content: center;
}
.camp-sidebar-title {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  font-size: 0.9rem;
  font-weight: 800;
  color: var(--text-primary);
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.camp-sidebar-title-ico { color: var(--accent); flex-shrink: 0; }
.camp-sidebar-collapse,
.camp-sidebar-expand,
.camp-sidebar-rail-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: transparent;
  border: none;
  color: var(--text-muted);
  cursor: pointer;
  border-radius: 7px;
  padding: 6px;
}
.camp-sidebar-collapse:hover,
.camp-sidebar-expand:hover,
.camp-sidebar-rail-btn:hover { background: var(--bg-hover); color: var(--accent); }
.camp-sidebar-rail-btn { color: var(--accent); }
.camp-sidebar-body {
  overflow-y: auto;
  padding: 6px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

@media (max-width: 900px) {
  /* On narrow screens the sidebar becomes a full-width block; disable collapse
     rail behaviour and horizontal stickiness. */
  .camp-sidebar,
  .camp-sidebar--collapsed {
    flex-basis: auto;
    width: 100%;
    position: static;
    max-height: none;
    align-items: stretch;
  }
  .camp-sidebar-body { max-height: 320px; }
}
</style>
