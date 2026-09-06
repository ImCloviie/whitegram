// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

#include "base/basic_types.h"
#include "ui/layers/generic_box.h"

class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class PasswordInput;
} // namespace Ui

namespace Whitegram::E2EE {

struct MessageContent;

not_null<Ui::PasswordInput*> AddPasswordInputRow(
	not_null<Ui::GenericBox*> box,
	rpl::producer<QString> placeholder);

void ShowViewOnceBox(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

void ShowCustomTtlBox(
	std::shared_ptr<ChatHelpers::Show> show,
	Fn<void(int seconds)> callback);

} // namespace Whitegram::E2EE
