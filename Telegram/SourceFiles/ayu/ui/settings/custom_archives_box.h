#pragma once

#include "ui/layers/box_content.h"

namespace Window {
class SessionController;
} // namespace Window

void CustomArchivesBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);

void EditCustomArchiveBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	int archiveId);

void SetArchivePinBox(
	not_null<Ui::GenericBox*> box,
	int archiveId);

void SetArchivePasswordBox(
	not_null<Ui::GenericBox*> box,
	int archiveId);

void CustomArchiveViewBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	int archiveId);

void CustomFolderHintBox(not_null<Ui::GenericBox*> box);

void CustomArchiveUnlockBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	int archiveId,
	Fn<void()> onUnlocked);
