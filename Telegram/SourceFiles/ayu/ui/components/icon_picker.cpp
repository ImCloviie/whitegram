// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/icon_picker.h"

#include "tray.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/ayu_logo.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "styles/style_ayu_styles.h"
#include "ui/painter.h"
#include "window/main_window.h"

#ifdef Q_OS_WIN
#include "ayu/utils/windows_utils.h"
#endif

#include <algorithm>

namespace {

std::vector<QString> DefaultAyuAppIcons() {
	return {
		AyuAssets::DEFAULT_ICON,
		AyuAssets::ALT_ICON,
		AyuAssets::DISCORD_ICON,
		AyuAssets::SPOTIFY_ICON,
		AyuAssets::EXTERA_ICON,
		AyuAssets::NOTHING_ICON,
		AyuAssets::BARD_ICON,
		AyuAssets::YAPLUS_ICON,
		AyuAssets::WIN95_ICON,
		AyuAssets::CHIBI_ICON,
		AyuAssets::CHIBI2_ICON,
		AyuAssets::EXTERA2_ICON,
	};
}

void applyIcon() {
#ifdef Q_OS_WIN
	AyuAssets::loadAppIco();
	reloadAppIconFromTaskBar();
#endif

	Window::OverrideApplicationIcon(AyuAssets::currentAppLogo());
	Core::App().refreshApplicationIcon();
	Core::App().tray().updateIconCounters();
	Core::App().domain().notifyUnreadBadgeChanged();
}

} // namespace

std::vector<QString> WhitegramAppIcons() {
	return {
		AyuAssets::WHITEGRAM_ICON,
	};
}

IconPicker::IconPicker(QWidget *parent, std::vector<QString> icons)
	: RpWidget(parent)
	, _icons(icons.empty() ? DefaultAyuAppIcons() : std::move(icons)) {
	widthValue() | rpl::on_next([=](int w) {
		const auto cell = w / kColumns;
		const auto iconSize = st::iconPickerIconSize;
		const auto contentSize = iconSize + st::iconPickerImagePadding * 2;
		const auto h = rowsCount() * cell - (cell - contentSize);
		resize(w, h);
	}, lifetime());
}

void IconPicker::drawIcon(QPainter &p, const QImage &icon, int x, int y, float strokeOpacity) {
	{
		PainterHighQualityEnabler hq(p);
		p.save();
		p.setPen(QPen(st::boxDividerBg, 0));
		p.setBrush(QBrush(st::boxDividerBg));
		p.setOpacity(strokeOpacity);
		p.drawRoundedRect(
			x + st::iconPickerSelectedPadding,
			y + st::iconPickerSelectedPadding,
			st::iconPickerIconSize + st::iconPickerSelectedPadding * 2,
			st::iconPickerIconSize + st::iconPickerSelectedPadding * 2,
			st::iconPickerSelectedRounding,
			st::iconPickerSelectedRounding
		);
		p.restore();
	}

	const auto rect = QRect(
		x + st::iconPickerImagePadding,
		y + st::iconPickerImagePadding,
		st::iconPickerIconSize,
		st::iconPickerIconSize
	);
	p.drawImage(rect, icon);
}

int IconPicker::cellWidth() const {
	return width() / kColumns;
}

int IconPicker::rowsCount() const {
	const auto count = int(_icons.size());
	return (count + kColumns - 1) / kColumns;
}

void IconPicker::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto cell = cellWidth();
	const auto iconSize = st::iconPickerIconSize;

	for (int row = 0; row < rowsCount(); row++) {
		const auto columns = std::min(kColumns, int(_icons.size()) - row * kColumns);
		for (int i = 0; i < columns; i++) {
			auto const idx = i + row * kColumns;

			const auto &iconName = _icons[idx];
			if (iconName.isEmpty()) {
				continue;
			}
			QImage icon;
			if (const auto cached = _cachedIcons.find(iconName); cached != _cachedIcons.end()) {
				icon = cached->second;
			} else {
				icon = _cachedIcons[iconName] = AyuAssets::loadPreview(iconName);
			}
			auto opacity = 0.0f;
			const auto current = AyuSettings::getInstance().appIcon();
			if (iconName == _wasSelected) {
				opacity = 1.0f - _animation.value(1.0f);
			} else if (iconName == current
				|| iconName == AyuAssets::currentAppLogoName()) {
				opacity = _wasSelected.isEmpty() ? 1.0f : _animation.value(1.0f);
			}

			const auto x = i * cell + (cell - iconSize) / 2;
			const auto y = row * cell;

			drawIcon(p, icon, x, y, opacity);
		}
	}
}

void IconPicker::mousePressEvent(QMouseEvent *e) {
	const auto &settings = AyuSettings::getInstance();
	auto changed = false;

	const auto cell = cellWidth();
	const auto iconSize = st::iconPickerIconSize;

	for (int row = 0; row < rowsCount(); row++) {
		const auto columns = std::min(kColumns, int(_icons.size()) - row * kColumns);
		for (int i = 0; i < columns; i++) {
			auto const idx = i + row * kColumns;

			const auto x = i * cell + (cell - iconSize) / 2;
			const auto y = row * cell;

			if (e->pos().x() >= x && e->pos().x() <= x + iconSize
				&& e->pos().y() >= y && e->pos().y() <= y + iconSize) {
				const auto &iconName = _icons[idx];
				if (iconName.isEmpty()) {
					break;
				}

				if (settings.appIcon() != iconName) {
					_wasSelected = settings.appIcon();
					_animation.start(
						[=]
						{
							update();
						},
						0.0,
						1.0,
						200,
						anim::easeOutCubic
					);
					AyuSettings::getInstance().setAppIcon(iconName);
				}
				changed = true;
				break;
			}
		}
	}

	if (changed) {
		applyIcon();

		repaint();
	}
}
