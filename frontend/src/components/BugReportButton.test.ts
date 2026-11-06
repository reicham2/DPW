import { mount } from '@vue/test-utils'
import { afterEach, describe, expect, it, vi } from 'vitest'
import BugReportButton from './BugReportButton.vue'
import { bugReportOpen, bugReportPrefill } from '../composables/useBugReport'

vi.mock('../composables/useApi', () => ({ apiFetch: vi.fn() }))
vi.mock('../composables/useDebugInfo', () => ({ collectDebugInfo: vi.fn() }))

describe('BugReportButton Easter egg', () => {
  let wrapper: ReturnType<typeof mount>

  afterEach(() => {
    wrapper?.unmount()
    bugReportOpen.value = false
    bugReportPrefill.value = ''
  })

  async function activateTitle(times: number) {
    for (let i = 0; i < times; i++) {
      await wrapper.get('.bug-title-trigger').trigger('click')
    }
  }

  it('reveals the original quote only after five title activations and can be dismissed', async () => {
    wrapper = mount(BugReportButton, { global: { stubs: { teleport: true } } })
    await wrapper.get('.bug-fab').trigger('click')
    expect(wrapper.find('[role="status"]').exists()).toBe(false)
    expect(wrapper.get('.bug-btn-primary').attributes('disabled')).toBeDefined()

    await activateTitle(4)
    expect(wrapper.find('[role="status"]').exists()).toBe(false)
    await activateTitle(1)
    expect(wrapper.get('[role="status"]').text()).toContain('de Delay gaht mir uf d Eier!!!!!!')

    await wrapper.get('[aria-label="Erinnerung schliessen"]').trigger('click')
    expect(wrapper.find('[role="status"]').exists()).toBe(false)
    await activateTitle(4)
    expect(wrapper.find('[role="status"]').exists()).toBe(false)
    await activateTitle(1)
    expect(wrapper.find('[role="status"]').exists()).toBe(true)
  })

  it('resets both partial and revealed state when the dialog closes', async () => {
    wrapper = mount(BugReportButton, { global: { stubs: { teleport: true } } })
    await wrapper.get('.bug-fab').trigger('click')
    await activateTitle(4)
    await wrapper.get('[aria-label="Schliessen"]').trigger('click')
    await wrapper.get('.bug-fab').trigger('click')
    await activateTitle(1)
    expect(wrapper.find('[role="status"]').exists()).toBe(false)
    await activateTitle(4)
    expect(wrapper.find('[role="status"]').exists()).toBe(true)
    await wrapper.get('.bug-btn-secondary').trigger('click')
    await wrapper.get('.bug-fab').trigger('click')
    expect(wrapper.find('[role="status"]').exists()).toBe(false)
  })
})
