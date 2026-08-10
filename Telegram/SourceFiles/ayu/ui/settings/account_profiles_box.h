#pragma once

#include "ui/layers/box_content.h"

namespace Window {
class SessionController;
} // namespace Window

void AccountProfilesBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);
