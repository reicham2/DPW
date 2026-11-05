import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushPromises, mount, type VueWrapper } from '@vue/test-utils'
import CronJobsPanel from './CronJobsPanel.vue'

const { apiFetchMock } = vi.hoisted(() => ({ apiFetchMock: vi.fn() }))
vi.mock('../composables/useApi', () => ({ apiFetch: apiFetchMock }))

function data(health = 'overdue', enabled = true) {
  return {
    active: true, started_at: 1700000000000, intervals_minutes: [1, 5, 15, 30, 60, 1440],
    jobs: [{
      id: 'test', name: 'Test job', interval_minutes: 1, enabled, running: false, health,
      next_run: enabled ? 1700000000000 : null, last_started: null, last_finished: null,
      last_success: null, duration_ms: 0, runs: 0, failures: 1, missed_runs: 3,
      last_error: 'Job fehlgeschlagen.',
    }],
  }
}
function response(body = data()) {
  return { ok: true, json: async () => body }
}

describe('CronJobsPanel', () => {
  let wrapper: VueWrapper | undefined
  beforeEach(() => {
    vi.useFakeTimers()
    apiFetchMock.mockReset()
    apiFetchMock.mockResolvedValue(response())
  })
  afterEach(() => {
    wrapper?.unmount()
    wrapper = undefined
    vi.useRealTimers()
  })

  it('shows unhealthy and never-executed jobs, cadence and missed runs', async () => {
    wrapper = mount(CronJobsPanel)
    await flushPromises()
    expect(apiFetchMock).toHaveBeenCalledWith('/api/admin/cron-jobs')
    expect(wrapper.text()).toContain('Überfällig')
    expect(wrapper.text()).toContain('Noch nicht ausgeführt')
    expect(wrapper.text()).toContain('0 / 1 / 3')
    expect(wrapper.text()).toContain('Job fehlgeschlagen.')
    expect(wrapper.text()).toContain('Täglich')
    expect(wrapper.text()).toContain('Neustart zurückgesetzt')
  })

  it('pauses and resumes through the authorized API and updates status', async () => {
    wrapper = mount(CronJobsPanel)
    await flushPromises()
    apiFetchMock.mockResolvedValueOnce(response(data('paused', false)))
    await wrapper.findAll('button')[1]!.trigger('click')
    await flushPromises()
    expect(apiFetchMock).toHaveBeenLastCalledWith('/api/admin/cron-jobs/test', {
      method: 'PUT', body: '{"enabled":false}',
    })
    expect(wrapper.text()).toContain('Pausiert')
    expect(wrapper.text()).toContain('Fortsetzen')
    await wrapper.findAll('button')[1]!.trigger('click')
    await flushPromises()
    expect(apiFetchMock).toHaveBeenLastCalledWith('/api/admin/cron-jobs/test', {
      method: 'PUT', body: '{"enabled":true}',
    })
  })

  it('polls and stops when leaving the panel', async () => {
    wrapper = mount(CronJobsPanel)
    await flushPromises()
    await vi.advanceTimersByTimeAsync(10000)
    expect(apiFetchMock).toHaveBeenCalledTimes(2)
    wrapper.unmount()
    wrapper = undefined
    await vi.advanceTimersByTimeAsync(30000)
    expect(apiFetchMock).toHaveBeenCalledTimes(2)
  })

  it('does not overlap slow requests or restart polling after unmount', async () => {
    let finish!: (value: ReturnType<typeof response>) => void
    apiFetchMock.mockReturnValueOnce(new Promise(resolve => { finish = resolve }))
    wrapper = mount(CronJobsPanel)
    await vi.advanceTimersByTimeAsync(30000)
    expect(apiFetchMock).toHaveBeenCalledTimes(1)
    wrapper.unmount()
    wrapper = undefined
    finish(response())
    await flushPromises()
    await vi.advanceTimersByTimeAsync(30000)
    expect(apiFetchMock).toHaveBeenCalledTimes(1)
  })

  it('surfaces failed polls without presenting an empty success', async () => {
    apiFetchMock.mockResolvedValueOnce({ ok: false, text: async () => 'Keine Berechtigung' })
    wrapper = mount(CronJobsPanel, { global: { stubs: { ErrorAlert: { props: ['error'], template: '<div>{{ error }}</div>' } } } })
    await flushPromises()
    expect(wrapper.text()).toContain('Keine Berechtigung')
    expect(wrapper.text()).not.toContain('Keine Jobs registriert.')
    await vi.advanceTimersByTimeAsync(10000)
    expect(wrapper.text()).toContain('Test job')
    expect(wrapper.text()).not.toContain('Keine Berechtigung')
  })
})
