#include "ayu/ui/settings/account_profiles_box.h"

#include "ayu/ayu_settings.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "data/data_user.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/checkbox.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include <rpl/rpl.h>

namespace {

struct IconEntry {
	QString path;
	QString label;
};

const auto kProfileIcons = std::vector<IconEntry>{
	{ QString(), QString::fromUtf8("Нет") },
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

void ChooseProfileIconBox(
	not_null<Ui::GenericBox*> box,
	int profileId,
	const QString &currentIcon) {

	box->setTitle(rpl::single(QString::fromUtf8("Выберите иконку")));

	constexpr auto kIconSize = 40;
	constexpr auto kIconSpacing = 6;
	constexpr auto kColumns = 6;
	constexpr auto kIconPad = 8;

	const auto rows = (int(kProfileIcons.size()) + kColumns - 1) / kColumns;
	const auto totalHeight = rows * (kIconSize + kIconSpacing) - kIconSpacing + kIconPad * 2;

	const auto grid = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, totalHeight),
		QMargins(kIconPad, kIconPad, kIconPad, 0));

	for (int i = 0; i < int(kProfileIcons.size()); ++i) {
		const auto &entry = kProfileIcons[i];
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
			AyuSettings::getInstance().setAccountProfileIcon(profileId, iconPath);
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

			if (iconPath.isEmpty()) {
				p.setPen(isActive ? st::windowFgActive->p : st::windowFg->p);
				p.setFont(st::semiboldFont);
				p.drawText(r, QString::fromUtf8("—"), QTextOption(Qt::AlignCenter));
			} else {
				const auto emoji = [&]() -> QString {
					for (const auto &e : kProfileIcons) {
						if (e.path == iconPath) return e.label;
					}
					return QString();
				}();
				if (!emoji.isEmpty()) {
					p.setPen(isActive ? st::windowFgActive->p : st::windowFg->p);
					p.setFont(st::semiboldFont->f);
					p.drawText(r, emoji, QTextOption(Qt::AlignCenter));
				}
			}
		}, btn->lifetime());
	}

	box->addButton(tr::lng_cancel(), [box] { box->closeBox(); });
}


void EditAccountProfileBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	int profileId) {

	auto& settings = AyuSettings::getInstance();
	const auto profiles = settings.accountProfiles();
	auto it = std::find_if(profiles.begin(), profiles.end(), [profileId](const AccountProfile& p) { return p.id == profileId; });
	
	const bool isNew = (profileId == -1 || it == profiles.end());
	const QString initialName = isNew ? QString() : it->name;
	const QString initialIcon = isNew ? QString() : it->icon;

	box->setTitle(rpl::single(isNew ? QString::fromUtf8("Новый профиль") : QString::fromUtf8("Редактировать профиль")));

	auto nameField = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(QString::fromUtf8("Название профиля")),
		initialName
	));
	nameField->setFocus();

	auto saveCallback = [=] {
		auto name = nameField->getLastText().trimmed();
		if (name.isEmpty()) return;

		auto& settings = AyuSettings::getInstance();
		if (isNew) {
			int newId = 1;
			auto currentProfiles = settings.accountProfiles();
			if (!currentProfiles.empty()) {
				newId = std::max_element(currentProfiles.begin(), currentProfiles.end(), [](const AccountProfile& a, const AccountProfile& b) {
					return a.id < b.id;
				})->id + 1;
			}
			settings.addAccountProfile({newId, name});
		} else {
			settings.updateAccountProfile({profileId, name, initialIcon});
		}
		box->closeBox();
	};

	box->addButton(tr::lng_settings_save(), saveCallback);
	box->addButton(tr::lng_cancel(), [box] { box->closeBox(); });
	
	if (!isNew) {
		box->addRow(object_ptr<Ui::SettingsButton>(
			box,
			rpl::single(QString::fromUtf8("Выбрать иконку")),
			st::settingsButton
		))->setClickedCallback([=] {
			box->getDelegate()->show(Box(ChooseProfileIconBox, profileId, initialIcon));
		});

		box->addRow(object_ptr<Ui::SettingsButton>(
			box,
			rpl::single(QString::fromUtf8("Назначить аккаунты")),
			st::settingsButton
		))->setClickedCallback([=] {
			box->getDelegate()->show(Box([profileId, controller](not_null<Ui::GenericBox*> assignBox) {
				assignBox->setTitle(rpl::single(QString::fromUtf8("Назначить аккаунты")));
				
				auto& settings = AyuSettings::getInstance();
				auto accounts = controller->session().domain().orderedAccounts();

				for (auto account : accounts) {
					if (!account->sessionExists()) continue;
					const auto accountId = account->session().uniqueId();
					auto currentProfiles = settings.getAccountProfileIds(accountId);
					const bool isAssigned = std::find(currentProfiles.begin(), currentProfiles.end(), profileId) != currentProfiles.end();

					auto checkbox = assignBox->addRow(object_ptr<Ui::Checkbox>(
						assignBox,
						account->session().user()->name(),
						isAssigned,
						st::defaultBoxCheckbox
					));

					checkbox->checkedChanges() | rpl::on_next([accountId, profileId](bool checked) {
						auto& s = AyuSettings::getInstance();
						if (checked) {
							s.assignAccountToProfile(accountId, profileId);
						} else {
							s.unassignAccountFromProfile(accountId, profileId);
						}
					}, checkbox->lifetime());
				}

				assignBox->addButton(tr::lng_close(), [assignBox] { assignBox->closeBox(); });
			}));
		});

		box->addRow(object_ptr<Ui::SettingsButton>(
			box,
			rpl::single(QString::fromUtf8("Удалить профиль")),
			st::settingsAttentionButtonWithIcon
		))->setClickedCallback([=] {
			box->getDelegate()->show(Box([profileId, box](not_null<Ui::GenericBox*> confirmBox) {
				confirmBox->setTitle(rpl::single(QString::fromUtf8("Удалить профиль")));
				confirmBox->addRow(object_ptr<Ui::FlatLabel>(
					confirmBox,
					rpl::single(QString::fromUtf8("Вы уверены, что хотите удалить этот профиль?")),
					st::boxLabel
				));
				confirmBox->addButton(tr::lng_box_delete(), [=] {
					AyuSettings::getInstance().removeAccountProfile(profileId);
					confirmBox->closeBox();
					box->closeBox();
				});
				confirmBox->addButton(tr::lng_cancel(), [confirmBox] { confirmBox->closeBox(); });
			}));
		});
	}
}

} // namespace

void AccountProfilesBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller) {

	box->setTitle(rpl::single(QString::fromUtf8("Профили аккаунтов")));

	auto& settings = AyuSettings::getInstance();
	
	box->addRow(object_ptr<Ui::SettingsButton>(
		box,
		rpl::single(QString::fromUtf8("Добавить профиль")),
		st::settingsButton
	))->setClickedCallback([=] {
		box->getDelegate()->show(Box(EditAccountProfileBox, controller, -1));
	});

	const auto profiles = settings.accountProfiles();
	for (const auto& profile : profiles) {
		box->addRow(object_ptr<Ui::SettingsButton>(
			box,
			rpl::single(profile.name),
			st::settingsButton
		))->setClickedCallback([=, id = profile.id] {
			box->getDelegate()->show(Box(EditAccountProfileBox, controller, id));
		});
	}

	box->addButton(tr::lng_close(), [box] { box->closeBox(); });
}
