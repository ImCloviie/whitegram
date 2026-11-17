// This file is part of Whitegram Desktop, based on AyuGram for Desktop.
// Copyright @Radolyn, 2026
#include "ayu/ui/components/percent_slider.h"

#include "base/algorithm.h"
#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/labels.h"
#include "styles/style_ayu_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

#include <QtGui/QMouseEvent>

namespace AyuUi {
namespace {

constexpr auto kNumberDuration = crl::time(260);
constexpr auto kHandleDuration = crl::time(320);

class PercentSlider final : public Ui::RpWidget {
public:
	PercentSlider(QWidget *parent, PercentSliderArgs &&args);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] int titleHeight() const;
	[[nodiscard]] QRect valueRect() const;
	[[nodiscard]] float64 progressOf(int value) const;
	[[nodiscard]] QString shownText() const;
	void setValue(int value, bool moveHandle);
	void setValueOver(bool over);
	void startEditing();
	void finishEditing(bool apply);

	const int _min = 0;
	const int _max = 100;
	const Fn<void(int)> _changed;
	const not_null<Ui::FlatLabel*> _title;
	const not_null<Ui::MediaSlider*> _slider;
	base::unique_qptr<Ui::NumberInput> _input;
	int _value = 0;
	bool _valueOver = false;
	Ui::Animations::Simple _number;
	Ui::Animations::Simple _handle;

};

PercentSlider::PercentSlider(QWidget *parent, PercentSliderArgs &&args)
: RpWidget(parent)
, _min(args.min)
, _max(std::max(args.max, args.min + 1))
, _changed(std::move(args.changed))
, _title(Ui::CreateChild<Ui::FlatLabel>(
	this,
	std::move(args.title),
	st::defaultFlatLabel))
, _slider(Ui::CreateChild<Ui::MediaSlider>(
	this,
	st::autoDownloadLimitSlider))
, _value(std::clamp(args.current, _min, _max)) {
	setMouseTracking(true);
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_slider->resize(_slider->width(), st::autoDownloadLimitSlider.seekSize.height());
	_slider->setPseudoDiscrete(
		_max - _min + 1,
		[=](int index) { return _min + index; },
		_value,
		[=](int value) {
			_handle.stop();
			setValue(value, false);
		},
		[=](int value) {
			setValue(value, false);
		});
}

int PercentSlider::titleHeight() const {
	return std::max(
		_title->height(),
		st::whitegramPercentInput.heightMin) + style::ConvertScale(6);
}

int PercentSlider::resizeGetHeight(int newWidth) {
	const auto &padding = st::settingsButtonNoIcon.padding;
	const auto inner = newWidth - padding.left() - padding.right();
	const auto valueWidth = style::ConvertScale(64);
	_title->resizeToWidth(std::max(inner - valueWidth, 1));
	const auto top = style::ConvertScale(8);
	_title->moveToLeft(
		padding.left(),
		top + (titleHeight() - _title->height()) / 2);
	const auto sliderTop = top + titleHeight();
	const auto sliderHeight = st::autoDownloadLimitSlider.seekSize.height()
		+ style::ConvertScale(12);
	_slider->resizeToWidth(inner);
	_slider->moveToLeft(
		padding.left(),
		sliderTop + (sliderHeight - _slider->height()) / 2);
	if (_input) {
		_input->setGeometry(valueRect());
	}
	return sliderTop + sliderHeight + style::ConvertScale(4);
}

QRect PercentSlider::valueRect() const {
	const auto &padding = st::settingsButtonNoIcon.padding;
	const auto width = style::ConvertScale(64);
	const auto height = st::whitegramPercentInput.heightMin;
	const auto top = style::ConvertScale(8) + (titleHeight() - height) / 2;
	return QRect(this->width() - padding.right() - width, top, width, height);
}

float64 PercentSlider::progressOf(int value) const {
	return (value - _min) / float64(_max - _min);
}

QString PercentSlider::shownText() const {
	return QString::number(int(base::SafeRound(_number.value(_value)))) + '%';
}

void PercentSlider::paintEvent(QPaintEvent *e) {
	if (_input) {
		return;
	}
	auto p = QPainter(this);
	const auto rect = valueRect();
	if (_valueOver) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		const auto radius = st::whitegramPercentInput.borderRadius;
		p.drawRoundedRect(rect, radius, radius);
	}
	const auto &margins = st::whitegramPercentInput.textMargins;
	p.setFont(st::settingsScaleLabel.style.font);
	p.setPen(st::windowActiveTextFg);
	p.drawText(
		rect.marginsRemoved(margins),
		shownText(),
		style::al_right);
}

void PercentSlider::mouseMoveEvent(QMouseEvent *e) {
	setValueOver(valueRect().contains(e->pos()));
}

void PercentSlider::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && valueRect().contains(e->pos())) {
		startEditing();
	}
}

void PercentSlider::leaveEventHook(QEvent *e) {
	setValueOver(false);
}

void PercentSlider::setValueOver(bool over) {
	if (_valueOver == over) {
		return;
	}
	_valueOver = over;
	setCursor(over ? style::cur_pointer : style::cur_default);
	update(valueRect());
}

void PercentSlider::setValue(int value, bool moveHandle) {
	value = std::clamp(value, _min, _max);
	if (moveHandle) {
		_handle.start(
			[=](float64 progress) { _slider->setValue(progress); },
			_slider->value(),
			progressOf(value),
			kHandleDuration,
			anim::easeOutCubic);
	}
	if (value == _value) {
		return;
	}
	const auto from = _number.value(_value);
	_value = value;
	_number.start(
		[=] { update(valueRect()); },
		from,
		value,
		kNumberDuration,
		anim::easeOutCubic);
	if (_changed) {
		_changed(value);
	}
}

void PercentSlider::startEditing() {
	if (_input) {
		return;
	}
	_input = base::make_unique_q<Ui::NumberInput>(
		this,
		st::whitegramPercentInput,
		rpl::single(QString()),
		QString::number(_value),
		_max);
	const auto input = _input.get();
	input->setGeometry(valueRect());
	input->show();
	input->selectAll();
	input->setFocus();
	update();
	connect(input, &Ui::MaskedInputField::submitted, [=] {
		finishEditing(true);
	});
	connect(input, &Ui::MaskedInputField::cancelled, [=] {
		finishEditing(false);
	});
	connect(input, &Ui::MaskedInputField::blurred, [=] {
		finishEditing(true);
	});
}

void PercentSlider::finishEditing(bool apply) {
	if (!_input) {
		return;
	}
	const auto text = _input->getLastText().trimmed();
	// WHY: the field emitted the signal we are in, so delete it later.
	const auto input = _input.release();
	input->hide();
	input->deleteLater();
	auto ok = false;
	const auto typed = text.toInt(&ok);
	if (apply && ok) {
		setValue(typed, true);
	}
	update();
}

} // namespace

object_ptr<Ui::RpWidget> CreatePercentSlider(
		QWidget *parent,
		PercentSliderArgs &&args) {
	return object_ptr<PercentSlider>(parent, std::move(args));
}

} // namespace AyuUi
