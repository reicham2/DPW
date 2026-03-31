<script setup lang="ts">
import { ref } from 'vue'

const emit = defineEmits<{
  create: [text: string]
}>()

const text = ref('')

function submit() {
  const trimmed = text.value.trim()
  if (!trimmed) return
  emit('create', trimmed)
  text.value = ''
}
</script>

<template>
  <form class="create-form" @submit.prevent="submit">
    <textarea
      v-model="text"
      placeholder="Neue Aktivität..."
      rows="3"
      @keydown.ctrl.enter="submit"
      @keydown.meta.enter="submit"
    />
    <button type="submit" :disabled="!text.trim()">Erstellen</button>
  </form>
</template>
