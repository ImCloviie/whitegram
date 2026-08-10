// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/boxes/account_notification_box.h"

#include "ayu/ayu_settings.h"
#include "core/application.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"

void FillAccountNotificationBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	not_null<Main::Session*> session) {
	box->setWidth(int(st::aboutWidth * 1.15));

	const auto user = session->user();
	const auto name = user ? user->name() : QString::number(session->userId().bare);

	box->setTitle(rpl::single(tr::lng_settings_section_notify(tr::now) + u": "_q + name));

	auto &accountNotif = AyuSettings::accountNotifications(session);

	const auto muteToggle = box->addRow(object_ptr<Ui::SettingsButton>(
		box,
		rpl::single(QString::fromUtf8("Полностью глушить аккаунт")),
		st::defaultSettingsButton
	))->toggleOn(rpl::single(accountNotif.completelyMute()));
	muteToggle->toggledChanges(
	) | rpl::filter([&accountNotif](bool checked) {
		return (checked != accountNotif.completelyMute());
	}) | rpl::on_next([&accountNotif, session](bool checked) {
		accountNotif.setCompletelyMute(checked);
		if (checked) {
			Core::App().notifications().clearFromSession(session);
		}
	}, muteToggle->lifetime());

	const auto enabledToggle = box->addRow(object_ptr<Ui::SettingsButton>(
		box,
		rpl::single(tr::lng_settings_desktop_notify(tr::now)),
		st::defaultSettingsButton
	))->toggleOn(rpl::single(accountNotif.enabled()));
	enabledToggle->toggledChanges(
	) | rpl::filter([&accountNotif](bool checked) {
		return (checked != accountNotif.enabled());
	}) | rpl::on_next([&accountNotif, session](bool checked) {
		accountNotif.setEnabled(checked);
		if (!checked) {
			Core::App().notifications().clearFromSession(session);
		}
	}, enabledToggle->lifetime());

	const auto soundToggle = box->addRow(object_ptr<Ui::SettingsButton>(
		box,
		rpl::single(tr::lng_settings_sound_allowed(tr::now)),
		st::defaultSettingsButton
	))->toggleOn(rpl::single(accountNotif.soundEnabled()));
	soundToggle->toggledChanges(
	) | rpl::filter([&accountNotif](bool checked) {
		return (checked != accountNotif.soundEnabled());
	}) | rpl::on_next([&accountNotif](bool checked) {
		accountNotif.setSoundEnabled(checked);
	}, soundToggle->lifetime());

	const auto includeUnreadToggle = box->addRow(object_ptr<Ui::SettingsButton>(
		box,
		rpl::single(QString::fromUtf8("Учитывать в общем счётчике")),
		st::defaultSettingsButton
	))->toggleOn(rpl::single(accountNotif.includeInUnreadCounter()));
	includeUnreadToggle->toggledChanges(
	) | rpl::filter([&accountNotif](bool checked) {
		return (checked != accountNotif.includeInUnreadCounter());
	}) | rpl::on_next([&accountNotif](bool checked) {
		accountNotif.setIncludeInUnreadCounter(checked);
	}, includeUnreadToggle->lifetime());

	const auto badgeToggle = box->addRow(object_ptr<Ui::SettingsButton>(
		box,
		rpl::single(QString::fromUtf8("Показывать бейдж на иконке")),
		st::defaultSettingsButton
	))->toggleOn(rpl::single(accountNotif.showUnreadBadge()));
	badgeToggle->toggledChanges(
	) | rpl::filter([&accountNotif](bool checked) {
		return (checked != accountNotif.showUnreadBadge());
	}) | rpl::on_next([&accountNotif](bool checked) {
		accountNotif.setShowUnreadBadge(checked);
	}, badgeToggle->lifetime());

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}
