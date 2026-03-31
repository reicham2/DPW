<script setup lang="ts">
import { ref, watch } from 'vue'
import type { Activity } from '../types'

const props = defineProps<{ activity: Activity }>()
const emit = defineEmits<{
  update: [id: string, text: string]
  delete: [id: string]
}>()

const localText = ref(props.activity.text)
let debounceTimer: ReturnType<typeof setTimeout> | null = null

// Sync incoming WS updates only when the user is not actively typing
watch(() => props.activity.text, (newText) => {
  if (!debounceTimer) {
    localText.value = newText
  }
})

function onInput() {
  if (debounceTimer) clearTimeout(debounceTimer)
  debounceTimer = setTimeout(() => {
    debounceTimer = null
    emit('update', props.activity.id, localText.value)
  }, 400)
}
</script>

<template>
  <div class="activity-item">
    <textarea
      v-model="localText"
      rows="3"
      @input="onInput"
      placeholder="Aktivität bearbeiten..."
    />
    <button class="delete-btn" @click="emit('delete', activity.id)" title="Löschen">✕</button>
  </div>
</template>
