// This file is part of Whitegram for Desktop.
#pragma once

#include <QtCore/QRectF>
#include <QtCore/QString>

class QPainter;

namespace AyuUi::Pictograms {

// Vector pictogram for a lyric word (a bone for "кости", a cigar for "труба"), 0 when none fits.
[[nodiscard]] int Find(const QString &normalized);

// Paints the pictogram near the word box; age is seconds since the word was sung.
void Paint(QPainter &p, int kind, const QRectF &word, double rowHeight, double age, int seed);

} // namespace AyuUi::Pictograms
