#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Ui {

class GenericBox;

// The own pre-beta badge can be recolored by its owner.
[[nodiscard]] bool HasColorableBadge(uint64 userId);
[[nodiscard]] QString BadgeColorButtonText();

void FillBadgeColorBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

} // namespace Ui
