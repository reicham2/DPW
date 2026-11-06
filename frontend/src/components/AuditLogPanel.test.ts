import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushPromises, mount, type VueWrapper } from '@vue/test-utils'
import AuditLogPanel from './AuditLogPanel.vue'

const { apiFetchMock } = vi.hoisted(() => ({ apiFetchMock: vi.fn() }))
vi.mock('../composables/useApi', () => ({ apiFetch: apiFetchMock }))

function response(entries: unknown[], next_cursor: string | null = null) {
  return { ok: true, json: async () => ({ entries, next_cursor }) }
}
function event(id = '9007199254740993', outcome = 'completed') {
  return {
    id, occurred_at: '2026-11-01T10:00:00Z', actor_id: 'user-1',
    actor_name: '<img src=x onerror=alert(1)>', action: 'Aktivität — Ändern',
    resource_type: 'activity', resource_id: 'resource-1', outcome, http_status: 200,
  }
}

describe('AuditLogPanel', () => {
  let wrapper: VueWrapper | undefined
  beforeEach(() => {
    apiFetchMock.mockReset()
    apiFetchMock.mockResolvedValue(response([event()], '9007199254740993'))
  })
  afterEach(() => wrapper?.unmount())
  function panel() {
    return mount(AuditLogPanel, {
      global: { stubs: { ErrorAlert: { props: ['error'], template: '<div class="error">{{ error }}</div>' } } },
    })
  }

  it('loads readable events and escapes names instead of rendering HTML', async () => {
    wrapper = panel()
    await flushPromises()
    expect(apiFetchMock).toHaveBeenCalledWith('/api/admin/audit-log?limit=50')
    expect(wrapper.text()).toContain('Erfolgreich')
    expect(wrapper.text()).toContain('<img src=x onerror=alert(1)>')
    expect(wrapper.find('img').exists()).toBe(false)
    expect(wrapper.text()).not.toContain('user-1')
    await wrapper.find('.audit-toggle').trigger('click')
    expect(wrapper.text()).toContain('user-1')
  })

  it('appends pages using string cursors without losing precision', async () => {
    wrapper = panel()
    await flushPromises()
    apiFetchMock.mockResolvedValueOnce(response([event('2', 'unknown')]))
    await wrapper.find('input').setValue('not applied yet')
    await wrapper.find('button.btn-secondary').trigger('click')
    await flushPromises()
    expect(apiFetchMock).toHaveBeenLastCalledWith('/api/admin/audit-log?limit=50&before=9007199254740993')
    expect(wrapper.findAll('tbody tr')).toHaveLength(2)
    expect(wrapper.text()).toContain('Unklar / Fehler')
    expect(wrapper.text()).not.toContain('Weitere laden')
  })

  it('applies actor/outcome filters and resets pagination', async () => {
    wrapper = panel()
    await flushPromises()
    await wrapper.find('input').setValue('Müller & Co')
    await wrapper.find('select').setValue('rejected')
    apiFetchMock.mockResolvedValueOnce(response([event('3', 'rejected')]))
    await wrapper.find('form').trigger('submit')
    await flushPromises()
    expect(apiFetchMock).toHaveBeenLastCalledWith('/api/admin/audit-log?limit=50&actor=M%C3%BCller+%26+Co&outcome=rejected')
    expect(wrapper.findAll('tbody tr')).toHaveLength(1)
    expect(wrapper.text()).toContain('Abgelehnt')
  })

  it('surfaces authorization/load failures without a success-shaped empty state', async () => {
    apiFetchMock.mockResolvedValueOnce({ ok: false, text: async () => 'Keine Berechtigung' })
    wrapper = panel()
    await flushPromises()
    expect(wrapper.text()).toContain('Keine Berechtigung')
    expect(wrapper.text()).not.toContain('Keine Audit-Einträge')
  })

  it('shows anonymous/pending events and prevents overlapping requests', async () => {
    let finish!: (value: ReturnType<typeof response>) => void
    apiFetchMock.mockReturnValueOnce(new Promise(resolve => { finish = resolve }))
    wrapper = panel()
    await wrapper.vm.$nextTick()
    expect(wrapper.find('button').attributes('disabled')).toBeDefined()
    await wrapper.find('form').trigger('submit')
    expect(apiFetchMock).toHaveBeenCalledTimes(1)
    finish(response([{ ...event('4', 'pending'), actor_id: null, actor_name: null, http_status: null }]))
    await flushPromises()
    expect(wrapper.text()).toContain('Ohne verifizierte Anmeldung')
    expect(wrapper.text()).toContain('Ausgang offen')
  })

  it('shows field-level changes and a readable target while protecting content', async () => {
    apiFetchMock.mockResolvedValueOnce(response([{
      ...event(), resource_type: 'user',
      details: { kind: 'changed', resource_name: 'Leiter Eins', changes: [
        { field: 'role', label: 'Rolle', before: 'Mitglied', after: 'Leiter', redacted: false },
        { field: 'department', label: 'Stufe', before: null, after: '<script>unsafe</script>', redacted: false },
        { field: 'programs', label: 'Programmblöcke', redacted: true },
      ] },
    }]))
    wrapper = panel()
    await flushPromises()
    expect(wrapper.text()).toContain('Benutzer: Leiter Eins')
    expect(wrapper.text()).not.toContain('Mitglied → Leiter')
    expect(wrapper.find('.audit-details').exists()).toBe(false)
    expect(wrapper.find('.audit-toggle').attributes('aria-expanded')).toBe('false')
    await wrapper.find('.audit-entry').trigger('click')
    expect(wrapper.find('.audit-toggle').attributes('aria-expanded')).toBe('true')
    expect(wrapper.text()).toContain('Mitglied → Leiter')
    expect(wrapper.text()).toContain('— → <script>unsafe</script>')
    expect(wrapper.text()).toMatch(/Programmblöcke:\s+geändert \(Inhalt geschützt\)/)
    expect(wrapper.find('script').exists()).toBe(false)
    expect(apiFetchMock).toHaveBeenCalledTimes(1)
    await wrapper.find('.audit-toggle').trigger('click')
    expect(wrapper.find('.audit-details').exists()).toBe(false)
  })

  it('distinguishes deleted, saved and legacy entries without inventing historical details', async () => {
    apiFetchMock.mockResolvedValueOnce(response([
      { ...event('1'), details: { kind: 'deleted', changes: [{ field: 'name', label: 'Name', before: 'Wald', after: null, redacted: false }] } },
      { ...event('2'), details: { kind: 'saved', changes: [{ field: 'enabled', label: 'Aktiviert', after: false, redacted: false }] } },
      event('3'),
    ]))
    wrapper = panel()
    await flushPromises()
    await wrapper.findAll('.audit-toggle')[0].trigger('click')
    expect(wrapper.text()).toContain('Wald → gelöscht')
    await wrapper.findAll('.audit-toggle')[1].trigger('click')
    expect(wrapper.text()).not.toContain('Wald → gelöscht')
    expect(wrapper.findAll('.audit-details')).toHaveLength(1)
    expect(wrapper.find('.audit-details').text()).toMatch(/Aktiviert:\s*Nein/)
    await wrapper.findAll('.audit-toggle')[2].trigger('click')
    expect(wrapper.text()).toContain('Keine Detaildaten vorhanden')
  })

  it('keeps expansion during pagination but closes it on refresh', async () => {
    wrapper = panel()
    await flushPromises()
    await wrapper.find('.audit-toggle').trigger('click')
    apiFetchMock.mockResolvedValueOnce(response([event('2')]))
    await wrapper.find('button.btn-secondary').trigger('click')
    await flushPromises()
    expect(wrapper.findAll('.audit-expanded')).toHaveLength(1)
    expect(wrapper.find('.audit-toggle').attributes('aria-expanded')).toBe('true')
    apiFetchMock.mockResolvedValueOnce(response([event()]))
    await wrapper.find('form').trigger('submit')
    await flushPromises()
    expect(wrapper.find('.audit-expanded').exists()).toBe(false)
  })

  it('bounds summaries without rendering full change values for a full page', async () => {
    const changes = Array.from({ length: 20 }, (_, i) => ({
      field: `field-${i}`, label: `Feld ${i}`, before: `before-${i}`, after: `after-${i}`, redacted: false,
    }))
    apiFetchMock.mockResolvedValueOnce(response(Array.from({ length: 50 }, (_, i) => ({
      ...event(String(i)), details: { kind: 'changed', changes },
    }))))
    wrapper = panel()
    await flushPromises()
    expect(wrapper.findAll('.audit-entry')).toHaveLength(50)
    expect(wrapper.findAll('.audit-details')).toHaveLength(0)
    expect(wrapper.text()).toContain('Feld 0, Feld 1, Feld 2 (+17)')
    expect(wrapper.text()).not.toContain('before-0')
    await wrapper.findAll('.audit-toggle')[49].trigger('click')
    expect(wrapper.findAll('.audit-details')).toHaveLength(1)
    expect(wrapper.text()).toContain('before-19 → after-19')
    expect(apiFetchMock).toHaveBeenCalledTimes(1)
  })
})
