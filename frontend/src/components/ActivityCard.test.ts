// @vitest-environment jsdom
import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'
import type { Activity } from '../types'
import ActivityCard from './ActivityCard.vue'

function activity(averageFeedbackRating: number | null): Activity {
	return {
		id: 'activity-1',
		title: 'Sommerlager',
		date: '2026-07-12',
		start_time: '14:00',
		end_time: '16:00',
		goal: '',
		guiding_thread: null,
		location: '',
		responsible: [],
		department: null,
		material: [],
		tn_material: [],
		siko_text: null,
		bad_weather_info: null,
		planned_participants_estimate: null,
		average_feedback_rating: averageFeedbackRating,
		created_at: '',
		updated_at: '',
		programs: [],
	}
}

describe('ActivityCard feedback average', () => {
	it('shows a labeled, color-scaled average, including a valid score of zero', () => {
		const wrapper = mount(ActivityCard, { props: { activity: activity(0) } })
		const badge = wrapper.get('.card-rating')

		expect(badge.text()).toBe('Feedback Ø 0,0 / 10')
		expect(badge.attributes('aria-label')).toBe('Durchschnittliche Feedbackbewertung 0,0 von 10')
		expect(badge.attributes('style')).toContain('hsl(0 65% 28%)')
	})

	it('formats fractional averages and shows no badge when no rated feedback exists', () => {
		const rated = mount(ActivityCard, { props: { activity: activity(7.5) } })
		expect(rated.get('.card-rating').text()).toBe('Feedback Ø 7,5 / 10')
		expect(rated.get('.card-rating').attributes('style')).toContain('hsl(90 65% 28%)')

		const unrated = mount(ActivityCard, { props: { activity: activity(null) } })
		expect(unrated.find('.card-rating').exists()).toBe(false)
	})
})
