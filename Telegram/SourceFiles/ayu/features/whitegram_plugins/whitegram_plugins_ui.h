// This is the source code of Whitegram Desktop.
//
// Whitegram Python plugin engine UI: the plugins settings page,
// message context menu items and per-plugin settings boxes.
//
// Copyright (c) 2026 Whitegram Team.

#pragma once

#include "base/basic_types.h"

class HistoryItem;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Whitegram::Plugins {

// Settings -> Whitegram -> Plugins page content.
void BuildPluginsPage(Settings::Builder::SectionBuilder &builder);

// Adds registered plugin items to the message context menu.
void AddMessageMenuItems(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	not_null<Window::SessionController*> controller);

// Copies a .plugin file into the plugins folder, trusts and loads it.
void InstallPluginFromPath(
	not_null<Window::SessionController*> controller,
	const QString &path);

} // namespace Whitegram::Plugins
