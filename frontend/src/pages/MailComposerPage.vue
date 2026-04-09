<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useActivities } from '../composables/useActivities';
import { useMailTemplates } from '../composables/useMailTemplates';
import { user } from '../composables/useAuth';
import type { Activity, Department } from '../types';

const route = useRoute()
const router = useRouter()
const activityId = route.params.id as string

const { fetchActivity } = useActivities()
const { fetchTemplate, sendMail, sending, error } = useMailTemplates()

const activity = ref<Activity | null>(null)
const subject = ref('')
const body = ref('')
const recipients = ref<string[]>([''])
const sent = ref(false)
const loadError = ref<string | null>(null)

function formatDateLong(d: string): string {
  return new Date(d + 'T00:00:00').toLocaleDateString('de-DE', {
    weekday: 'long', day: 'numeric', month: 'long', year: 'numeric'
  })
}

function formatPrograms(act: Activity): string {
  if (!act.programs.length) return '—'
  return act.programs.map(p =>
    `${p.time ? p.time + ' min' : '—'} – ${p.title}${p.responsible ? ' (' + p.responsible + ')' : ''}${p.description ? ': ' + p.description : ''}`
  ).join('\n')
}

function replaceTemplateVars(text: string, act: Activity): string {
  return text
    .replace(/\{\{titel\}\}/gi,            act.title)
    .replace(/\{\{datum\}\}/gi,            formatDateLong(act.date))
    .replace(/\{\{startzeit\}\}/gi,        act.start_time)
    .replace(/\{\{endzeit\}\}/gi,          act.end_time)
    .replace(/\{\{ort\}\}/gi,              act.location)
    .replace(/\{\{verantwortlich\}\}/gi,   act.responsible)
    .replace(/\{\{abteilung\}\}/gi,        act.department ?? '—')
    .replace(/\{\{ziel\}\}/gi,             act.goal)
    .replace(/\{\{material\}\}/gi,         act.material.join(', ') || '—')
    .replace(/\{\{schlechtwetter\}\}/gi,   act.bad_weather_info ?? '—')
    .replace(/\{\{programm\}\}/gi,         formatPrograms(act))
}

onMounted(async () => {
  const act = await fetchActivity(activityId)
  if (!act) {
    loadError.value = 'Aktivität nicht gefunden.'
    return
  }
  activity.value = act

  if (act.department) {
    const tpl = await fetchTemplate(act.department as Department)
    if (tpl) {
      subject.value = replaceTemplateVars(tpl.subject, act)
      body.value    = replaceTemplateVars(tpl.body, act)
    }
  }
})

function addRecipient() {
	recipients.value.push('');
}

function removeRecipient(index: number) {
	recipients.value.splice(index, 1);
}

async function handleSend() {
	const validTo = recipients.value.map((r) => r.trim()).filter(Boolean);
	if (validTo.length === 0 || !subject.value.trim() || !body.value.trim())
		return;

	const fromEmail = user.value?.email ?? '';
	const bodyHtml = body.value.replace(/\n/g, '<br>');

	const ok = await sendMail(validTo, subject.value, bodyHtml, fromEmail);
	if (ok) sent.value = true;
}
</script>

<template>
	<header class="header">
		<button class="btn-back" @click="router.back()">← Zurück</button>
		<h1>Mail senden</h1>
	</header>

	<main class="main">
		<p v-if="loadError" class="error">{{ loadError }}</p>

		<div v-else-if="sent" class="mail-sent">
			<p class="mail-sent-text">✅ Mail wurde erfolgreich versendet!</p>
			<button class="btn-primary" @click="router.back()">
				Zurück zur Aktivität
			</button>
		</div>

		<form v-else-if="activity" class="detail-form" @submit.prevent="handleSend">
			<!-- Activity info -->
			<div class="mail-activity-info">
				<span class="card-dept-badge" v-if="activity.department">{{
					activity.department
				}}</span>
				<strong>{{ activity.title }}</strong>
			</div>

			<!-- From (read-only) -->
			<div class="form-group">
				<label>Absender</label>
				<input
					type="email"
					:value="user?.email ?? ''"
					disabled
					class="mail-from"
				/>
			</div>

			<!-- Recipients -->
			<div class="form-group">
				<label>Empfänger <span class="required">*</span></label>
				<div class="mail-recipients">
					<div v-for="(_, i) in recipients" :key="i" class="mail-recipient-row">
						<input
							v-model="recipients[i]"
							type="email"
							placeholder="email@example.com"
							required
						/>
						<button
							v-if="recipients.length > 1"
							type="button"
							class="btn-remove-sm"
							@click="removeRecipient(i)"
						>
							×
						</button>
					</div>
					<button type="button" class="btn-add" @click="addRecipient">
						+ Empfänger
					</button>
				</div>
			</div>

			<!-- Subject -->
			<div class="form-group">
				<label>Betreff <span class="required">*</span></label>
				<input v-model="subject" type="text" required />
			</div>

			<!-- Body -->
			<div class="form-group">
				<label>Nachricht <span class="required">*</span></label>
				<textarea v-model="body" rows="12" required></textarea>
			</div>

			<p v-if="error" class="error">{{ error }}</p>

			<div class="form-actions">
				<button type="button" class="btn-secondary" @click="router.back()">
					Abbrechen
				</button>
				<button type="submit" class="btn-primary" :disabled="sending">
					{{ sending ? 'Senden...' : '📧 Senden' }}
				</button>
			</div>
		</form>

		<p v-else class="loading">Laden...</p>
	</main>
</template>
