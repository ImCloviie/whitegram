// This is the source code of Whitegram for Desktop.
//
// Copyright @Whitegram, 2026
#include "ayu/ui/components/lyrics_window.h"

#include "ayu/ayu_settings.h"
#include "styles/style_widgets.h"
#include "ui/painter.h"

#include <QtGui/QWheelEvent>

#include <algorithm>
#include <cmath>

namespace AyuUi {
namespace {

constexpr auto kLineH = 34;
constexpr auto kActiveH = 46;
constexpr auto kPad = 20;

double hash01(int seed, int salt) {
	auto h = uint32(seed * 2654435761u + salt * 40503u);
	h ^= h >> 15;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	return (h % 1000) / 1000.;
}

} // namespace

LyricsWindow::LyricsWindow(QWidget *parent)
: RpWidget(parent) {
	setWindowTitle(QString::fromUtf8("Whitegram — Текст песни"));
	setAttribute(Qt::WA_DeleteOnClose, false);
	resize(400, 520);
	setMinimumSize(320, 360);

	_anim.init([=] {
		// Smooth-scroll interpolation + pulse repaint while visible.
		const auto diff = _scrollTarget - _scroll;
		if (std::abs(diff) > 0.5) {
			_scroll += diff * 0.18;
			update();
		} else if (diff != 0.) {
			_scroll = _scrollTarget;
			update();
		} else {
			// Keep pulsing the active line when pulse mode is on.
			if (AyuSettings::getInstance().musicLyricsAnimation() == 2) {
				update();
			}
		}
		return isVisible();
	});
}

void LyricsWindow::setTrack(const QString &artist, const QString &title) {
	_artist = artist;
	_title = title;
	update();
}

void LyricsWindow::setLyrics(const Ayu::Ui::Lyrics::Data &data) {
	_data = data;
	_active = -1;
	_scroll = 0.;
	_scrollTarget = 0.;
	_autoFollow = true;
	update();
}

void LyricsWindow::setPosition(long long positionMs, int active, double progress) {
	_positionMs = positionMs;
	_progress = progress;
	if (active != _active) {
		_active = active;
		_lineChangedAt = crl::now();
		ensureVisible(active);
	}
	update();
}

QFont LyricsWindow::lineFont(bool active) const {
	const auto scale = AyuSettings::getInstance().musicLyricsFontScale() / 100.;
	auto f = QFont();
	f.setPixelSize(int((active ? 19 : 15) * scale));
	f.setBold(active);
	return f;
}

std::vector<LyricsWindow::WordLayout> LyricsWindow::layoutWords(
		const QString &text,
		const QFont &font,
		int lineSeed) const {
	const auto scatter = AyuSettings::getInstance().musicLyricsArtScatter();
	auto out = std::vector<WordLayout>();
	const auto fm = QFontMetrics(font);
	for (const auto &w : text.split(u' ', Qt::SkipEmptyParts)) {
		auto wl = WordLayout();
		wl.word = w;
		wl.w = fm.horizontalAdvance(w);
		if (scatter) {
			const auto h1 = hash01(lineSeed, int(qHash(w)) & 0x7fffffff);
			const auto h2 = hash01(int(qHash(w)) & 0x7fffffff, lineSeed);
			wl.angle = (h1 - 0.5) * 10.; // -5..+5 degrees
			wl.dy = (h2 - 0.5) * 6.; // -3..+3 px
		}
		out.push_back(std::move(wl));
	}
	return out;
}

void LyricsWindow::paintLine(
		QPainter &p,
		const QString &text,
		const QRect &rect,
		bool active,
		double progress,
		double fade,
		int lineSeed) const {
	const auto mode = AyuSettings::getInstance().musicLyricsAnimation();
	const auto font = lineFont(active);
	p.setFont(font);

	const auto words = layoutWords(text, font, lineSeed);
	const auto spaceW = QFontMetrics(font).horizontalAdvance(QChar(u' ')) + 3;
	auto totalW = 0.;
	for (const auto &w : words) totalW += w.w;
	totalW += spaceW * std::max(0, int(words.size()) - 1);
	auto x = rect.x() + (rect.width() - totalW) / 2.;
	const auto cy = rect.center().y();

	const auto drawWords = [&](const QColor &color, const QRect *clip = nullptr) {
		p.save();
		if (clip) p.setClipRect(*clip);
		p.setPen(color);
		auto cx = x;
		for (const auto &w : words) {
			p.save();
			p.translate(cx + w.w / 2., cy + w.dy);
			if (w.angle != 0.) p.rotate(w.angle);
			// Shadow for readability.
			p.setPen(QColor(0, 0, 0, int(color.alpha() * 0.55)));
			p.drawText(QRectF(-w.w / 2. + 1, -font.pixelSize() / 2. + 1, w.w, font.pixelSize() + 8), Qt::AlignCenter, w.word);
			p.setPen(color);
			p.drawText(QRectF(-w.w / 2., -font.pixelSize() / 2., w.w, font.pixelSize() + 8), Qt::AlignCenter, w.word);
			p.restore();
			cx += w.w + spaceW;
		}
		p.restore();
	};

	if (!active) {
		auto c = QColor(150, 150, 155, int(190 * fade));
		drawWords(c);
		return;
	}
	// Active line by mode.
	if (mode == 0) { // karaoke fill
		drawWords(QColor(150, 150, 155, 200));
		if (progress >= 0.) {
			const auto fillW = totalW * std::clamp(progress, 0., 1.);
			auto clip = QRect(int(x), rect.y(), int(fillW), rect.height());
			drawWords(QColor(255, 255, 255, int(255 * fade)), &clip);
		} else {
			drawWords(QColor(255, 255, 255, int(255 * fade)));
		}
	} else if (mode == 1) { // fade + glow
		// Glow pass.
		for (auto i = 0; i < 4; ++i) {
			const auto a = int(28 * fade);
			p.save();
			p.setPen(QColor(120, 180, 255, a));
			// Slight offsets fake the bloom.
			auto cx = x;
			for (const auto &w : words) {
				p.save();
				p.translate(cx + w.w / 2. + (i - 1.5), cy + w.dy + (i % 2 ? 1 : -1));
				if (w.angle != 0.) p.rotate(w.angle);
				p.drawText(QRectF(-w.w / 2., -font.pixelSize() / 2., w.w, font.pixelSize() + 8), Qt::AlignCenter, w.word);
				p.restore();
				cx += w.w + spaceW;
			}
			p.restore();
		}
		drawWords(QColor(255, 255, 255, int(255 * fade)));
	} else { // pulse
		const auto t = crl::now() / 1000.;
		const auto s = 1. + 0.035 * std::sin(t * 3.2);
		p.save();
		p.translate(rect.center().x(), cy);
		p.scale(s, s);
		p.translate(-rect.center().x(), -cy);
		drawWords(QColor(255, 235, 170, int(255 * fade)));
		p.restore();
	}
}

void LyricsWindow::ensureVisible(int active) {
	if (active < 0 || !_autoFollow) {
		return;
	}
	// Each line i occupies kLineH, active occupies kActiveH.
	auto y = double(kPad + 54);
	for (auto i = 0; i < active; ++i) y += kLineH;
	_scrollTarget = std::max(0., y - height() / 2.);
	if (!_anim.animating()) _anim.start();
}

void LyricsWindow::updateAutoScroll() {
	ensureVisible(_active);
}

void LyricsWindow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.fillRect(rect(), QColor(18, 18, 22, 242));

	// Header.
	p.setPen(QColor(255, 255, 255));
	auto hf = QFont();
	hf.setPixelSize(15);
	hf.setBold(true);
	p.setFont(hf);
	const auto head = (_artist.isEmpty() && _title.isEmpty())
		? QString::fromUtf8("Текст песни")
		: _artist + QString::fromUtf8(" — ") + _title;
	p.drawText(QRect(kPad, 12, width() - 2 * kPad, 24), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(head, Qt::ElideRight, width() - 2 * kPad));
	p.setPen(QColor(140, 140, 148));
	auto sf = QFont();
	sf.setPixelSize(12);
	p.setFont(sf);
	const auto sub = _data.hasData
		? (_data.synced
			? QString::fromUtf8("синхронизированный текст • lrclib")
			: QString::fromUtf8("без таймингов — мягкая прокрутка"))
		: (_artist.isEmpty()
			? QString()
			: QString::fromUtf8("ищу текст…"));
	p.drawText(QRect(kPad, 36, width() - 2 * kPad, 18), Qt::AlignLeft | Qt::AlignVCenter, sub);

	if (!_data.hasData) {
		p.setPen(QColor(150, 150, 158));
		auto f = QFont();
		f.setPixelSize(14);
		p.setFont(f);
		p.drawText(rect(), Qt::AlignCenter,
			_artist.isEmpty()
				? QString::fromUtf8("Включите музыку — текст появится здесь")
				: QString::fromUtf8("Текст не найден :("));
		return;
	}

	const auto now = crl::now();
	const auto sinceChange = now - _lineChangedAt;
	const auto changeFade = std::clamp(sinceChange / 280., 0., 1.);

	auto y = double(kPad + 54) - _scroll;
	for (auto i = 0; i < int(_data.lines.size()); ++i) {
		const auto isActive = (i == _active);
		const auto h = isActive ? kActiveH : kLineH;
		if (y + h < 60 || y > height()) {
			y += h;
			continue;
		}
		auto fade = 1.;
		if (isActive) {
			fade = 0.35 + 0.65 * changeFade;
		} else if (i == _active - 1 || i == _active + 1) {
			fade = 0.9;
		} else {
			fade = 0.75;
		}
		paintLine(p, _data.lines[i].text, QRect(kPad, int(y), width() - 2 * kPad, h), isActive, isActive ? _progress : -1., fade, i * 131 + 7);
		y += h;
	}
}

void LyricsWindow::wheelEvent(QWheelEvent *e) {
	_autoFollow = false;
	_scrollTarget = std::clamp(_scrollTarget - e->angleDelta().y() * 0.6, 0., 4000.);
	_scroll = std::clamp(_scroll - e->angleDelta().y() * 0.6, 0., 4000.);
	// Return to auto-follow when scrolled near the active line.
	if (_active >= 0) {
		auto y = double(kPad + 54);
		for (auto i = 0; i < _active; ++i) y += kLineH;
		if (std::abs((y - height() / 2.) - _scroll) < 60.) _autoFollow = true;
	}
	update();
	if (!_anim.animating()) _anim.start();
	e->accept();
}

void LyricsWindow::mousePressEvent(QMouseEvent *e) {
	// Click toggles auto-follow back on.
	_autoFollow = true;
	updateAutoScroll();
	if (!_anim.animating()) _anim.start();
	QWidget::mousePressEvent(e);
}

void LyricsWindow::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	updateAutoScroll();
}

} // namespace AyuUi
