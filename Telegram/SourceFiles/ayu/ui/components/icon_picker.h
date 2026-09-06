// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

#include <vector>

[[nodiscard]] std::vector<QString> WhitegramAppIcons();

class IconPicker : public Ui::RpWidget
{
public:
	explicit IconPicker(
		QWidget *parent,
		std::vector<QString> icons = {});
	~IconPicker() = default;

	static constexpr int kColumns = 4;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void drawIcon(QPainter &p, const QImage &icon, int x, int y, float strokeOpacity);
	[[nodiscard]] int cellWidth() const;
	[[nodiscard]] int rowsCount() const;

	std::vector<QString> _icons;
	Ui::Animations::Simple _animation;
	QString _wasSelected;
	std::unordered_map<QString, QImage> _cachedIcons;

};
