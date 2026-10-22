#pragma once
#include "../eye_composition.hpp"

namespace vr::gameplay::weapons::gunstock
{
	// Head-locked calibration text, composed into each eye after the scene.
	// The native message feeds are not drawn in the headset.
	void present_prompt(const eye_composition::event& event,
	                    ID3D11DeviceContext* context,
	                    ID3D11ShaderResourceView* background,
	                    ID3D11RenderTargetView* output) noexcept;
} // namespace vr::gameplay::weapons::gunstock
