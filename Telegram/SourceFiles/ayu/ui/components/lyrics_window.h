// This is the source code of Whitegram for Desktop.
//
// Copyright @Whitegram, 2026
#pragma once

#include "ayu/ui/utils/lyrics_engine.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace AyuUi {

class LyricsWindow final : public Ui::RpWidget {
public:
	explicit LyricsWindow(QWidget *parent = nullptr);

	void setTrack(const QString &artist, const QString &title);
	void setLyrics(const Ayu::Ui::Lyrics::Data &data);
	// positionMs: current playback ms; active: index from ActiveLineIndex (-1 ok).
	void setPosition(long long positionMs, int active, double progress);

protected:
	void paintEvent(QPaintEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	struct WordLayout {
		QString word;
		double angle = 0.; // degrees
		double dy = 0.;
		double w = 0.;
	};

	[[nodiscard]] QFont lineFont(bool active) const;
	[[nodiscard]] std::vector<WordLayout> layoutWords(
		const QString &text,
		const QFont &font,
		int lineSeed) const;
	void paintLine(
		QPainter &p,
		const QString &text,
		const QRect &rect,
		bool active,
		double progress,
		double fade,
		int lineSeed) const;
	void ensureVisible(int active);
	void updateAutoScroll();

	Ayu::Ui::Lyrics::Data _data;
	QString _artist;
	QString _title;
	long long _positionMs = 0;
	int _active = -1;
	double _progress = -1.;
	double _scroll = 0.; // px from top
	double _scrollTarget = 0.;
	bool _autoFollow = true;
	crl::time _lineChangedAt = 0;
	Ui::Animations::Basic _anim;
};

} // namespace AyuUi
