#pragma once

#include "base/basic_types.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Whitegram::SelfDestruct {

// --- Mode 1: Local Self-Destruct ---
void ShowSelfDestructSettingsBox(
	not_null<Window::SessionController*> controller);
void ShowSelfDestructExecuteBox(
	not_null<Window::SessionController*> controller);

// --- Mode 2: Total Destruction (Whitegram + Accounts) ---
void ShowTotalDestructSettingsBoxImpl(
	not_null<Window::SessionController*> controller);
void ShowTotalDestructExecuteBoxImpl(
	not_null<Window::SessionController*> controller);

} // namespace Whitegram::SelfDestruct
