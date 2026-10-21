#pragma once
#include "../controller_input.hpp"
#include "../spatial_math.hpp"
#include <array>

namespace vr::gameplay::weapons::gunstock
{
	// Called once per solved hand frame with the same wrist targets that drive
	// weapon aiming. Arms nothing on its own; vr_gunstockCalibrate requests a capture.
	void observe(const controller_input::frame& input,
	             const std::array<spatial_math::anchor, 2>& targets,
	             std::array<bool, 2> tracked,
	             spatial_math::vec head_position,
	             spatial_math::vec head_forward,
	             float units_per_meter) noexcept;
} // namespace vr::gameplay::weapons::gunstock
