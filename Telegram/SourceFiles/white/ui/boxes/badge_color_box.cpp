#include "white/ui/boxes/badge_color_box.h"

#include "white/features/whitegram_backend/whitegram_backend.h"
#include "white/utils/telegram_helpers.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "ui/abstract_button.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/toast/toast.h"
#include "ui/widgets/labels.h"

#include <QtCore/QPointer>

namespace Ui {
namespace {

constexpr auto kBadge = "prebeta";

struct BadgeColor {
	const char *id;
	const char *ru;
	const char *en;
};

// The same ids as the backend, the first one is the default.
constexpr auto kColors = std::array<BadgeColor, 5>{ {
	{ "orange", "Оранжевый", "Orange" },
	{ "blue", "Синий", "Blue" },
	{ "red", "Красный", "Red" },
	{ "green", "Зелёный", "Green" },
	{ "violet", "Фиолетовый", "Violet" },
} };

[[nodiscard]] QString Tr(const char *ru, const char *en) {
	const auto &lang = Lang::GetInstance();
	const auto isRu = lang.id().startsWith(u"ru"_q)
		|| lang.baseId().startsWith(u"ru"_q);
	return QString::fromUtf8(isRu ? ru : en);
}

[[nodiscard]] QString IconId(const QString &color) {
	return QString::fromLatin1(kBadge) + u'-' + color;
}

[[nodiscard]] QString CurrentColor(uint64 userId) {
	const auto badge = Whitegram::Backend::AdminBadgeFor(userId);
	const auto prefix = QString::fromLatin1(kBadge) + u'-';
	return (badge && badge->customId.startsWith(prefix))
		? badge->customId.mid(prefix.size())
		: QString::fromLatin1(kColors.front().id);
}

} // namespace

bool HasColorableBadge(uint64 userId) {
	const auto badge = Whitegram::Backend::AdminBadgeFor(userId);
	const auto base = QString::fromLatin1(kBadge);
	return badge
		&& badge->isCustom()
		&& (badge->customId == base
			|| badge->customId.startsWith(base + u'-'));
}

QString BadgeColorButtonText() {
	return Tr("Сменить цвет", "Change color");
}

void FillBadgeColorBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto userId = session->userId().bare;
	const auto initial = CurrentColor(userId);
	const auto selected = box->lifetime().make_state<
		rpl::variable<QString>>(initial);
	const auto saving = box->lifetime().make_state<bool>(false);

	box->setTitle(rpl::single(Tr("Цвет значка", "Badge color")));
	box->setWidth(st::boxWideWidth);
	box->addRow(object_ptr<FlatLabel>(
		box,
		rpl::single(Tr(
			"Значок pre-beta есть только у первых тестеров. "
			"Выбери ему цвет — его увидят все, у кого Whitegram.",
			"Only the first testers have the pre-beta badge. "
			"Pick its color, everyone using Whitegram will see it.")),
		st::boxLabel));

	const auto tileWidth = style::ConvertScale(58);
	const auto tileHeight = style::ConvertScale(80);
	const auto iconSize = style::ConvertScale(32);
	const auto radius = style::ConvertScale(12);
	const auto row = box->addRow(object_ptr<RpWidget>(box));
	row->resize(row->width(), tileHeight);

	auto tiles = std::vector<not_null<AbstractButton*>>();
	for (const auto &color : kColors) {
		const auto id = QString::fromLatin1(color.id);
		const auto name = Tr(color.ru, color.en);
		const auto iconId = IconId(id);
		Whitegram::Backend::LoadCustomBadgeIcon(iconId);

		const auto tile = CreateChild<AbstractButton>(row);
		tile->resize(tileWidth, tileHeight);
		tile->setClickedCallback([=] {
			*selected = id;
		});
		tile->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(tile);
			auto hq = PainterHighQualityEnabler(p);
			const auto active = (selected->current() == id);
			const auto half = style::ConvertScale(1) * 1.;
			const auto rect = QRectF(tile->rect()).marginsRemoved(
				{ half, half, half, half });
			p.setPen(active
				? QPen(st::activeButtonBg, style::ConvertScale(2))
				: QPen(st::windowBgOver, style::ConvertScale(1)));
			p.setBrush(tile->isOver() ? st::windowBgOver : st::windowBg);
			p.drawRoundedRect(rect, radius, radius);

			const auto icon = Whitegram::Backend::CustomBadgeIcon(iconId);
			const auto size = QSize(iconSize, iconSize);
			const auto top = style::ConvertScale(12);
			if (!icon.isNull()) {
				p.drawImage(
					QRect(QPoint((tileWidth - iconSize) / 2, top), size),
					FitBadgeImage(icon, size));
			}
			p.setFont(st::normalFont);
			p.setPen(active ? st::windowFg : st::windowSubTextFg);
			p.drawText(
				QRect(
					0,
					top + iconSize + style::ConvertScale(8),
					tileWidth,
					st::normalFont->height),
				name,
				style::al_center);
		}, tile->lifetime());
		rpl::merge(
			selected->changes() | rpl::to_empty,
			Whitegram::Backend::badgeUpdates()
		) | rpl::on_next([=] {
			tile->update();
		}, tile->lifetime());
		tiles.push_back(tile);
	}

	row->widthValue() | rpl::on_next([=](int width) {
		const auto count = int(tiles.size());
		const auto gap = (count > 1)
			? std::max(0, (width - count * tileWidth) / (count - 1))
			: 0;
		for (auto i = 0; i != count; ++i) {
			tiles[i]->moveToLeft(i * (tileWidth + gap), 0);
		}
	}, row->lifetime());

	box->addButton(tr::lng_settings_save(), [=] {
		const auto color = selected->current();
		if (color == initial) {
			box->closeBox();
			return;
		} else if (*saving) {
			return;
		}
		*saving = true;
		const auto weak = QPointer<GenericBox>(box.get());
		Whitegram::Backend::SetBadgeColor(session, color, [=](QString error) {
			if (!error.isEmpty()) {
				if (weak) {
					*saving = false;
				}
				Toast::Show(Tr(
					"Не удалось сменить цвет: ",
					"Could not change the color: ") + error);
				return;
			}
			Toast::Show(Tr("Цвет значка обновлён", "Badge color updated"));
			if (weak) {
				weak->closeBox();
			}
		});
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

} // namespace Ui
