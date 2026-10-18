// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#include "ayu/features/whitegram_crypto_charts.h"

#include "ui/painter.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainterPath>

#include <algorithm>

namespace Whitegram {
namespace {

[[nodiscard]] QString FormatPriceText(
		const QString &prefix,
		double price,
		int precision) {
	auto text = prefix
		+ QString::number(price, 'f', precision);
	return text;
}

[[nodiscard]] QString FormatDate(qint64 timestamp) {
	const auto dateTime = QDateTime::fromMSecsSinceEpoch(timestamp);
	return dateTime.toString(u"dd.MM HH:mm"_q);
}

} // namespace

std::vector<ChartEntry> ChartMath::Downsample(
		const std::vector<ChartEntry> &points,
		int maxPoints) {
	if (int(points.size()) <= maxPoints || points.empty()) {
		return points;
	}
	auto result = std::vector<ChartEntry>();
	result.reserve(maxPoints + 1);
	const auto step = double(points.size() - 1) / double(maxPoints - 1);
	for (auto i = 0; i != maxPoints; ++i) {
		const auto index = int(std::round(i * step));
		result.push_back(points[std::min(
			index,
			int(points.size()) - 1)]);
	}
	return result;
}

double ChartMath::MinPrice(const std::vector<ChartEntry> &p) {
	if (p.empty()) {
		return 0.0;
	}
	auto result = p.front().price;
	for (const auto &entry : p) {
		result = std::min(result, entry.price);
	}
	return result;
}

double ChartMath::MaxPrice(const std::vector<ChartEntry> &p) {
	if (p.empty()) {
		return 0.0;
	}
	auto result = p.front().price;
	for (const auto &entry : p) {
		result = std::max(result, entry.price);
	}
	return result;
}

double ChartMath::Normalize(double value, double min, double max) {
	if (max <= min) {
		return 0.5;
	}
	constexpr auto kPadding = 0.05;
	const auto span = max - min;
	const auto clamped = std::min(std::max(value, min), max);
	const auto raw = (clamped - min) / span;
	const auto padded = kPadding + raw * (1.0 - 2 * kPadding);
	return std::min(std::max(padded, 0.0), 1.0);
}

// --------------------------------------------------------------------
// Sparkline
// --------------------------------------------------------------------

Sparkline::Sparkline(QWidget *parent, int heightPx)
: RpWidget(parent)
, _fixedHeight(heightPx) {
	setMouseTracking(true);
	setCursor(style::cur_pointer);
}

void Sparkline::setEntries(std::vector<ChartEntry> entries) {
	_entries = ChartMath::Downsample(std::move(entries), 48);
	_hoverIndex = -1;
	update();
}

void Sparkline::setColor(const QColor &line) {
	_line = line;
	update();
}

void Sparkline::setPrecision(int precision) {
	_precision = precision;
	update();
}

void Sparkline::setPricePrefix(const QString &prefix) {
	_prefix = prefix;
	update();
}

rpl::producer<> Sparkline::clicks() const {
	return _clicks.events();
}

int Sparkline::resizeGetHeight(int newWidth) {
	return _fixedHeight;
}

std::vector<QPointF> Sparkline::computePoints() const {
	auto result = std::vector<QPointF>();
	if (_entries.empty() || width() <= 0 || height() <= 0) {
		return result;
	}

	const auto min = ChartMath::MinPrice(_entries);
	const auto max = ChartMath::MaxPrice(_entries);
	result.reserve(_entries.size());
	for (auto i = 0; i != int(_entries.size()); ++i) {
		const auto x = (_entries.size() > 1)
			? double(width()) * double(i)
				/ double(_entries.size() - 1)
			: 0.0;
		const auto y = double(height())
			* (1.0 - ChartMath::Normalize(
				_entries[i].price,
				min,
				max));
		result.push_back(QPointF(x, y));
	}
	return result;
}

void Sparkline::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);

	const auto polyline = computePoints();
	if (polyline.size() < 2) {
		return;
	}

	// The area below the line, with a soft fill.
	auto fill = QPainterPath();
	fill.moveTo(polyline.front());
	for (const auto &point : polyline) {
		fill.lineTo(point);
	}
	fill.lineTo(polyline.back().x(), height());
	fill.lineTo(polyline.front().x(), height());
	fill.closeSubpath();

	auto fillGradient = QLinearGradient(0, 0, 0, height());
	fillGradient.setColorAt(
		0.0,
		QColor(_line.red(), _line.green(), _line.blue(), 70));
	fillGradient.setColorAt(
		1.0,
		QColor(_line.red(), _line.green(), _line.blue(), 0));
	p.fillPath(fill, QBrush(fillGradient));

	// The line itself.
	auto path = QPainterPath();
	path.moveTo(polyline.front());
	for (auto i = 1; i != polyline.size(); ++i) {
		path.lineTo(polyline[i]);
	}
	p.setPen(QPen(_line, 1.8));
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);

	// Hover marker + tooltip.
	if (_hoverIndex >= 0 && _hoverIndex < int(_entries.size())) {
		const auto &point = polyline[_hoverIndex];
		p.setPen(QPen(_line, 1.4));
		p.setBrush(_line);
		p.drawEllipse(point, 2.5, 2.5);

		const auto text = FormatPriceText(
			_prefix,
			_entries[_hoverIndex].price,
			_precision);
		p.setFont(st::defaultTextStyle.font);
		const auto metrics = QFontMetrics(p.font());
		const auto textWidth = metrics.horizontalAdvance(text);
		const auto textHeight = metrics.height();

		// Tooltip pinned above the sparkline row, centered on the point.
		auto tooltipRect = QRect(
			int(point.x()) - textWidth / 2 - 6,
			0,
			textWidth + 12,
			textHeight + 4);
		tooltipRect.moveTo(
			std::max(0, std::min(
				tooltipRect.x(),
				width() - tooltipRect.width())),
			0);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 180));
		p.drawRoundedRect(tooltipRect, 4, 4);
		p.setPen(QPen(Qt::white));
		p.drawText(
			tooltipRect,
			Qt::AlignCenter,
			text);
	}
}

int Sparkline::indexAtPos(int x) const {
	if (_entries.empty() || width() <= 0) {
		return -1;
	}
	const auto ratio = double(x) / double(width());
	const auto index = int(std::round(
		ratio * double(_entries.size() - 1)));
	return std::min(std::max(index, 0), int(_entries.size()) - 1);
}

void Sparkline::updateHover(int x) {
	const auto index = indexAtPos(x);
	if (_hoverIndex != index) {
		_hoverIndex = index;
		update();
	}
}

void Sparkline::mouseMoveEvent(QMouseEvent *e) {
	updateHover(e->pos().x());
}

void Sparkline::leaveEventHook(QEvent *e) {
	if (_hoverIndex != -1) {
		_hoverIndex = -1;
		update();
	}
}

void Sparkline::mousePressEvent(QMouseEvent *e) {
	_clicks.fire({});
}

// --------------------------------------------------------------------
// FullChart
// --------------------------------------------------------------------

FullChart::FullChart(QWidget *parent, int heightPx)
: RpWidget(parent)
, _fixedHeight(heightPx) {
	setMouseTracking(true);
}

void FullChart::setEntries(std::vector<ChartEntry> entries) {
	_entries = ChartMath::Downsample(std::move(entries), 200);
	_hoverIndex = -1;
	_error = QString();
	update();
}

void FullChart::setError(const QString &message) {
	_entries = {};
	_hoverIndex = -1;
	_error = message;
	update();
}

rpl::producer<> FullChart::retryRequests() const {
	return _retryRequests.events();
}

void FullChart::setColor(const QColor &line) {
	_line = line;
	update();
}

void FullChart::setPrecision(int precision) {
	_precision = precision;
	update();
}

void FullChart::setPricePrefix(const QString &prefix) {
	_prefix = prefix;
	update();
}

int FullChart::resizeGetHeight(int newWidth) {
	return _fixedHeight;
}

void FullChart::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setRenderHint(QPainter::Antialiasing);

	if (!_error.isEmpty()) {
		p.setFont(st::defaultTextStyle.font);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect().adjusted(0, 0, 0, -60),
			Qt::AlignCenter,
			_error);

		// "Retry" as a simple drawn hint; the whole chart area is a
		// click target for the retry.
		p.setPen(st::windowActiveTextFg);
		p.drawText(
			rect().adjusted(0, 40, 0, -20),
			Qt::AlignCenter,
			u"↻"_q);
		return;
	}

	if (_entries.size() < 2) {
		p.setFont(st::defaultTextStyle.font);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect(),
			Qt::AlignCenter,
			u"Loading..."_q);
		return;
	}

	const auto chartRect = QRect(
		_leftPad,
		_topPad,
		std::max(1, width() - _leftPad - _rightPad),
		std::max(1, height() - _topPad - _bottomPad));

	const auto min = ChartMath::MinPrice(_entries);
	const auto max = ChartMath::MaxPrice(_entries);

	// Horizontal grid lines + Y labels.
	p.setFont(st::defaultTextStyle.font);
	const auto gridCount = 4;
	for (auto i = 0; i <= gridCount; ++i) {
		const auto ratio = double(i) / double(gridCount);
		const auto y = chartRect.top()
			+ int(ratio * double(chartRect.height()));
		p.setPen(QPen(st::windowSubTextFg->c, 1, Qt::DotLine));
		p.drawLine(
			chartRect.left(),
			y,
			chartRect.right(),
			y);

		const auto value = max - (max - min) * ratio;
		const auto label = FormatPriceText(_prefix, value, _precision);
		p.setPen(st::windowSubTextFg);
		const auto metrics = QFontMetrics(p.font());
		p.drawText(
			QRect(0, y - metrics.height() / 2, _leftPad - 4, metrics.height()),
			Qt::AlignRight | Qt::AlignVCenter,
			label);
	}

	// X labels: first, middle, last.
	const auto labelFor = [&](const ChartEntry &entry) {
		return QDateTime::fromMSecsSinceEpoch(entry.timestamp)
			.toString(u"dd.MM HH:mm"_q);
	};
	const auto metrics = QFontMetrics(p.font());
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(
			chartRect.left(),
			height() - _bottomPad + 4,
			200,
			metrics.height()),
		Qt::AlignLeft | Qt::AlignVCenter,
		labelFor(_entries.front()));
	p.drawText(
		QRect(
			chartRect.right() - 200,
			height() - _bottomPad + 4,
			200,
			metrics.height()),
		Qt::AlignRight | Qt::AlignVCenter,
		labelFor(_entries.back()));
	p.drawText(
		QRect(
			chartRect.center().x() - 100,
			height() - _bottomPad + 4,
			200,
			metrics.height()),
		Qt::AlignHCenter | Qt::AlignVCenter,
		labelFor(_entries[_entries.size() / 2]));

	// The polyline over the chart rect.
	auto points = std::vector<QPointF>();
	points.reserve(_entries.size());
	for (const auto &entry : _entries) {
		const auto x = chartRect.left()
			+ double(&entry - &_entries.front())
				/ double(_entries.size() - 1)
				* chartRect.width();
		const auto y = chartRect.bottom()
			- ChartMath::Normalize(entry.price, min, max)
				* chartRect.height();
		points.push_back(QPointF(x, y));
	}

	// Area fill.
	auto fill = QPainterPath();
	fill.moveTo(points.front());
	for (const auto &point : points) {
		fill.lineTo(point);
	}
	fill.lineTo(points.back().x(), chartRect.bottom());
	fill.lineTo(points.front().x(), chartRect.bottom());
	fill.closeSubpath();
	auto fillGradient = QLinearGradient(
		0,
		chartRect.top(),
		0,
		chartRect.bottom());
	fillGradient.setColorAt(
		0.0,
		QColor(_line.red(), _line.green(), _line.blue(), 70));
	fillGradient.setColorAt(
		1.0,
		QColor(_line.red(), _line.green(), _line.blue(), 0));
	p.fillPath(fill, QBrush(fillGradient));

	// Line.
	auto path = QPainterPath();
	path.moveTo(points.front());
	for (auto i = 1; i != points.size(); ++i) {
		path.lineTo(points[i]);
	}
	p.setPen(QPen(_line, 1.8));
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);

	// Crosshair + tooltip.
	if (_hoverIndex >= 0 && _hoverIndex < int(_entries.size())) {
		const auto &point = points[_hoverIndex];
		const auto &entry = _entries[_hoverIndex];

		p.setPen(QPen(st::windowSubTextFg->c, 1, Qt::DashLine));
		p.drawLine(
			int(point.x()),
			chartRect.top(),
			int(point.x()),
			chartRect.bottom());
		p.setBrush(_line);
		p.setPen(QPen(_line, 1.8));
		p.drawEllipse(point, 3.5, 3.5);

		const auto priceText = FormatPriceText(
			_prefix,
			entry.price,
			_precision);
		const auto dateText = FormatDate(entry.timestamp);
		const auto text = priceText + u"  \u00b7  "_q + dateText;
		const auto textWidth = metrics.horizontalAdvance(text);
		auto tooltipRect = QRect(
			0,
			0,
			textWidth + 16,
			metrics.height() + 8);
		// Keep the tooltip inside the widget, following the mouse.
		auto tooltipX = int(point.x()) - tooltipRect.width() / 2;
		tooltipX = std::max(
			chartRect.left(),
			std::min(tooltipX, chartRect.right() - tooltipRect.width()));
		auto tooltipY = chartRect.top() + 6;
		if (point.y() - chartRect.top() < tooltipRect.height() + 12) {
			tooltipY = chartRect.bottom() - tooltipRect.height() - 6;
		}
		tooltipRect.moveTo(tooltipX, tooltipY);

		p.setPen(Qt::NoPen);
		p.setBrush(st::boxBg);
		p.drawRoundedRect(tooltipRect, 6, 6);
		p.setPen(QPen(_line));
		p.drawRoundedRect(tooltipRect, 6, 6);
		p.setPen(st::boxTextFg);
		p.drawText(tooltipRect, Qt::AlignCenter, text);
	}
}

int FullChart::indexAtPos(int x) const {
	if (_entries.empty() || width() <= 0) {
		return -1;
	}
	const auto chartLeft = _leftPad;
	const auto chartWidth = width() - _leftPad - _rightPad;
	if (chartWidth <= 0) {
		return -1;
	}
	const auto ratio = double(x - chartLeft) / double(chartWidth);
	const auto index = int(std::round(
		ratio * double(_entries.size() - 1)));
	return std::min(std::max(index, 0), int(_entries.size()) - 1);
}

void FullChart::updateHover(int x) {
	const auto index = indexAtPos(x);
	if (_hoverIndex != index) {
		_hoverIndex = index;
		update();
	}
}

void FullChart::mouseMoveEvent(QMouseEvent *e) {
	if (_error.isEmpty()) {
		updateHover(e->pos().x());
	}
}

void FullChart::mousePressEvent(QMouseEvent *e) {
	if (!_error.isEmpty()) {
		// Clicking anywhere in the failed chart retries the request.
		_retryRequests.fire({});
	}
}

void FullChart::leaveEventHook(QEvent *e) {
	if (_hoverIndex != -1) {
		_hoverIndex = -1;
		update();
	}
}

} // namespace Whitegram
