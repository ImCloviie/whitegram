#include "ayu/features/self_destruct/self_destruct_box.h"

#include "ayu/features/self_destruct/self_destruct.h"
#include "api/api_cloud_password.h"
#include "base/timer.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "core/core_cloud_password.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "data/data_user.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/qt_object_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include <vector>

namespace Whitegram::SelfDestruct {
namespace {

[[nodiscard]] bool IsRussian() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	return id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
}

[[nodiscard]] QString Tr(const QString &ru, const QString &en) {
	return IsRussian() ? ru : en;
}

[[nodiscard]] not_null<Ui::PasswordInput*> AddPasswordRow(
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

class PasswordBoxLockout final {
public:
	PasswordBoxLockout(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::FlatLabel*> errorLabel,
		std::vector<not_null<Ui::PasswordInput*>> fields,
		Fn<void()> sync,
		Fn<bool()> isLocked,
		Fn<crl::time()> remaining)
	: _errorLabel(errorLabel)
	, _fields(std::move(fields))
	, _sync(std::move(sync))
	, _isLocked(std::move(isLocked))
	, _remaining(std::move(remaining)) {
		if (_sync) {
			_sync();
		}
		const auto timer = box->lifetime().make_state<base::Timer>();
		timer->setCallback([=] { refresh(); });
		timer->callEach(1000);
		refresh();
	}

	void addActionButton(not_null<Ui::AbstractButton*> button) {
		_actionButtons.push_back(button.get());
		refresh();
	}

	void refresh() {
		const auto locked = _isLocked ? _isLocked() : false;
		for (const auto field : _fields) {
			field->setEnabled(!locked);
			field->setDisplayFocused(!locked && field->hasFocus());
			if (locked) {
				field->setText(QString());
				field->hideError();
			}
		}
		for (const auto &button : _actionButtons) {
			if (button) {
				button->setEnabled(!locked);
			}
		}
		if (locked) {
			const auto left = _remaining ? _remaining() : 0;
			_errorLabel->setText(FormatLockoutMessage(left));
			_errorLabel->show();
		}
	}

	void applyNow() {
		refresh();
	}

private:
	const not_null<Ui::FlatLabel*> _errorLabel;
	const std::vector<not_null<Ui::PasswordInput*>> _fields;
	Fn<void()> _sync;
	Fn<bool()> _isLocked;
	Fn<crl::time()> _remaining;
	std::vector<QPointer<Ui::AbstractButton>> _actionButtons;
};

[[nodiscard]] bool VerifyFieldHelper(
		not_null<Ui::PasswordInput*> field,
		not_null<Ui::FlatLabel*> errorLabel,
		not_null<PasswordBoxLockout*> lockout,
		Fn<PasswordVerifyResult(const QString&)> verify) {
	const auto result = verify(field->getLastText());
	if (result.status == PasswordVerifyStatus::Success) {
		return true;
	}
	if (result.status == PasswordVerifyStatus::LockedOut) {
		lockout->applyNow();
		return false;
	}
	errorLabel->setText(result.message);
	errorLabel->show();
	field->showError();
	return false;
}

// =========================================================================
// MODE 1: Local Self-Destruct Dialogs
// =========================================================================

void ShowLocalConfirmExecuteBox(
		not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setTitle(rpl::single(
			Tr(u"Самоуничтожение Whitegram"_q, u"Whitegram Self-destruct"_q)));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Tr(
				u"Введите пароль локального самоуничтожения."_q,
				u"Enter your local self-destruct password."_q)),
			st::boxLabel));
		const auto password = AddPasswordRow(
			box,
			rpl::single(Tr(u"Пароль"_q, u"Password"_q)));
		const auto errorLabel = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString()),
			st::boxLabel));
		errorLabel->setTextColorOverride(st::boxTextFgError->c);
		errorLabel->hide();

		const auto lockout = box->lifetime().make_state<PasswordBoxLockout>(
			box,
			errorLabel,
			std::vector<not_null<Ui::PasswordInput*>>{ password },
			SyncPasswordLockout,
			IsPasswordLockedOut,
			PasswordLockoutRemaining);

		const auto submit = [=] {
			if (IsPasswordLockedOut()) {
				lockout->applyNow();
				return;
			}
			if (!VerifyFieldHelper(password, errorLabel, lockout, VerifyPassword)) {
				return;
			}
			box->closeBox();
			ExecuteLocalSelfDestruct();
		};

		QObject::connect(
			password,
			&Ui::MaskedInputField::submitted,
			submit);
		const auto confirmButton = box->addButton(
			rpl::single(Tr(u"Уничтожить Whitegram"_q, u"Destroy Whitegram"_q)),
			submit);
		lockout->addActionButton(confirmButton.data());
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		if (!IsPasswordLockedOut()) {
			password->setFocus();
		}
	}));
}

void ShowLocalPasswordConfirmBox(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &description,
		const QString &confirmText,
		Fn<void()> then) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setTitle(rpl::single(title));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(description),
			st::boxLabel));
		const auto password = AddPasswordRow(
			box,
			rpl::single(Tr(u"Пароль"_q, u"Password"_q)));
		const auto errorLabel = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString()),
			st::boxLabel));
		errorLabel->setTextColorOverride(st::boxTextFgError->c);
		errorLabel->hide();

		const auto lockout = box->lifetime().make_state<PasswordBoxLockout>(
			box,
			errorLabel,
			std::vector<not_null<Ui::PasswordInput*>>{ password },
			SyncPasswordLockout,
			IsPasswordLockedOut,
			PasswordLockoutRemaining);

		const auto submit = [=] {
			if (IsPasswordLockedOut()) {
				lockout->applyNow();
				return;
			}
			if (!VerifyFieldHelper(password, errorLabel, lockout, VerifyPassword)) {
				return;
			}
			box->closeBox();
			then();
		};

		QObject::connect(
			password,
			&Ui::MaskedInputField::submitted,
			submit);
		const auto confirmButton = box->addButton(
			rpl::single(confirmText),
			submit);
		lockout->addActionButton(confirmButton.data());
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		if (!IsPasswordLockedOut()) {
			password->setFocus();
		}
	}));
}

// =========================================================================
// MODE 2: Total Destruction Dialogs (Client + Accounts)
// =========================================================================

struct Account2FAInfo {
	uint64 userId = 0;
	QString name;
	bool has2FA = false;
	not_null<Main::Session*> session;
};

void CollectAccounts2FA(
		not_null<Window::SessionController*> controller,
		Fn<void(std::vector<Account2FAInfo>)> done) {
	if (!Core::IsAppLaunched() || !Core::App().domain().started()) {
		done({});
		return;
	}

	struct CollectState {
		std::vector<Account2FAInfo> accounts;
		int pending = 0;
	};
	const auto state = std::make_shared<CollectState>();

	const auto &accountList = Core::App().domain().accounts();
	for (const auto &[index, account] : accountList) {
		if (!account) {
			continue;
		}
		const auto session = account->maybeSession();
		if (!session) {
			continue;
		}
		++state->pending;

		const auto userId = session->userId().bare;
		const auto accountName = session->user()->name();

		session->api().cloudPassword().reload();
		session->api().cloudPassword().state(
		) | rpl::take(
			1
		) | rpl::on_next([=](const Core::CloudPasswordState &pwState) {
			state->accounts.push_back({
				.userId = userId,
				.name = accountName,
				.has2FA = pwState.hasPassword,
				.session = session,
			});
			if (--state->pending <= 0) {
				done(std::move(state->accounts));
			}
		}, controller->lifetime());
	}

	if (state->pending == 0) {
		done({});
	}
}

void ShowTotalCountdownConfirmBox(
		not_null<Window::SessionController*> controller,
		Fn<void()> onConfirmed) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setTitle(rpl::single(Tr(
			u"Подтверждение тотального уничтожения"_q,
			u"Total Destruction Confirmation"_q)));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Tr(
				u"ВНИМАНИЕ! Этот режим предназначен для полного уничтожения.\n\n"
				u"При активации все аккаунты Telegram будут БЕЗВОЗВРАТНО УДАЛЕНЫ "
				u"с серверов Telegram (все чаты, каналы, боты и данные), "
				u"а клиент Whitegram будет стёрт.\n\n"
				u"Это действие НЕЛЬЗЯ отменить."_q,
				u"ATTENTION! This mode is designed for complete destruction.\n\n"
				u"When activated, all Telegram accounts will be PERMANENTLY "
				u"DELETED from Telegram servers (all chats, channels, bots, data), "
				u"and the Whitegram client will be erased.\n\n"
				u"This action CANNOT be undone."_q)),
			st::boxLabel));

		struct CountdownState {
			int secondsLeft = 10;
		};
		const auto state = box->lifetime().make_state<CountdownState>();

		const auto buttonText = box->lifetime().make_state<
			rpl::variable<QString>>(
			Tr(
				u"Подождите... (%1)"_q.arg(state->secondsLeft),
				u"Wait... (%1)"_q.arg(state->secondsLeft)));

		const auto confirmButton = box->addButton(
			buttonText->value(),
			[=] {
				if (state->secondsLeft <= 0) {
					box->closeBox();
					onConfirmed();
				}
			});
		confirmButton->setEnabled(false);

		const auto timer = box->lifetime().make_state<base::Timer>();
		timer->setCallback([=] {
			--state->secondsLeft;
			if (state->secondsLeft <= 0) {
				*buttonText = Tr(
					u"Активировать тотальное уничтожение"_q,
					u"Activate total destruction"_q);
				confirmButton->setEnabled(true);
				timer->cancel();
			} else {
				*buttonText = Tr(
					u"Подождите... (%1)"_q.arg(state->secondsLeft),
					u"Wait... (%1)"_q.arg(state->secondsLeft));
			}
		});
		timer->callEach(1000);

		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void AskTotal2FAPasswordsSequentially(
		not_null<Window::SessionController*> controller,
		const QString &totalPassword,
		std::vector<Account2FAInfo> accounts2FA,
		int currentIndex,
		Fn<void()> onAllDone) {
	if (currentIndex >= int(accounts2FA.size())) {
		onAllDone();
		return;
	}

	const auto &acc = accounts2FA[currentIndex];

	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setTitle(rpl::single(Tr(
			u"Подтверждение 2FA личности"_q,
			u"2FA Identity Confirmation"_q)));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Tr(
				u"Для включения тотального уничтожения введите облачный пароль (2FA) "
				u"для аккаунта:\n\n%1"_q.arg(acc.name),
				u"To enable total destruction, enter the cloud password (2FA) "
				u"for account:\n\n%1"_q.arg(acc.name))),
			st::boxLabel));

		const auto password = AddPasswordRow(
			box,
			rpl::single(Tr(u"Облачный пароль"_q, u"Cloud password"_q)));
		const auto errorLabel = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString()),
			st::boxLabel));
		errorLabel->setTextColorOverride(st::boxTextFgError->c);
		errorLabel->hide();

		const auto submit = [=] {
			const auto text = password->getLastText();
			if (text.isEmpty()) {
				errorLabel->setText(Tr(u"Введите пароль"_q, u"Enter password"_q));
				errorLabel->show();
				password->showError();
				return;
			}

			errorLabel->setText(Tr(u"Проверка через сервер..."_q, u"Verifying with server..."_q));
			errorLabel->show();

			acc.session->api().cloudPassword().check(text
			) | rpl::on_error_done([=](const QString &error) {
				if (error == u"PASSWORD_HASH_INVALID"_q
					|| error == u"SRP_PASSWORD_CHANGED"_q) {
					errorLabel->setText(Tr(
						u"Неверный 2FA пароль"_q,
						u"Wrong 2FA password"_q));
				} else {
					errorLabel->setText(Tr(
						u"Ошибка: %1"_q.arg(error),
						u"Error: %1"_q.arg(error)));
				}
				errorLabel->show();
				password->showError();
			}, [=] {
				StoreTotalDestructCloudPassword(
					acc.userId,
					text,
					totalPassword);
				box->closeBox();
				AskTotal2FAPasswordsSequentially(
					controller,
					totalPassword,
					accounts2FA,
					currentIndex + 1,
					onAllDone);
			}, box->lifetime());
		};

		QObject::connect(
			password,
			&Ui::MaskedInputField::submitted,
			submit);
		box->addButton(tr::lng_continue(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		password->setFocus();
	}));
}

void StartTotalDestructEnableFlow(
		not_null<Window::SessionController*> controller,
		const QString &newTotalPassword) {
	CollectAccounts2FA(
		controller,
		[=](std::vector<Account2FAInfo> accounts) {
			std::vector<Account2FAInfo> accounts2FA;
			for (auto &a : accounts) {
				if (a.has2FA) {
					accounts2FA.push_back(std::move(a));
				}
			}

			if (accounts2FA.empty()) {
				ShowTotalCountdownConfirmBox(controller, [=] {
					SetTotalDestructPassword(newTotalPassword);
					controller->showToast(Tr(
						u"Тотальное уничтожение настроено"_q,
						u"Total destruction configured"_q));
				});
				return;
			}

			ClearTotalDestructCloudPasswords();
			AskTotal2FAPasswordsSequentially(
				controller,
				newTotalPassword,
				std::move(accounts2FA),
				0,
				[=] {
					ShowTotalCountdownConfirmBox(controller, [=] {
						SetTotalDestructPassword(newTotalPassword);
						controller->showToast(Tr(
							u"Тотальное уничтожение настроено"_q,
							u"Total destruction configured"_q));
					});
				});
		});
}

void ShowTotalPasswordConfirmBox(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &description,
		const QString &confirmText,
		Fn<void()> then) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setTitle(rpl::single(title));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(description),
			st::boxLabel));
		const auto password = AddPasswordRow(
			box,
			rpl::single(Tr(u"Пароль"_q, u"Password"_q)));
		const auto errorLabel = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString()),
			st::boxLabel));
		errorLabel->setTextColorOverride(st::boxTextFgError->c);
		errorLabel->hide();

		const auto lockout = box->lifetime().make_state<PasswordBoxLockout>(
			box,
			errorLabel,
			std::vector<not_null<Ui::PasswordInput*>>{ password },
			SyncTotalDestructPasswordLockout,
			IsTotalDestructPasswordLockedOut,
			TotalDestructPasswordLockoutRemaining);

		const auto submit = [=] {
			if (IsTotalDestructPasswordLockedOut()) {
				lockout->applyNow();
				return;
			}
			if (!VerifyFieldHelper(
					password,
					errorLabel,
					lockout,
					VerifyTotalDestructPassword)) {
				return;
			}
			box->closeBox();
			then();
		};

		QObject::connect(
			password,
			&Ui::MaskedInputField::submitted,
			submit);
		const auto confirmButton = box->addButton(
			rpl::single(confirmText),
			submit);
		lockout->addActionButton(confirmButton.data());
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		if (!IsTotalDestructPasswordLockedOut()) {
			password->setFocus();
		}
	}));
}

void ShowTotalConfirmExecuteBox(
		not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setTitle(rpl::single(
			Tr(u"ТОТАЛЬНОЕ УНИЧТОЖЕНИЕ"_q, u"TOTAL DESTRUCTION"_q)));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Tr(
				u"Введите пароль тотального уничтожения.\n\n"
				u"Все аккаунты будут немедленно удалены с серверов, "
				u"а Whitegram будет стерт с этого компьютера."_q,
				u"Enter your total destruction password.\n\n"
				u"All accounts will be immediately deleted from servers, "
				u"and Whitegram will be erased from this computer."_q)),
			st::boxLabel));
		const auto password = AddPasswordRow(
			box,
			rpl::single(Tr(u"Пароль"_q, u"Password"_q)));
		const auto errorLabel = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString()),
			st::boxLabel));
		errorLabel->setTextColorOverride(st::boxTextFgError->c);
		errorLabel->hide();

		const auto lockout = box->lifetime().make_state<PasswordBoxLockout>(
			box,
			errorLabel,
			std::vector<not_null<Ui::PasswordInput*>>{ password },
			SyncTotalDestructPasswordLockout,
			IsTotalDestructPasswordLockedOut,
			TotalDestructPasswordLockoutRemaining);

		const auto submit = [=] {
			if (IsTotalDestructPasswordLockedOut()) {
				lockout->applyNow();
				return;
			}
			const auto text = password->getLastText();
			if (!VerifyFieldHelper(
					password,
					errorLabel,
					lockout,
					VerifyTotalDestructPassword)) {
				return;
			}
			box->closeBox();
			ExecuteTotalDestruct(text);
		};

		QObject::connect(
			password,
			&Ui::MaskedInputField::submitted,
			submit);
		const auto confirmButton = box->addButton(
			rpl::single(Tr(
				u"Уничтожить всё безвозвратно"_q,
				u"Destroy everything permanently"_q)),
			submit);
		lockout->addActionButton(confirmButton.data());
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		if (!IsTotalDestructPasswordLockedOut()) {
			password->setFocus();
		}
	}));
}

} // namespace

// =========================================================================
// Public Mode 1 Dialogs
// =========================================================================

void ShowSelfDestructSettingsBox(
		not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setTitle(rpl::single(Tr(
			u"Пароль локального самоуничтожения"_q,
			u"Local self-destruct password"_q)));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Tr(
				u"Пароль нужен для локального удаления Whitegram, кэша, "
				u"модулей и tdata на этом компьютере.\n"
				u"Аккаунты Telegram не затрагиваются."_q,
				u"Password is required to locally delete Whitegram, cache, "
				u"modules, and tdata on this computer.\n"
				u"Telegram accounts are not affected."_q)),
			st::boxLabel));

		const auto changing = HasPassword();
		Ui::PasswordInput *current = nullptr;
		if (changing) {
			current = AddPasswordRow(
				box,
				rpl::single(Tr(u"Текущий пароль"_q, u"Current password"_q)));
		}

		const auto password = AddPasswordRow(
			box,
			rpl::single(Tr(u"Новый пароль"_q, u"New password"_q)));
		const auto confirm = AddPasswordRow(
			box,
			rpl::single(Tr(u"Повторите пароль"_q, u"Repeat password"_q)));
		const auto errorLabel = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString()),
			st::boxLabel));
		errorLabel->setTextColorOverride(st::boxTextFgError->c);
		errorLabel->hide();

		std::vector<not_null<Ui::PasswordInput*>> fields;
		if (current) {
			fields.push_back(current);
		}
		fields.push_back(password);
		fields.push_back(confirm);

		const auto lockout = box->lifetime().make_state<PasswordBoxLockout>(
			box,
			errorLabel,
			std::move(fields),
			SyncPasswordLockout,
			IsPasswordLockedOut,
			PasswordLockoutRemaining);

		const auto submit = [=] {
			if (IsPasswordLockedOut()) {
				lockout->applyNow();
				return;
			}
			if (changing) {
				if (!VerifyFieldHelper(
						current,
						errorLabel,
						lockout,
						VerifyPassword)) {
					return;
				}
			}
			const auto first = password->getLastText();
			const auto second = confirm->getLastText();
			if (first.size() < 4) {
				errorLabel->setText(Tr(
					u"Минимум 4 символа"_q,
					u"At least 4 characters"_q));
				errorLabel->show();
				password->showError();
				return;
			}
			if (first != second) {
				errorLabel->setText(Tr(
					u"Пароли не совпадают"_q,
					u"Passwords do not match"_q));
				errorLabel->show();
				confirm->showError();
				return;
			}
			SetPassword(first);
			controller->showToast(tr::lng_box_done(tr::now));
			box->closeBox();
		};

		QObject::connect(password, &Ui::MaskedInputField::submitted, submit);
		QObject::connect(confirm, &Ui::MaskedInputField::submitted, submit);
		if (current) {
			QObject::connect(current, &Ui::MaskedInputField::submitted, submit);
		}
		const auto saveButton = box->addButton(tr::lng_settings_save(), submit);
		lockout->addActionButton(saveButton.data());
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		if (changing) {
			box->addLeftButton(
				rpl::single(Tr(u"Удалить пароль"_q, u"Remove password"_q)),
				[=] {
					ShowLocalPasswordConfirmBox(
						controller,
						Tr(u"Удалить пароль"_q, u"Remove password"_q),
						Tr(
							u"Введите текущий пароль самоуничтожения."_q,
							u"Enter your current self-destruct password."_q),
						Tr(u"Удалить"_q, u"Remove"_q),
						[=] {
							ClearPassword();
							controller->showToast(tr::lng_box_done(tr::now));
							box->closeBox();
						});
				});
		}
		if (!IsPasswordLockedOut()) {
			if (current) {
				current->setFocus();
			} else {
				password->setFocus();
			}
		}
	}));
}

void ShowSelfDestructExecuteBox(
		not_null<Window::SessionController*> controller) {
	if (!HasPassword()) {
		controller->showToast(Tr(
			u"Сначала задайте пароль самоуничтожения"_q,
			u"Set a self-destruct password first"_q));
		ShowSelfDestructSettingsBox(controller);
		return;
	}

	const auto warningText = Tr(
		u"Будет полностью удалена папка с Whitegram — клиент, "
		u"modules, tdata, логи и все остальные файлы в ней.\n\n"
		u"Аккаунты Telegram останутся нетронутыми на серверах.\n\n"
		u"Это действие нельзя отменить."_q,
		u"The entire Whitegram folder will be removed — the "
		u"client, modules, tdata, logs, and all other files in "
		u"it.\n\n"
		u"Telegram accounts remain untouched on servers.\n\n"
		u"This action cannot be undone."_q);

	controller->show(Ui::MakeConfirmBox({
		.text = { warningText },
		.confirmed = [=](Fn<void()> close) {
			close();
			ShowLocalConfirmExecuteBox(controller);
		},
		.confirmText = Tr(u"Продолжить"_q, u"Continue"_q),
	}));
}

// =========================================================================
// Public Mode 2 Dialogs (Total Destruction)
// =========================================================================

void ShowTotalDestructSettingsBoxImpl(
		not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setTitle(rpl::single(Tr(
			u"Пароль тотального уничтожения"_q,
			u"Total destruction password"_q)));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Tr(
				u"Этот пароль активирует экстренное удаление всех аккаунтов Telegram "
				u"с серверов Telegram вместе с локальным стиранием Whitegram.\n\n"
				u"При установке пароля потребуется подтвердить 2FA-пароли активных аккаунтов."_q,
				u"This password activates emergency deletion of all Telegram accounts "
				u"from servers along with local wiping of Whitegram.\n\n"
				u"Setting the password requires confirming 2FA passwords for active accounts."_q)),
			st::boxLabel));

		const auto changing = HasTotalDestructPassword();
		Ui::PasswordInput *current = nullptr;
		if (changing) {
			current = AddPasswordRow(
				box,
				rpl::single(Tr(u"Текущий пароль"_q, u"Current password"_q)));
		}

		const auto password = AddPasswordRow(
			box,
			rpl::single(Tr(u"Новый пароль"_q, u"New password"_q)));
		const auto confirm = AddPasswordRow(
			box,
			rpl::single(Tr(u"Повторите пароль"_q, u"Repeat password"_q)));
		const auto errorLabel = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString()),
			st::boxLabel));
		errorLabel->setTextColorOverride(st::boxTextFgError->c);
		errorLabel->hide();

		std::vector<not_null<Ui::PasswordInput*>> fields;
		if (current) {
			fields.push_back(current);
		}
		fields.push_back(password);
		fields.push_back(confirm);

		const auto lockout = box->lifetime().make_state<PasswordBoxLockout>(
			box,
			errorLabel,
			std::move(fields),
			SyncTotalDestructPasswordLockout,
			IsTotalDestructPasswordLockedOut,
			TotalDestructPasswordLockoutRemaining);

		const auto submit = [=] {
			if (IsTotalDestructPasswordLockedOut()) {
				lockout->applyNow();
				return;
			}
			if (changing) {
				if (!VerifyFieldHelper(
						current,
						errorLabel,
						lockout,
						VerifyTotalDestructPassword)) {
					return;
				}
			}
			const auto first = password->getLastText();
			const auto second = confirm->getLastText();
			if (first.size() < 4) {
				errorLabel->setText(Tr(
					u"Минимум 4 символа"_q,
					u"At least 4 characters"_q));
				errorLabel->show();
				password->showError();
				return;
			}
			if (first != second) {
				errorLabel->setText(Tr(
					u"Пароли не совпадают"_q,
					u"Passwords do not match"_q));
				errorLabel->show();
				confirm->showError();
				return;
			}
			box->closeBox();
			StartTotalDestructEnableFlow(controller, first);
		};

		QObject::connect(password, &Ui::MaskedInputField::submitted, submit);
		QObject::connect(confirm, &Ui::MaskedInputField::submitted, submit);
		if (current) {
			QObject::connect(current, &Ui::MaskedInputField::submitted, submit);
		}
		const auto saveButton = box->addButton(tr::lng_continue(), submit);
		lockout->addActionButton(saveButton.data());
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		if (changing) {
			box->addLeftButton(
				rpl::single(Tr(u"Отключить режим"_q, u"Disable mode"_q)),
				[=] {
					ShowTotalPasswordConfirmBox(
						controller,
						Tr(u"Отключить тотальное уничтожение"_q, u"Disable total destruction"_q),
						Tr(
							u"Введите текущий пароль тотального уничтожения."_q,
							u"Enter your current total destruction password."_q),
						Tr(u"Отключить"_q, u"Disable"_q),
						[=] {
							ClearTotalDestructPassword();
							controller->showToast(tr::lng_box_done(tr::now));
							box->closeBox();
						});
				});
		}
		if (!IsTotalDestructPasswordLockedOut()) {
			if (current) {
				current->setFocus();
			} else {
				password->setFocus();
			}
		}
	}));
}

void ShowTotalDestructExecuteBoxImpl(
		not_null<Window::SessionController*> controller) {
	if (!HasTotalDestructPassword()) {
		controller->showToast(Tr(
			u"Сначала настройте тотальное уничтожение"_q,
			u"Configure total destruction first"_q));
		ShowTotalDestructSettingsBoxImpl(controller);
		return;
	}

	const auto warningText = Tr(
		u"ВНИМАНИЕ! ТОТАЛЬНОЕ УНИЧТОЖЕНИЕ!\n\n"
		u"Все авторизованные аккаунты Telegram будут БЕЗВОЗВРАТНО УДАЛЕНЫ "
		u"с серверов Telegram (все чаты, каналы, группы, контакты и данные).\n\n"
		u"Папка с Whitegram на этом ПК будет полностью стёрта.\n\n"
		u"Это действие НЕОБРАТИМО!"_q,
		u"ATTENTION! TOTAL DESTRUCTION!\n\n"
		u"All authorized Telegram accounts will be PERMANENTLY DELETED "
		u"from Telegram servers (all chats, channels, groups, contacts, data).\n\n"
		u"The Whitegram folder on this PC will be completely erased.\n\n"
		u"This action is IRREVERSIBLE!"_q);

	controller->show(Ui::MakeConfirmBox({
		.text = { warningText },
		.confirmed = [=](Fn<void()> close) {
			close();
			ShowTotalConfirmExecuteBox(controller);
		},
		.confirmText = Tr(u"Я понимаю риски. Продолжить"_q, u"I understand the risks. Continue"_q),
	}));
}

} // namespace Whitegram::SelfDestruct
