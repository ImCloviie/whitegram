/*
This file is part of Whitegram for Desktop, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ayu/features/whitegram_e2ee/whitegram_one_time_media.h"

#include "ayu/features/whitegram_e2ee/whitegram_e2ee.h"
#include "ayu/features/whitegram_e2ee/whitegram_one_time_view.h"
#include "ayu/features/whitegram_e2ee/whitegram_view_status.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/painter.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QPainterPath>

namespace Whitegram::E2EE {
namespace {

constexpr auto kPulsePeriod = crl::time(2400);
constexpr auto kCountdownTick = crl::time(250);

[[nodiscard]] QString TtlText(int ttl) {
	if (ttl == 0) {
		return u"1 просмотр"_q;
	} else if (ttl < 0) {
		return u"без таймера"_q;
	} else if (ttl < 60) {
		return QString::number(ttl) + u" с"_q;
	} else if (ttl < 3600) {
		return QString::number(ttl / 60) + u" мин"_q;
	}
	return QString::number(ttl / 3600) + u" ч"_q;
}

} // namespace

OneTimeMedia::OneTimeMedia(not_null<HistoryItem*> parent)
: Data::Media(parent) {
}

std::unique_ptr<Data::Media> OneTimeMedia::clone(
		not_null<HistoryItem*> parent) {
	return std::make_unique<OneTimeMedia>(parent);
}

TextWithEntities OneTimeMedia::notificationText() const {
	return TextWithEntities::Simple(u"🔒 Одноразовое сообщение"_q);
}

QString OneTimeMedia::pinnedTextSubstring() const {
	return u"одноразовое сообщение"_q;
}

TextForMimeData OneTimeMedia::clipboardText() const {
	return TextForMimeData::Simple(u"Одноразовое сообщение"_q);
}

bool OneTimeMedia::allowsForward() const {
	return false;
}

bool OneTimeMedia::allowsEdit() const {
	return false;
}

bool OneTimeMedia::consumeMessageText(const TextWithEntities &text) {
	return true;
}

bool OneTimeMedia::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool OneTimeMedia::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> OneTimeMedia::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<OneTimeView>(message);
}

OneTimeView::OneTimeView(not_null<HistoryView::Element*> parent)
: HistoryView::Media(parent)
, _open(MakeViewOnceClickHandler(parent->data()))
, _tick([=] { _parent->repaint(); }) {
	if (parent->data()->out()) {
		ViewStatusUpdates(
		) | rpl::filter([=](const QString &hash) {
			return (hash == _hash);
		}) | rpl::on_next([=] {
			_parent->repaint();
		}, _lifetime);
	}
}

OneTimeView::~OneTimeView() = default;

QSize OneTimeView::countOptimalSize() {
	return QSize(style::ConvertScale(264), style::ConvertScale(172));
}

void OneTimeView::resolve() const {
	if (_resolved) {
		return;
	}
	_resolved = true;
	const auto item = _parent->data();
	const auto payload = GetEncryptedPayload(item);
	_hash = PacketHash(payload);
	if (const auto content = GetLocalSentMessageById(item->fullId())) {
		_ttl = content->ttl;
	} else if (auto content = Decrypt(
			payload,
			GetOrGenerateIdentityKeyPair().privateKey)) {
		_ttl = content->ttl;
		SecureCleanseString(content->text);
	}
}

OneTimeView::Status OneTimeView::status() const {
	const auto item = _parent->data();
	if (!item->out()) {
		return { .badge = u"E2EE"_q, .subtitle = u"Нажмите, чтобы открыть"_q };
	}
	resolve();
	const auto session = &item->history()->session();
	const auto view = ViewStatus(_hash);
	if (!view) {
		WatchViewStatus(session, _hash);
		return { .badge = TtlText(_ttl), .subtitle = u"Ещё не открыто"_q };
	} else if (view->ttl <= 0) {
		return {
			.badge = TtlText(view->ttl),
			.subtitle = (view->ttl == 0)
				? u"Открыто · исчезнет после закрытия"_q
				: u"Открыто"_q,
		};
	}
	const auto left = std::max(view->expiresAt() - ServerNowMs(), int64(0));
	if (!left && !_revoking) {
		_revoking = true;
		const auto id = item->fullId();
		crl::on_main(session, [=] {
			if (const auto item = session->data().message(id)) {
				RevokeOnce(item);
			}
		});
	}
	_tick.callOnce(kCountdownTick);
	const auto seconds = (left + 999) / 1000;
	return {
		.badge = u"осталось "_q + TtlText(int(seconds)),
		.subtitle = u"Открыто · исчезнет через "_q + TtlText(int(seconds)),
		.progress = double(left) / (view->ttl * 1000.),
	};
}

bool OneTimeView::hasHeavyPart() const {
	return (_spoiler != nullptr);
}

void OneTimeView::unloadHeavyPart() {
	_spoiler = nullptr;
}

QPoint OneTimeView::resolveCustomInfoRightBottom() const {
	const auto skipx = (st::msgDateImgDelta + st::msgDateImgPadding.x());
	const auto skipy = (st::msgDateImgDelta + st::msgDateImgPadding.y());
	return QPoint(width() - skipx, height() - skipy);
}

void OneTimeView::draw(
		Painter &p,
		const HistoryView::PaintContext &context) const {
	if (width() < 1 || height() < 1) {
		return;
	}
	const auto card = QRect(0, 0, width(), height());
	const auto radius = double(st::bubbleRadiusLarge);
	const auto out = _parent->hasOutLayout();
	auto hq = PainterHighQualityEnabler(p);
	auto path = QPainterPath();
	path.addRoundedRect(QRectF(card), radius, radius);

	auto background = QLinearGradient(card.topLeft(), card.bottomRight());
	background.setColorAt(0., out ? QColor(44, 58, 104) : QColor(36, 40, 60));
	background.setColorAt(1., QColor(9, 10, 18));
	p.fillPath(path, background);

	if (!_spoiler) {
		_spoiler = std::make_unique<Ui::SpoilerAnimation>([=] {
			_parent->repaint();
		});
	}
	p.save();
	p.setClipPath(path);
	const auto frame = Ui::DefaultImageSpoiler().frame(
		_spoiler->index(context.now, context.paused));
	Ui::FillSpoilerRect(p, card, frame);
	auto vignette = QRadialGradient(
		QPointF(card.center()),
		std::max(card.width(), card.height()) * 0.62);
	vignette.setColorAt(0., QColor(0, 0, 0, 0));
	vignette.setColorAt(1., QColor(0, 0, 0, 150));
	p.fillRect(card, vignette);
	p.restore();

	const auto size = st::msgFileLayout.thumbSize;
	const auto center = QPoint(card.center().x(), card.y() + card.height() * 2 / 5);
	const auto circle = QRect(
		center.x() - size / 2,
		center.y() - size / 2,
		size,
		size);
	const auto phase = context.paused
		? 0.
		: double(context.now % kPulsePeriod) / kPulsePeriod;
	const auto ring = circle.marginsAdded(QMargins(1, 1, 1, 1)
		* int(std::round(style::ConvertScale(4) + phase * style::ConvertScale(10))));
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(140, 170, 255, int(90 * (1. - phase))));
	p.drawEllipse(ring);
	p.setBrush(QColor(0, 0, 0, 120));
	p.drawEllipse(circle);
	p.setPen(QPen(QColor(255, 255, 255, 70), style::ConvertScale(1)));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(circle);
	const auto current = status();
	if (current.progress >= 0.) {
		auto pen = QPen(QColor(255, 255, 255, 230), style::ConvertScale(3));
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		const auto arc = circle.marginsAdded(QMargins(1, 1, 1, 1) * style::ConvertScale(4));
		p.drawArc(arc, 90 * 16, int(360 * 16 * current.progress));
		p.setPen(Qt::NoPen);
	}
	const auto &icon = st::menuIconLock;
	icon.paint(
		p,
		QPoint(
			circle.x() + (size - icon.width()) / 2,
			circle.y() + (size - icon.height()) / 2),
		width(),
		QColor(255, 255, 255));

	const auto textTop = circle.y() + size + style::ConvertScale(12);
	p.setFont(st::semiboldFont);
	p.setPen(QColor(255, 255, 255, 240));
	p.drawText(
		QRect(0, textTop, width(), st::semiboldFont->height),
		Qt::AlignHCenter | Qt::AlignTop,
		u"Одноразовое сообщение"_q);
	p.setFont(st::normalFont);
	p.setPen(QColor(255, 255, 255, 165));
	p.drawText(
		QRect(
			0,
			textTop + st::semiboldFont->height + style::ConvertScale(2),
			width(),
			st::normalFont->height),
		Qt::AlignHCenter | Qt::AlignTop,
		current.subtitle);

	const auto badge = current.badge;
	const auto padding = st::msgDateImgPadding;
	const auto badgeWidth = st::normalFont->width(badge) + 2 * padding.x();
	const auto badgeHeight = st::normalFont->height + 2 * padding.y();
	const auto badgeRect = QRect(
		st::msgDateImgDelta,
		st::msgDateImgDelta,
		badgeWidth,
		badgeHeight);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 110));
	p.drawRoundedRect(badgeRect, badgeHeight / 2., badgeHeight / 2.);
	p.setPen(QColor(255, 255, 255, 230));
	p.drawText(badgeRect, Qt::AlignCenter, badge);

	if (context.selected()) {
		p.fillPath(path, context.st->msgSelectOverlay()->b);
	}
	_parent->drawInfo(
		p,
		context,
		width(),
		height(),
		width(),
		HistoryView::InfoDisplayType::Image);
}

HistoryView::TextState OneTimeView::textState(
		QPoint point,
		HistoryView::StateRequest request) const {
	auto result = HistoryView::TextState(_parent);
	if (!QRect(0, 0, width(), height()).contains(point)) {
		return result;
	}
	const auto info = _parent->bottomInfoTextState(
		width(),
		height(),
		point,
		HistoryView::InfoDisplayType::Image);
	if (info.link
		|| info.cursor != HistoryView::CursorState::None
		|| info.customTooltip) {
		return info;
	}
	result.link = _open;
	return result;
}

} // namespace Whitegram::E2EE
