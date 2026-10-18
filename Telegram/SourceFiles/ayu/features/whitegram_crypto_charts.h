// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

#include "ui/rp_widget.h"

#include <QtGui/QColor>

#include <vector>

namespace Whitegram {

// A data point as consumed by the chart widgets.
struct ChartEntry {
	qint64 timestamp = 0; // Unix ms.
	double price = 0.0;
};

// Shared geometry/math for both chart widgets.
class ChartMath final {
public:
	// Aggregates points down to at most `maxPoints` by even sampling.
	[[nodiscard]] static std::vector<ChartEntry> Downsample(
		const std::vector<ChartEntry> &points,
		int maxPoints);

	[[nodiscard]] static double MinPrice(const std::vector<ChartEntry> &p);
	[[nodiscard]] static double MaxPrice(const std::vector<ChartEntry> &p);

	// Maps a value into [0, 1] with a 5% padding at both ends.
	[[nodiscard]] static double Normalize(
		double value,
		double min,
		double max);
};

// A small minimalist sparkline with an optional hover tooltip showing
// the price at the hovered moment. Designed to sit at the right side of
// a sidebar rate row.
class Sparkline final : public Ui::RpWidget {
public:
	Sparkline(QWidget *parent, int heightPx = 30);

	void setEntries(std::vector<ChartEntry> entries);
	void setColor(const QColor &line);
	void setPrecision(int precision);
	void setPricePrefix(const QString &prefix);

	[[nodiscard]] rpl::producer<> clicks() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	int indexAtPos(int x) const;
	void updateHover(int x);
	// Pixel points are recomputed from the live widget size on every
	// paint: the widget is placed via setGeometry after the entries are
	// set, so a cached polyline would stay collapsed at width 0.
	[[nodiscard]] std::vector<QPointF> computePoints() const;

	std::vector<ChartEntry> _entries;
	QColor _line = QColor(0x8c, 0xa5, 0xd8);
	int _precision = 2;
	QString _prefix;
	int _fixedHeight = 30;

	int _hoverIndex = -1;

	rpl::event_stream<> _clicks;

};

// A full-featured interactive chart shown inside a box: axis labels,
// vertical crosshair, price + date tooltip that follows the mouse, and
// a left-to-right reveal animation when data arrives.
class FullChart final : public Ui::RpWidget {
public:
	FullChart(QWidget *parent, int heightPx = 260);

	void setEntries(std::vector<ChartEntry> entries);
	void setError(const QString &message);
	void setColor(const QColor &line);
	void setPrecision(int precision);
	void setPricePrefix(const QString &prefix);

	[[nodiscard]] rpl::producer<> retryRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	int indexAtPos(int x) const;
	void updateHover(int x);

	std::vector<ChartEntry> _entries;
	QString _error;
	QColor _line = QColor(0x8c, 0xa5, 0xd8);
	int _precision = 2;
	QString _prefix;
	int _fixedHeight = 260;

	int _hoverIndex = -1;
	int _leftPad = 14;
	int _rightPad = 14;
	int _topPad = 18;
	int _bottomPad = 30;

	rpl::event_stream<> _retryRequests;

};

} // namespace Whitegram
