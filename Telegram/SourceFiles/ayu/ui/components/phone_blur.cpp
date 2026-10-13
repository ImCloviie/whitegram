// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/phone_blur.h"

#include "ayu/ayu_settings.h"
#include "base/event_filter.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/widgets/labels.h"
#include "styles/style_ayu_styles.h"

#include <QPaintEvent>
#include <QtGui/QMouseEvent>

namespace Ayu::UI {
namespace {

constexpr auto kRevealDuration = crl::time(180);

} // namespace

PhoneBlur::PhoneBlur(
	QWidget *parent,
	not_null<::Ui::FlatLabel*> phoneLabel)
: ::Ui::RpWidget(parent)
, _phoneLabel(phoneLabel) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	refreshMode();
	hide();

	setupLabelEvents();

	AyuSettings::getInstance().hidePhoneNumberChanges(
	) | rpl::on_next([=](bool) {
		syncWithSettings(anim::type::normal);
	}, lifetime());

	AyuSettings::getInstance().phoneRevealModeChanges(
	) | rpl::on_next([=](PhoneRevealMode) {
		refreshMode();
		syncWithSettings(anim::type::normal);
	}, lifetime());

	_phoneLabel->geometryValue(
	) | rpl::filter([=](const QRect &r) {
		return !r.isNull() && BlurEnabled();
	}) | rpl::on_next([=](const QRect &) {
		updateGeometryToLabel();
		raise();
	}, lifetime());

	syncWithSettings(anim::type::instant);
}

void PhoneBlur::setupLabelEvents() {
	base::install_event_filter(_phoneLabel, [=](not_null<QEvent*> e) {
		if (!BlurEnabled()) {
			return base::EventFilterResult::Continue;
		}
		const auto type = e->type();
		if (_hoverMode) {
			if (type == QEvent::Enter) {
				setRevealed(true, anim::type::normal);
			} else if (type == QEvent::Leave) {
				setRevealed(false, anim::type::normal);
			}
		} else if (type == QEvent::MouseButtonPress) {
			const auto press = static_cast<QMouseEvent*>(e.get());
			if (press->button() == Qt::LeftButton) {
				setRevealed(!_revealed, anim::type::normal);
			}
		}
		return base::EventFilterResult::Continue;
	}, lifetime());
}

bool PhoneBlur::BlurEnabled() const {
	return AyuSettings::getInstance().hidePhoneNumber();
}

void PhoneBlur::refreshMode() {
	_hoverMode = (AyuSettings::getInstance().phoneRevealMode()
		== PhoneRevealMode::OnHover);
}

void PhoneBlur::syncWithSettings(anim::type animated) {
	if (BlurEnabled()) {
		updateGeometryToLabel();
		show();
		raise();
		setRevealed(_hoverMode ? false : _revealed, animated);
	} else {
		setRevealed(true, animated);
	}
}

void PhoneBlur::updateGeometryToLabel() {
	const auto mapped = ::Ui::MapFrom(
		parentWidget(),
		_phoneLabel,
		QPoint(0, 0));
	setGeometryToLeft(
		mapped.x(),
		mapped.y(),
		_phoneLabel->width(),
		_phoneLabel->height());
	update();
}

void PhoneBlur::setRevealed(bool revealed, anim::type animated) {
	if (animated == anim::type::instant) {
		_revealing.stop();
		_revealed = revealed;
		update();
		return;
	}
	const auto target = revealed ? 0. : 1.;
	const auto current = _revealing.value(target);
	_revealed = revealed;
	_revealing.start([=](float64) {
		update();
	}, current, target, kRevealDuration, anim::easeOutCubic);
	update();
}

void PhoneBlur::paintEvent(QPaintEvent *e) {
	if (!BlurEnabled() || _revealed) {
		return;
	}
	const auto progress = _revealing.value(1.);
	if (progress <= 0.) {
		return;
	}

	if (_blurred.isNull()
		|| _blurred.size() / style::DevicePixelRatio() != size()) {
		auto grabbed = GrabLabelImage();
		_blurred = grabbed.isNull()
			? QImage()
			: Images::BlurLargeImage(
				std::move(grabbed),
				style::ConvertScale(st::phoneBlurRadius));
	}

	auto p = QPainter(this);
	if (!_blurred.isNull()) {
		p.setOpacity(progress);
		p.drawImage(rect(), _blurred);
	}
}

QImage PhoneBlur::GrabLabelImage() {
	if (_phoneLabel->size().isEmpty()) {
		return {};
	}
	auto result = QImage(
		_phoneLabel->size(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	_phoneLabel->render(&p, QPoint(0, 0));
	return result;
}

} // namespace Ayu::UI
