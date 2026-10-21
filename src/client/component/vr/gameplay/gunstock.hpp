#pragma once
#include "../spatial_math.hpp"
#include <atomic>
#include <cmath>
#include <optional>

// Physical gunstock support. A rigid stock locks both controllers together, so
// the rear controller's orientation fully describes the stock. Two-hand weapons
// keep that orientation (plus one calibrated offset) while the front hand
// supports them: neither the front hand's angle nor its position steers the
// barrel, and grabbing the foregrip never swings the gun.
// Header-only and disabled by default: offline tests keep ordinary aiming.
namespace vr::gameplay::weapons::gunstock
{
	using spatial_math::quat;
	using spatial_math::vec;
	using angles = std::array<float, 3>; // pitch, yaw, roll in degrees, for a right-hand rear grip

	// A stock is held near the natural aim; a larger capture is a measurement error.
	inline constexpr float max_offset_degrees = 45.f;
	// Separation between the mounted controllers, in meters.
	inline constexpr float acquire_min_meters = .10f, acquire_max_meters = 1.0f;
	inline constexpr float retain_min_meters = .08f, retain_max_meters = 1.2f;
	// The front controller must lie within this cone around the barrel.
	inline constexpr float acquire_cone_cosine = .5f;

	struct configuration
	{
		bool enabled{};
		angles offset{};
	};
	using settings_provider = configuration (*)() noexcept;
	inline std::atomic<settings_provider> provider{nullptr};
	inline void set_settings_provider(settings_provider value) noexcept
	{
		provider.store(value);
	}
	inline configuration settings() noexcept
	{
		const auto source = provider.load();
		return source ? source() : configuration{};
	}

	inline quat axis_angle(vec axis, float degrees) noexcept
	{
		const float half = degrees * .00872664625997164788f; // degrees * pi / 360
		const float s = std::sin(half);
		return {axis[0] * s, axis[1] * s, axis[2] * s, std::cos(half)};
	}
	// Gun-local axes: +X forward, +Y left, +Z up. Positive values turn left,
	// tilt up and roll the right side down, matching controller calibration.
	// A left-hand rear grip mirrors yaw and roll across the stock's vertical plane.
	inline quat offset_rotation(angles degrees, bool left_rear) noexcept
	{
		const float yaw = left_rear ? -degrees[1] : degrees[1];
		const float roll = left_rear ? -degrees[2] : degrees[2];
		return spatial_math::normalize(spatial_math::multiply(
		    spatial_math::multiply(axis_angle({0, 0, 1}, yaw), axis_angle({0, 1, 0}, -degrees[0])),
		    axis_angle({1, 0, 0}, roll)));
	}
	// Gun rotation for a stocked two-hand weapon, from the rear controller alone.
	inline quat stocked_rotation(const configuration& config, quat rear_rotation, bool left_rear) noexcept
	{
		return spatial_math::normalize(
		    spatial_math::multiply(spatial_math::normalize(rear_rotation), offset_rotation(config.offset, left_rear)));
	}
	inline bool valid(angles degrees) noexcept
	{
		for (float x : degrees)
			if (!std::isfinite(x) || std::abs(x) > max_offset_degrees)
				return false;
		return true;
	}
	// Offset that turns the rear controller's aim into the player's level gaze.
	// The player sights a distant point along the stock, so gaze and barrel are
	// parallel there. World up is +Z. Returns the raw angles even when they
	// exceed the sanity limit, so the caller can report the rejection.
	inline std::optional<angles> measure(quat rear_rotation, vec head_forward, bool left_rear) noexcept
	{
		using namespace spatial_math;
		if (!std::isfinite(length(head_forward)) || length(head_forward) < 1e-3f)
			return std::nullopt;
		const auto look = unit(head_forward);
		if (std::abs(look[2]) > .95f)
			return std::nullopt;
		const auto up = unit(sub(vec{0, 0, 1}, scale(look, look[2])));
		const auto desired = from_axis({look, cross(up, look), up});
		const auto offset = normalize(multiply(conjugate(normalize(rear_rotation)), desired));
		const auto x = rotate(offset, {1, 0, 0});
		constexpr float degrees = 57.2957795130823208768f;
		const float pitch = std::asin(std::clamp(x[2], -1.f, 1.f)) * degrees;
		const float yaw = std::atan2(x[1], x[0]) * degrees;
		const auto swing = multiply(axis_angle({0, 0, 1}, yaw), axis_angle({0, 1, 0}, -pitch));
		const auto y = rotate(normalize(multiply(conjugate(swing), offset)), {0, 1, 0});
		const float roll = std::atan2(y[2], y[1]) * degrees;
		angles result{pitch, left_rear ? -yaw : yaw, left_rear ? -roll : roll};
		for (float value : result)
			if (!std::isfinite(value))
				return std::nullopt;
		return result;
	}
	inline std::optional<angles> capture(quat rear_rotation, vec head_forward, bool left_rear) noexcept
	{
		const auto result = measure(rear_rotation, head_forward, left_rear);
		if (!result || !valid(*result))
			return std::nullopt;
		return result;
	}
	// Angle between two directions in degrees, for diagnostics.
	inline float angle_between(vec a, vec b) noexcept
	{
		using namespace spatial_math;
		const float c = std::clamp(dot(unit(a), unit(b)), -1.f, 1.f);
		return std::acos(c) * 57.2957795130823208768f;
	}
	// The front controller can take the support grip anywhere ahead of the rear
	// hand. A fixed stock spacing rarely matches each weapon's authored foregrip.
	inline bool reaches(vec forward, vec rear, vec front, float units_per_meter) noexcept
	{
		using namespace spatial_math;
		if (!std::isfinite(units_per_meter) || units_per_meter <= 0)
			return false;
		const auto delta = sub(front, rear);
		const float meters = length(delta) / units_per_meter;
		if (!std::isfinite(meters) || meters < acquire_min_meters || meters > acquire_max_meters)
			return false;
		return dot(unit(delta), unit(forward)) >= acquire_cone_cosine;
	}
	inline bool retains(vec rear, vec front, float units_per_meter) noexcept
	{
		using namespace spatial_math;
		if (!std::isfinite(units_per_meter) || units_per_meter <= 0)
			return false;
		const float meters = length(sub(front, rear)) / units_per_meter;
		return std::isfinite(meters) && meters >= retain_min_meters && meters <= retain_max_meters;
	}
} // namespace vr::gameplay::weapons::gunstock
