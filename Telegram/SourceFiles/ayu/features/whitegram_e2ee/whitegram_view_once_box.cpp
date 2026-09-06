// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#include "ayu/features/whitegram_e2ee/whitegram_view_once_box.h"

#include "apiwrap.h"
#include "ayu/features/whitegram_e2ee/whitegram_e2ee.h"
#include "base/timer.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "ui/boxes/time_picker_box.h"
#include "ui/effects/animations.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include <QtGui/QPainterPath>
#include <rpl/range.h>

namespace Whitegram::E2EE {

not_null<Ui::PasswordInput*> AddPasswordInputRow(
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

namespace {

void DeleteMessageRemotelyAndLocally(
		not_null<Main::Session*> session,
		not_null<HistoryItem*> item) {
	const auto id = item->id.bare;
	const auto peer = item->history()->peer;

	if (const auto channel = peer->asChannel()) {
		session->api().request(MTPchannels_DeleteMessages(
			channel->inputChannel(),
			MTP_vector<MTPint>(1, MTP_int(id))
		)).send();
	} else {
		session->api().request(MTPmessages_DeleteMessages(
			MTP_flags(MTPmessages_DeleteMessages::Flag::f_revoke),
			MTP_vector<MTPint>(1, MTP_int(id))
		)).send();
	}

	item->destroy();
}

[[nodiscard]] QString FormatCountdown(int64 leftSec) {
	if (leftSec >= 3600) {
		const auto h = leftSec / 3600;
		const auto m = (leftSec % 3600) / 60;
		const auto s = leftSec % 60;
		return QString(u"%1 ч. %2 мин. %3 сек."_q).arg(h).arg(m).arg(s);
	} else if (leftSec >= 60) {
		const auto m = leftSec / 60;
		const auto s = leftSec % 60;
		return QString(u"%1 мин. %2 сек."_q).arg(m).arg(s);
	}
	return QString(u"%1 сек."_q).arg(leftSec);
}

class SpoilerOverlay final : public Ui::RpWidget {
public:
	SpoilerOverlay(
		QWidget *parent,
		Fn<void()> revealed);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	Fn<void()> _revealedCallback;
	Ui::SpoilerAnimation _spoiler;
	Ui::Animations::Simple _revealAnim;
	bool _revealing = false;
	bool _revealed = false;
	bool _hovered = false;

};

SpoilerOverlay::SpoilerOverlay(
	QWidget *parent,
	Fn<void()> revealed)
: Ui::RpWidget(parent)
, _revealedCallback(std::move(revealed))
, _spoiler([=] { update(); }) {
	setCursor(style::cur_pointer);
	setAttribute(Qt::WA_TransparentForMouseEvents, false);
	show();

	events() | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Enter) {
			_hovered = true;
			update();
		} else if (e->type() == QEvent::Leave) {
			_hovered = false;
			update();
		}
	}, lifetime());
}

void SpoilerOverlay::paintEvent(QPaintEvent *e) {
	if (_revealed) {
		return;
	}
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);

	const auto progress = _revealAnim.value(_revealing ? 1.0 : 0.0);
	if (progress >= 1.0) {
		return;
	}
	p.setOpacity(1.0 - progress);

	const auto radius = 16.0;
	auto clipPath = QPainterPath();
	clipPath.addRoundedRect(QRectF(rect()), radius, radius);
	p.setClipPath(clipPath);

	p.fillPath(clipPath, QColor(16, 22, 30, 240));

	const auto frame = Ui::DefaultImageSpoiler().frame(
		_spoiler.index(crl::now(), false));
	Ui::FillSpoilerRect(p, rect(), frame);

	const auto pillWidth = 240;
	const auto pillHeight = 42;
	const auto pillRect = QRect(
		(width() - pillWidth) / 2,
		(height() - pillHeight) / 2,
		pillWidth,
		pillHeight);

	auto pillPath = QPainterPath();
	pillPath.addRoundedRect(QRectF(pillRect), pillHeight / 2.0, pillHeight / 2.0);

	const auto pillBg = _hovered
		? QColor(28, 38, 52, 230)
		: QColor(14, 18, 25, 215);
	p.fillPath(pillPath, pillBg);

	p.setPen(QPen(_hovered ? QColor(255, 255, 255, 60) : QColor(255, 255, 255, 30), 1.0));
	p.drawPath(pillPath);

	p.setPen(Qt::white);
	p.setFont(st::boxLabel.style.font);
	p.drawText(
		pillRect,
		u"👁️  Нажмите для просмотра"_q,
		QTextOption(Qt::AlignCenter));
}

void SpoilerOverlay::mousePressEvent(QMouseEvent *e) {
	if (_revealing || _revealed || e->button() != Qt::LeftButton) {
		return;
	}
	_revealing = true;
	if (_revealedCallback) {
		_revealedCallback();
	}
	_revealAnim.start([=] {
		update();
		if (!_revealAnim.animating()) {
			_revealed = true;
			setAttribute(Qt::WA_TransparentForMouseEvents, true);
			hide();
		}
	}, 0.0, 1.0, 250, anim::easeOutCirc);
}

void SetupDecryptedContentBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		not_null<HistoryItem*> item,
		const MessageContent &content) {
	const auto ttl = content.ttl;
	const auto isOut = item->out();

	box->setTitle(u"🔒 Одноразовое сообщение"_q);

	const auto subtitle = isOut
		? (ttl == 0
			? u"👁️ Вы отправили одноразовое сообщение (1 просмотр)"_q
			: (ttl > 0
				? QString(u"⏱️ Вы отправили исчезающее сообщение (таймер: %1)"_q).arg(FormatCountdown(ttl))
				: u"🔒 Защищено сквозным шифрованием (E2EE)"_q))
		: (ttl == 0
			? u"👁️ 1 просмотр • Сообщение исчезнет после закрытия"_q
			: (ttl > 0
				? QString(u"⏱️ Исчезнет через %1 после открытия"_q).arg(FormatCountdown(ttl))
				: u"🔒 Защищено сквозным шифрованием (E2EE)"_q));

	const auto timerLabel = box->addRow(
		object_ptr<Ui::FlatLabel>(box, subtitle, st::boxLabel),
		QMargins(0, 0, 0, 8));

	auto card = object_ptr<Ui::RpWidget>(box);
	const auto rawCard = card.data();

	const auto textLabel = Ui::CreateChild<Ui::FlatLabel>(
		rawCard,
		content.text,
		st::boxLabel);
	textLabel->setSelectable(true);

	struct BoxState {
		base::Timer timer;
		crl::time expireAt = 0;
		bool destroyed = false;
		bool countdownStarted = false;
	};
	const auto state = box->lifetime().make_state<BoxState>();

	const auto destroyMessage = [=] {
		if (state->destroyed) {
			return;
		}
		state->destroyed = true;
		state->timer.cancel();
		DeleteMessageRemotelyAndLocally(session, item);
	};

	const auto startCountdown = [=] {
		if (isOut || ttl <= 0 || state->countdownStarted) {
			return;
		}
		state->countdownStarted = true;
		state->expireAt = crl::now() + ttl * crl::time(1000);
		state->timer.setCallback([=] {
			if (state->destroyed) {
				return;
			}
			const auto now = crl::now();
			if (now >= state->expireAt) {
				destroyMessage();
				box->closeBox();
			} else {
				const auto leftMs = state->expireAt - now;
				const auto leftSec = (leftMs + 999) / 1000;
				if (leftSec <= 3) {
					timerLabel->setText(
						QString(u"⚠️ Самоуничтожение через %1 сек.!"_q)
							.arg(leftSec));
				} else {
					timerLabel->setText(
						QString(u"⏱️ Исчезнет через %1"_q)
							.arg(FormatCountdown(leftSec)));
				}
			}
		});
		state->timer.callEach(100);
	};

	const auto overlay = Ui::CreateChild<SpoilerOverlay>(
		rawCard,
		[=] { startCountdown(); });

	rawCard->paintRequest(
	) | rpl::on_next([=](const QRect &) {
		auto p = QPainter(rawCard);
		PainterHighQualityEnabler hq(p);
		const auto radius = 16.0;
		auto path = QPainterPath();
		path.addRoundedRect(QRectF(rawCard->rect()), radius, radius);

		p.fillPath(path, st::windowBgOver->c);
		p.setPen(QPen(st::windowBgRipple->c, 1.0));
		p.drawPath(path);
	}, rawCard->lifetime());

	rawCard->widthValue(
	) | rpl::on_next([=](int width) {
		const auto paddingH = 16;
		const auto paddingV = 16;
		const auto textWidth = std::max(width - 2 * paddingH, 10);
		textLabel->resizeToWidth(textWidth);
		textLabel->moveToLeft(paddingH, paddingV);

		const auto cardHeight = std::max(textLabel->height() + 2 * paddingV, 130);
		rawCard->resize(width, cardHeight);
		overlay->setGeometry(QRect(0, 0, width, cardHeight));
	}, rawCard->lifetime());

	box->addRow(std::move(card), QMargins(0, 0, 0, 8));

	if (!isOut) {
		box->boxClosing(
		) | rpl::on_next([=] {
			if (ttl == 0 || state->countdownStarted) {
				destroyMessage();
			}
		}, box->lifetime());
	}

	box->addButton(tr::lng_close(), [=] {
		if (!isOut && (ttl == 0 || state->countdownStarted)) {
			destroyMessage();
		}
		box->closeBox();
	});
}

} // namespace

void ShowViewOnceBox(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto session = &controller->session();
	auto rawText = GetEncryptedPayload(item);
	if (rawText.isEmpty()) {
		rawText = item->originalText().text;
	}
	const auto myPrivKey = GetOrGenerateIdentityKeyPair().privateKey;

	auto decrypted = Decrypt(rawText, myPrivKey);
	if (!decrypted.has_value()) {
		decrypted = GetLocalSentMessage(rawText);
	}
	if (!decrypted.has_value()) {
		decrypted = GetLocalSentMessage(item->originalText().text);
	}
	if (!decrypted.has_value()) {
		if (const auto bytes = ExtractPayloadBytes(rawText)) {
			decrypted = GetLocalSentMessage(QString::fromLatin1(bytes->toHex()));
		}
	}
	if (!decrypted.has_value()) {
		if (const auto bytes = ExtractPayloadBytes(item->originalText().text)) {
			decrypted = GetLocalSentMessage(QString::fromLatin1(bytes->toHex()));
		}
	}

	controller->show(Box([=](not_null<Ui::GenericBox*> box) mutable {
		box->setTitle(u"🔒 Одноразовое сообщение"_q);

		if (!decrypted.has_value() && IsPasscodeEncryptedMessage(rawText)) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				u"Это сообщение защищено паролем. Введите пароль для просмотра:"_q,
				st::boxLabel));

			const auto field = AddPasswordInputRow(
				box,
				rpl::single(u"Пароль"_q));

			box->addButton(rpl::single(u"Расшифровать"_q), [=]() mutable {
				const auto pass = field->text();
				const auto res = DecryptWithPasscode(rawText, pass);
				if (res.has_value()) {
					box->closeBox();
					auto copyItem = item;
					controller->show(Box([=](not_null<Ui::GenericBox*> innerBox) {
						SetupDecryptedContentBox(
							innerBox,
							session,
							copyItem,
							*res);
					}));
				} else {
					field->showError();
				}
			});
			box->addButton(tr::lng_cancel(), [=] {
				box->closeBox();
			});
			return;
		}

		if (!decrypted.has_value()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				u"Не удалось расшифровать сообщение. Возможно, оно предназначено для другого пользователя или ключ не совпадает."_q,
				st::boxLabel));
			box->addButton(tr::lng_close(), [=] { box->closeBox(); });
			return;
		}

		SetupDecryptedContentBox(box, session, item, *decrypted);
	}));
}

#include "chat_helpers/compose/compose_show.h"

void ShowCustomTtlBox(
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<void(int seconds)> callback) {
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(u"Одноразовое сообщение"_q);

		const auto ttls = std::vector<TimeId>{
			1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
			15, 20, 25, 30, 45, 60,
			120, 180, 300, 600, 900, 1800,
			3600, 7200, 14400, 28800, 43200,
			86400, 172800, 604800,
		};

		auto phrases = std::vector<QString>();
		phrases.reserve(ttls.size());
		for (const auto t : ttls) {
			if (t < 60) {
				phrases.push_back(QString(u"%1 сек"_q).arg(t));
			} else if (t < 3600) {
				phrases.push_back(QString(u"%1 мин"_q).arg(t / 60));
			} else if (t < 86400) {
				phrases.push_back(QString(u"%1 ч"_q).arg(t / 3600));
			} else {
				phrases.push_back(QString(u"%1 дн"_q).arg(t / 86400));
			}
		}

		const auto pickerCallback = Ui::TimePickerBox(box, ttls, phrases, 15);

		box->addButton(rpl::single(u"Отправить"_q), [=] {
			const auto seconds = pickerCallback();
			box->closeBox();
			if (callback) {
				callback(seconds);
			}
		});

		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

} // namespace Whitegram::E2EE
