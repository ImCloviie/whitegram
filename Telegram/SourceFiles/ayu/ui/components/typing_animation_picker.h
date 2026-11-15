// This file is part of Whitegram for Desktop.
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

class TypingAnimationPicker final : public Ui::RpWidget {
public:
	struct Args {
		std::vector<rpl::producer<QString>> names;
		rpl::producer<QString> sample;
		int current = 0;
		Fn<void(int)> chosen;
	};

	TypingAnimationPicker(QWidget *parent, Args &&args);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] int columns(int width) const;
	[[nodiscard]] int cardHeight() const;
	[[nodiscard]] QRect cardRect(int index) const;
	[[nodiscard]] int indexAt(QPoint point) const;
	void setHovered(int index);
	void paintCard(QPainter &p, int index, crl::time now);
	void paintPreview(QPainter &p, int index, QRect area, crl::time now);

	std::vector<QString> _names;
	QString _sample;
	Fn<void(int)> _chosen;
	int _selected = 0;
	int _previous = -1;
	int _hovered = -1;
	int _pressed = -1;

	QFont _previewFont;
	crl::time _started = 0;
	Ui::Animations::Basic _loop;
	Ui::Animations::Simple _selection;

};
