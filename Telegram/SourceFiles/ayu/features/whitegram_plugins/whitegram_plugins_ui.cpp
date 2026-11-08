// This is the source code of Whitegram Desktop.
//
// Whitegram Python plugin engine UI.
// Copyright (c) 2026 Whitegram Team.

#include "ayu/features/whitegram_plugins/whitegram_plugins_ui.h"

#include "ayu/features/whitegram_plugins/whitegram_plugins.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_main.h"
#include "ayu/ui/boxes/plugin_info_box.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "settings/settings_builder.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QCursor>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtWidgets/QFileDialog>

#include <algorithm>
#include <array>
#include <vector>

namespace Whitegram::Plugins {
namespace {

[[nodiscard]] bool IsRuLanguage() {
	const auto id = Lang::GetInstance().id();
	const auto base = Lang::GetInstance().baseId();
	return id.startsWith(u"ru"_q) || base.startsWith(u"ru"_q);
}

[[nodiscard]] QString Lang2(const QString &ru, const QString &en) {
	return IsRuLanguage() ? ru : en;
}

[[nodiscard]] QString StateText(const QJsonObject &info) {
	const auto error = info.value(u"error"_q).toString();
	if (error == u"UNKNOWN_SOURCE"_q) {
		return Lang2(
			u"неизвестный источник"_q,
			u"unknown source"_q);
	}
	if (!error.isEmpty()) {
		const auto lines = error.trimmed().split(u'\n');
		return Lang2(u"ошибка"_q, u"error"_q)
			+ u": "_q
			+ lines.back();
	}
	if (!info.value(u"loaded"_q).toBool()) {
		return Lang2(u"выключен"_q, u"disabled"_q);
	}
	if (!info.value(u"trusted"_q).toBool()) {
		return Lang2(u"непроверенный"_q, u"unverified"_q);
	}
	return QString();
}

void ShowPluginSettingsBox(
		not_null<Window::SessionController*> controller,
		const QString &pluginId,
		const QString &name);

void ShowSourceBox(
		not_null<Window::SessionController*> controller,
		const QString &name,
		const QString &path) {
	const auto source = Call(u"source"_q, QJsonObject{
		{ u"path"_q, path },
	}).toString();
	if (source.isEmpty()) {
		controller->showToast(Lang2(
			u"Не удалось прочитать файл плагина."_q,
			u"Could not read the plugin file."_q));
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(name));
		const auto label = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(source.left(100000)),
				st::boxDividerLabel));
		label->setSelectable(true);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		box->addButton(
			rpl::single(Lang2(u"Копировать"_q, u"Copy"_q)),
			[=] {
				QGuiApplication::clipboard()->setText(source);
			});
	}));
}

void ShowDeleteConfirm(
		not_null<Window::SessionController*> controller,
		const QString &pluginId) {
	controller->show(Ui::MakeConfirmBox({
		.text = Lang2(
			u"Плагин и его настройки будут удалены."_q,
			u"The plugin and its settings will be deleted."_q),
		.confirmed = [=](Fn<void()> close) {
			Call(u"delete"_q, QJsonObject{
				{ u"id"_q, pluginId },
			});
			notifyListChanged();
			close();
		},
		.confirmText = Lang2(u"Удалить"_q, u"Delete"_q),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

[[nodiscard]] QString SubtitleText(const QJsonObject &info) {
	QStringList parts;
	const auto version = info.value(u"version"_q).toString();
	if (!version.isEmpty()) {
		parts.push_back(u"v"_q + version);
	}
	const auto author = info.value(u"author"_q).toString();
	if (!author.isEmpty()) {
		parts.push_back(author);
	}
	const auto state = StateText(info);
	if (!state.isEmpty()) {
		parts.push_back(state);
	}
	return parts.join(u" • "_q);
}

constexpr auto kPluginSlideDuration = 250;

void PopulatePluginCard(
		not_null<Ui::VerticalLayout*> outer,
		not_null<Window::SessionController*> controller,
		const QJsonObject &info) {
	// Keep overlay children inside a layout-owned widget so refresh removes
	// the old icon and its subscriptions along with the rest of the card.
	const auto card = outer->add(object_ptr<Ui::VerticalLayout>(outer));
	const auto kIconSize = st::wgPluginIconSize;
	const auto kIconGap = st::wgPluginIconGap;
	const auto padding = st::wgPluginCardPadding;
	const auto shift = kIconSize + kIconGap;

	const auto id = info.value(u"id"_q).toString();
	const auto path = info.value(u"path"_q).toString();
	const auto name = info.value(u"name"_q).toString();

	Ui::AddSkip(card, padding);

	const auto button = card->add(
		object_ptr<Ui::SettingsButton>(
			card,
			rpl::single(name),
			st::wgPluginNameButton));
	auto namePadding = st::wgPluginNameButton.padding;
	namePadding.setLeft(namePadding.left() + shift);
	button->setPaddingOverride(namePadding);
	const auto toggled = card->lifetime().make_state<
		rpl::variable<bool>>(
			info.value(u"enabled"_q).toBool() && !SafeMode());
	button->toggleOn(toggled->value());
	button->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		if (enabled && SafeMode()) {
			controller->showToast(Lang2(
				u"Сначала выключите безопасный режим."_q,
				u"Turn off safe mode first."_q));
			crl::on_main(button, [=] {
				toggled->force_assign(false);
			});
			return;
		}
		Call(u"set_enabled"_q, QJsonObject{
			{ u"id"_q, id },
			{ u"enabled"_q, enabled },
		});
		notifyListChanged();
	}, button->lifetime());

	// Colored letter badge.
	static const auto kColors = std::array {
		&st::historyPeer1UserpicBg,
		&st::historyPeer2UserpicBg,
		&st::historyPeer3UserpicBg,
		&st::historyPeer4UserpicBg,
		&st::historyPeer5UserpicBg,
		&st::historyPeer6UserpicBg,
	};
	const auto colorIndex = qHash(id) % kColors.size();
	const auto badge = Ui::CreateChild<Ui::RpWidget>(card);
	badge->resize(kIconSize, kIconSize);
	badge->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(badge);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor((*kColors[colorIndex])->c));
		p.drawRoundedRect(badge->rect(), kIconSize / 4., kIconSize / 4.);
		const auto letter = name.isEmpty()
			? u"?"_q
			: name.left(1).toUpper();
		auto font = QFont(st::normalFont);
		font.setPixelSize(kIconSize / 2);
		font.setBold(true);
		p.setFont(font);
		p.setPen(QColor(Qt::white));
		p.drawText(badge->rect(), Qt::AlignCenter, letter);
	}, badge->lifetime());

	const auto meta = card->add(
		object_ptr<Ui::FlatLabel>(
			card,
			rpl::single(SubtitleText(info)),
			st::wgPluginMeta),
		style::margins(
			padding + shift,
			4,
			padding,
			0));

	rpl::combine(
		button->geometryValue(),
		meta->geometryValue()
	) | rpl::on_next([=](QRect nameRect, QRect line) {
		const auto top = nameRect.y();
		const auto bottom = line.y() + line.height();
		badge->moveToLeft(
			padding,
			top + (bottom - top - kIconSize) / 2);
		badge->raise();
	}, badge->lifetime());

	const auto description = info.value(u"description"_q).toString();
	if (!description.isEmpty()) {
		card->add(
			object_ptr<Ui::FlatLabel>(
				card,
				rpl::single(description),
				st::boxLabel),
			style::margins(
				padding,
				st::wgPluginIconGap,
				padding,
				0))->setSelectable(true);
	}

	const auto error = info.value(u"error"_q).toString();
	if (!error.isEmpty() && error != u"UNKNOWN_SOURCE"_q) {
		card->add(
			object_ptr<Ui::FlatLabel>(
				card,
				rpl::single(Lang2(u"Ошибка: "_q, u"Error: "_q)
					+ error.trimmed().split(u'\n').back()),
				st::boxLabel),
			style::margins(
				padding,
				st::wgPluginIconGap,
				padding,
				0))->setSelectable(true);
	}

	// Actions row.
	const auto actions = card->add(
		object_ptr<Ui::FixedHeightWidget>(card, st::wgPluginAction.height),
		style::margins(
			padding - 6,
			st::wgPluginIconGap,
			padding - 6,
			0));
	auto buttons = std::vector<not_null<Ui::IconButton*>>();
	const auto addAction = [&](
			const style::icon &icon,
			const QString &label,
			Fn<void()> callback) {
		const auto action = Ui::CreateChild<Ui::IconButton>(
			actions,
			st::wgPluginAction);
		action->setIconOverride(&icon, &icon);
		action->setAccessibleName(label);
		action->setToolTip(label);
		action->setClickedCallback(std::move(callback));
		buttons.push_back(action);
	};
	if (info.value(u"has_settings"_q).toBool()) {
		addAction(st::menuIconSettings, Lang2(u"Настройки"_q, u"Settings"_q), [=] {
			ShowPluginSettingsBox(controller, id, name);
		});
	}
	if (!info.value(u"trusted"_q).toBool()) {
		addAction(st::menuIconLock, Lang2(u"Доверять плагину"_q, u"Trust plugin"_q), [=] {
			Call(u"trust"_q, QJsonObject{
				{ u"path"_q, path },
			});
			notifyListChanged();
		});
	}
	addAction(st::menuIconEdit, Lang2(u"Исходный код"_q, u"Source code"_q), [=] {
		ShowSourceBox(controller, name, path);
	});
	addAction(st::menuIconFile, Lang2(u"Показать файл"_q, u"Show file"_q), [=] {
		File::ShowInFolder(path);
	});
	const auto remove = Ui::CreateChild<Ui::IconButton>(
		actions,
		st::wgPluginAction);
	remove->setIconOverride(
		&st::menuIconDeleteAttention,
		&st::menuIconDeleteAttention);
	remove->setClickedCallback([=] {
		ShowDeleteConfirm(controller, id);
	});
	remove->setAccessibleName(Lang2(u"Удалить плагин"_q, u"Delete plugin"_q));
	remove->setToolTip(remove->accessibleName());
	actions->widthValue(
	) | rpl::on_next([=](int width) {
		auto left = 0;
		for (const auto action : buttons) {
			action->moveToLeft(left, 0, width);
			left += action->width();
		}
		remove->moveToRight(0, 0, width);
	}, actions->lifetime());

	// The row is already visible when refreshing an existing card. Showing
	// only the card's direct children does not show these new grandchildren.
	actions->showChildren();
	Ui::AddSkip(card, st::wgPluginIconGap);
	card->showChildren();
}

struct PluginCardState {
	QString id;
	not_null<Ui::SlideWrap<Ui::VerticalLayout>*> wrap;
	not_null<Ui::VerticalLayout*> body;
	QJsonObject last;
};

struct PluginListState {
	std::vector<PluginCardState> cards;
	rpl::variable<QString> emptyTitle;
	rpl::variable<QString> emptyText;
	rpl::variable<QString> emptyAction;
	Ui::SlideWrap<Ui::VerticalLayout> *empty = nullptr;
};

enum class EmptyPluginState {
	Ready,
	Disabled,
	Unavailable,
	Failed,
};

[[nodiscard]] EmptyPluginState CurrentEmptyPluginState() {
	return (!Compiled() || !Available())
		? EmptyPluginState::Unavailable
		: !EngineEnabled()
		? EmptyPluginState::Disabled
		: !Started()
		? EmptyPluginState::Failed
		: EmptyPluginState::Ready;
}

void SetEngineRunning(bool enabled) {
	SetEngineEnabled(enabled);
	if (enabled) {
		Start();
	} else {
		Shutdown();
	}
	notifyListChanged();
}

void UpdateEmptyPluginState(not_null<PluginListState*> state) {
	switch (CurrentEmptyPluginState()) {
	case EmptyPluginState::Ready:
		state->emptyTitle = Lang2(u"Расширьте Whitegram"_q, u"Make Whitegram yours"_q);
		state->emptyText = Lang2(
			u"Добавьте первый плагин: новые команды, инструменты и маленькие удобства для ваших чатов.\n\nВыберите файл .plugin или откройте его прямо в чате."_q,
			u"Add your first plugin for new commands, tools and little conveniences in your chats.\n\nChoose a .plugin file or open one directly in a chat."_q);
		state->emptyAction = Lang2(u"Установить плагин"_q, u"Install a plugin"_q);
		break;
	case EmptyPluginState::Disabled:
		state->emptyTitle = Lang2(u"Плагины на паузе"_q, u"Plugins are paused"_q);
		state->emptyText = Lang2(
			u"Включите движок, чтобы пользоваться плагинами и добавлять новые. Уже установленные плагины и их настройки сохранены."_q,
			u"Enable the engine to use plugins and add new ones. Your installed plugins and their settings are saved."_q);
		state->emptyAction = Lang2(u"Включить движок"_q, u"Enable plugin engine"_q);
		break;
	case EmptyPluginState::Unavailable:
		state->emptyTitle = Lang2(u"Подготовим плагины"_q, u"Set up plugins"_q);
		state->emptyText = Lang2(
			u"Для работы плагинов нужен Python из комплекта Whitegram. Откройте настройки движка, чтобы узнать, чего не хватает."_q,
			u"Plugins need the Python runtime included with Whitegram. Open engine settings to see what is missing."_q);
		state->emptyAction = Lang2(u"Настройки движка"_q, u"Engine settings"_q);
		break;
	case EmptyPluginState::Failed:
		state->emptyTitle = Lang2(u"Не удалось запустить"_q, u"Could not start plugins"_q);
		state->emptyText = Lang2(
			u"Движок плагинов не запустился. Подробности доступны в настройках движка."_q,
			u"The plugin engine could not start. Open engine settings for details."_q);
		state->emptyAction = Lang2(u"Настройки движка"_q, u"Engine settings"_q);
		break;
	}
}

void RefreshPluginList(
		not_null<Ui::VerticalLayout*> rows,
		not_null<PluginListState*> state,
		not_null<Window::SessionController*> controller,
		bool animated) {
	const auto animType = animated
		? anim::type::normal
		: anim::type::instant;

	auto infos = QVector<QJsonObject>();
	for (const auto &entry : Call(u"list"_q).toArray()) {
		const auto info = entry.toObject();
		if (!info.isEmpty()) {
			infos.push_back(info);
		}
	}

	const auto found = [&](const QString &id) {
		return std::find_if(infos.begin(), infos.end(),
			[&](const QJsonObject &info) {
				return info.value(u"id"_q).toString() == id;
			}) != infos.end();
	};

	// Animate out the cards of removed plugins.
	for (auto i = state->cards.size(); i != 0; --i) {
		const auto index = i - 1;
		const auto entry = state->cards[index];
		if (found(entry.id)) {
			continue;
		}
		entry.wrap->setDuration(kPluginSlideDuration);
		entry.wrap->toggle(false, animType);
		const auto guard = QPointer<Ui::RpWidget>(entry.wrap.get());
		base::call_delayed(animated ? (kPluginSlideDuration + 30) : 0,
			[guard] { delete guard.data(); });
		state->cards.erase(state->cards.begin() + index);
	}

	// Update existing cards, slide in the new ones.
	for (const auto &info : infos) {
		const auto id = info.value(u"id"_q).toString();
		const auto existing = std::find_if(
			state->cards.begin(),
			state->cards.end(),
			[&](const PluginCardState &card) { return card.id == id; });
		if (existing != state->cards.end()) {
			if (existing->last != info) {
				existing->body->clear();
				PopulatePluginCard(existing->body, controller, info);
				existing->last = info;
			}
			continue;
		}
		const auto wrap = rows->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				rows,
				object_ptr<Ui::VerticalLayout>(rows),
				style::margins(0, 0, 0, st::wgPluginCardInset)),
			style::margins(st::wgPluginCardInset, 0, st::wgPluginCardInset, 0))->setDuration(
					kPluginSlideDuration);
		wrap->toggle(false, anim::type::instant);
		const auto body = wrap->entity();
		body->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(body);
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver->c);
			p.drawRoundedRect(body->rect(), st::wgPluginCardRadius, st::wgPluginCardRadius);
		}, body->lifetime());
		PopulatePluginCard(body, controller, info);
		wrap->toggle(true, animType);
		state->cards.push_back({
			id,
			not_null(wrap),
			not_null(body),
			info,
		});
	}

	UpdateEmptyPluginState(state);
	state->empty->toggle(infos.isEmpty(), animType);
}

void ShowPluginSettingsBox(
		not_null<Window::SessionController*> controller,
		const QString &pluginId,
		const QString &name) {
	const auto items = Call(u"settings"_q, QJsonObject{
		{ u"id"_q, pluginId },
	}).toArray();
	if (items.isEmpty()) {
		controller->showToast(Lang2(
			u"У плагина нет настроек."_q,
			u"The plugin has no settings."_q));
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(name));
		for (const auto &value : items) {
			const auto item = value.toObject();
			const auto type = item.value(u"type"_q).toString();
			const auto index = item.value(u"index"_q).toInt();
			const auto label = item.value(u"label"_q).toString();
			const auto description = item.value(u"description"_q).toString();
			if (type == u"switch"_q) {
				const auto enabled = item.value(u"value"_q).toBool();
				const auto button = box->addRow(
					object_ptr<Ui::SettingsButton>(
						box,
						rpl::single(label),
						st::settingsButtonNoIcon));
				button->toggleOn(rpl::single(enabled));
				button->toggledChanges(
				) | rpl::on_next([=](bool toggled) {
					Call(u"setting_changed"_q, QJsonObject{
						{ u"id"_q, pluginId },
						{ u"index"_q, index },
						{ u"value"_q, toggled },
					});
				}, button->lifetime());
			} else if (type == u"button"_q) {
				const auto button = box->addRow(
					object_ptr<Ui::SettingsButton>(
						box,
						rpl::single(label),
						st::settingsButtonNoIcon));
				button->setClickedCallback([=] {
					Call(u"setting_click"_q, QJsonObject{
						{ u"id"_q, pluginId },
						{ u"index"_q, index },
					});
				});
			} else {
				box->addRow(object_ptr<Ui::FlatLabel>(
					box,
					rpl::single(
						(type == u"label"_q) ? label : description),
					st::boxDividerLabel));
			}
		}
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

QString EngineStatus() {
	return !Compiled() || !Available()
		? Lang2(u"Недоступен"_q, u"Unavailable"_q)
		: !RuntimeError().isEmpty()
		? Lang2(u"Ошибка запуска"_q, u"Startup error"_q)
		: !Started()
		? Lang2(u"Выключен"_q, u"Off"_q)
		: SafeMode()
		? Lang2(u"Безопасный режим"_q, u"Safe mode"_q)
		: Lang2(u"Работает"_q, u"Running"_q);
}

void ShowEngineSettings(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(Lang2(u"Движок плагинов"_q, u"Plugin engine"_q)));
		const auto addText = [&](QString text) {
			box->addRow(object_ptr<Ui::FlatLabel>(box, rpl::single(std::move(text)), st::boxDividerLabel));
		};
		addText(Lang2(
			u"Плагины расширяют возможности Whitegram. Здесь можно управлять их запуском и доступом."_q,
			u"Plugins add features to Whitegram. Manage how they run and which sources you allow."_q));
		if (!Compiled() || !Available() || !RuntimeError().isEmpty()) {
			addText(!Available()
				? Lang2(u"Для запуска нужна папка python из комплекта Whitegram рядом с приложением."_q,
					u"Place the python folder included with Whitegram next to the application."_q)
				: RuntimeError());
		}
		const auto addToggle = [&](QString title, bool value, Fn<void(bool)> changed) {
			const auto button = box->addRow(object_ptr<Ui::SettingsButton>(
				box, rpl::single(std::move(title)), st::settingsButtonNoIcon), style::margins());
			button->toggleOn(rpl::single(value));
			button->toggledChanges() | rpl::on_next(std::move(changed), button->lifetime());
		};
		addToggle(Lang2(u"Безопасный режим"_q, u"Safe mode"_q), SafeMode(), [](bool value) {
			SetSafeMode(value);
		});
		addText(Lang2(u"Временно отключает все плагины, сохраняя их настройки."_q,
			u"Temporarily disables all plugins and keeps their settings."_q));
		addToggle(Lang2(u"Неизвестные источники"_q, u"Unknown sources"_q), AllowUnknownSources(), [](bool value) {
			SetAllowUnknownSources(value);
		});
		addText(Lang2(u"Разрешает запуск плагинов, которым вы ещё не доверяли."_q,
			u"Allows plugins you have not trusted yet to run."_q));
		const auto folder = box->addRow(object_ptr<Ui::SettingsButton>(box,
			rpl::single(Lang2(u"Открыть папку плагинов"_q, u"Open plugins folder"_q)),
			st::settingsButtonNoIcon), style::margins());
		folder->setClickedCallback([] {
			QDesktopServices::openUrl(QUrl::fromLocalFile(PluginsDir()));
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void InstallFromFile(not_null<Window::SessionController*> controller) {
	const auto file = QFileDialog::getOpenFileName(
		nullptr,
		Lang2(u"Установить плагин"_q, u"Install plugin"_q),
		PluginsDir(),
		u"Plugins (*.plugin *.py)"_q);
	if (file.isEmpty()) {
		return;
	}
	auto input = QFile(file);
	if (!input.open(QIODevice::ReadOnly)) {
		controller->showToast(Lang2(u"Не удалось открыть файл."_q, u"Could not open the file."_q));
		return;
	}
	auto metadata = Ui::ParsePluginMetadata(input.read(4 * 1024 * 1024));
	if (metadata.id.isEmpty()) {
		controller->showToast(Lang2(u"В файле нет описания плагина."_q, u"Plugin metadata is missing."_q));
		return;
	}
	Ui::ShowPluginInfoBox(controller, file, std::move(metadata));
}

[[nodiscard]] QJsonObject MessageContext(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const auto from = item->from();
	return QJsonObject{
		{ u"dialog_id"_q, double(DialogId(peer)) },
		{ u"message_id"_q, double(item->id.bare) },
		{ u"sender_id"_q, double(from ? DialogId(from) : 0) },
		{ u"text"_q, item->originalText().text },
		{ u"out"_q, item->out() },
	};
}

} // namespace

void AddMessageMenuItems(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		not_null<Window::SessionController*> controller) {
	if (!Started()) {
		return;
	}
	const auto items = MenuItems(u"MESSAGE_CONTEXT_MENU"_q);
	if (items.empty()) {
		return;
	}
	const auto context = MessageContext(item);
	menu->addSeparator();
	for (const auto &entry : items) {
		menu->addAction(entry.text, [=] {
			ClickMenuItem(entry.id, context);
		});
	}
}

void InstallPluginFromPath(
		not_null<Window::SessionController*> controller,
		const QString &path) {
	if (!Started()) {
		controller->showToast(Lang2(
			u"Движок плагинов не запущен (проверьте настройки)."_q,
			u"Plugin engine is not running (check settings)."_q));
		return;
	}
	const auto info = Call(u"install"_q, QJsonObject{
		{ u"path"_q, path },
		{ u"trust"_q, true },
	}).toObject();
	if (info.isEmpty()) {
		controller->showToast(Lang2(
			u"Не удалось установить плагин (см. лог)."_q,
			u"Plugin installation failed (see log)."_q));
	} else {
		controller->showToast(Lang2(
			u"Плагин установлен: "_q,
			u"Plugin installed: "_q)
			+ info.value(u"name"_q).toString());
	}
	notifyListChanged();
}

void BuildPluginsPage(Settings::Builder::SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();

	const auto engineToggle = builder.addButton({
		.id = u"whitegram/plugins/enabled"_q,
		.title = Settings::trWhitegram(
			QString::fromUtf8("Включить движок плагинов"),
			QString("Enable plugin engine")),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(rpl::empty)
			| rpl::then(listChanges())
			| rpl::map([] { return EngineEnabled(); }),
		.keywords = { u"python"_q, u"плагины"_q, u"plugins"_q },
	});
	if (engineToggle) {
		engineToggle->toggledChanges(
		) | rpl::filter([](bool enabled) {
			return enabled != EngineEnabled();
		}) | rpl::on_next(SetEngineRunning, engineToggle->lifetime());
	}
	builder.addButton({
		.id = u"whitegram/plugins/engine"_q,
		.title = Settings::trWhitegram(u"Движок плагинов"_q, u"Plugin engine"_q),
		.icon = { &st::menuIconSettings },
		.label = rpl::single(rpl::empty) | rpl::then(listChanges()) | rpl::map([] { return EngineStatus(); }),
		.onClick = [=] { ShowEngineSettings(not_null(controller)); },
		.keywords = { u"python"_q, u"safe"_q, u"trust"_q, u"folder"_q },
	});

	builder.addButton({
		.id = u"whitegram/plugins/install"_q,
		.title = Settings::trWhitegram(
			QString::fromUtf8("Установить из файла"),
			QString("Install from file")),
		.icon = { &st::menuIconDownload },
		.onClick = [=] {
			InstallFromFile(not_null(controller));
		},
		.keywords = { u"python"_q, u"plugins"_q, u"install"_q },
	});
	builder.addSkip();

	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(Settings::trWhitegram(
		QString::fromUtf8("Установленные плагины"),
		QString("Installed plugins")));

	const auto rows = builder.add(
	[&](const Settings::Builder::WidgetContext &ctx) {
		return Settings::Builder::SectionBuilder::WidgetToAdd{
			.widget = object_ptr<Ui::VerticalLayout>(ctx.container),
		};
	});
	if (rows) {
		const auto layout = static_cast<Ui::VerticalLayout*>(rows);
		const auto state = layout->lifetime().make_state<PluginListState>();
		UpdateEmptyPluginState(not_null(state));
		state->empty = layout->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				layout,
				object_ptr<Ui::VerticalLayout>(layout)),
			style::margins(st::wgPluginCardInset, 0, st::wgPluginCardInset, st::wgPluginCardInset))
			->setDuration(kPluginSlideDuration);
		const auto panel = state->empty->entity();
		panel->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(panel);
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver->c);
			p.drawRoundedRect(panel->rect(), st::wgPluginCardRadius, st::wgPluginCardRadius);
		}, panel->lifetime());
		Ui::AddSkip(panel, 24);
		const auto illustration = panel->add(object_ptr<Ui::FixedHeightWidget>(panel, 80));
		illustration->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(illustration);
			PainterHighQualityEnabler hq(p);
			p.translate(illustration->width() / 2. - 40., 0.);
			p.setPen(Qt::NoPen);
			auto tint = st::lightButtonFg->c;
			tint.setAlphaF(.12);
			p.setBrush(tint);
			p.drawEllipse(QRectF(0, 0, 80, 80));
			p.setBrush(st::lightButtonFg->c);
			p.drawRoundedRect(QRectF(19, 23, 18, 18), 5, 5);
			p.drawRoundedRect(QRectF(45, 17, 18, 18), 5, 5);
			p.drawRoundedRect(QRectF(19, 47, 18, 18), 5, 5);
			p.drawRoundedRect(QRectF(43, 47, 18, 18), 5, 5);
		}, illustration->lifetime());
		const auto padding = st::wgPluginCardPadding;
		panel->add(object_ptr<Ui::FlatLabel>(panel,
			state->emptyTitle.value(), st::wgPluginInstallTitle),
			style::margins(padding, 16, padding, 0), style::al_top);
		panel->add(object_ptr<Ui::FlatLabel>(panel,
			state->emptyText.value(), st::wgPluginInstallMeta),
			style::margins(padding, 12, padding, 0), style::al_top);
		const auto action = panel->add(object_ptr<Ui::RoundButton>(panel,
			state->emptyAction.value(), st::wgPluginInstallButton),
			style::margins(padding, 24, padding, 0));
		action->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		panel->widthValue() | rpl::on_next([=](int width) {
			action->setFullWidth(width - 2 * padding);
		}, action->lifetime());
		action->setClickedCallback([=] {
			switch (CurrentEmptyPluginState()) {
			case EmptyPluginState::Ready:
				InstallFromFile(not_null(controller));
				break;
			case EmptyPluginState::Disabled:
				SetEngineRunning(true);
				break;
			case EmptyPluginState::Unavailable:
			case EmptyPluginState::Failed:
				ShowEngineSettings(not_null(controller));
				break;
			}
		});
		Ui::AddSkip(panel, 24);
		state->empty->toggle(true, anim::type::instant);
		RefreshPluginList(
			not_null(layout),
			not_null(state),
			not_null(controller),
			false);
		listChanges(
		) | rpl::on_next([=] {
			RefreshPluginList(
				not_null(layout),
				not_null(state),
				not_null(controller),
				true);
		}, layout->lifetime());
	}

	builder.addSkip();
}

} // namespace Whitegram::Plugins
