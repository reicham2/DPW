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
})
