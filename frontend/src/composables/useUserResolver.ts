import { useUsers } from './useUsers'

const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i

export function isUUID(s: string): boolean {
	return UUID_RE.test(s)
}

export function useUserResolver() {
	const { users, fetchUsers } = useUsers()
	let refetchTriggered = false

	function resolveResponsibleName(idOrText: string): string {
		if (!isUUID(idOrText)) return idOrText
		const user = users.value.find(u => u.id === idOrText)
		if (user) return user.display_name
		if (!refetchTriggered) {
			refetchTriggered = true
			fetchUsers(true).finally(() => { refetchTriggered = false })
		}
		return idOrText
	}

	function resolveResponsibleColor(idOrText: string): string {
		if (!isUUID(idOrText)) return '#4f8cff'
		const user = users.value.find(u => u.id === idOrText)
		return user?.avatar_color || '#4f8cff'
	}

	function resolveResponsibleNames(entries: string[]): string[] {
		return entries.map(resolveResponsibleName)
	}

	return { resolveResponsibleName, resolveResponsibleColor, resolveResponsibleNames, isUUID }
}
