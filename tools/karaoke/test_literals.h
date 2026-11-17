#pragma once
#include <QtCore/QString>
// Standalone tests do not use the application's precompiled header.
inline QString operator""_q(const char16_t *text, std::size_t length) {
    return QString::fromUtf16(reinterpret_cast<const ushort*>(text), int(length));
}
