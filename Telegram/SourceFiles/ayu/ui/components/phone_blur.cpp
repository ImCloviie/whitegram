// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/phone_blur.h"

#include "ayu/ayu_settings.h"
#include "base/algorithm.h"
#include "base/event_filter.h"
#include "styles/style_ayu_styles.h"
#include "styles/palette.h"
#include "ui/ui_utility.h"
#include "ui/widgets/labels.h"

#include <crl/crl_time.h>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPaintEvent>

#include <cmath>
#include <random>

namespace Ayu::UI {
namespace {

constexpr auto kRevealDuration = crl::time(180);
constexpr auto kParticleCount = 48;

[[nodiscard]] QColor StarTint(std::mt19937 &rng) {
	const auto roll = std::uniform_real_distribution<float64>(0., 1.)(rng);
	if (roll < 0.7) {
		return QColor(235, 240, 255); // star white
	} else if (roll < 0.9) {
		return QColor(150, 190, 255); // cool blue
	}
	return QColor(255, 215, 170); // warm giant
}

} // namespace

PhoneBlur::PhoneBlur(
	QWidget *parent,
	not_null<::Ui::FlatLabel*> phoneLabel)
: ::Ui::RpWidget(parent)
, _phoneLabel(phoneLabel) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	refreshMode();
	hide();

	_twinkle.init([=](crl::time) {
		update();
		return true;
	});

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
	updateAnimationState();
}

void PhoneBlur::updateGeometryToLabel() {
	const auto mapped = ::Ui::MapFrom(
		parentWidget(),
		_phoneLabel,
		QPoint(0, 0));
	// Hug the number itself, not the whole (usually full-width) label:
	// covering empty space next to the text looked like a stray stripe.
	const auto textWidth = std::max(
		0,
		std::min(_phoneLabel->width(), _phoneLabel->textMaxWidth()));
	setGeometryToLeft(
		mapped.x(),
		mapped.y(),
		textWidth,
		_phoneLabel->height());
	update();
}

void PhoneBlur::setRevealed(bool revealed, anim::type animated) {
	if (animated == anim::type::instant) {
		_revealing.stop();
		_revealed = revealed;
		update();
		updateAnimationState();
		return;
	}
	const auto target = revealed ? 0. : 1.;
	const auto current = _revealing.value(target);
	_revealed = revealed;
	_revealing.start([=](float64) {
		update();
		updateAnimationState();
	}, current, target, kRevealDuration, anim::easeOutCubic);
	update();
	updateAnimationState();
}

void PhoneBlur::ensureParticles() {
	if (!_particles.empty()) {
		return;
	}
	auto rng = std::mt19937(0x57A2F1EDu); // fixed seed: one calm sky
	auto unit = std::uniform_real_distribution<float64>(0., 1.);
	_particles.reserve(kParticleCount);
	for (auto i = 0; i < kParticleCount; ++i) {
		_particles.push_back({
			.x = unit(rng),
			.y = unit(rng),
			.drift = 0.003 + unit(rng) * 0.009, // cross in ~80..300 s
			.phase = unit(rng) * 6.2831853,
			.frequency = 1.0 + unit(rng) * 1.6,
			.size = 0.5 + unit(rng) * 0.8,
			.tint = StarTint(rng),
		});
	}
}

void PhoneBlur::updateAnimationState() {
	const auto covered = BlurEnabled() && !_revealed && isVisible();
	if (covered || _revealing.animating()) {
		_twinkle.start();
	} else {
		_twinkle.stop();
	}
}

void PhoneBlur::showEvent(QShowEvent *e) {
	RpWidget::showEvent(e);
	updateAnimationState();
}

void PhoneBlur::hideEvent(QHideEvent *e) {
	RpWidget::hideEvent(e);
	updateAnimationState();
}

void PhoneBlur::paintEvent(QPaintEvent *e) {
	if (!BlurEnabled() || _revealed) {
		return;
	}
	const auto progress = _revealing.value(1.);
	if (progress <= 0.) {
		return;
	}
	ensureParticles();

	const auto r = rect();
	const auto radius = style::ConvertScale(st::phoneBlurRadius);

	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setOpacity(progress);

	auto path = QPainterPath();
	path.addRoundedRect(QRectF(r), radius, radius);
	p.setClipPath(path);

	// Deep-space backdrop derived from the window background.
	const auto base = st::windowBg->c;
	p.fillRect(r, QColor(
		6 + base.red() / 8,
		9 + base.green() / 8,
		20 + base.blue() / 6));

	// Drifting, twinkling stars.
	const auto t = crl::now() / 1000.;
	const auto w = float64(r.width());
	const auto h = float64(r.height());
	p.setPen(Qt::NoPen);
	for (const auto &particle : _particles) {
		const auto x = std::fmod(particle.x + t * particle.drift, 1.) * w;
		const auto y = particle.y * h;
		const auto twinkle = 0.35
			+ 0.65 * (0.5 + 0.5 * std::sin(
				t * particle.frequency + particle.phase));
		const auto alpha = int(base::SafeRound(255 * twinkle));

		auto halo = particle.tint;
		halo.setAlpha(alpha / 5);
		p.setBrush(halo);
		p.drawEllipse(QPointF(x, y), particle.size * 2.2, particle.size * 2.2);

		auto core = particle.tint;
		core.setAlpha(alpha);
		p.setBrush(core);
		p.drawEllipse(QPointF(x, y), particle.size, particle.size);
	}
}

} // namespace Ayu::UI
