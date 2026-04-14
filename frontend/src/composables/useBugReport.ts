import { ref } from 'vue';

export const bugReportOpen = ref(false);
export const bugReportPrefill = ref('');

export function openBugReport(description: string) {
	bugReportPrefill.value = description;
	bugReportOpen.value = true;
}
