#pragma once
#include "component/vr/gameplay/gunstock.hpp"
#include "component/vr/gameplay/weapon_profile.hpp"
#include "component/vr/gameplay/support_retention.hpp"

namespace gunstock_tests
{
	namespace gs = vr::gameplay::weapons::gunstock;
	using vr::spatial_math::quat;
	using vr::spatial_math::vec;
	inline gs::configuration enabled_settings() noexcept
	{
		return {true, {6.f, -4.f, 3.f}};
	}
	template <class Check> void run(Check&& check)
	{
		using namespace vr::spatial_math;
		const auto same = [](vec a, vec b) { return length(sub(a, b)) < 1e-3f; };
		const quat identity{0, 0, 0, 1};
		const auto zero = gs::capture(identity, {0, 0, 0}, {20, 0, 0}, {1, 0, 0}, false);
		check(zero && std::abs((*zero)[0]) < 1e-3f && std::abs((*zero)[1]) < 1e-3f && std::abs((*zero)[2]) < 1e-3f,
		      "a level stock sighted straight ahead captures a zero gunstock offset");
		const auto pitched = gs::capture(identity, {0, 0, 0}, {20, 0, -5}, {1, 0, 0}, false);
		check(pitched && std::abs((*pitched)[0] - 14.036f) < .01f,
		      "a stock line below the gaze captures a positive (upward) pitch offset");
		// Capture then aim reproduces the level gaze for either shoulder and a rolled rear controller.
		for (const bool left : {false, true})
		{
			const quat rear_rotation =
			    normalize(multiply(gs::axis_angle({1, 0, 0}, 25), gs::axis_angle({0, 0, 1}, left ? -10.f : 10.f)));
			const vec rear{3, -2, 50}, front = add(rear, {18, left ? -3.f : 3.f, -4});
			const auto gaze = unit(vec{1, left ? -.2f : .2f, .05f});
			const auto offset = gs::capture(rear_rotation, rear, front, gaze, left);
			check(offset.has_value(), "a plausible shouldered pose captures an offset");
			if (!offset)
				continue;
			const auto aimed = gs::aimed({true, *offset}, rear_rotation, rear, front, left);
			check(aimed && same(rotate(*aimed, {1, 0, 0}), gaze) && std::abs(rotate(*aimed, {0, 1, 0})[2]) < 1e-3f,
			      "the calibrated stock aims along the captured gaze with a level horizon");
		}
		check(!gs::capture(identity, {0, 0, 0}, {0.1f, 0, 0}, {1, 0, 0}, false),
		      "coincident controllers cannot define a stock line");
		check(!gs::capture(identity, {0, 0, 0}, {20, 0, 0}, {0, 0, 1}, false), "looking straight up is rejected");

		// Acquisition ignores the authored foregrip distance but keeps a forward cone and spacing bounds.
		constexpr float units = 39.37f;
		check(gs::reaches({1, 0, 0}, {0, 0, 0}, {0.45f * units, 0, 0}, units),
		      "the front controller supports anywhere ahead of the rear hand");
		check(!gs::reaches({1, 0, 0}, {0, 0, 0}, {0, 0, -0.45f * units}, units),
		      "a hand below the rear grip (a waist holster) is not a support grip");
		check(!gs::reaches({1, 0, 0}, {0, 0, 0}, {0.05f * units, 0, 0}, units), "touching hands do not take support");
		check(gs::retains({0, 0, 0}, {0.6f * units, 0, 0}, units) && !gs::retains({0, 0, 0}, {1.5f * units, 0, 0}, units),
		      "support is retained across the whole mounted spacing range");

		// Disabled by default: ordinary authored-span aiming is unchanged.
		const vec span{10, 0, -2};
		const auto ordinary = vr::gameplay::weapons::aimed_rotation(
		    vr::gameplay::weapons::aim_rule::two_hand, identity, {0, 0, 0}, {0, 10, -2}, span, 1);
		check(same(rotate(ordinary, span), scale(unit(vec{0, 10, -2}), length(span))),
		      "without gunstock mode the authored grip span still points at the support hand");
		gs::set_settings_provider(enabled_settings);
		const auto stocked = vr::gameplay::weapons::aimed_rotation(
		    vr::gameplay::weapons::aim_rule::two_hand, identity, {0, 0, 0}, {0, 10, -2}, span, 1);
		const auto expected = gs::aimed(enabled_settings(), identity, {0, 0, 0}, {0, 10, -2}, false);
		check(expected && same(rotate(stocked, {1, 0, 0}), rotate(*expected, {1, 0, 0})),
		      "gunstock mode replaces the authored span with the calibrated stock line");
		const auto rear_only = vr::gameplay::weapons::aimed_rotation(
		    vr::gameplay::weapons::aim_rule::rear_hand, identity, {0, 0, 0}, {0, 10, -2}, span, 1);
		check(same(rotate(rear_only, {1, 0, 0}), {1, 0, 0}), "one-hand weapons ignore gunstock mode");
		gs::set_settings_provider(nullptr);
	}
} // namespace gunstock_tests
