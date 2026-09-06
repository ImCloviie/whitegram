#include "ayu/ui/settings/custom_archives_box.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/custom_archives/custom_archives.h"
#include "ayu/ui/ayu_userpic.h"
#include "base/unique_qptr.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "inline_bots/bot_attach_web_view.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "ui/abstract_button.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

#include <algorithm>
#include <rpl/skip.h>
#include <QtCore/QRegularExpression>

namespace {

struct IconEntry {
	QString path;
	QString label;
};

const auto kArchiveIcons = std::vector<IconEntry>{
	{ QString(), QString::fromUtf8("—") },
	{ u"folders/folders_home"_q, u"🏠"_q },
	{ u"folders/folders_work"_q, u"💼"_q },
	{ u"folders/folders_game"_q, u"🎮"_q },
	{ u"folders/folders_crown"_q, u"👑"_q },
	{ u"folders/folders_favorite"_q, u"⭐"_q },
	{ u"folders/folders_love"_q, u"❤️"_q },
	{ u"folders/folders_cat"_q, u"🐱"_q },
	{ u"folders/folders_book"_q, u"📖"_q },
	{ u"folders/folders_note"_q, u"🎵"_q },
	{ u"folders/folders_palette"_q, u"🎨"_q },
	{ u"folders/folders_sport"_q, u"⚽"_q },
	{ u"folders/folders_travel"_q, u"✈️"_q },
	{ u"folders/folders_light"_q, u"💡"_q },
	{ u"folders/folders_money"_q, u"💰"_q },
	{ u"folders/folders_study"_q, u"🎓"_q },
	{ u"folders/folders_flower"_q, u"🌸"_q },
	{ u"folders/folders_party"_q, u"🎉"_q },
	{ u"folders/folders_mask"_q, u"🎭"_q },
	{ u"folders/folders_like"_q, u"👍"_q },
	{ u"folders/folders_private"_q, u"🔒"_q },
	{ u"folders/folders_trade"_q, u"📊"_q },
	{ u"folders/folders_airplane"_q, u"🛫"_q },
	{ u"folders/folders_custom"_q, u"📁"_q },
};

[[nodiscard]] QString IconLabel(const QString &path) {
	for (const auto &entry : kArchiveIcons) {
		if (entry.path == path) {
			return entry.label;
		}
	}
	return QString::fromUtf8("—");
}

[[nodiscard]] QString ChatsCountText(int count) {
	const auto n = count % 100;
	const auto n1 = n % 10;
	if (n > 10 && n < 20) {
		return QString::fromUtf8("%1 чатов").arg(count);
	} else if (n1 == 1) {
		return QString::fromUtf8("%1 чат").arg(count);
	} else if (n1 >= 2 && n1 <= 4) {
		return QString::fromUtf8("%1 чата").arg(count);
	}
	return QString::fromUtf8("%1 чатов").arg(count);
}

[[nodiscard]] QString FolderSubtitle(const CustomArchive &archive) {
	auto details = QStringList();
	details.push_back(ChatsCountText(int(archive.peerIds.size())));
	if (archive.hasPin() && archive.hasPassword()) {
		details.push_back(QString::fromUtf8("PIN + пароль"));
	} else if (archive.hasPin()) {
		details.push_back(u"PIN"_q);
	} else if (archive.hasPassword()) {
		details.push_back(QString::fromUtf8("пароль"));
	}
	if (archive.hideFromMainList) {
		details.push_back(QString::fromUtf8("скрыты из списка"));
	}
	return details.join(u" · "_q);
}

[[nodiscard]] QString ArchiveTitle(const CustomArchive &archive) {
	auto title = archive.name;
	if (!archive.icon.isEmpty()) {
		title = IconLabel(archive.icon) + u" "_q + title;
	}
	if (archive.isLocked()) {
		title = u"🔒 "_q + title;
	}
	return title;
}

[[nodiscard]] bool IsPinned(const CustomArchive &archive, uint64 peerId) {
	return std::find(
		archive.pinnedPeerIds.begin(),
		archive.pinnedPeerIds.end(),
		peerId) != archive.pinnedPeerIds.end();
}

[[nodiscard]] bool IsValidPin(const QString &pin) {
	static const auto kPin = QRegularExpression(u"^\\d{4,12}$"_q);
	return kPin.match(pin).hasMatch();
}

[[nodiscard]] bool IsValidPassword(const QString &password) {
	return password.size() >= 4 && password.size() <= 128;
}

[[nodiscard]] QString UnlockHint(const CustomArchive &archive) {
	if (archive.hasPin() && archive.hasPassword()) {
		return QString::fromUtf8("Введите PIN-код или пароль папки");
	} else if (archive.hasPin()) {
		return QString::fromUtf8("Введите PIN-код папки");
	}
	return QString::fromUtf8("Введите пароль папки");
}

not_null<Ui::PasswordInput*> AddPasswordRow(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> placeholder) {
	const auto &st = st::defaultInputField;
	auto container = object_ptr<Ui::RpWidget>(box);
	const auto raw = container.data();
	raw->resize(raw->width(), st.heightMin);
	const auto field = Ui::CreateChild<Ui::PasswordInput>(
		raw,
		st,
		std::move(placeholder),
		QString());
	raw->widthValue(
	) | rpl::on_next([=](int width) {
		field->resize(
			std::max(width, st.widthMin),
			field->height());
		field->moveToLeft(0, 0);
		raw->resize(width, field->height());
	}, raw->lifetime());
	box->addRow(std::move(container));
	return field;
}

void EnsureUnlockedThen(
		not_null<Ui::GenericBox*> parent,
		not_null<Window::SessionController*> controller,
		int archiveId,
		Fn<void()> then) {
	if (AyuFeatures::CustomArchives::IsArchiveUnlocked(archiveId)) {
		then();
		return;
	}
	parent->getDelegate()->show(Box(
		CustomArchiveUnlockBox,
		controller,
		archiveId,
		[=] {
			AyuFeatures::CustomArchives::UnlockArchive(archiveId);
			then();
		}));
}

} // namespace

void SetArchivePinBox(
		not_null<Ui::GenericBox*> box,
		int archiveId) {
	const auto existing = AyuSettings::getInstance().findCustomArchive(archiveId);
	const auto requireOld = existing && existing->hasPin();
	box->setTitle(rpl::single(requireOld
		? QString::fromUtf8("Изменить PIN-код")
		: QString::fromUtf8("PIN-код папки")));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(QString::fromUtf8("Только цифры, от 4 до 12 символов.")),
		st::boxLabel));

	Ui::PasswordInput *oldField = nullptr;
	if (requireOld) {
		oldField = AddPasswordRow(
			box,
			rpl::single(QString::fromUtf8("Текущий PIN"))).get();
	}
	const auto pinField = AddPasswordRow(
		box,
		rpl::single(QString::fromUtf8("Новый PIN")));
	const auto confirmField = AddPasswordRow(
		box,
		rpl::single(QString::fromUtf8("Повторите PIN")));
	const auto errorLabel = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(QString()),
		st::boxLabel));
	errorLabel->setTextColorOverride(st::boxTextFgError->c);
	errorLabel->hide();

	const auto submit = [=] {
		if (requireOld && oldField) {
			if (!AyuFeatures::CustomArchives::CheckArchivePin(
					archiveId,
					oldField->getLastText())) {
				errorLabel->setText(QString::fromUtf8(
					"Неверный текущий PIN-код."));
				errorLabel->show();
				oldField->showError();
				return;
			}
		}
		const auto entered = pinField->getLastText();
		const auto repeated = confirmField->getLastText();
		if (!IsValidPin(entered)) {
			errorLabel->setText(QString::fromUtf8(
				"PIN должен содержать от 4 до 12 цифр."));
			errorLabel->show();
			pinField->showError();
			return;
		}
		if (entered != repeated) {
			errorLabel->setText(QString::fromUtf8("PIN-коды не совпадают."));
			errorLabel->show();
			confirmField->showError();
			return;
		}
		AyuFeatures::CustomArchives::SetArchivePin(archiveId, entered);
		box->closeBox();
	};

	if (oldField) {
		QObject::connect(oldField, &Ui::MaskedInputField::submitted, submit);
	}
	QObject::connect(pinField, &Ui::MaskedInputField::submitted, submit);
	QObject::connect(confirmField, &Ui::MaskedInputField::submitted, submit);
	box->addButton(tr::lng_settings_save(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	(oldField ? oldField : pinField.get())->setFocus();
}

void SetArchivePasswordBox(
		not_null<Ui::GenericBox*> box,
		int archiveId) {
	const auto existing = AyuSettings::getInstance().findCustomArchive(archiveId);
	const auto requireOld = existing && existing->hasPassword();
	box->setTitle(rpl::single(requireOld
		? QString::fromUtf8("Изменить пароль")
		: QString::fromUtf8("Пароль папки")));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(QString::fromUtf8("Не меньше 4 символов.")),
		st::boxLabel));

	Ui::PasswordInput *oldField = nullptr;
	if (requireOld) {
		oldField = AddPasswordRow(
			box,
			rpl::single(QString::fromUtf8("Текущий пароль"))).get();
	}
	const auto passwordField = AddPasswordRow(
		box,
		rpl::single(QString::fromUtf8("Новый пароль")));
	const auto confirmField = AddPasswordRow(
		box,
		rpl::single(QString::fromUtf8("Повторите пароль")));
	const auto errorLabel = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(QString()),
		st::boxLabel));
	errorLabel->setTextColorOverride(st::boxTextFgError->c);
	errorLabel->hide();

	const auto submit = [=] {
		if (requireOld && oldField) {
			if (!AyuFeatures::CustomArchives::CheckArchivePassword(
					archiveId,
					oldField->getLastText())) {
				errorLabel->setText(QString::fromUtf8(
					"Неверный текущий пароль."));
				errorLabel->show();
				oldField->showError();
				return;
			}
		}
		const auto entered = passwordField->getLastText();
		const auto repeated = confirmField->getLastText();
		if (!IsValidPassword(entered)) {
			errorLabel->setText(QString::fromUtf8(
				"Пароль должен быть от 4 до 128 символов."));
			errorLabel->show();
			passwordField->showError();
			return;
		}
		if (entered != repeated) {
			errorLabel->setText(QString::fromUtf8("Пароли не совпадают."));
			errorLabel->show();
			confirmField->showError();
			return;
		}
		AyuFeatures::CustomArchives::SetArchivePassword(archiveId, entered);
		box->closeBox();
	};

	if (oldField) {
		QObject::connect(oldField, &Ui::MaskedInputField::submitted, submit);
	}
	QObject::connect(passwordField, &Ui::MaskedInputField::submitted, submit);
	QObject::connect(confirmField, &Ui::MaskedInputField::submitted, submit);
	box->addButton(tr::lng_settings_save(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	(oldField ? oldField : passwordField.get())->setFocus();
}

namespace {

void ChooseArchiveIconBox(
		not_null<Ui::GenericBox*> box,
		const QString &currentIcon,
		Fn<void(QString)> done) {
	box->setTitle(rpl::single(QString::fromUtf8("Выберите иконку")));

	constexpr auto kIconSize = 40;
	constexpr auto kIconSpacing = 6;
	constexpr auto kColumns = 6;
	constexpr auto kIconPad = 8;

	const auto rows = (int(kArchiveIcons.size()) + kColumns - 1) / kColumns;
	const auto totalHeight = rows * (kIconSize + kIconSpacing)
		- kIconSpacing
		+ kIconPad * 2;

	const auto grid = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, totalHeight),
		QMargins(kIconPad, kIconPad, kIconPad, 0));

	for (auto i = 0; i < int(kArchiveIcons.size()); ++i) {
		const auto &entry = kArchiveIcons[i];
		const auto col = i % kColumns;
		const auto row = i / kColumns;
		const auto isActive = (entry.path == currentIcon);
		const auto iconPath = entry.path;

		const auto btn = Ui::CreateChild<Ui::AbstractButton>(grid);
		btn->resize(kIconSize, kIconSize);
		btn->move(
			col * (kIconSize + kIconSpacing),
			row * (kIconSize + kIconSpacing));
		btn->show();

		btn->setClickedCallback([=] {
			done(iconPath);
			box->closeBox();
		});

		btn->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(btn);
			p.setRenderHint(QPainter::Antialiasing);
			const auto r = btn->rect();
			if (isActive) {
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBgActive->b);
				p.drawRoundedRect(r, 10, 10);
			} else if (btn->isOver()) {
				auto bg = st::windowBgOver->c;
				bg.setAlpha(120);
				p.setPen(Qt::NoPen);
				p.setBrush(bg);
				p.drawRoundedRect(r, 10, 10);
			}
			p.setPen(isActive ? st::windowFgActive->p : st::windowFg->p);
			p.setFont(st::semiboldFont);
			p.drawText(r, IconLabel(iconPath), QTextOption(Qt::AlignCenter));
		}, btn->lifetime());
	}

	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

[[nodiscard]] not_null<Ui::SettingsButton*> AddFolderToggle(
		not_null<Ui::VerticalLayout*> container,
		const QString &title,
		const QString &description,
		bool checked,
		const style::icon *icon) {
	const auto button = Settings::AddButtonWithIcon(
		container,
		rpl::single(title),
		st::settingsButton,
		{ icon });
	button->toggleOn(rpl::single(checked));
	button->setAttribute(Qt::WA_AlwaysShowToolTips);
	button->setToolTip(description);
	return button;
}

void RebuildArchivesList(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<Ui::GenericBox*> box) {
	while (container->count() > 0) {
		delete container->widgetAt(0);
	}

	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(QString::fromUtf8(
			"Папки живут только в Whitegram и не трогают "
			"обычный архив Telegram.")));
	Ui::AddSkip(container);

	Settings::AddButtonWithIcon(
		container,
		rpl::single(QString::fromUtf8("Создать папку")),
		st::settingsButton,
		{ &st::menuIconAdd }
	)->setClickedCallback([=] {
		box->getDelegate()->show(Box(EditCustomArchiveBox, controller, -1));
	});

	auto archives = AyuSettings::getInstance().customArchives();
	std::sort(archives.begin(), archives.end(), [](
			const CustomArchive &a,
			const CustomArchive &b) {
		if (a.order != b.order) {
			return a.order < b.order;
		}
		return a.id < b.id;
	});

	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	if (archives.empty()) {
		Ui::AddSkip(container);
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				rpl::single(QString::fromUtf8(
					"Пока нет ни одной папки.\n"
					"Создайте первую — она появится в списке чатов.")),
				st::boxDividerLabel),
			st::boxRowPadding);
		Ui::AddSkip(container);
	} else {
		Ui::AddSubsectionTitle(
			container,
			rpl::single(QString::fromUtf8("Ваши папки")));
	}

	const auto &rowSt = st::defaultDialogRow;
	const auto photo = rowSt.photoSize;
	const auto rowHeight = rowSt.height;
	for (const auto &archive : archives) {
		const auto id = archive.id;
		const auto name = archive.name;
		const auto icon = archive.icon;
		const auto locked = archive.isLocked();
		const auto subtitle = FolderSubtitle(archive);
		const auto row = container->add(
			object_ptr<Ui::AbstractButton>(container));
		row->setPointerCursor(true);
		row->resize(row->width(), rowHeight);
		row->widthValue(
		) | rpl::on_next([=](int width) {
			row->resize(width, rowHeight);
		}, row->lifetime());
		row->setClickedCallback([=] {
			box->getDelegate()->show(
				Box(EditCustomArchiveBox, controller, id));
		});
		row->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(row);
			if (row->isOver() || row->isDown()) {
				p.fillRect(row->rect(), st::windowBgOver);
			}
			const auto photoTop = rowSt.padding.top();
			const auto photoLeft = rowSt.padding.left();
			{
				PainterHighQualityEnabler hq(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::historyPeerArchiveUserpicBg);
				AyuUserpic::PaintShape(p, photoLeft, photoTop, photo);
			}
			p.setPen(st::historyPeerUserpicFg);
			p.setFont(st::semiboldFont);
			p.drawText(
				QRect(photoLeft, photoTop, photo, photo),
				locked
					? u"🔐"_q
					: AyuFeatures::CustomArchives::FolderIconEmoji(icon),
				QTextOption(style::al_center));
			const auto textLeft = rowSt.nameLeft;
			const auto textWidth = std::max(
				row->width() - textLeft - rowSt.padding.right(),
				0);
			const auto nameFont = st::semiboldFont;
			const auto statusFont = st::normalFont;
			p.setPen(st::dialogsNameFg);
			p.setFont(nameFont);
			p.drawText(
				textLeft,
				rowSt.nameTop + nameFont->ascent,
				nameFont->elided(name, textWidth));
			p.setPen(st::windowSubTextFg);
			p.setFont(statusFont);
			p.drawText(
				textLeft,
				rowSt.textTop + statusFont->ascent,
				statusFont->elided(subtitle, textWidth));
			p.fillRect(
				0,
				row->height() - st::lineWidth,
				row->width(),
				st::lineWidth,
				st::shadowFg);
		}, row->lifetime());
	}
	container->resizeToWidth(container->width());
}

} // namespace

void EditCustomArchiveBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		int archiveId) {
	auto &settings = AyuSettings::getInstance();
	const auto existing = settings.findCustomArchive(archiveId);
	const auto isNew = (archiveId <= 0) || !existing;

	auto draft = isNew ? CustomArchive() : *existing;
	if (isNew) {
		draft.muted = true;
		draft.hideFromMainList = true;
		draft.showInChatList = true;
		draft.showInMainMenu = false;
		if (draft.icon.isEmpty()) {
			draft.icon = u"folders/folders_custom"_q;
		}
	}

	const auto selectedIcon = std::make_shared<QString>(draft.icon);

	box->setTitle(rpl::single(isNew
		? QString::fromUtf8("Новая папка")
		: QString::fromUtf8("Настройка папки")));
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(isNew
			? QString::fromUtf8(
				"Папка появится в списке чатов. Обычный архив Telegram "
				"не затрагивается.")
			: QString::fromUtf8(
				"Имя, поведение, защита и содержимое папки.")));

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(QString::fromUtf8("Название")));

	const auto header = container->add(
		object_ptr<Ui::RpWidget>(container),
		st::boxRowPadding);
	const auto &rowSt = st::defaultDialogRow;
	const auto photo = rowSt.photoSize;
	const auto nameField = Ui::CreateChild<Ui::InputField>(
		header,
		st::defaultInputField,
		rpl::single(QString::fromUtf8("Например: Работа")),
		draft.name);
	const auto iconButton = Ui::CreateChild<Ui::AbstractButton>(header);
	iconButton->resize(photo, photo);
	iconButton->show();
	iconButton->setPointerCursor(true);
	iconButton->setAttribute(Qt::WA_AlwaysShowToolTips);
	iconButton->setToolTip(QString::fromUtf8(
		"Нажмите, чтобы выбрать иконку папки"));
	const auto applyExisting = std::make_shared<Fn<void()>>();
	const auto live = !isNew
		&& !AyuSettings::getInstance().confirmFolderChanges();
	iconButton->setClickedCallback([=] {
		box->getDelegate()->show(Box(
			ChooseArchiveIconBox,
			*selectedIcon,
			[=](QString path) {
				*selectedIcon = path;
				iconButton->update();
				if (live && *applyExisting) {
					(*applyExisting)();
				}
			}));
	});
	nameField->setFocus();
	header->widthValue(
	) | rpl::on_next([=](int width) {
		const auto left = photo + rowSt.padding.left();
		nameField->resize(
			std::max(width - left, st::defaultInputField.widthMin),
			nameField->height());
		header->resize(width, std::max(nameField->height(), photo));
		const auto top = (header->height() - nameField->height()) / 2;
		nameField->moveToLeft(left, top);
		iconButton->moveToLeft(0, (header->height() - photo) / 2);
	}, header->lifetime());
	iconButton->paintRequest(
	) | rpl::on_next([=] {
		auto p = Painter(iconButton);
		const auto current = AyuSettings::getInstance().findCustomArchive(
			archiveId);
		const auto locked = current
			&& current->isLocked()
			&& !AyuFeatures::CustomArchives::IsArchiveUnlocked(archiveId);
		const auto emoji = locked
			? u"🔐"_q
			: AyuFeatures::CustomArchives::FolderIconEmoji(*selectedIcon);
		{
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::historyPeerArchiveUserpicBg);
			AyuUserpic::PaintShape(p, 0, 0, photo);
		}
		p.setPen(st::historyPeerUserpicFg);
		p.setFont(st::semiboldFont);
		p.drawText(
			iconButton->rect(),
			emoji,
			QTextOption(style::al_center));
	}, iconButton->lifetime());
	AyuSettings::getInstance().customArchivesChanges(
	) | rpl::on_next([=] {
		iconButton->update();
	}, iconButton->lifetime());

	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(QString::fromUtf8(
				"Нажмите на значок, чтобы выбрать иконку")),
			st::boxDividerLabel),
		st::boxRowPadding);

	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(QString::fromUtf8("Поведение")));

	const auto muted = AddFolderToggle(
		container,
		QString::fromUtf8("Заглушать чаты при добавлении"),
		QString::fromUtf8(
			"Новые чаты в этой папке сразу получают вечное уведомление "
			"«без звука». Уже добавленные чаты не меняются."),
		draft.muted,
		&st::menuIconMute);
	const auto hideFromMainList = AddFolderToggle(
		container,
		QString::fromUtf8("Скрывать чаты из основного списка"),
		QString::fromUtf8(
			"Чаты из папки пропадают из обычного списка слева и остаются "
			"только внутри папки. Если выключить, они снова появятся "
			"в основном списке."),
		draft.hideFromMainList,
		&st::menuIconArchive);
	const auto showInChatList = AddFolderToggle(
		container,
		QString::fromUtf8("Показывать папку в списке чатов"),
		QString::fromUtf8(
			"Строка папки вверху основного списка чатов. Если выключить, "
			"папку можно открыть только из настроек."),
		draft.showInChatList,
		&st::menuIconShowInFolder);
	const auto collapsed = AddFolderToggle(
		container,
		QString::fromUtf8("Свёрнутый вид в списке чатов"),
		QString::fromUtf8(
			"Узкая полоска вместо полной строки: имя и число непрочитанных, "
			"как у свёрнутого архива Telegram."),
		draft.collapsed,
		&st::menuIconCollapse);

	Ui::PasswordInput *newPinField = nullptr;
	Ui::PasswordInput *newPasswordField = nullptr;
	if (isNew) {
		Ui::AddSkip(container);
		Ui::AddDividerText(
			container,
			rpl::single(QString::fromUtf8(
				"PIN (4–12 цифр) и/или пароль. При открытии хватит любого.")));
		Ui::AddSkip(container);
		Ui::AddSubsectionTitle(
			container,
			rpl::single(QString::fromUtf8("Защита")));
		newPinField = AddPasswordRow(
			box,
			rpl::single(QString::fromUtf8("PIN-код"))).get();
		newPasswordField = AddPasswordRow(
			box,
			rpl::single(QString::fromUtf8("Пароль"))).get();
	}

	*applyExisting = [=] {
		if (isNew) {
			return;
		}
		auto name = nameField->getLastText().trimmed();
		if (name.isEmpty()) {
			return;
		}
		auto &settings = AyuSettings::getInstance();
		auto updated = draft;
		updated.name = name;
		updated.muted = muted->toggled();
		updated.hideFromMainList = hideFromMainList->toggled();
		updated.showInChatList = showInChatList->toggled();
		updated.showInMainMenu = false;
		updated.collapsed = collapsed->toggled();
		updated.icon = *selectedIcon;
		if (const auto current = settings.findCustomArchive(archiveId)) {
			updated.peerIds = current->peerIds;
			updated.pinnedPeerIds = current->pinnedPeerIds;
			updated.order = current->order;
			updated.pinSalt = current->pinSalt;
			updated.pinHash = current->pinHash;
			updated.passwordSalt = current->passwordSalt;
			updated.passwordHash = current->passwordHash;
		}
		settings.updateCustomArchive(updated);
		AyuFeatures::CustomArchives::RefreshAllFolderPeers(
			&controller->session());
	};

	if (live) {
		muted->toggledValue(
		) | rpl::skip(1) | rpl::on_next([=](bool) {
			(*applyExisting)();
		}, box->lifetime());
		hideFromMainList->toggledValue(
		) | rpl::skip(1) | rpl::on_next([=](bool) {
			(*applyExisting)();
		}, box->lifetime());
		showInChatList->toggledValue(
		) | rpl::skip(1) | rpl::on_next([=](bool) {
			(*applyExisting)();
		}, box->lifetime());
		collapsed->toggledValue(
		) | rpl::skip(1) | rpl::on_next([=](bool) {
			(*applyExisting)();
		}, box->lifetime());
		nameField->changes(
		) | rpl::on_next([=] {
			(*applyExisting)();
		}, box->lifetime());
	}

	if (!live) {
		box->addButton(tr::lng_settings_save(), [=] {
			auto &settings = AyuSettings::getInstance();
			auto name = nameField->getLastText().trimmed();
			if (name.isEmpty()) {
				nameField->showError();
				return;
			}
			if (isNew) {
				const auto pinText = newPinField
					? newPinField->getLastText()
					: QString();
				const auto passwordText = newPasswordField
					? newPasswordField->getLastText()
					: QString();
				if (!pinText.isEmpty() && !IsValidPin(pinText)) {
					newPinField->showError();
					return;
				}
				if (!passwordText.isEmpty()
					&& !IsValidPassword(passwordText)) {
					newPasswordField->showError();
					return;
				}
				settings.addCustomArchive(name);
				const auto &archives = settings.customArchives();
				if (!archives.empty()) {
					auto created = archives.back();
					created.muted = muted->toggled();
					created.hideFromMainList = hideFromMainList->toggled();
					created.showInChatList = showInChatList->toggled();
					created.showInMainMenu = false;
					created.collapsed = collapsed->toggled();
					created.icon = *selectedIcon;
					settings.updateCustomArchive(created);
					if (!pinText.isEmpty()) {
						AyuFeatures::CustomArchives::SetArchivePin(
							created.id,
							pinText);
						AyuFeatures::CustomArchives::LockArchive(created.id);
					}
					if (!passwordText.isEmpty()) {
						AyuFeatures::CustomArchives::SetArchivePassword(
							created.id,
							passwordText);
						AyuFeatures::CustomArchives::LockArchive(created.id);
					}
				}
			} else {
				(*applyExisting)();
			}
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	} else {
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}

	if (!isNew) {
		box->boxClosing(
		) | rpl::on_next([=] {
			if (controller->openedCustomArchive().current() != archiveId) {
				AyuFeatures::CustomArchives::LockArchive(archiveId);
			}
		}, box->lifetime());
	}

	if (isNew) {
		return;
	}

	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(
		container,
		rpl::single(QString::fromUtf8("Оформление и доступ")));

	Settings::AddButtonWithIcon(
		container,
		rpl::single(QString::fromUtf8("Открыть папку")),
		st::settingsButton,
		{ &st::menuIconArchiveOpen }
	)->setClickedCallback([=] {
		AyuFeatures::CustomArchives::OpenArchive(controller, archiveId);
	});

	const auto currentLock = AyuSettings::getInstance().findCustomArchive(
		archiveId);
	const auto pinTitle = (currentLock && currentLock->hasPin())
		? QString::fromUtf8("Изменить PIN-код")
		: QString::fromUtf8("Установить PIN-код");
	Settings::AddButtonWithIcon(
		container,
		rpl::single(pinTitle),
		st::settingsButton,
		{ &st::menuIconLock }
	)->setClickedCallback([=] {
		const auto current = AyuSettings::getInstance().findCustomArchive(
			archiveId);
		if (current && current->hasPin()) {
			box->getDelegate()->show(Box(SetArchivePinBox, archiveId));
			return;
		}
		EnsureUnlockedThen(box, controller, archiveId, [=] {
			box->getDelegate()->show(Box(SetArchivePinBox, archiveId));
		});
	});

	const auto passwordTitle = (currentLock && currentLock->hasPassword())
		? QString::fromUtf8("Изменить пароль")
		: QString::fromUtf8("Установить пароль");
	Settings::AddButtonWithIcon(
		container,
		rpl::single(passwordTitle),
		st::settingsButton,
		{ &st::menuIconLock }
	)->setClickedCallback([=] {
		const auto current = AyuSettings::getInstance().findCustomArchive(
			archiveId);
		if (current && current->hasPassword()) {
			box->getDelegate()->show(Box(SetArchivePasswordBox, archiveId));
			return;
		}
		EnsureUnlockedThen(box, controller, archiveId, [=] {
			box->getDelegate()->show(Box(SetArchivePasswordBox, archiveId));
		});
	});

	if (currentLock && currentLock->isLocked()) {
		Settings::AddButtonWithIcon(
			container,
			rpl::single(QString::fromUtf8("Снять защиту")),
			st::settingsButton,
			{ &st::menuIconLock }
		)->setClickedCallback([=] {
			EnsureUnlockedThen(box, controller, archiveId, [=] {
				AyuFeatures::CustomArchives::ClearArchiveLock(archiveId);
			});
		});

		if (AyuFeatures::CustomArchives::IsArchiveUnlocked(archiveId)) {
			Settings::AddButtonWithIcon(
				container,
				rpl::single(QString::fromUtf8("Заблокировать сейчас")),
				st::settingsButton,
				{ &st::menuIconLock }
			)->setClickedCallback([=] {
				AyuFeatures::CustomArchives::LockArchive(archiveId);
			});
		}
	}

	Settings::AddButtonWithIcon(
		container,
		rpl::single(QString::fromUtf8("Добавить чат")),
		st::settingsButton,
		{ &st::menuIconAddToFolder }
	)->setClickedCallback([=] {
		EnsureUnlockedThen(box, controller, archiveId, [=] {
			auto types = InlineBots::PeerTypes();
			types |= InlineBots::PeerType::Bot;
			types |= InlineBots::PeerType::User;
			types |= InlineBots::PeerType::Group;
			types |= InlineBots::PeerType::Broadcast;
			Window::ShowChooseRecipientBox(
				controller,
				[=](not_null<Data::Thread*> thread) {
					AyuFeatures::CustomArchives::MovePeerToArchive(
						controller,
						thread->peer(),
						archiveId);
					return true;
				},
				rpl::single(QString::fromUtf8("Добавить в папку")),
				nullptr,
				types);
		});
	});

	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	Settings::AddButtonWithIcon(
		container,
		rpl::single(QString::fromUtf8("Удалить папку")),
		st::settingsAttentionButtonWithIcon,
		{ &st::menuIconDelete }
	)->setClickedCallback([=] {
		const auto doDelete = [=] {
			box->getDelegate()->show(Ui::MakeConfirmBox({
				.text = QString::fromUtf8(
					"Чаты вернутся в основной список, если были скрыты. "
					"Обычный архив Telegram не изменится."),
				.confirmed = [=](Fn<void()> &&close) {
					std::vector<uint64> peerIds;
					if (const auto current = AyuSettings::getInstance()
							.findCustomArchive(archiveId)) {
						peerIds = current->peerIds;
					}
					AyuFeatures::CustomArchives::LockArchive(archiveId);
					AyuSettings::getInstance().removeCustomArchive(archiveId);
					for (const auto peerIdValue : peerIds) {
						if (const auto peer = controller->session()
								.data()
								.peerLoaded(PeerId(peerIdValue))) {
							AyuFeatures::CustomArchives::RefreshPeerChatList(
								peer);
						}
					}
					close();
					box->closeBox();
				},
				.confirmText = tr::lng_box_delete(),
				.confirmStyle = &st::attentionBoxButton,
				.title = QString::fromUtf8("Удалить папку"),
			}));
		};
		EnsureUnlockedThen(box, controller, archiveId, doDelete);
	});
}

void CustomArchivesBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(rpl::single(QString::fromUtf8("Папки чатов")));
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();
	RebuildArchivesList(container, controller, box);

	AyuSettings::getInstance().customArchivesChanges(
	) | rpl::on_next([=] {
		RebuildArchivesList(container, controller, box);
	}, box->lifetime());

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void CustomArchiveViewBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		int archiveId) {
	const auto archive = AyuSettings::getInstance().findCustomArchive(archiveId);
	if (!archive) {
		box->closeBox();
		return;
	}

	box->setTitle(rpl::single(ArchiveTitle(*archive)));
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();
	const auto menuHolder = box->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();

	const auto rebuild = [=] {
		while (container->count() > 0) {
			delete container->widgetAt(0);
		}

		const auto current = AyuSettings::getInstance().findCustomArchive(archiveId);
		if (!current) {
			box->closeBox();
			return;
		}

		container->add(object_ptr<Ui::SettingsButton>(
			container,
			rpl::single(QString::fromUtf8("Добавить чат")),
			st::settingsButton
		))->setClickedCallback([=] {
			auto types = InlineBots::PeerTypes();
			types |= InlineBots::PeerType::Bot;
			types |= InlineBots::PeerType::User;
			types |= InlineBots::PeerType::Group;
			types |= InlineBots::PeerType::Broadcast;
			Window::ShowChooseRecipientBox(
				controller,
				[=](not_null<Data::Thread*> thread) {
					AyuFeatures::CustomArchives::MovePeerToArchive(
						controller,
						thread->peer(),
						archiveId);
					return true;
				},
				rpl::single(QString::fromUtf8("Добавить в папку")),
				nullptr,
				types);
		});

		auto ordered = current->peerIds;
		std::stable_sort(ordered.begin(), ordered.end(), [&](
				uint64 a,
				uint64 b) {
			return IsPinned(*current, a) && !IsPinned(*current, b);
		});

		for (const auto peerIdValue : ordered) {
			const auto peer = controller->session().data().peer(
				PeerId(peerIdValue));
			const auto title = (IsPinned(*current, peerIdValue)
				? u"📌 "_q
				: QString()) + peer->name();
			const auto button = container->add(object_ptr<Ui::SettingsButton>(
				container,
				rpl::single(title),
				st::settingsButton));
			button->setClickedCallback([=] {
				controller->showPeerHistory(
					peer,
					Window::SectionShow::Way::Forward);
				controller->window().hideSettingsAndLayer();
			});
			button->setAcceptBoth(true);
			button->clicks(
			) | rpl::filter([](Qt::MouseButton which) {
				return which == Qt::RightButton;
			}) | rpl::on_next([=] {
				*menuHolder = base::make_unique_q<Ui::PopupMenu>(
					button,
					st::popupMenuExpandedSeparator);
				const auto addAction = Ui::Menu::CreateAddActionCallback(
					menuHolder->get());
				const auto archiveNow = AyuSettings::getInstance()
					.findCustomArchive(archiveId);
				if (!archiveNow) {
					return;
				}
				const auto pinned = IsPinned(*archiveNow, peerIdValue);
				addAction(
					pinned
						? QString::fromUtf8("Открепить")
						: QString::fromUtf8("Закрепить"),
					[=] {
						AyuSettings::getInstance().setPeerPinnedInCustomArchive(
							archiveId,
							peerIdValue,
							!pinned);
					},
					&st::menuIconPin);
				addAction(
					QString::fromUtf8("Убрать из папки"),
					[=] {
						AyuFeatures::CustomArchives::RemovePeerFromArchives(
							controller,
							peer);
					},
					&st::menuIconUnarchive);
				(*menuHolder)->popup(QCursor::pos());
			}, button->lifetime());
		}

		if (ordered.empty()) {
			container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					rpl::single(QString::fromUtf8(
						"В этой папке пока нет чатов.")),
					st::boxLabel),
				st::boxRowPadding);
		}
		container->resizeToWidth(container->width());
	};

	rebuild();
	AyuSettings::getInstance().customArchivesChanges(
	) | rpl::on_next(rebuild, box->lifetime());

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void CustomArchiveUnlockBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*>,
		int archiveId,
		Fn<void()> onUnlocked) {
	const auto archive = AyuSettings::getInstance().findCustomArchive(archiveId);
	if (!archive || !archive->isLocked()) {
		if (onUnlocked) {
			onUnlocked();
		}
		box->closeBox();
		return;
	}

	box->setTitle(rpl::single(QString::fromUtf8("Папка защищена")));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(UnlockHint(*archive)),
		st::boxLabel));

	const auto secretField = AddPasswordRow(
		box,
		rpl::single(QString::fromUtf8("PIN или пароль")));
	const auto errorLabel = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(QString::fromUtf8("Неверный PIN или пароль.")),
		st::boxLabel));
	errorLabel->setTextColorOverride(st::boxTextFgError->c);
	errorLabel->hide();

	const auto submit = [=] {
		const auto entered = secretField->getLastText();
		if (!AyuFeatures::CustomArchives::CheckArchiveSecret(
				archiveId,
				entered)) {
			errorLabel->show();
			secretField->showError();
			secretField->selectAll();
			secretField->setFocus();
			return;
		}
		AyuFeatures::CustomArchives::UnlockArchive(archiveId);
		box->closeBox();
		if (onUnlocked) {
			onUnlocked();
		}
	};

	QObject::connect(secretField, &Ui::MaskedInputField::changed, [=] {
		errorLabel->hide();
	});
	QObject::connect(secretField, &Ui::MaskedInputField::submitted, submit);
	box->addButton(tr::lng_passcode_submit(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	secretField->setFocus();
}

void CustomFolderHintBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(QString::fromUtf8("Как это работает?")));
	box->setWidth(st::boxWideWidth);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(QString::fromUtf8(
			"Папки Whitegram живут только на этом устройстве и не "
			"заменяют архив Telegram.\n\n"
			"Чаты в папке можно скрыть из основного списка — они "
			"останутся в папке. PIN и пароль защищают открытие папки "
			"на этом клиенте.\n\n"
			"Если отправить чат в архив Telegram, он пропадёт из папки "
			"и появится в обычном архиве. Добавление в папку, наоборот, "
			"достаёт чат из архива Telegram.")),
		st::boxLabel));
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}
