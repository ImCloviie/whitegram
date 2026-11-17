// This file is part of Whitegram for Desktop.
#include "ayu/ui/components/typing_animation_picker.h"

#include "ayu/ayu_ui_settings.h"
#include "ui/effects/animation_value.h"
#include "ui/painter.h"
#include "styles/palette.h"
#include "styles/style_basic.h"

#include <QtGui/QFontMetricsF>
#include <QtGui/QPainterPath>

namespace {

constexpr auto kCharInterval = crl::time(110);
constexpr auto kLead = crl::time(320);
constexpr auto kHold = crl::time(1300);
constexpr auto kFadeOut = crl::time(260);
constexpr auto kPause = crl::time(220);
constexpr auto kCaretBlink = crl::time(500);
constexpr auto kSelectDuration = crl::time(200);

[[nodiscard]] crl::time LongestDuration() {
	auto result = 0;
	for (auto i = 0; i <= AyuUiSettings::kTypingAnimationMax; ++i) {
		result = std::max(
			result,
			AyuUiSettings::TypingAnimationDuration(
				AyuUiSettings::TypingAnimation(i)));
	}
	return result;
}

} // namespace

TypingAnimationPicker::TypingAnimationPicker(QWidget *parent, Args &&args)
: RpWidget(parent)
, _names(args.names.size())
, _chosen(std::move(args.chosen))
, _selected(args.current)
, _started(crl::now()) {
	for (auto i = 0; i != int(args.names.size()); ++i) {
		std::move(args.names[i]) | rpl::on_next([=](const QString &name) {
			_names[i] = name;
			update();
		}, lifetime());
	}
	std::move(args.sample) | rpl::on_next([=](const QString &sample) {
		_sample = sample;
		_started = crl::now();
		update();
	}, lifetime());

	_previewFont = st::semiboldFont->f;
	_previewFont.setPixelSize(style::ConvertScale(19));

	setMouseTracking(true);
	_loop.init([=](crl::time) {
		update();
		return true;
	});
	_loop.start();
}

int TypingAnimationPicker::columns(int width) const {
	return (width >= style::ConvertScale(330)) ? 3 : 2;
}

int TypingAnimationPicker::cardHeight() const {
	return style::ConvertScale(86);
}

int TypingAnimationPicker::resizeGetHeight(int newWidth) {
	const auto count = int(_names.size());
	const auto cols = columns(newWidth);
	const auto rows = (count + cols - 1) / cols;
	const auto gap = style::ConvertScale(8);
	return rows ? (rows * cardHeight() + (rows - 1) * gap) : 0;
}

QRect TypingAnimationPicker::cardRect(int index) const {
	const auto cols = columns(width());
	const auto gap = style::ConvertScale(8);
	const auto cardWidth = (width() - (cols - 1) * gap) / cols;
	const auto col = index % cols;
	const auto row = index / cols;
	return QRect(
		col * (cardWidth + gap),
		row * (cardHeight() + gap),
		cardWidth,
		cardHeight());
}

int TypingAnimationPicker::indexAt(QPoint point) const {
	for (auto i = 0; i != int(_names.size()); ++i) {
		if (cardRect(i).contains(point)) {
			return i;
		}
	}
	return -1;
}

void TypingAnimationPicker::setHovered(int index) {
	if (_hovered == index) {
		return;
	}
	_hovered = index;
	setCursor((index >= 0) ? style::cur_pointer : style::cur_default);
	update();
}

void TypingAnimationPicker::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);
	const auto now = crl::now();
	for (auto i = 0; i != int(_names.size()); ++i) {
		if (cardRect(i).intersects(e->rect())) {
			paintCard(p, i, now);
		}
	}
}

void TypingAnimationPicker::paintCard(QPainter &p, int index, crl::time now) {
	const auto rect = cardRect(index);
	const auto radius = style::ConvertScale(12);
	const auto progress = _selection.value(1.);
	const auto selectedness = (index == _selected)
		? progress
		: (index == _previous)
		? (1. - progress)
		: 0.;

	const auto hovered = (index == _hovered) ? 1. : 0.;
	const auto pressed = (index == _pressed && index == _hovered);
	const auto bg = anim::color(
		st::windowBgOver,
		st::windowBgRipple,
		pressed ? 1. : hovered * 0.6);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRoundedRect(rect, radius, radius);

	if (selectedness > 0.) {
		const auto stroke = style::ConvertScale(2);
		auto border = st::activeLineFg->c;
		border.setAlphaF(border.alphaF() * selectedness);
		auto pen = QPen(border);
		pen.setWidthF(stroke);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto half = stroke / 2.;
		p.drawRoundedRect(
			QRectF(rect).marginsRemoved({ half, half, half, half }),
			radius - half,
			radius - half);

		const auto size = style::ConvertScale(18) * selectedness;
		const auto inset = style::ConvertScale(8);
		const auto center = QPointF(
			rect.x() + rect.width() - inset - style::ConvertScale(9),
			rect.y() + inset + style::ConvertScale(9));
		p.setPen(Qt::NoPen);
		p.setBrush(st::activeLineFg->c);
		p.drawEllipse(center, size / 2., size / 2.);

		auto check = QPen(st::activeButtonFg->c);
		check.setWidthF(style::ConvertScale(2) * selectedness);
		check.setCapStyle(Qt::RoundCap);
		check.setJoinStyle(Qt::RoundJoin);
		p.setPen(check);
		const auto unit = size / 18.;
		auto path = QPainterPath();
		path.moveTo(center + QPointF(-4.5 * unit, 0.3 * unit));
		path.lineTo(center + QPointF(-1.5 * unit, 3.2 * unit));
		path.lineTo(center + QPointF(4.5 * unit, -3. * unit));
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	}

	const auto labelHeight = style::ConvertScale(30);
	const auto preview = QRect(
		rect.x(),
		rect.y() + style::ConvertScale(4),
		rect.width(),
		rect.height() - labelHeight - style::ConvertScale(4));
	paintPreview(p, index, preview, now);

	const auto &font = st::normalFont;
	const auto label = QRect(
		rect.x() + style::ConvertScale(8),
		rect.y() + rect.height() - labelHeight,
		rect.width() - style::ConvertScale(16),
		labelHeight - style::ConvertScale(8));
	p.setFont(font);
	p.setPen(anim::color(
		st::windowSubTextFg,
		st::windowActiveTextFg,
		selectedness));
	p.drawText(
		label,
		Qt::AlignHCenter | Qt::AlignVCenter,
		font->elided(_names[index], label.width()));
}

void TypingAnimationPicker::paintPreview(
		QPainter &p,
		int index,
		QRect area,
		crl::time now) {
	const auto count = int(_sample.size());
	if (!count) {
		return;
	}
	const auto mode = AyuUiSettings::TypingAnimation(index);
	const auto duration = AyuUiSettings::TypingAnimationDuration(mode);
	const auto typingEnd = kLead
		+ (count - 1) * kCharInterval
		+ LongestDuration();
	const auto period = typingEnd + kHold + kFadeOut + kPause;
	const auto local = (now - _started) % period;
	const auto fadeStart = typingEnd + kHold;
	const auto wordOpacity = (local <= fadeStart)
		? 1.
		: std::max(1. - float64(local - fadeStart) / kFadeOut, 0.);
	if (wordOpacity <= 0.) {
		return;
	}

	const auto metrics = QFontMetricsF(_previewFont);
	const auto total = metrics.horizontalAdvance(_sample);
	const auto left = area.x() + (area.width() - total) / 2.;
	const auto glyphHeight = metrics.ascent() - metrics.descent();
	const auto baseline = area.y() + (area.height() + glyphHeight) / 2.;
	const auto lineHeight = metrics.height();
	const auto text = st::windowFg->c;
	const auto accent = st::windowActiveTextFg->c;

	p.setFont(_previewFont);
	auto caretX = left;
	auto typed = 0;
	for (auto i = 0; i != count; ++i) {
		const auto appear = kLead + i * kCharInterval;
		if (local < appear) {
			break;
		}
		++typed;
		const auto x = left + metrics.horizontalAdvance(_sample.left(i));
		const auto ch = _sample.mid(i, 1);
		const auto w = metrics.horizontalAdvance(ch);
		caretX = x + w;

		const auto progress = duration
			? (float64(local - appear) / duration)
			: 1.;
		const auto frame = AyuUiSettings::ComputeTypingAnimationFrame(
			mode,
			progress);
		const auto opacity = frame.opacity * wordOpacity;
		if (opacity <= 0.) {
			continue;
		}
		const auto center = QPointF(x + w / 2., baseline - glyphHeight / 2.);
		p.save();
		p.setOpacity(opacity);
		p.translate(center + QPointF(0., frame.offsetY * lineHeight));
		if (frame.scale != 1.) {
			p.scale(frame.scale, frame.scale);
		}
		p.translate(-center);
		p.setPen(anim::color(text, accent, frame.accent));
		p.drawText(QPointF(x, baseline), ch);
		p.restore();
	}

	const auto typing = (typed < count)
		|| (local < kLead + (count - 1) * kCharInterval + kCharInterval);
	const auto caretShown = (local < fadeStart)
		&& (typing || ((local / kCaretBlink) % 2 == 0));
	if (caretShown) {
		const auto caretWidth = std::max(style::ConvertScale(2), 2);
		const auto caretTop = baseline - metrics.ascent() * 0.9;
		const auto caretHeight = metrics.ascent() * 0.9
			+ metrics.descent() * 0.6;
		p.setPen(Qt::NoPen);
		p.setBrush(accent);
		p.drawRoundedRect(
			QRectF(
				caretX + style::ConvertScale(2),
				caretTop,
				caretWidth,
				caretHeight),
			caretWidth / 2.,
			caretWidth / 2.);
	}
}

void TypingAnimationPicker::mouseMoveEvent(QMouseEvent *e) {
	setHovered(indexAt(e->pos()));
}

void TypingAnimationPicker::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_pressed = indexAt(e->pos());
	update();
}

void TypingAnimationPicker::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	update();
	if (e->button() != Qt::LeftButton
		|| pressed < 0
		|| pressed != indexAt(e->pos())
		|| pressed == _selected) {
		return;
	}
	_previous = _selected;
	_selected = pressed;
	_selection.stop();
	_selection.start([=] { update(); }, 0., 1., kSelectDuration);
	if (_chosen) {
		_chosen(pressed);
	}
}

void TypingAnimationPicker::leaveEventHook(QEvent *e) {
	setHovered(-1);
}
