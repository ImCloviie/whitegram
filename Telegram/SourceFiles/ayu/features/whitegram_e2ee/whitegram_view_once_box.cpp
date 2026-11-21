// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#include "ayu/features/whitegram_e2ee/whitegram_view_once_box.h"

#include "apiwrap.h"
#include "ayu/features/streamer_mode/streamer_mode.h"
#include "ayu/ayu_settings.h"
#include "ayu/features/whitegram_e2ee/whitegram_e2ee.h"
#include "ayu/features/whitegram_e2ee/whitegram_view_status.h"
#include "base/timer.h"
#include "mainwindow.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "styles/style_menu_icons.h"
#include "ui/boxes/time_picker_box.h"
#include "ui/effects/animations.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text.h"
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

constexpr auto kDissolveDuration = crl::time(900);
constexpr auto kStatusTick = crl::time(100);

[[nodiscard]] QString FormatCountdown(int64 leftSec) {
	if (leftSec >= 3600) {
		return QString(u"%1 ч %2 мин"_q).arg(leftSec / 3600).arg((leftSec % 3600) / 60);
	} else if (leftSec >= 60) {
		return QString(u"%1 мин %2 с"_q).arg(leftSec / 60).arg(leftSec % 60);
	}
	return QString(u"%1 с"_q).arg(leftSec);
}

[[nodiscard]] QString KindText(int ttl) {
	if (ttl == 0) {
		return u"Один просмотр"_q;
	} else if (ttl < 0) {
		return u"Без таймера"_q;
	}
	return u"Исчезнет через "_q + FormatCountdown(ttl) + u" после открытия"_q;
}

class MessageCard final : public Ui::RpWidget {
public:
	MessageCard(QWidget *parent, const QString &text)
	: RpWidget(parent)
	, _text(st::boxLabel.style, text)
	, _spoiler([=] { update(); }) {
	}

	void dissolve(Fn<void()> done) {
		if (_dissolving) {
			return;
		}
		_dissolving = true;
		_dissolve.start([=] {
			update();
			if (!_dissolve.animating() && done) {
				done();
			}
		}, 0., 1., kDissolveDuration, anim::easeInCubic);
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto inner = std::max(newWidth - 2 * padding().x(), 1);
		return std::max(
			_text.countHeight(inner) + 2 * padding().y(),
			style::ConvertScale(88));
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto progress = _dissolve.value(_dissolving ? 1. : 0.);
		const auto radius = double(st::roundRadiusLarge) * 2;
		auto path = QPainterPath();
		path.addRoundedRect(QRectF(rect()), radius, radius);

		p.setOpacity(1. - progress * 0.6);
		p.fillPath(path, st::windowBgOver);
		p.setPen(QPen(st::windowBgRipple, style::ConvertScale(1)));
		p.drawPath(path);

		p.setOpacity(1. - progress);
		p.setPen(st::windowFg);
		_text.drawLeft(
			p,
			padding().x(),
			padding().y(),
			width() - 2 * padding().x(),
			width());

		if (progress > 0.) {
			p.save();
			p.setClipPath(path);
			p.setOpacity(std::min(1., progress * 2.) * (1. - progress * 0.7));
			const auto frame = Ui::DefaultImageSpoiler().frame(
				_spoiler.index(crl::now(), false));
			Ui::FillSpoilerRect(p, rect(), frame);
			p.restore();
		}
	}

private:
	[[nodiscard]] QPoint padding() const {
		return QPoint(style::ConvertScale(18), style::ConvertScale(16));
	}

	Ui::Text::String _text;
	Ui::SpoilerAnimation _spoiler;
	Ui::Animations::Simple _dissolve;
	bool _dissolving = false;

};

class HeaderRow final : public Ui::RpWidget {
public:
	HeaderRow(QWidget *parent, QString title, QString subtitle)
	: RpWidget(parent)
	, _title(std::move(title))
	, _subtitle(std::move(subtitle)) {
		resize(width(), style::ConvertScale(48));
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto size = style::ConvertScale(40);
		const auto circle = QRect(0, (height() - size) / 2, size, size);
		p.setPen(Qt::NoPen);
		p.setBrush(st::activeButtonBg);
		p.drawEllipse(circle);
		const auto &icon = st::menuIconLock;
		icon.paint(
			p,
			QPoint(
				circle.x() + (size - icon.width()) / 2,
				circle.y() + (size - icon.height()) / 2),
			width(),
			st::activeButtonFg->c);

		const auto left = size + style::ConvertScale(12);
		const auto available = width() - left;
		const auto top = (height() - st::semiboldFont->height - st::normalFont->height - style::ConvertScale(2)) / 2;
		p.setFont(st::semiboldFont);
		p.setPen(st::windowBoldFg);
		p.drawTextLeft(
			left,
			top,
			width(),
			st::semiboldFont->elided(_title, available));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawTextLeft(
			left,
			top + st::semiboldFont->height + style::ConvertScale(2),
			width(),
			st::normalFont->elided(_subtitle, available));
	}

private:
	const QString _title;
	const QString _subtitle;

};

class StatusRow final : public Ui::RpWidget {
public:
	explicit StatusRow(QWidget *parent) : RpWidget(parent) {
		resize(width(), style::ConvertScale(36));
	}

	void set(QString text, double progress, bool attention) {
		if (_text == text && _progress == progress && _attention == attention) {
			return;
		}
		_text = std::move(text);
		_progress = progress;
		_attention = attention;
		update();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		p.setFont(st::normalFont);
		p.setPen(_attention ? st::attentionButtonFg : st::windowSubTextFg);
		p.drawTextLeft(0, 0, width(), _text);
		if (_progress < 0.) {
			return;
		}
		const auto line = style::ConvertScale(4);
		const auto top = st::normalFont->height + style::ConvertScale(8);
		const auto bar = QRectF(0, top, width(), line);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgRipple);
		p.drawRoundedRect(bar, line / 2., line / 2.);
		p.setBrush(_attention ? st::attentionButtonFg : st::activeButtonBg);
		p.drawRoundedRect(
			QRectF(bar.x(), bar.y(), bar.width() * std::clamp(_progress, 0., 1.), line),
			line / 2.,
			line / 2.);
	}

private:
	QString _text;
	double _progress = -1.;
	bool _attention = false;

};

void SetupDecryptedContentBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		not_null<HistoryItem*> item,
		const MessageContent &content,
		const QString &payload) {
	const auto ttl = content.ttl;
	const auto isOut = item->out();
	const auto hash = PacketHash(payload);

	box->setWidth(st::boxWideWidth);
	box->setTitle(rpl::single(u"Одноразовое сообщение"_q));
	box->lifetime().make_state<AyuFeatures::StreamerMode::ScopedViewOnceGuard>(
		AyuFeatures::StreamerMode::RequireViewOnceGuard());
	if (AyuFeatures::StreamerMode::isStreamerModeActive()) {
		AyuFeatures::StreamerMode::hideWidgetWindow(box);
	}

	const auto peer = item->history()->peer;
	box->addRow(
		object_ptr<HeaderRow>(
			box,
			isOut
				? (u"Вы отправили · "_q + peer->name())
				: (u"От "_q + item->from()->name()),
			KindText(ttl)),
		QMargins(st::boxRowPadding.left(), 0, st::boxRowPadding.right(), style::ConvertScale(14)));
	const auto card = box->addRow(
		object_ptr<MessageCard>(box, content.text),
		QMargins(st::boxRowPadding.left(), 0, st::boxRowPadding.right(), style::ConvertScale(12)));
	const auto status = box->addRow(
		object_ptr<StatusRow>(box),
		QMargins(st::boxRowPadding.left(), 0, st::boxRowPadding.right(), 0));

	struct State {
		base::Timer timer;
		crl::time expireAt = 0;
		bool finished = false;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto finish = [=](bool animated) {
		if (state->finished) {
			return;
		}
		state->finished = true;
		state->timer.cancel();
		const auto destroy = [=] {
			RevokeOnce(item);
		};
		if (animated) {
			card->dissolve([=] {
				box->closeBox();
				destroy();
			});
		} else {
			destroy();
		}
	};

	if (!isOut) {
		MarkViewed(payload);
		ReportOpened(session, payload, ttl);
		if (ttl > 0) {
			state->expireAt = crl::now() + ttl * crl::time(1000);
		}
	} else {
		WatchViewStatus(session, hash);
	}

	const auto refresh = [=] {
		if (state->finished) {
			return;
		}
		if (!isOut) {
			if (ttl > 0) {
				const auto left = std::max(state->expireAt - crl::now(), crl::time(0));
				status->set(
					u"Исчезнет через "_q + FormatCountdown((left + 999) / 1000),
					double(left) / (ttl * 1000.),
					left <= 3000);
				if (!left) {
					finish(true);
				}
			} else if (ttl == 0) {
				status->set(u"Исчезнет, как только вы закроете окно"_q, -1., false);
			} else {
				status->set(u"Защищено сквозным шифрованием"_q, -1., false);
			}
			return;
		}
		const auto view = ViewStatus(hash);
		if (!view) {
			WatchViewStatus(session, hash);
			status->set(u"Собеседник ещё не открыл сообщение"_q, -1., false);
		} else if (view->ttl > 0) {
			const auto left = std::max(view->expiresAt() - ServerNowMs(), int64(0));
			status->set(
				u"Собеседник открыл · осталось "_q + FormatCountdown((left + 999) / 1000),
				double(left) / (view->ttl * 1000.),
				left <= 3000);
			if (!left) {
				finish(true);
			}
		} else if (view->ttl == 0) {
			status->set(u"Собеседник открыл · исчезнет после закрытия"_q, -1., false);
		} else {
			status->set(u"Собеседник открыл сообщение"_q, -1., false);
		}
	};
	state->timer.setCallback(refresh);
	state->timer.callEach(kStatusTick);
	refresh();

	if (!isOut && ttl >= 0) {
		box->boxClosing(
		) | rpl::on_next([=] {
			finish(false);
		}, box->lifetime());
	}
	box->addButton(tr::lng_close(), [=] {
		box->closeBox();
	});
}

} // namespace

void ShowViewOnceBox(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto session = &controller->session();
	if (AyuSettings::getInstance().hideTelegramOnViewOnce()) {
		AyuFeatures::StreamerMode::hideWidgetWindow(
			controller->widget().get());
	}

	auto rawText = GetEncryptedPayload(item);
	if (rawText.isEmpty()) {
		rawText = item->originalText().text;
	}
	const auto myPrivKey = GetOrGenerateIdentityKeyPair().privateKey;

	auto decrypted = item->out()
		? GetLocalSentMessageById(item->fullId())
		: std::optional<MessageContent>();
	if (!decrypted.has_value()) {
		decrypted = Decrypt(rawText, myPrivKey);
	}
	if (!decrypted.has_value() && item->out()) {
		decrypted = GetLocalSentMessage(rawText);
	}

	auto refusal = QString();
	if (decrypted.has_value() && !item->out()) {
		const auto from = item->from()->asUser();
		const auto senderOk = from
			&& !from->isBot()
			&& (peerToUser(from->id).bare == decrypted->senderId);
		const auto recipientOk = !decrypted->recipientId
			|| (decrypted->recipientId == session->userId().bare);
		if (item->Has<HistoryMessageForwarded>() || !senderOk || !recipientOk) {
			refusal = u"Это копия одноразового сообщения. Открыть можно только оригинал, присланный самим автором."_q;
		} else if (WasViewed(rawText)) {
			refusal = u"Это сообщение уже было открыто, второй раз его не посмотреть."_q;
		}
		if (!refusal.isEmpty()) {
			auto &text = decrypted->text;
			SecureCleanseString(text);
			decrypted = std::nullopt;
		}
	}

	controller->show(Box([=](not_null<Ui::GenericBox*> box) mutable {
		box->setTitle(rpl::single(u"Одноразовое сообщение"_q));
		box->lifetime().make_state<AyuFeatures::StreamerMode::ScopedViewOnceGuard>(
			AyuFeatures::StreamerMode::RequireViewOnceGuard());
		if (AyuFeatures::StreamerMode::isStreamerModeActive()) {
			AyuFeatures::StreamerMode::hideWidgetWindow(box);
		}

		if (!refusal.isEmpty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(box, refusal, st::boxLabel));
			box->addButton(tr::lng_close(), [=] { box->closeBox(); });
			return;
		}
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
							*res,
							rawText);
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
				u"Не удалось расшифровать сообщение: оно зашифровано не для ключа этого устройства. Попросите собеседника отправить его ещё раз — новое сообщение откроется на всех ваших устройствах."_q,
				st::boxLabel));
			box->addButton(tr::lng_close(), [=] { box->closeBox(); });
			return;
		}

		SetupDecryptedContentBox(box, session, item, *decrypted, rawText);
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
