#pragma once
#include "weapon_identity.hpp"
#include <string>

// Diagnostic only. Every change of a weapon's support hand, and every time the
// rendered support hand leaves a gun that still owns it, appends one line with
// the controller state to minidumps/overlord-support.txt. Callable from the
// server and render threads; never affects gameplay.
namespace vr::gameplay::weapons::support_journal
{
	void note(const char* what, weapon_identity id, int hand, const char* cause, const std::string& detail = {}) noexcept;
}
