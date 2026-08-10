// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

namespace Window {
class SessionController;
}

namespace Main {
class Session;
}

namespace Ui {
class GenericBox;
}

void FillAccountNotificationBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<Main::Session*> session);
