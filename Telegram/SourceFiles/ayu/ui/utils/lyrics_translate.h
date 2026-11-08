// This file is part of Whitegram for Desktop.
#pragma once

#include <QtCore/QString>

#include <vector>

namespace Ayu::Ui::Lyrics {

struct Translation {
	bool ok = false;
	QString source;
	QString target;
	std::vector<QString> lines;
	std::vector<QString> keys;
};

[[nodiscard]] bool LooksLike(const std::vector<QString> &lines, const QString &language);
[[nodiscard]] Translation TranslateSync(
	const std::vector<QString> &lines,
	const std::vector<QString> &keys,
	const QString &target);

} // namespace Ayu::Ui::Lyrics
