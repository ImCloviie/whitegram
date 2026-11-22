// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "white/ui/settings/settings_main.h"

#include "settings/sections/settings_main.h"
#include "lang_auto.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session_settings.h"
#include "white/white_settings.h"
#include "white/features/custom_profile_background/custom_profile_background.h"
#include "white/features/connection_bypass/connection_bypass.h"
#include "white/features/tg_ws_proxy/tg_ws_proxy.h"
#include "white/ui/whitegram_startup_animation.h"
#include "ui/widgets/checkbox.h"
#include "white/features/glass_effect/glass_effect.h"
#include "white/features/self_destruct/self_destruct.h"
#include "white/features/self_destruct/self_destruct_box.h"
#include "white/features/custom_archives/custom_archives.h"
#include "white/ui/white_logo.h"
#include "white/ui/components/icon_picker.h"
#include "white/ui/components/percent_slider.h"
#include "white/ui/settings/custom_archives_box.h"
#include "white/ui/settings/settings_appearance.h"
#include "white/ui/settings/settings_white.h"
#include "white/ui/settings/settings_chats.h"
#include "white/ui/settings/settings_filters.h"
#include "white/ui/settings/settings_general.h"
#include "core/version.h"
#include "core/update_checker.h"
#include "white/features/whitegram_backend/whitegram_backend.h"
#include "white/features/whitegram_plugins/whitegram_plugins.h"
#include "white/features/whitegram_plugins/whitegram_plugins_ui.h"
#include "white/utils/telegram_helpers.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_white_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/text/format_values.h"
#include "styles/style_boxes.h"
#include "white/ui/settings/white_builder.h"
#include "white/ui/components/typing_animation_picker.h"
#include "ui/toast/toast.h"
#include "ui/painter.h"
#include "lottie/lottie_icon.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/layers/generic_box.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_dialogs.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/checkbox.h"
#include "window/window_peer_menu.h"
#include "data/data_peer.h"
#include "data/data_thread.h"
#include "boxes/peer_list_box.h"
#include "styles/style_white_icons.h"
#include <algorithm>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QListWidget>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QSettings>
#include <QtGui/QClipboard>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "window/window_controller.h"

#include <QDesktopServices>
#include <QFileInfo>

#include "core/application.h"
#include "core/file_utilities.h"

namespace Settings {

using namespace Builder;
using namespace WhiteBuilder;

namespace {

namespace Background = WhiteFeatures::CustomProfileBackground;

void BuildLogo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto logo = object_ptr<Ui::RpWidget>(ctx.container);
		const auto logoRaw = logo.data();
		logoRaw->resize(
			QSize(st::settingsCloudPasswordIconSize,
				st::settingsCloudPasswordIconSize));
		logoRaw->setNaturalWidth(st::settingsCloudPasswordIconSize);
		logoRaw->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(logoRaw);
			const auto image = WhiteAssets::currentAppLogoPad();
			if (!image.isNull()) {
				const auto size = st::settingsCloudPasswordIconSize;
				const auto scaled = image.scaled(
					size * style::DevicePixelRatio(),
					size * style::DevicePixelRatio(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
				p.drawImage(QRect(0, 0, size, size), scaled);
			}
		}, logoRaw->lifetime());
		return { .widget = std::move(logo), .align = style::al_top };
	});
}

// Whitegram

void BuildWhitegramVersionInfo(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(
					QString("Whitegram v")
					+ QString::fromLatin1(AppVersionStr)),
				st::boxTitle),
			.align = style::al_top,
		};
	});

	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				rpl::single(Whitegram::Backend::BetaBadgeText()),
				st::centeredBoxLabel),
			.align = style::al_top,
		};
	});

	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<Ui::FlatLabel>(
				ctx.container,
				trWhitegram(
					QString::fromUtf8("Telegram Desktop с дополнительными функциями и настройками."),
					QString("Telegram Desktop with extra features and settings.")),
				st::centeredBoxLabel),
			.align = style::al_top,
		};
	});
}

void BuildCryptoSettings(SectionBuilder &builder) {
	auto white = WhiteSectionBuilder(builder);

	builder.addSubsectionTitle(
		trWhitegram(
			QString::fromUtf8("Курсы криптовалют"),
			QString("Cryptocurrency Rates")));

	white.addSettingToggle({
		.id = u"whitegram/showCryptoInDrawer"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать в боковом меню"),
			QString("Show in side menu")),
		.getter = &WhiteSettings::showCryptoInDrawer,
		.setter = &WhiteSettings::setShowCryptoInDrawer,
	});

	auto *settings = &WhiteSettings::getInstance();

	auto getFiatOptions = [] {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		return std::vector<QString>{
			isRu ? QString::fromUtf8("Доллар США (USD)") : QString("US Dollar (USD)"),
			isRu ? QString::fromUtf8("Евро (EUR)") : QString("Euro (EUR)"),
			isRu ? QString::fromUtf8("Российский рубль (RUB)") : QString("Russian Ruble (RUB)"),
		};
	};

	auto currentFiat = rpl::combine(
		WhiteSettings::getInstance().cryptoFiatCurrencyValue(),
		trWhitegram(u"ru"_q, u"en"_q)
	) | rpl::map([=](int val, const QString &) {
		const auto options = getFiatOptions();
		return (val >= 0 && val < int(options.size()))
			? options[val]
			: options[0];
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"whitegram/cryptoFiatCurrency"_q,
		.title = trWhitegram(
			QString::fromUtf8("Валюта отображения"),
			QString("Display Currency")),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentFiat),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						WhiteSettings::getInstance().setCryptoFiatCurrency(index);
					};
					const auto options = getFiatOptions();
					SingleChoiceBox(box, {
						.title = trWhitegram(
							QString::fromUtf8("Валюта отображения"),
							QString("Display Currency")),
						.options = options,
						.initialSelection = settings->cryptoFiatCurrency(),
						.callback = save,
					});
				}));
		},
	});

	white.addSectionDivider();

	builder.addSubsectionTitle(
		trWhitegram(
			QString::fromUtf8("Отображаемые монеты"),
			QString("Displayed Coins")));

	white.addSettingToggle({
		.id = u"whitegram/showBTC"_q,
		.title = rpl::single(QString("Bitcoin (BTC)")),
		.getter = &WhiteSettings::showBTC,
		.setter = &WhiteSettings::setShowBTC,
	});
	white.addSettingToggle({
		.id = u"whitegram/showETH"_q,
		.title = rpl::single(QString("Ethereum (ETH)")),
		.getter = &WhiteSettings::showETH,
		.setter = &WhiteSettings::setShowETH,
	});
	white.addSettingToggle({
		.id = u"whitegram/showTON"_q,
		.title = rpl::single(QString("Gram (prev. Toncoin)")),
		.getter = &WhiteSettings::showTON,
		.setter = &WhiteSettings::setShowTON,
	});
	white.addSettingToggle({
		.id = u"whitegram/showSOL"_q,
		.title = rpl::single(QString("Solana (SOL)")),
		.getter = &WhiteSettings::showSOL,
		.setter = &WhiteSettings::setShowSOL,
	});
	white.addSettingToggle({
		.id = u"whitegram/showBNB"_q,
		.title = rpl::single(QString("BNB")),
		.getter = &WhiteSettings::showBNB,
		.setter = &WhiteSettings::setShowBNB,
	});

	white.addSectionDivider();

	builder.addSubsectionTitle(
		trWhitegram(
			QString::fromUtf8("Обычные валюты (к текущей)"),
			QString("Fiat Currencies (to current)")));

	white.addSettingToggle({
		.id = u"whitegram/showFiatUSD"_q,
		.title = trWhitegram(
			QString::fromUtf8("Доллар США (USD)"),
			QString("US Dollar (USD)")),
		.getter = &WhiteSettings::showFiatUSD,
		.setter = &WhiteSettings::setShowFiatUSD,
	});
	white.addSettingToggle({
		.id = u"whitegram/showFiatEUR"_q,
		.title = trWhitegram(
			QString::fromUtf8("Евро (EUR)"),
			QString("Euro (EUR)")),
		.getter = &WhiteSettings::showFiatEUR,
		.setter = &WhiteSettings::setShowFiatEUR,
	});
	white.addSettingToggle({
		.id = u"whitegram/showFiatRUB"_q,
		.title = trWhitegram(
			QString::fromUtf8("Российский рубль (RUB)"),
			QString("Russian Ruble (RUB)")),
		.getter = &WhiteSettings::showFiatRUB,
		.setter = &WhiteSettings::setShowFiatRUB,
	});

	white.addSectionDivider();
	builder.addSubsectionTitle(
		trWhitegram(
			QString::fromUtf8("Настройки графиков"),
			QString("Chart Settings")));

	white.addSettingToggle({
		.id = u"whitegram/showCryptoCharts"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать мини-графики"),
			QString("Show mini charts")),
		.getter = &WhiteSettings::showCryptoCharts,
		.setter = &WhiteSettings::setShowCryptoCharts,
	});
	white.addSettingToggle({
		.id = u"whitegram/showCryptoChanges"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать изменение за 24 часа"),
			QString("Show 24-hour change")),
		.getter = &WhiteSettings::showCryptoChanges,
		.setter = &WhiteSettings::setShowCryptoChanges,
	});

	const auto chartDays = WhiteSettings::getInstance().cryptoChartDays();
	const auto chartDaysIndex = (chartDays == 1)
		? 0
		: (chartDays == 7)
			? 1
			: (chartDays == 30)
				? 2
				: 3;
	white.addChooseButton({
		.id = u"whitegram/cryptoChartDays"_q,
		.title = trWhitegram(
			QString::fromUtf8("Период мини-графиков"),
			QString("Mini chart period")),
		.boxTitle = trWhitegram(
			QString::fromUtf8("Период мини-графиков"),
			QString("Mini chart period")),
		.initialSelection = chartDaysIndex,
		.options = {
			QString::fromUtf8("24 часа"),
			QString::fromUtf8("7 дней"),
			QString::fromUtf8("30 дней"),
			QString::fromUtf8("90 дней"),
		},
		.setter = [](int index) {
			static constexpr int kDays[] = { 1, 7, 30, 90 };
			if (index >= 0 && index < 4) {
				WhiteSettings::getInstance().setCryptoChartDays(kDays[index]);
			}
		},
	});
}

void BuildWhitegramLinks(SectionBuilder &builder) {
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(tr::white_LinksHeader());

	const auto controller = builder.controller();

	builder.addButton({
		.id = u"whitegram/channel"_q,
		.title = tr::white_LinksChannel(),
		.icon = { &st::menuIconChannel },
		.label = rpl::single(QString("@whitegrambeta")),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("whitegrambeta"),
			});
		},
	});
	builder.addButton({
		.id = u"whitegram/support"_q,
		.title = trWhitegram(
			QString::fromUtf8("Поддержка"),
			QString("Support")),
		.icon = { &st::menuIconBot },
		.label = rpl::single(QString("@Whitegram_Support_bot")),
		.onClick = [=] {
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("Whitegram_Support_bot"),
			});
		},
	});

	builder.addSkip();
}

const auto kWhitegramCryptoMeta = BuildHelper({
	.id = WhitegramCrypto::Id(),
	.parentId = WhitegramGeneral::Id(),
	.title = QString::fromUtf8("Курсы криптовалют"),
	.icon = &st::menuIconTradable,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	BuildCryptoSettings(builder);
	builder.addSkip();
});

const auto kWhitegramTimeSpentMeta = BuildHelper({
	.id = WhitegramTimeSpent::Id(),
	.parentId = WhitegramGeneral::Id(),
	.title = QString::fromUtf8("Счетчик времени"),
	.icon = &st::menuIconTimer,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	rpl::producer<QString> timeText = rpl::combine(
		WhiteSettings::getInstance().timeSpentValue(builder.session()->userId().bare),
		WhiteSettings::getInstance().timeSpentFormatValue(),
		trWhitegram(QString::fromUtf8("ru"), QString("en"))
	) | rpl::map([](int64 minutes, int format, const QString &) {
		return QString::fromUtf8("Проведено в Whitegram: ")
			+ formatTimeSpent(minutes, format);
	});
	builder.addButton({
		.id = u"whitegram/timespent"_q,
		.title = std::move(timeText),
		.icon = { &st::menuIconTimer },
	});
	
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	auto white = WhiteSectionBuilder(builder);
	white.addSettingToggle({
		.id = u"whitegram/showTimeSpentInDrawer"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать в боковом меню"),
			QString("Show in side menu")),
		.getter = &WhiteSettings::showTimeSpentInDrawer,
		.setter = &WhiteSettings::setShowTimeSpentInDrawer,
	});

	const auto settings = &WhiteSettings::getInstance();

	const auto getFormatOptions = [] {
		return std::vector<QString>{
			QString::fromUtf8("Только часы (340 ч)"),
			QString::fromUtf8("Часы и минуты (340 ч. 11 мин.)"),
			QString::fromUtf8("Только дни (14 дн.)"),
			QString::fromUtf8("Дни и часы (14 дн. 3 ч.)"),
		};
	};

	auto currentFormat = rpl::combine(
		WhiteSettings::getInstance().timeSpentFormatValue(),
		trWhitegram(u"ru"_q, u"en"_q)
	) | rpl::map([=](int val, const QString &) {
		const auto options = getFormatOptions();
		return (val >= 0 && val < int(options.size()))
			? options[val]
			: options[0];
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"whitegram/timeSpentFormat"_q,
		.title = trWhitegram(
			QString::fromUtf8("Формат отображения"),
			QString("Display Format")),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentFormat),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						WhiteSettings::getInstance().setTimeSpentFormat(index);
					};
					SingleChoiceBox(box, {
						.title = trWhitegram(
							QString::fromUtf8("Формат отображения"),
							QString("Display Format")),
						.options = getFormatOptions(),
						.initialSelection = settings->timeSpentFormat(),
						.callback = save,
					});
				}));
		},
	});

	// The drawer counter label template.
	const auto getLabelOptions = [] {
		return std::vector<QString>{
			QString::fromUtf8("В приложении: 356 ч"),
			QString::fromUtf8("В Whitegram: 356 ч"),
			QString::fromUtf8("Залипание: 356 ч"),
			QString::fromUtf8("Онлайн: 356 ч"),
		};
	};
	auto currentLabel = WhiteSettings::getInstance().timeSpentLabelStyleValue(
	) | rpl::map([=](int val) {
		const auto options = getLabelOptions();
		return (val >= 0 && val < int(options.size()))
			? options[val]
			: options[0];
	});
	builder.addButton({
		.id = u"whitegram/timeSpentLabelStyle"_q,
		.title = trWhitegram(
			QString::fromUtf8("Текст в боковом меню"),
			QString("Side menu text")),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentLabel),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						WhiteSettings::getInstance().setTimeSpentLabelStyle(index);
					};
					SingleChoiceBox(box, {
						.title = trWhitegram(
							QString::fromUtf8("Текст в боковом меню"),
							QString("Side menu text")),
						.options = getLabelOptions(),
						.initialSelection = settings->timeSpentLabelStyle(),
						.callback = save,
					});
				}));
		},
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	white.addSettingToggle({
		.id = u"whitegram/showTimeSpentInProfile"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать время в моем профиле"),
			QString("Show time in my profile")),
		.getter = &WhiteSettings::showTimeSpentInProfile,
		.setter = &WhiteSettings::setShowTimeSpentInProfile,
	});
	builder.addSkip();
});

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

class UpdatesProgressBar final : public Ui::RpWidget {
public:
	explicit UpdatesProgressBar(QWidget *parent) : Ui::RpWidget(parent) {
		resize(280, st::whitegramUpdatesProgressHeight);
	}

	void setProgress(float64 ratio) {
		const auto next = std::clamp(ratio, 0., 1.);
		if (_ratio == next) {
			return;
		}
		_ratio = next;
		update();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::whitegramUpdatesProgressRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(rect(), radius, radius);
		if (_ratio <= 0.) {
			return;
		}
		auto gradient = QLinearGradient(0, 0, width(), 0);
		gradient.setStops({
			{ 0., st::groupCallLive1->c },
			{ 1., st::groupCallLive2->c },
		});
		p.setBrush(QBrush(std::move(gradient)));
		const auto filled = std::max(
			radius * 2,
			int(std::ceil(width() * _ratio)));
		p.drawRoundedRect(
			QRect(0, 0, filled, height()),
			radius,
			radius);
	}

private:
	float64 _ratio = 0.;

};

class BypassMirrorCard final : public Ui::RpWidget {
public:
	explicit BypassMirrorCard(QWidget *parent)
	: Ui::RpWidget(parent)
	, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
		_content->heightValue(
		) | rpl::on_next([=](int height) {
			const auto padding = st::whitegramBypassMirrorCardPadding;
			const auto full = padding.top() + height + padding.bottom();
			if (this->height() != full) {
				resize(width(), full);
			}
		}, lifetime());
	}

	[[nodiscard]] not_null<Ui::VerticalLayout*> content() const {
		return _content;
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::whitegramBypassMirrorCardRadius;
		const auto rect = this->rect();
		p.setPen(QPen(st::windowBgRipple, st::lineWidth));
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			rect - QMargins(st::lineWidth, st::lineWidth, st::lineWidth, st::lineWidth),
			radius,
			radius);

		auto gradient = QLinearGradient(0, 0, width(), 0);
		gradient.setStops({
			{ 0., st::groupCallLive1->c },
			{ 1., st::groupCallLive2->c },
		});
		QPainterPath clip;
		clip.addRoundedRect(rect, radius, radius);
		p.setClipPath(clip);
		p.fillRect(QRect(0, 0, width(), 3), QBrush(std::move(gradient)));
	}

	int resizeGetHeight(int newWidth) override {
		const auto padding = st::whitegramBypassMirrorCardPadding;
		const auto inner = newWidth - padding.left() - padding.right();
		if (inner > 0) {
			_content->resizeToWidth(inner);
		}
		_content->moveToLeft(padding.left(), padding.top(), newWidth);
		return padding.top() + _content->height() + padding.bottom();
	}

private:
	const not_null<Ui::VerticalLayout*> _content;

};

class ProfileBackgroundCard final : public Ui::RpWidget {
public:
	ProfileBackgroundCard(
		QWidget *parent,
		QString title,
		QString hint,
		uint64 peerId,
		::CustomProfileBackground::Zone zone,
		Fn<void()> pick,
		Fn<void()> remove)
	: Ui::RpWidget(parent)
	, _title(std::move(title))
	, _hint(std::move(hint))
	, _peerId(peerId)
	, _zone(zone)
	, _pick(std::move(pick))
	, _remove(std::move(remove))
	, _renderer([=] { update(); }) {
		setMouseTracking(true);
		WhiteSettings::getInstance().customProfileBackgroundChanges(
		) | rpl::on_next([=] {
			reload();
		}, lifetime());
		Whitegram::Backend::profileBackgroundUpdates(
		) | rpl::on_next([=](uint64 userId) {
			if (userId == _peerId) {
				reload();
			}
		}, lifetime());
		WhiteSettings::getInstance().profileBackgroundDimChanges(
		) | rpl::on_next([=](int) {
			update();
		}, lifetime());
		reload();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);

		const auto radius = st::profileBackgroundCardRadius;
		const auto rect = this->rect().marginsRemoved(
			st::profileBackgroundCardMargin);
		if (rect.isEmpty()) {
			return;
		}
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect, radius, radius);

		const auto inner = rect.marginsRemoved(st::profileBackgroundCardPadding);

		// Preview plate.
		const auto preview = QRect(
			inner.x(),
			inner.y(),
			inner.width(),
			st::profileBackgroundPreviewHeight);
		const auto previewRadius = st::profileBackgroundPreviewRadius;
		p.setBrush(st::windowBg);
		p.drawRoundedRect(preview, previewRadius, previewRadius);

		const auto path = BackgroundFilePath();
		if (!path.isEmpty()) {
			p.save();
			auto clip = QPainterPath();
			clip.addRoundedRect(
				QRectF(preview),
				previewRadius,
				previewRadius);
			p.setClipPath(clip);
			if (_renderer.paint(p, preview, preview.height(), 0, 0)) {
				// The same dimming as on the profile itself, so the
				// preview shows the designed look.
				Background::PaintInfoOverlay(p, preview, st::windowBg->c);
			} else {
				// A video whose first frame is not painted yet.
				p.fillRect(preview, st::windowBgOver);
			}
			p.restore();
		}

		// Title.
		p.setPen(st::windowActiveTextFg);
		p.setFont(st::semiboldFont);
		const auto textTop = preview.bottom()
			+ 1 + st::profileBackgroundZoneSkip;
		p.drawText(
			QRect(inner.x(), textTop, inner.width(), st::semiboldFont->height),
			_title,
			Qt::AlignVCenter | Qt::AlignLeft);

		// Status badge under the title.
		const auto has = !path.isEmpty();
		const auto badgeText = has
			? QString::fromUtf8("Настроен")
			: QString::fromUtf8("Не задан");
		p.setFont(st::defaultTextStyle.font);
		const auto badgeWidth = st::defaultTextStyle.font->width(badgeText)
			+ st::profileBackgroundBadgePadding.left()
			+ st::profileBackgroundBadgePadding.right();
		const auto badgeHeight = st::defaultTextStyle.font->height
			+ st::profileBackgroundBadgePadding.top()
			+ st::profileBackgroundBadgePadding.bottom();
		const auto badge = QRect(
			inner.x(),
			textTop + st::semiboldFont->height + 4,
			badgeWidth,
			badgeHeight);
		p.setBrush(has ? st::groupCallLive1 : st::windowSubTextFg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(badge, st::profileBackgroundBadgeRadius, st::profileBackgroundBadgeRadius);
		p.setPen(st::windowBg);
		p.drawText(badge, badgeText, Qt::AlignVCenter | Qt::AlignHCenter);

		// Remove button on the right of the badge, only when set.
		if (has) {
			const auto removeText = Lang::GetInstance().id().startsWith(u"ru"_q)
				? QString::fromUtf8("Удалить")
				: QString("Remove");
			const auto removeWidth = st::defaultTextStyle.font->width(removeText)
				+ st::profileBackgroundBadgePadding.left()
				+ st::profileBackgroundBadgePadding.right();
			const auto remove = QRect(
				inner.x() + inner.width() - removeWidth,
				badge.y(),
				removeWidth,
				badgeHeight);
			_removeRect = remove;
			p.setBrush(Qt::NoBrush);
			p.setPen(st::boxTextFgError->c);
			p.setFont(st::defaultTextStyle.font);
			p.drawText(remove, removeText, Qt::AlignVCenter | Qt::AlignHCenter);
		} else {
			_removeRect = QRect();
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			if (_remove && _removeRect.contains(e->pos())) {
				_remove();
			} else {
				_pick();
			}
		}
	}

	int resizeGetHeight(int newWidth) override {
		return st::profileBackgroundCardMargin.top()
			+ st::profileBackgroundCardPadding.top()
			+ st::profileBackgroundPreviewHeight
			+ st::profileBackgroundZoneSkip
			+ st::semiboldFont->height
			+ 4
			+ st::defaultTextStyle.font->height
			+ 2 * st::profileBackgroundBadgePadding.top()
			+ st::profileBackgroundCardPadding.bottom()
			+ st::profileBackgroundCardMargin.bottom();
	}

private:
	[[nodiscard]] QString BackgroundFilePath() const {
		return WhiteFeatures::CustomProfileBackground::BackgroundFilePath(
			_peerId,
			_zone);
	}

	void reload() {
		if (!BackgroundFilePath().isEmpty()) {
			_renderer.setPeer(_peerId, _zone);
		} else {
			_renderer.clear();
		}
		update();
	}

	QString _title;
	QString _hint;
	uint64 _peerId = 0;
	::CustomProfileBackground::Zone _zone = ::CustomProfileBackground::Zone::Full;
	Fn<void()> _pick;
	Fn<void()> _remove;
	QRect _removeRect;
	Background::Renderer _renderer;

};

class UpdatesHeroCard final : public Ui::RpWidget {
public:
	explicit UpdatesHeroCard(QWidget *parent)
	: Ui::RpWidget(parent)
	, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
		_content->heightValue(
		) | rpl::on_next([=](int height) {
			const auto padding = st::whitegramUpdatesCardPadding;
			const auto full = padding.top() + height + padding.bottom();
			if (this->height() != full) {
				resize(width(), full);
			}
		}, lifetime());
	}

	[[nodiscard]] not_null<Ui::VerticalLayout*> content() const {
		return _content;
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::whitegramUpdatesCardRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect(), radius, radius);

		const auto accent = st::whitegramUpdatesCardAccentHeight;
		auto gradient = QLinearGradient(0, 0, width(), 0);
		gradient.setStops({
			{ 0., st::groupCallLive1->c },
			{ 1., st::groupCallLive2->c },
		});

		QPainterPath clip;
		clip.addRoundedRect(rect(), radius, radius);
		p.setClipPath(clip);
		p.fillRect(QRect(0, 0, width(), accent), QBrush(std::move(gradient)));
	}

	int resizeGetHeight(int newWidth) override {
		const auto padding = st::whitegramUpdatesCardPadding;
		const auto inner = newWidth - padding.left() - padding.right();
		if (inner > 0) {
			_content->resizeToWidth(inner);
		}
		_content->moveToLeft(padding.left(), padding.top(), newWidth);
		return padding.top() + _content->height() + padding.bottom();
	}

private:
	const not_null<Ui::VerticalLayout*> _content;

};

class UpdatesLogo final : public Ui::RpWidget {
public:
	explicit UpdatesLogo(QWidget *parent) : Ui::RpWidget(parent) {
		const auto size = st::whitegramUpdatesLogoSize;
		resize(size, size);
		setNaturalWidth(size);
		paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(this);
			auto hq = PainterHighQualityEnabler(p);
			const auto size = st::whitegramUpdatesLogoSize;
			const auto outer = QRectF(0, 0, size, size);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBg);
			p.drawEllipse(outer);
			const auto image = WhiteAssets::currentAppLogoPad();
			if (!image.isNull()) {
				const auto inner = size - 2 * st::whitegramUpdatesLogoInnerInset;
				const auto scaled = image.scaled(
					inner * style::DevicePixelRatio(),
					inner * style::DevicePixelRatio(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
				const auto pos = (size - inner) / 2;
				p.drawImage(QRectF(pos, pos, inner, inner), scaled);
			}
		}, lifetime());
	}

};

#endif // !TDESKTOP_DISABLE_AUTOUPDATE

void BuildWhitegramUpdatesPage(SectionBuilder &builder) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (Core::UpdaterDisabled()) {
		builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
			return {
				.widget = object_ptr<Ui::FlatLabel>(
					ctx.container,
					trWhitegram(
						QString::fromUtf8(
							"Обновления недоступны в этой сборке."),
						QString("Updates are unavailable in this build.")),
					st::centeredBoxLabel),
			};
		});
		return;
	}

	builder.addSkip();
	builder.add([controller = builder.controller()](
			const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto card = Ui::CreateChild<UpdatesHeroCard>(
			ctx.container.get());
		const auto content = card->content();

		content->add(
			object_ptr<UpdatesLogo>(content),
			style::al_center);
		content->add(Ui::CreateSkipWidget(content, st::whitegramUpdatesLogoSkip));

		const auto versionBadge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::single(Whitegram::Backend::BetaBadgeText()),
				st::whitegramUpdatesVersionTitle),
			st::whitegramUpdatesBadgePadding);
		versionBadge->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(versionBadge);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			p.drawRoundedRect(
				versionBadge->rect(),
				st::whitegramUpdatesBadgeRadius,
				st::whitegramUpdatesBadgeRadius);
		}, versionBadge->lifetime());
		content->add(
			object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>::fromRaw(versionBadge),
			style::al_center);
		content->add(Ui::CreateSkipWidget(content, st::whitegramUpdatesBaseVersionSkip));
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				rpl::single(
					QString::fromLatin1("Telegram Desktop v")
					+ QString::fromLatin1(AppVersionStr)),
				st::whitegramUpdatesBaseVersion),
			style::al_center);
		content->add(Ui::CreateSkipWidget(content, st::whitegramUpdatesStatusSkip));

		const auto texts = Ui::CreateChild<rpl::event_stream<QString>>(
			content.get());
		const auto status = Ui::CreateChild<Ui::FlatLabel>(
			content,
			texts->events(),
			st::whitegramUpdatesStatus);
		status->setAttribute(Qt::WA_TransparentForMouseEvents);
		content->add(object_ptr<Ui::FlatLabel>::fromRaw(status), style::al_center);

		const auto progressWrap = content->add(
			object_ptr<Ui::SlideWrap<UpdatesProgressBar>>(
				content,
				object_ptr<UpdatesProgressBar>(content)));
		const auto progress = progressWrap->entity();
		progressWrap->show(anim::type::instant);

		content->add(Ui::CreateSkipWidget(content, st::whitegramUpdatesProgressSkip));

		const auto button = Ui::CreateChild<Ui::RoundButton>(
			content,
			rpl::single(QString()),
			st::whitegramUpdatesActionButton);
		button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		const auto pillRadius = st::whitegramUpdatesActionButton.height / 2;
		button->setCornerRadii(
			pillRadius,
			pillRadius,
			pillRadius,
			pillRadius);
		content->add(object_ptr<Ui::RoundButton>::fromRaw(button), style::al_center);

		const auto refreshButton = [=](const QString &label, bool enabled) {
			button->setText(rpl::single(label));
			button->setEnabled(enabled);
		};
		const auto showStatus = [=](const QString &text) {
			texts->fire_copy(text);
		};
		const auto setProgress = [=](int already, int total) {
			if (total > 0) {
				progress->setProgress(already / float64(total));
			} else {
				progress->setProgress(0.);
			}
		};
		const auto isRu = [] {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			return id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		};
		const auto latestText = [=] {
			return isRu()
				? u"У вас установлена последняя версия"_q
				: u"You have the latest version"_q;
		};
		const auto availableText = [=] {
			return isRu()
				? u"Доступно новое обновление"_q
				: u"A new update is available"_q;
		};
		const auto checkingText = [=] {
			return isRu()
				? u"Проверка обновлений..."_q
				: u"Checking for updates..."_q;
		};
		const auto readyText = [=] {
			return isRu()
				? u"Обновление готово к установке"_q
				: u"Update is ready to install"_q;
		};
		const auto updateLabel = [=] {
			return isRu()
				? u"Обновить Whitegram"_q
				: u"Update Whitegram"_q;
		};
		const auto installLabel = [=] {
			return isRu()
				? u"Установить и перезапустить"_q
				: u"Install and restart"_q;
		};
		const auto checkLabel = [=] {
			return isRu()
				? u"Проверить обновления"_q
				: u"Check for updates"_q;
		};
		const auto checkingButtonLabel = [=] {
			return isRu()
				? u"Проверка..."_q
				: u"Checking..."_q;
		};
		const auto applyState = [=] {
			Core::UpdateChecker checker;
			using State = Core::UpdateChecker::State;
			const auto state = checker.state();
			if (state == State::Ready) {
				showStatus(readyText());
				setProgress(0, 0);
				refreshButton(installLabel(), true);
			} else if (state == State::Download) {
				const auto done = checker.already();
				const auto total = checker.size();
				setProgress(done, total);
				if (total > 0) {
					showStatus(Ui::FormatDownloadText(done, total));
				} else {
					showStatus(checkingText());
				}
				refreshButton(updateLabel(), false);
			} else if (Whitegram::Backend::IsUpdateAvailable()) {
				showStatus(availableText());
				setProgress(0, 0);
				refreshButton(updateLabel(), true);
			} else {
				showStatus(latestText());
				setProgress(0, 0);
				refreshButton(checkLabel(), true);
			}
		};

		applyState();
		Whitegram::Backend::FetchClientInfo();
		Whitegram::Backend::CheckUpdateAvailability();

		Core::UpdateChecker checker;
		std::move(checker.checking()) | rpl::on_next([=] {
			showStatus(checkingText());
			setProgress(0, 0);
			refreshButton(checkingButtonLabel(), false);
		}, card->lifetime());
		std::move(checker.isLatest()) | rpl::on_next([=] {
			Whitegram::Backend::RefreshUpdateAvailability();
			applyState();
		}, card->lifetime());
		std::move(checker.failed()) | rpl::on_next([=] {
			applyState();
			controller->showToast(isRu()
				? u"Не удалось проверить обновления"_q
				: u"Failed to check for updates"_q);
		}, card->lifetime());
		std::move(checker.ready()) | rpl::on_next([=] {
			applyState();
		}, card->lifetime());
		std::move(checker.progress()) | rpl::on_next([=](
				Core::UpdateChecker::Progress value) {
			setProgress(value.already, value.size);
			if (value.size > 0) {
				showStatus(Ui::FormatDownloadText(
					value.already,
					value.size));
			}
			refreshButton(updateLabel(), false);
		}, card->lifetime());
		std::move(Whitegram::Backend::updateAvailableValue(
		)) | rpl::on_next([=] {
			applyState();
		}, card->lifetime());

		button->setClickedCallback([=] {
			Core::UpdateChecker checker;
			if (checker.state() == Core::UpdateChecker::State::Ready) {
				Whitegram::Backend::InstallReadyUpdate();
				return;
			}
			showStatus(checkingText());
			setProgress(0, 0);
			refreshButton(checkingButtonLabel(), false);
			Whitegram::Backend::FetchClientInfo();
			Whitegram::Backend::CheckUpdateAvailability();
			Whitegram::Backend::TriggerUpdateDownload();
		});

		content->widthValue() | rpl::on_next([=](int inner) {
			if (inner > 0) {
				button->setFullWidth(inner);
				progress->resize(inner, st::whitegramUpdatesProgressHeight);
			}
		}, card->lifetime());

		return {
			.widget = object_ptr<UpdatesHeroCard>::fromRaw(card),
			.margin = st::whitegramUpdatesCardMargin,
		};
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	auto white = WhiteSectionBuilder(builder);
	white.addSettingToggle({
		.id = u"whitegram/autoUpdate"_q,
		.title = trWhitegram(
			QString::fromUtf8("Автообновление"),
			QString("Automatic updates")),
		.getter = &WhiteSettings::whitegramAutoUpdate,
		.setter = &WhiteSettings::setWhitegramAutoUpdate,
	});

	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Whitegram будет автоматически проверять и скачивать "
			"обновления при запуске."),
		QString(
			"Whitegram will automatically check and download "
			"updates on startup.")));
	builder.addSkip();
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
}

// One line of a "what will be removed" list: a muted icon and text.
void AddSelfDestructInfoLine(
		not_null<Ui::VerticalLayout*> container,
		const style::icon &icon,
		rpl::producer<QString> text) {
	const auto &bst = st::settingsButton;
	const auto row = container->add(object_ptr<Ui::RpWidget>(container));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		row,
		std::move(text),
		st::whitegramSelfDestructInfoLabel);
	const auto vskip = st::whitegramSelfDestructInfoSkip;
	row->widthValue(
	) | rpl::on_next([=](int width) {
		const auto left = bst.padding.left();
		label->resizeToWidth(std::max(width - left - bst.padding.right(), 1));
		label->moveToLeft(left, vskip);
		row->resize(
			width,
			std::max(label->height(), icon.height()) + 2 * vskip);
	}, row->lifetime());
	row->paintOn([=, &icon](QPainter &p) {
		icon.paint(
			p,
			bst.iconLeft,
			(row->height() - icon.height()) / 2,
			row->width(),
			st::windowSubTextFg->c);
	});
}

void BuildWhitegramSelfDestructPage(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto passwordLabel = [](rpl::producer<bool> has) {
		return rpl::combine(
			std::move(has),
			rpl::single(rpl::empty) | rpl::then(
				Lang::GetInstance().idChanges() | rpl::to_empty)
		) | rpl::map([](bool has, auto) {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q)
				|| baseId.startsWith(u"ru"_q);
			return has
				? (isRu ? QString::fromUtf8("Задан") : QString("Set"))
				: (isRu ? QString::fromUtf8("Не задан") : QString("Not set"));
		});
	};
	using InfoLines = std::vector<
		std::pair<const style::icon*, std::pair<QString, QString>>>;
	const auto lines = [](InfoLines list) {
		return [=](const BuildContext &ctx) {
			v::match(ctx, [&](const WidgetContext &wctx) {
				for (const auto &[icon, text] : list) {
					AddSelfDestructInfoLine(
						wctx.container,
						*icon,
						trWhitegram(text.first, text.second));
				}
			}, [](const SearchContext &) {
			});
		};
	};

	// Header: a red circle with the auto-delete timer animation.
	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			const auto container = wctx.container;
			auto icon = CreateLottieIcon(
				container,
				{
					.name = u"ttl"_q,
					.sizeOverride = st::dialogsSuggestionDeniedAuthLottie,
				},
				st::dialogsSuggestionDeniedAuthLottieMargins,
				[] { return st::windowFgActive->c; });
			const auto animate = std::move(icon.animate);
			const auto iconRow = container->add(
				std::move(icon.widget),
				st::settingsBlockedListIconPadding,
				style::al_top);
			const auto circle = Ui::CreateChild<Ui::RpWidget>(
				container.get());
			circle->lower();
			circle->setAttribute(Qt::WA_TransparentForMouseEvents);
			circle->paintOn([=](QPainter &p) {
				auto hq = PainterHighQualityEnabler(p);
				const auto size = st::dialogsSuggestionDeniedAuthLottieCircle;
				p.setPen(Qt::NoPen);
				p.setBrush(st::attentionButtonFg);
				p.drawEllipse(QRect(
					QPoint(
						(circle->width() - size.width()) / 2,
						(circle->height() - size.height()) / 2),
					size));
			});
			iconRow->geometryValue(
			) | rpl::on_next([=](const QRect &g) {
				circle->setGeometry(g);
			}, circle->lifetime());
			animate(anim::repeat::once);

			Ui::AddSkip(container);
			container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					trWhitegram(
						QString::fromUtf8("Самоуничтожение"),
						QString("Self-destruct")),
					st::boxTitle),
				style::margins(),
				style::al_top);
			Ui::AddSkip(container, st::settingsCheckboxesSkip);
			container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					trWhitegram(
						QString::fromUtf8(
							"Удаляет Whitegram за пару секунд, если к "
							"компьютеру может получить доступ кто-то "
							"другой. Пароль задаётся заранее."),
						QString(
							"Removes Whitegram in seconds if someone else "
							"may get access to this computer. The password "
							"is set in advance.")),
					st::centeredBoxLabel),
				st::boxRowPadding,
				style::al_top);
			Ui::AddSkip(container);
		}, [](const SearchContext &) {
		});
	});

	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Удалить Whitegram"),
		QString("Remove Whitegram")));
	builder.add(lines({
		{ &st::menuIconChatBubble, {
			QString::fromUtf8("Переписка и медиа на этом компьютере"),
			QString("Chats and media on this computer") } },
		{ &st::menuIconClear, {
			QString::fromUtf8("Кэш, логи и временные файлы"),
			QString("Cache, logs and temporary files") } },
		{ &st::menuIconShortcut, {
			QString::fromUtf8("Ярлыки и записи в Windows"),
			QString("Shortcuts and Windows entries") } },
	}));
	builder.addSkip();
	builder.addButton({
		.id = u"whitegram/localSelfDestructPassword"_q,
		.title = trWhitegram(
			QString::fromUtf8("Пароль"),
			QString("Password")),
		.icon = { &st::menuIconLock },
		.label = passwordLabel(Whitegram::SelfDestruct::HasPasswordValue()),
		.onClick = [=] {
			Whitegram::SelfDestruct::ShowSettings(controller);
		},
	});
	builder.addButton({
		.id = u"whitegram/localSelfDestructExecute"_q,
		.title = trWhitegram(
			QString::fromUtf8("Удалить Whitegram"),
			QString("Remove Whitegram")),
		.st = &st::settingsAttentionButtonWithIcon,
		.icon = { &st::menuIconDeleteAttention },
		.onClick = [=] {
			Whitegram::SelfDestruct::ShowExecuteBox(controller);
		},
	});
	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Аккаунты Telegram не затрагиваются: на телефоне и других "
			"устройствах всё останется."),
		QString(
			"Telegram accounts are not affected: everything stays on "
			"your phone and other devices.")));

	builder.addSkip();
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Удалить вместе с аккаунтами"),
		QString("Remove with accounts")));
	builder.add(lines({
		{ &st::menuIconProfile, {
			QString::fromUtf8("Все аккаунты, в которые выполнен вход"),
			QString("Every account you are logged into") } },
		{ &st::menuIconChatBubble, {
			QString::fromUtf8("Их переписка и контакты"),
			QString("Their chats and contacts") } },
		{ &st::menuIconDelete, {
			QString::fromUtf8("Whitegram с этого компьютера"),
			QString("Whitegram on this computer") } },
	}));
	builder.addSkip();
	builder.addButton({
		.id = u"whitegram/totalDestructPassword"_q,
		.title = trWhitegram(
			QString::fromUtf8("Пароль"),
			QString("Password")),
		.icon = { &st::menuIconLock },
		.label = passwordLabel(
			Whitegram::SelfDestruct::HasTotalDestructPasswordValue()),
		.onClick = [=] {
			Whitegram::SelfDestruct::ShowTotalDestructSettingsBox(controller);
		},
	});
	builder.addButton({
		.id = u"whitegram/totalDestructExecute"_q,
		.title = trWhitegram(
			QString::fromUtf8("Удалить Whitegram и аккаунты"),
			QString("Remove Whitegram and accounts")),
		.st = &st::settingsAttentionButtonWithIcon,
		.icon = { &st::menuIconDeleteAttention },
		.onClick = [=] {
			Whitegram::SelfDestruct::ShowTotalDestructExecuteBox(controller);
		},
	});
	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Отменить нельзя. Созданные вами каналы и группы останутся "
			"без владельца.\n\n"
			"Нужен отдельный пароль и облачные пароли аккаунтов. Забыли "
			"пароль — удалите папку Whitegram вручную."),
		QString(
			"This can't be undone. Channels and groups you created are "
			"left without an owner.\n\n"
			"Needs a separate password and the accounts' cloud passwords. "
			"Forgot the password? Delete the Whitegram folder manually.")));
	builder.addSkip();
}

const auto kWhitegramUpdatesMeta = BuildHelper({
	.id = WhitegramUpdates::Id(),
	.parentId = WhitegramGeneral::Id(),
	.title = QString::fromUtf8("Обновление"),
	.icon = &st::menuIconRestore,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	BuildWhitegramUpdatesPage(builder);
	builder.addSkip();
});

void BuildWhitegramCrashReports(SectionBuilder &builder) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	auto white = WhiteSectionBuilder(builder);
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Отчёты о сбоях"),
		QString("Crash reports")));
	white.addSettingToggle({
		.id = u"white/crashReporting"_q,
		.altIds = { u"white/crashlytics"_q },
		.title = trWhitegram(
			QString::fromUtf8("Предлагать отправку отчёта"),
			QString("Offer to send a report")),
		.getter = &WhiteSettings::crashReporting,
		.setter = &WhiteSettings::setCrashReporting,
		.icon = { &st::menuIconReport },
	});
	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Если Whitegram аварийно закроется, при следующем запуске он "
			"предложит отправить отчёт разработчикам. В отчёт входят "
			"технический лог и дамп памяти приложения. Без вашего "
			"согласия ничего не отправляется."),
		QString(
			"If Whitegram crashes, it offers to send a report to the "
			"developers on the next launch. The report contains the "
			"technical log and the app memory dump. Nothing is sent "
			"without your consent.")));
	builder.addSkip();
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
}

const auto kWhitegramGeneralMeta = BuildHelper({
	.id = WhitegramGeneral::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Основное"),
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	builder.addDividerText(
		trWhitegram(
			QString::fromUtf8(
				"Обновления, аккаунты и приватность."),
			QString(
				"Updates, accounts and privacy.")));
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Разделы"),
		QString("Sections")));

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Обновление"),
			QString("Updates")),
		.targetSection = WhitegramUpdates::Id(),
		.icon = { &st::menuIconRestore },
		.keywords = {
			u"update"_q,
			u"version"_q,
			QString::fromUtf8("обновление"),
			QString::fromUtf8("версия"),
		},
	});
	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Курсы криптовалют"),
			QString("Cryptocurrency Rates")),
		.targetSection = WhitegramCrypto::Id(),
		.icon = { &st::menuIconTradable },
		.keywords = {
			u"crypto"_q,
			u"bitcoin"_q,
			u"gram"_q,
			u"ton"_q,
			QString::fromUtf8("крипто"),
			QString::fromUtf8("курс"),
		},
	});
	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Счетчик времени"),
			QString("Time Spent Counter")),
		.targetSection = WhitegramTimeSpent::Id(),
		.icon = { &st::menuIconTimer },
		.keywords = {
			u"time"_q,
			QString::fromUtf8("время"),
			QString::fromUtf8("счётчик"),
		},
	});
	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Звук при запуске"),
			QString("Startup Sound")),
		.targetSection = WhitegramStartupSound::Id(),
		.icon = { &st::menuIconSoundOn },
		.keywords = {
			u"sound"_q,
			u"startup"_q,
			QString::fromUtf8("звук"),
			QString::fromUtf8("запуск"),
		},
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Аккаунты"),
		QString("Accounts")));

	auto white = WhiteSectionBuilder(builder);
	white.addSettingToggle({
		.id = u"whitegram/accountProfilesEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Профили аккаунтов"),
			QString("Account Profiles")),
		.getter = &WhiteSettings::accountProfilesEnabled,
		.setter = &WhiteSettings::setAccountProfilesEnabled,
		.icon = { &st::menuIconAddAccount },
		.keywords = {
			u"profile"_q,
			u"account"_q,
			QString::fromUtf8("профиль"),
			QString::fromUtf8("аккаунт"),
		},
	});

	int maxProfCurrent = WhiteSettings::getInstance().maxAccountProfiles();
	int maxProfIndex = 1;
	if (maxProfCurrent == 5) maxProfIndex = 0;
	else if (maxProfCurrent == 10) maxProfIndex = 1;
	else if (maxProfCurrent == 15) maxProfIndex = 2;
	else if (maxProfCurrent == 20) maxProfIndex = 3;
	else if (maxProfCurrent == 30) maxProfIndex = 4;
	else maxProfIndex = 5;

	auto getProfOptions = [] {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		return std::vector<QString>{
			QString::fromUtf8("5"),
			QString::fromUtf8("10"),
			QString::fromUtf8("15"),
			QString::fromUtf8("20"),
			QString::fromUtf8("30"),
			isRu ? QString::fromUtf8("Своё значение...") : QString("Custom value..."),
		};
	};

	white.addChooseButton({
		.id = u"whitegram/maxAccountProfiles"_q,
		.title = trWhitegram(
			QString::fromUtf8("Максимум профилей"),
			QString("Max Account Profiles")),
		.boxTitle = trWhitegram(
			QString::fromUtf8("Максимум профилей"),
			QString("Max Account Profiles")),
		.initialSelection = maxProfIndex,
		.options = getProfOptions(),
		.setter = [=](int index) {
			if (index == 5) {
				const auto controller = builder.controller();
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					box->setTitle(trWhitegram(
						QString::fromUtf8("Максимум профилей"),
						QString("Max Account Profiles")));
					const auto field = box->addRow(object_ptr<Ui::InputField>(
						box,
						st::defaultInputField,
						trWhitegram(
							QString::fromUtf8("Число профилей (от 1 до 50)"),
							QString("Number of profiles (1 to 50)")),
						QString::number(WhiteSettings::getInstance().maxAccountProfiles())));
					box->addButton(tr::lng_box_ok(), [=] {
						const auto text = field->getLastText().trimmed();
						bool ok = false;
						const int val = text.toInt(&ok);
						if (ok && val > 0) {
							WhiteSettings::getInstance().setMaxAccountProfiles(val);
						}
						box->closeBox();
					});
					box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
				}));
			} else {
				const int vals[] = { 5, 10, 15, 20, 30 };
				WhiteSettings::getInstance().setMaxAccountProfiles(vals[index]);
			}
		},
		.icon = { &st::menuIconManage },
		.shown = WhiteSettings::getInstance().enableAccountProfilesValue(),
	});

	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Профили позволяют держать несколько аккаунтов в одном окне "
			"и быстро переключаться между ними. Лимит — максимальное число "
			"профилей, которые можно создать."),
		QString(
			"Profiles let you keep multiple accounts in one window and "
			"switch between them quickly. The limit is the maximum number "
			"of profiles you can create.")));
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Приватность"),
		QString("Privacy")));

	white.addSettingToggle({
		.id = u"whitegram/hideTelegramOnViewOnce"_q,
		.title = trWhitegram(
			QString::fromUtf8("Скрывать окно от записи экрана при просмотре одноразовых медиа"),
			QString("Hide window from screen capture while viewing view-once media")),
		.getter = &WhiteSettings::hideTelegramOnViewOnce,
		.setter = &WhiteSettings::setHideTelegramOnViewOnce,
		.icon = { &st::whiteStreamerModeMenuIcon },
		.keywords = {
			u"streamer"_q,
			u"view-once"_q,
			QString::fromUtf8("стример"),
			QString::fromUtf8("одноразовые"),
			QString::fromUtf8("скрыть"),
		},
	});

	{
		const auto controller = builder.controller();
		builder.addButton({
			.id = u"whitegram/hidePhoneNumber"_q,
			.title = trWhitegram(
				QString::fromUtf8("Скрывать номер телефона в моём профиле"),
				QString("Hide phone number in my profile")),
			.icon = { &st::menuIconPhone },
			.toggled = builder.session()->settings().phoneNumberHiddenValue(),
			.onClick = [=] {
				auto &settings = controller->session().settings();
				settings.setPhoneNumberHidden(
					!settings.phoneNumberHidden());
				controller->session().saveSettingsDelayed();
			},
			.keywords = {
				u"phone"_q,
				u"spoiler"_q,
				QString::fromUtf8("номер"),
				QString::fromUtf8("телефон"),
				QString::fromUtf8("скрыть"),
				QString::fromUtf8("спойлер"),
			},
		});
	}

	{
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		const auto revealOptions = std::vector{
			QString(isRu
				? QString::fromUtf8("При клике (как в Telegram)")
				: QString("On click (like Telegram)")),
			QString(isRu
				? QString::fromUtf8("При наведении")
				: QString("On hover")),
		};
		white.addChooseButton({
			.id = u"whitegram/phoneRevealMode"_q,
			.title = trWhitegram(
				QString::fromUtf8("Режим раскрытия номера"),
				QString("Phone reveal mode")),
			.boxTitle = trWhitegram(
				QString::fromUtf8("Режим раскрытия номера"),
				QString("Phone reveal mode")),
			.initialSelection = static_cast<int>(
				WhiteSettings::getInstance().phoneRevealMode()),
			.options = revealOptions,
			.setter = [](int i) {
				WhiteSettings::getInstance().setPhoneRevealMode(
					static_cast<PhoneRevealMode>(i));
			},
			.icon = { &st::menuIconPhone },
		});
	}

	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Номер скрывается спойлером, как в Telegram: включить его можно "
			"и правым кликом по номеру в профиле. Режим «При наведении» — "
			"расширение Whitegram: номер показывается, пока курсор над ним."),
		QString(
			"The number is hidden with a spoiler, like in Telegram: it can "
			"also be toggled by right-clicking the number in the profile. "
			"The 'On hover' mode is a Whitegram extension: the number shows "
			"while the cursor is over it.")));
	builder.addSkip();

	BuildAyuGeneralContent(builder);
	BuildAyuOtherToggles(builder);
	BuildWhitegramCrashReports(builder);
});

void BuildWhitegramStartupSound(SectionBuilder &builder) {
	builder.addSkip();

	auto white = WhiteSectionBuilder(builder);
	white.addSettingToggle({
		.id = u"whitegram/startupSoundEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Включить звук при запуске"),
			QString("Play sound on startup")),
		.getter = &WhiteSettings::startupSoundEnabled,
		.setter = &WhiteSettings::setStartupSoundEnabled,
		.icon = { &st::menuIconSoundOn },
	});

	builder.addDividerText(
		trWhitegram(
			QString::fromUtf8(
				"Короткий звук при запуске Whitegram. Можно выбрать "
				"свой файл (WAV, MP3, OGG, M4A) или оставить "
				"стандартный звук Telegram."),
			QString(
				"A short sound when Whitegram starts. Pick your own "
				"file (WAV, MP3, OGG, M4A) or keep the default "
				"Telegram sound.")));

	builder.addSkip();

	const auto formatCurrentSound = [](const QString &storedPath) {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q)
			|| baseId.startsWith(u"ru"_q);
		if (storedPath.isEmpty()) {
			return isRu
				? QString::fromUtf8("Стандартный звук Telegram")
				: QString("Default Telegram sound");
		}
		return QFileInfo(storedPath).fileName();
	};

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Файл"),
		QString("Sound file")));

	builder.addButton({
		.id = u"whitegram/startupSoundChoose"_q,
		.title = trWhitegram(
			QString::fromUtf8("Звук"),
			QString("Sound")),
		.icon = { &st::menuIconFile },
		.label = WhiteSettings::getInstance().startupSoundPathValue(
		) | rpl::map([=](const QString &path) {
			const auto name = formatCurrentSound(path);
			const auto missing = !path.isEmpty() && !QFileInfo::exists(path);
			if (!missing) {
				return name;
			}
			const auto id = Lang::GetInstance().id();
			const auto isRu = id.startsWith(u"ru"_q)
				|| Lang::GetInstance().baseId().startsWith(u"ru"_q);
			return name + (isRu
				? QString::fromUtf8(" (не найден)")
				: QString(" (not found)"));
		}),
		.onClick = [=] {
			const auto filters = u"Audio files (*.wav *.mp3 *.ogg *.m4a);;"_q
				+ FileDialog::AllFilesFilter();
			FileDialog::GetOpenPath(
				Core::App().getFileDialogParent(),
				u"Open audio file"_q,
				filters,
				[=](const FileDialog::OpenResult &result) {
					if (result.paths.isEmpty()) {
						return;
					}
					auto &settings = WhiteSettings::getInstance();
					settings.setStartupSoundPath(result.paths.front());
					if (!settings.startupSoundEnabled()) {
						settings.setStartupSoundEnabled(true);
					}
				});
		},
	});

	builder.addButton({
		.id = u"whitegram/startupSoundPreview"_q,
		.title = trWhitegram(
			QString::fromUtf8("Прослушать"),
			QString("Preview")),
		.icon = { &st::menuIconSoundOn },
		.onClick = [=] {
			WhiteSettings::getInstance().previewStartupSound();
		},
	});

	builder.addButton({
		.id = u"whitegram/startupSoundStop"_q,
		.title = trWhitegram(
			QString::fromUtf8("Остановить"),
			QString("Stop")),
		.icon = { &st::menuIconCancel },
		.onClick = [=] {
			WhiteSettings::getInstance().stopStartupSound();
		},
	});

	builder.addButton({
		.id = u"whitegram/startupSoundReset"_q,
		.title = trWhitegram(
			QString::fromUtf8("Сбросить на стандартный"),
			QString("Reset to default")),
		.icon = { &st::menuIconRestore },
		.onClick = [=] {
			WhiteSettings::getInstance().setStartupSoundPath(QString());
		},
	});

	builder.addSkip();
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Громкость"),
		QString("Volume")));

	white.addSlider({
		.id = u"whitegram/startupSoundVolume"_q,
		.title = trWhitegram(
			QString::fromUtf8("Громкость"),
			QString("Volume")),
		.showTitle = false,
		.steps = 21,
		.current = WhiteSettings::getInstance().startupSoundVolume(),
		.indexToValue = [](int index) { return index * 5; },
		.onFinalChanged = [](int value) {
			WhiteSettings::getInstance().setStartupSoundVolume(value);
		},
		.formatLabel = [](int value) {
			return QString::number(value) + u'%';
		},
		.keywords = { u"volume"_q, u"sound"_q, u"loud"_q },
	});

	builder.addSkip();
}

const auto kWhitegramStartupSoundMeta = BuildHelper({
	.id = WhitegramStartupSound::Id(),
	.parentId = WhitegramGeneral::Id(),
	.title = QString::fromUtf8("Звук при запуске"),
	.icon = &st::menuIconSoundOn,
}, BuildWhitegramStartupSound);

const auto kWhitegramStartupAnimMeta = BuildHelper({
	.id = WhitegramStartupAnim::Id(),
	.parentId = WhitegramAppearance::Id(),
	.title = QString::fromUtf8("Анимация при входе"),
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto wrap = object_ptr<Ui::VerticalLayout>(ctx.container);
		const auto layout = wrap.data();
		const auto preview = layout->add(
			object_ptr<Ui::WhitegramStartupPreview>(layout),
			st::boxRowPadding);
		rpl::merge(
			WhiteSettings::getInstance().startupAnimationTypeValue(
			) | rpl::to_empty,
			WhiteSettings::getInstance().startupAnimationShowIconValue(
			) | rpl::to_empty
		) | rpl::on_next([=] {
			preview->restart();
		}, preview->lifetime());
		Ui::AddSkip(layout, st::settingsCheckboxesSkip);
		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				trWhitegram(
					QString::fromUtf8(
						"Нажмите на превью, чтобы повторить. При запуске "
						"анимацию можно пропустить кликом или любой клавишей."),
					QString(
						"Click the preview to replay. On launch you can skip "
						"the animation with a click or any key.")),
				st::boxDividerLabel),
			st::boxRowPadding);
		return { .widget = std::move(wrap) };
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	auto white = WhiteSectionBuilder(builder);

	white.addSettingToggle({
		.id = u"whitegram/startupAnimationEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Включить анимацию при запуске"),
			QString("Enable Startup Animation")),
		.getter = &WhiteSettings::startupAnimationEnabled,
		.setter = &WhiteSettings::setStartupAnimationEnabled,
	});

	white.addSettingToggle({
		.id = u"whitegram/startupAnimationShowIcon"_q,
		.title = trWhitegram(
			QString::fromUtf8("Показывать иконку"),
			QString("Show Icon")),
		.getter = &WhiteSettings::startupAnimationShowIcon,
		.setter = &WhiteSettings::setStartupAnimationShowIcon,
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto wrap = object_ptr<Ui::VerticalLayout>(ctx.container);
		const auto layout = wrap.data();
		Ui::AddSubsectionTitle(
			layout,
			trWhitegram(
				QString::fromUtf8("Стиль анимации"),
				QString("Animation Style")));
		const auto isRu = Lang::GetInstance().id().startsWith(u"ru"_q)
			|| Lang::GetInstance().baseId().startsWith(u"ru"_q);
		const auto group = std::make_shared<Ui::RadiobuttonGroup>(
			Ui::NormalizeWhitegramStartupStyle(
				WhiteSettings::getInstance().startupAnimationType()));
		for (const auto i : Ui::WhitegramStartupStyles()) {
			layout->add(
				object_ptr<Ui::Radiobutton>(
					layout,
					group,
					i,
					Ui::WhitegramStartupStyleName(i, isRu),
					st::settingsChatbotsAccess),
				st::settingsChatbotsAccessMargins);
		}
		group->setChangedCallback([](int value) {
			WhiteSettings::getInstance().setStartupAnimationType(value);
		});
		return { .widget = std::move(wrap) };
	}, [] {
		return SearchEntry{
			.id = u"whitegram/startupAnimationType"_q,
			.title = QString::fromUtf8("Стиль анимации"),
			.keywords = {
				u"animation"_q,
				u"splash"_q,
				QString::fromUtf8("анимац"),
			},
			.section = WhitegramStartupAnim::Id(),
		};
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	int currentFps = WhiteSettings::getInstance().startupAnimationFps();
	int fpsIndex = 4;
	if (currentFps == 60) fpsIndex = 0;
	else if (currentFps == 90) fpsIndex = 1;
	else if (currentFps == 120) fpsIndex = 2;
	else if (currentFps == 165) fpsIndex = 3;
	else if (currentFps == 240) fpsIndex = 4;
	else fpsIndex = 5;

	auto getFpsOptions = [] {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		return std::vector<QString>{
			QString("60 FPS"),
			QString("90 FPS"),
			QString("120 FPS"),
			QString("165 FPS"),
			QString("240 FPS"),
			isRu ? QString::fromUtf8("Своё значение...") : QString("Custom value..."),
		};
	};

	white.addChooseButton({
		.id = u"whitegram/startupAnimationFps"_q,
		.title = trWhitegram(
			QString::fromUtf8("Частота кадров (FPS)"),
			QString("Frame Rate (FPS)")),
		.boxTitle = trWhitegram(
			QString::fromUtf8("Частота кадров (FPS)"),
			QString("Frame Rate (FPS)")),
		.initialSelection = fpsIndex,
		.options = getFpsOptions(),
		.setter = [=](int index) {
			if (index == 5) {
				const auto controller = builder.controller();
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					box->setTitle(trWhitegram(
						QString::fromUtf8("Частота кадров (FPS)"),
						QString("Frame Rate (FPS)")));
					const auto field = box->addRow(object_ptr<Ui::InputField>(
						box,
						st::defaultInputField,
						trWhitegram(
							QString::fromUtf8("Значение FPS (от 15 до 360)"),
							QString("FPS Value (15 to 360)")),
						QString::number(WhiteSettings::getInstance().startupAnimationFps())));
					box->addButton(tr::lng_box_ok(), [=] {
						const auto text = field->getLastText().trimmed();
						bool ok = false;
						const int val = text.toInt(&ok);
						if (ok && val >= 15 && val <= 360) {
							WhiteSettings::getInstance().setStartupAnimationFps(val);
						}
						box->closeBox();
					});
					box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
				}));
			} else {
				static const int fpsValues[] = { 60, 90, 120, 165, 240 };
				if (index >= 0 && index < 5) {
					WhiteSettings::getInstance().setStartupAnimationFps(fpsValues[index]);
				}
			}
		},
	});

	builder.addSkip();
});

[[nodiscard]] QString ArchiveRowTitle(const CustomArchive &archive) {
	auto title = archive.name;
	if (!archive.icon.isEmpty()) {
		if (archive.icon.endsWith(u"home"_q)) title = u"🏠 "_q + title;
		else if (archive.icon.endsWith(u"work"_q)) title = u"💼 "_q + title;
		else if (archive.icon.endsWith(u"game"_q)) title = u"🎮 "_q + title;
		else if (archive.icon.endsWith(u"favorite"_q)) title = u"⭐ "_q + title;
		else if (archive.icon.endsWith(u"love"_q)) title = u"❤️ "_q + title;
		else if (archive.icon.endsWith(u"private"_q)) title = u"🔒 "_q + title;
		else title = u"📁 "_q + title;
	}
	if (archive.isLocked()) {
		title = u"🔐 "_q + title;
	}
	return title;
}

[[nodiscard]] QString ArchiveRowLabel(const CustomArchive &archive) {
	const auto count = int(archive.peerIds.size());
	auto parts = QStringList();
	parts.push_back(QString::number(count));
	if (archive.hasPin() && archive.hasPassword()) {
		parts.push_back(QString::fromUtf8("PIN + пароль"));
	} else if (archive.hasPin()) {
		parts.push_back(u"PIN"_q);
	} else 	if (archive.hasPassword()) {
		parts.push_back(QString::fromUtf8("пароль"));
	}
	return parts.join(u" · "_q);
}

void BuildArchivesList(SectionBuilder &builder) {
	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			ctx.container,
			object_ptr<Ui::VerticalLayout>(ctx.container));
		const auto slide = wrap.data();
		const auto layout = slide->entity();
		const auto controller = ctx.controller;

		const auto rebuild = [=] {
			while (layout->count() > 0) {
				delete layout->widgetAt(0);
			}

			const auto enabled = WhiteSettings::getInstance().enableCustomArchives();
			slide->toggle(enabled, anim::type::normal);
			if (!enabled) {
				layout->resizeToWidth(layout->width());
				return;
			}

			auto archives = WhiteSettings::getInstance().customArchives();
			std::sort(archives.begin(), archives.end(), [](
					const CustomArchive &a,
					const CustomArchive &b) {
				if (a.order != b.order) {
					return a.order < b.order;
				}
				return a.id < b.id;
			});

			const auto create = AddButtonWithIcon(
				layout,
				trWhitegram(
					QString::fromUtf8("Создать папку"),
					QString("Create folder")),
				st::settingsButton,
				{ &st::menuIconAddToFolder });
			create->setClickedCallback([=] {
				controller->show(Box(EditCustomArchiveBox, controller, -1));
			});

			if (archives.empty()) {
				layout->add(
					object_ptr<Ui::FlatLabel>(
						layout,
						trWhitegram(
							QString::fromUtf8(
								"Папок пока нет."),
							QString(
								"No folders yet.")),
						st::boxDividerLabel),
					st::boxRowPadding);
			}

			for (const auto &archive : archives) {
				const auto archiveId = archive.id;
				const auto button = AddButtonWithLabel(
					layout,
					rpl::single(ArchiveRowTitle(archive)),
					rpl::single(ArchiveRowLabel(archive)),
					st::settingsButton,
					{ &st::menuIconArchive });
				button->setClickedCallback([=] {
					WhiteFeatures::CustomArchives::OpenArchive(
						controller,
						archiveId);
				});
			}

			const auto manage = AddButtonWithIcon(
				layout,
				trWhitegram(
					QString::fromUtf8("Управление папками"),
					QString("Manage folders")),
				st::settingsButton,
				{ &st::menuIconSettings });
			manage->setClickedCallback([=] {
				controller->show(Box(CustomArchivesBox, controller));
			});

			layout->resizeToWidth(layout->width());
		};

		rebuild();
		rpl::merge(
			WhiteSettings::getInstance().enableCustomArchivesChanges(
			) | rpl::to_empty,
			WhiteSettings::getInstance().customArchivesChanges()
		) | rpl::on_next(rebuild, layout->lifetime());

		return { .widget = std::move(wrap) };
	});
}

const auto kWhitegramArchivesMeta = BuildHelper({
	.id = WhitegramArchives::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Папки чатов"),
	.icon = &st::menuIconArchive,
}, [](SectionBuilder &builder) {
	builder.addDividerText(
		trWhitegram(
			QString::fromUtf8(
				"Отдельные папки для чатов. Чаты из них можно убрать "
				"из общего списка, а саму папку закрыть PIN-кодом "
				"или паролем."),
			QString(
				"Separate folders for chats. You can remove their "
				"chats from the main list and lock the folder with "
				"a PIN or password.")));
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Настройки"),
		QString("Settings")));

	auto white = WhiteSectionBuilder(builder);
	white.addSettingToggle({
		.id = u"whitegram/customArchivesEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Папки чатов"),
			QString("Chat folders")),
		.getter = &WhiteSettings::customArchivesEnabled,
		.setter = &WhiteSettings::setCustomArchivesEnabled,
		.icon = { &st::menuIconArchive },
	});

	white.addSettingToggle({
		.id = u"whitegram/confirmFolderChanges"_q,
		.title = trWhitegram(
			QString::fromUtf8("Сохранять папки вручную"),
			QString("Save folder edits manually")),
		.getter = &WhiteSettings::confirmFolderChanges,
		.setter = &WhiteSettings::setConfirmFolderChanges,
		.icon = { &st::menuIconEdit },
	});

	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"При ручном сохранении изменения применяются только после "
			"нажатия «Сохранить».\n\n"
			"Если выключить папки, они пропадут из списка чатов, но "
			"их настройки останутся."),
		QString(
			"With manual saving, changes apply only after you press "
			"Save.\n\n"
			"If you turn folders off, they disappear from the chat "
			"list, but their settings are kept.")));

	builder.addSkip();
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Ваши папки"),
		QString("Your folders")));

	BuildArchivesList(builder);

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Чтобы добавить чат в папку, нажмите на него правой кнопкой "
			"мыши и выберите «Папки»."),
		QString(
			"To add a chat to a folder, right-click it and choose "
			"Folders.")));
	builder.addSkip();
});

const auto kWhitegramAppearanceMeta = BuildHelper({
	.id = WhitegramAppearance::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Оформление"),
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	builder.addSkip();
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Иконка приложения"),
		QString("Application icon")));
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<IconPicker>(
				ctx.container,
				WhitegramAppIcons()),
			.margin = st::settingsButtonNoIcon.padding,
		};
	});
	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Нажмите на иконку, чтобы поставить её приложению, "
			"на панели задач и в трее."),
		QString(
			"Tap an icon to apply it to the app, the taskbar and the tray.")));
	builder.addSkip();

	auto white = WhiteSectionBuilder(builder);
	{
		using namespace WhiteFeatures::GlassEffect;

		builder.addSubsectionTitle(trWhitegram(
			QString::fromUtf8("Стекло и прозрачность"),
			QString("Glass & transparency")));
		white.addSettingToggle({
			.id = u"whitegram/glassEffect"_q,
			.title = trWhitegram(
				QString::fromUtf8("Эффект стекла"),
				QString("Glass effect")),
			.getter = &WhiteSettings::glassEffect,
			.setter = &WhiteSettings::setGlassEffect,
			.icon = { &st::menuIconChangeColors },
			.keywords = {
				u"glass"_q,
				u"blur"_q,
				u"transparent"_q,
				u"opacity"_q,
				QString::fromUtf8("стекло"),
				QString::fromUtf8("прозрачность"),
				QString::fromUtf8("размытие"),
			},
		});
		const auto addPercentSlider = [&](
				rpl::producer<QString> title,
				int min,
				int max,
				int current,
				Fn<void(int)> changed) {
			const auto searchTitle = std::make_shared<rpl::producer<QString>>(
				rpl::duplicate(title));
			builder.add([=](const WidgetContext &ctx)
			-> SectionBuilder::WidgetToAdd {
				return {
					.widget = WhiteUi::CreatePercentSlider(
						ctx.container,
						WhiteUi::PercentSliderArgs{
							.title = rpl::duplicate(*searchTitle),
							.min = min,
							.max = max,
							.current = current,
							.changed = changed,
						}),
				};
			});
		};
		const auto &settings = WhiteSettings::getInstance();
		builder.scope([&] {
			white.addSettingToggle({
				.id = u"whitegram/glassBlur"_q,
				.title = trWhitegram(
					QString::fromUtf8("Размытие фона"),
					QString("Background blur")),
				.getter = &WhiteSettings::glassBlur,
				.setter = &WhiteSettings::setGlassBlur,
				.icon = { &st::menuIconPhoto },
				.keywords = { u"blur"_q, QString::fromUtf8("размытие") },
			});
			builder.scope([&] {
				addPercentSlider(
					trWhitegram(
						QString::fromUtf8("Сила размытия"),
						QString("Blur strength")),
					0,
					100,
					WhiteSettings::getInstance().glassBlurStrength(),
					[](int value) {
						WhiteSettings::getInstance().setGlassBlurStrength(value);
					});
			}, settings.glassBlurValue());
			addPercentSlider(
				trWhitegram(
					QString::fromUtf8("Непрозрачность фона"),
					QString("Background opacity")),
				kMinOpacity,
				kMaxOpacity,
				WhiteSettings::getInstance().glassOpacity(),
				[](int value) {
					WhiteSettings::getInstance().setGlassOpacity(value);
				});
		}, settings.glassEffectValue());
		builder.addSkip();
		builder.addDividerText(trWhitegram(
			QString::fromUtf8(
				"Прозрачным становится только фон: текст, иконки и сообщения "
				"остаются чёткими. Боковое меню, профили и окна становятся "
				"матовым стеклом. Первое включение требует перезапуска."),
			QString(
				"Only the background turns translucent: text, icons and "
				"messages stay crisp. The side menu, profiles and boxes turn "
				"into frosted glass. The first enable needs a restart.")));
		builder.addSkip();
	}

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Боковое меню"),
		QString("Side menu")));
	white.addSettingToggle({
		.id = u"whitegram/showGlassToggleInDrawer"_q,
		.title = trWhitegram(
			QString::fromUtf8("Эффект стекла"),
			QString("Glass effect")),
		.getter = &WhiteSettings::showGlassToggleInDrawer,
		.setter = &WhiteSettings::setShowGlassToggleInDrawer,
		.icon = { &st::menuIconChangeColors },
	});
	white.addSettingToggle({
		.id = u"whitegram/showSendBotInDrawer"_q,
		.title = trWhitegram(
			QString::fromUtf8("CryptoBot (@send)"),
			QString("CryptoBot (@send)")),
		.getter = &WhiteSettings::showSendBotInDrawer,
		.setter = &WhiteSettings::setShowSendBotInDrawer,
		.icon = { &st::menuIconSend },
	});
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Добавляет в боковое меню переключатель эффекта стекла и "
			"кнопку CryptoBot (@send) (t.me/send). Это не Telegram "
			"Wallet (@wallet)."),
		QString(
			"Adds the glass effect switch and the CryptoBot (@send) "
			"button (t.me/send) to the side menu. This is not Telegram "
			"Wallet (@wallet).")));
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Ввод текста"),
		QString("Text input")));
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto name = [](const char *ru, const char *en) {
			return trWhitegram(QString::fromUtf8(ru), QString(en));
		};
		auto names = std::vector<rpl::producer<QString>>();
		names.push_back(name("Выключена", "Off"));
		names.push_back(name("Проявление", "Fade"));
		names.push_back(name("Всплытие", "Rise"));
		names.push_back(name("Падение", "Drop"));
		names.push_back(name("Пружина", "Pop"));
		names.push_back(name("Свечение", "Glow"));
		return {
			.widget = object_ptr<TypingAnimationPicker>(
				ctx.container,
				TypingAnimationPicker::Args{
					.names = std::move(names),
					.sample = name("Привет", "Hello"),
					.current = WhiteSettings::getInstance().typingAnimation(),
					.chosen = [](int index) {
						WhiteSettings::getInstance().setTypingAnimation(index);
					},
				}),
			.margin = QMargins(
				st::settingsButtonNoIcon.padding.left(),
				style::ConvertScale(4),
				st::settingsButtonNoIcon.padding.right(),
				style::ConvertScale(4)),
		};
	});
	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Плавно анимирует символы, которые вы набираете в поле "
			"ввода. Видно только вам."),
		QString(
			"Smoothly animates the characters you type in input fields. "
			"Only visible to you.")));
	builder.addSkip();

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Анимация при входе"),
			QString("Startup Animation")),
		.targetSection = WhitegramStartupAnim::Id(),
		.icon = { &st::menuIconShowAll },
	});

	builder.addSkip();
	builder.addDivider();
	BuildAyuAppearanceContent(builder);
	// "Фон профиля" lives on the "My Profile" page (settings_information.cpp),
	// next to the photo and the bio, so it is not listed here.
});

const auto kWhitegramProfileBackgroundMeta = BuildHelper({
	.id = WhitegramProfileBackground::Id(),
	.parentId = WhitegramAppearance::Id(),
	.title = QString::fromUtf8("Фон профиля"),
	.icon = &st::menuIconPhoto,
}, [](SectionBuilder &builder) {
	namespace Background = WhiteFeatures::CustomProfileBackground;
	using Zone = Background::Zone;
	auto white = WhiteSectionBuilder(builder);
	const auto peerId = builder.session()->userPeerId().value;
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addDividerText(
		trWhitegram(
			QString::fromUtf8(
				"Свой фон для вашего профиля: шапка, весь профиль "
				"или блок «Информация». Поддерживаются фото и видео."),
			QString(
				"Custom background for your profile: header, full "
				"profile or the info block. Photos and videos "
				"are supported.")));
	builder.addSkip();

	auto addZone = [&](
			Zone zone,
			const QString &titleRu,
			const QString &titleEn,
			const QString &hintRu,
			const QString &hintEn) {
		const auto isRu = Lang::GetInstance().id().startsWith(u"ru"_q)
			|| Lang::GetInstance().baseId().startsWith(u"ru"_q);
		builder.add([&](const WidgetContext &ctx)
				-> SectionBuilder::WidgetToAdd {
			return {
				.widget = object_ptr<ProfileBackgroundCard>(
					ctx.container,
					isRu ? titleRu : titleEn,
					isRu ? hintRu : hintEn,
					peerId,
					zone,
					[=] { Background::PickBackground(controller, peerId, zone); },
					[=] { Background::RemoveBackground(peerId, zone); }),
			};
		});
		builder.add(nullptr, [titleEn] {
			return SearchEntry{
				.id = u"whitegram/profileBackground/"_q,
				.title = titleEn,
				.keywords = { u"background"_q, u"profile"_q },
			};
		});
	};

	addZone(
		Zone::Top,
		QString::fromUtf8("Фон шапки"),
		QString("Header background"),
		QString::fromUtf8(
			"Показывает только верхнюю часть профиля: аватар, имя и статус."),
		QString(
			"Covers only the top of the profile: avatar, name and status."));
	addZone(
		Zone::Full,
		QString::fromUtf8("Фон всего профиля"),
		QString("Full profile background"),
		QString::fromUtf8(
			"Покрывает весь профиль — от шапки до конца прокрутки. "
			"Страница «Истории» остаётся без изменений."),
		QString(
			"Covers the whole profile, from the header down to the end of "
			"the scroll. The stories page is not affected."));
	addZone(
		Zone::Bottom,
		QString::fromUtf8("Фон блока «Информация»"),
		QString("Info section background"),
		QString::fromUtf8(
			"Покрывает только поля с телефоном, username и ID. "
			"Не применяется, если задан фон всего профиля."),
		QString(
			"Covers only the phone, username and ID fields. Not used when "
			"the full profile background is set."));

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Оформление"),
		QString("Look")));

	white.addSlider({
		.id = u"whitegram/profileBackgroundDim"_q,
		.title = trWhitegram(
			QString::fromUtf8("Затемнение фона"),
			QString("Background dimming")),
		.steps = 101,
		.current = WhiteSettings::getInstance().profileBackgroundDim(),
		.indexToValue = [](int index) { return index; },
		.onChanged = [](int value) {
			WhiteSettings::getInstance().setProfileBackgroundDim(value);
		},
		.onFinalChanged = [](int value) {
			WhiteSettings::getInstance().setProfileBackgroundDim(value);
		},
		.formatLabel = [](int value) {
			return QString::number(value) + u'%';
		},
		.keywords = { u"dim"_q, QString::fromUtf8("затемнение") },
	});

	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Затемнение помогает сохранить читаемость текста на светлых "
			"фонах. 100% — как задумано, 0% — без затемнения. Другие "
			"пользователи Whitegram тоже увидят эти фоны. Лучше всего "
			"подходят вертикальные изображения вроде 1080×2800."),
		QString(
			"Dimming keeps text readable on bright backgrounds. 100% is "
			"the designed look, 0% is no dimming. Other Whitegram users "
			"will see these backgrounds too. Vertical images like "
			"1080×2800 work best.")));
	builder.addSkip();
});
[[nodiscard]] std::vector<QString> MusicIslandExpandModeOptions() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
	return std::vector<QString>{
		isRu ? QString::fromUtf8("При наведении") : QString("On hover"),
		isRu ? QString::fromUtf8("По клику") : QString("On click"),
		isRu ? QString::fromUtf8("Наведение и клик") : QString("Hover and click"),
	};
}

[[nodiscard]] std::vector<QString> MusicIslandCornerOptions() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
	return std::vector<QString>{
		isRu ? QString::fromUtf8("Сверху слева") : QString("Top left"),
		isRu ? QString::fromUtf8("Сверху справа") : QString("Top right"),
		isRu ? QString::fromUtf8("Снизу слева") : QString("Bottom left"),
		isRu ? QString::fromUtf8("Снизу справа") : QString("Bottom right"),
		isRu ? QString::fromUtf8("Своё место") : QString("Custom spot"),
	};
}

[[nodiscard]] std::vector<QString> MusicIslandBackgroundModeOptions() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
	return std::vector<QString>{
		isRu ? QString::fromUtf8("Заполнить") : QString("Fill"),
		isRu ? QString::fromUtf8("Вписать") : QString("Fit"),
		isRu ? QString::fromUtf8("Растянуть") : QString("Stretch"),
	};
}

[[nodiscard]] QString MusicIslandBackgroundCurrentText(const QString &path) {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
	if (path.isEmpty()) {
		return isRu
			? QString::fromUtf8("Фон не выбран")
			: QString("No background selected");
	}
	if (!QFileInfo::exists(path)) {
		return isRu
			? QString::fromUtf8("Файл не найден")
			: QString("File not found");
	}
	return QFileInfo(path).fileName();
}

[[nodiscard]] rpl::producer<bool> MusicIslandFlagValue(
		bool (WhiteSettings::*getter)() const,
		rpl::producer<bool> (WhiteSettings::*changes)() const) {
	const auto settings = &WhiteSettings::getInstance();
	return rpl::single((settings->*getter)()) | rpl::then((settings->*changes)());
}

[[nodiscard]] rpl::producer<bool> MusicIslandHasBackgroundValue() {
	const auto settings = &WhiteSettings::getInstance();
	return rpl::combine(
		settings->musicIslandCustomBackgroundEnabledValue(),
		settings->musicIslandCustomBackgroundPathValue()
	) | rpl::map([](bool enabled, const QString &path) {
		return enabled && !path.isEmpty();
	});
}

void ShowWeatherCityBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(trWhitegram(
		QString::fromUtf8("Город для погоды"),
		QString("Weather city")));
	box->setWidth(st::boxWideWidth);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		trWhitegram(
			QString::fromUtf8(
				"Погода на экране караоке берётся с Open-Meteo для этого "
				"города. Оставьте поле пустым — город определится по "
				"IP-адресу."),
			QString(
				"Karaoke weather comes from Open-Meteo for this city. "
				"Leave it empty to detect the city by IP address.")),
		st::boxLabel));
	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::whitegramBypassMirrorField,
			trWhitegram(QString::fromUtf8("Например, Москва"), QString("For example, London")),
			WhiteSettings::getInstance().musicWeatherCity()),
		st::whitegramBypassMirrorFieldMargin);
	field->setFocus();
	const auto apply = [=] {
		WhiteSettings::getInstance().setMusicWeatherCity(field->getLastText().trimmed());
		box->closeBox();
	};
	box->addButton(trWhitegram(QString::fromUtf8("Сохранить"), QString("Save")), apply);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	field->submits() | rpl::on_next([=](Qt::KeyboardModifiers) {
		apply();
	}, field->lifetime());
}

const auto kWhitegramMusicIslandMeta = BuildHelper({
	.id = WhitegramMusicIsland::Id(),
	.parentId = MainId(),
	.title = QString::fromUtf8("Музыкальный островок"),
	.icon = &st::menuIconSoundOn,
}, [](SectionBuilder &builder) {
	auto white = WhiteSectionBuilder(builder);
	const auto settings = &WhiteSettings::getInstance();

	builder.addSkip();
	white.addSettingToggle({
		.id = u"whitegram/musicIslandEnabled"_q,
		.title = trWhitegram(
			QString::fromUtf8("Музыкальный островок"),
			QString("Music island")),
		.getter = &WhiteSettings::musicIslandEnabled,
		.setter = &WhiteSettings::setMusicIslandEnabled,
		.icon = { &st::menuIconSoundOn },
		.keywords = {
			u"music"_q,
			u"island"_q,
			u"player"_q,
			QString::fromUtf8("музыка"),
			QString::fromUtf8("островок"),
			QString::fromUtf8("плеер"),
		},
	});
	builder.addSkip();
	builder.addDividerText(trWhitegram(
		QString::fromUtf8(
			"Плавающий плеер поверх чата. Появляется, когда играет "
			"музыка, и раскрывается при наведении или клике."),
		QString(
			"A floating player over the chat. It appears while music is "
			"playing and expands on hover or click.")));

	builder.scope([&] {
		builder.addSkip();
		builder.addSubsectionTitle(trWhitegram(
			QString::fromUtf8("Расположение"),
			QString("Placement")));

		white.addChooseButton({
			.id = u"whitegram/musicIslandPosition"_q,
			.title = trWhitegram(
				QString::fromUtf8("Положение"),
				QString("Position")),
			.boxTitle = trWhitegram(
				QString::fromUtf8("Положение островка"),
				QString("Island position")),
			.initialSelection = ([&] {
				if (settings->musicIslandPosition() == 4) {
					return 4;
				}
				const auto x = settings->musicIslandFreeX();
				const auto y = settings->musicIslandFreeY();
				if (x <= 50 && y <= 50) return 0;
				if (x >= 950 && y <= 50) return 1;
				if (x <= 50 && y >= 950) return 2;
				if (x >= 950 && y >= 950) return 3;
				return 4;
			})(),
			.options = MusicIslandCornerOptions(),
			.setter = [](int index) {
				auto &islandSettings = WhiteSettings::getInstance();
				islandSettings.setMusicIslandPosition(index);
				if (index == 4) {
					return;
				}
				islandSettings.setMusicIslandFreeX(
					(index == 0 || index == 2) ? 0 : 1000);
				islandSettings.setMusicIslandFreeY(
					(index < 2) ? 0 : 1000);
			},
			.icon = { &st::menuIconPin },
			.keywords = {
				u"corner"_q,
				u"position"_q,
				u"drag"_q,
				u"free"_q,
				QString::fromUtf8("угол"),
				QString::fromUtf8("положение"),
				QString::fromUtf8("перетаскивание"),
				QString::fromUtf8("свободное"),
			},
		});

		white.addSettingToggle({
			.id = u"whitegram/musicIslandLocked"_q,
			.title = trWhitegram(
				QString::fromUtf8("Запретить перетаскивание"),
				QString("Disable dragging")),
			.getter = &WhiteSettings::musicIslandLocked,
			.setter = &WhiteSettings::setMusicIslandLocked,
			.icon = { &st::menuIconLock },
			.keywords = {
				u"lock"_q,
				u"pin"_q,
				u"fixed"_q,
				QString::fromUtf8("закрепить"),
				QString::fromUtf8("зафиксировать"),
				QString::fromUtf8("позиция"),
			},
		});

		builder.addSkip();
		builder.addDividerText(trWhitegram(
			QString::fromUtf8(
				"Перетащите островок мышью в любое место окна — "
				"он там и останется."),
			QString(
				"Drag the island anywhere in the window — it stays where "
				"you drop it.")));
		builder.addSkip();

		builder.addSubsectionTitle(trWhitegram(
			QString::fromUtf8("Поведение"),
			QString("Behaviour")));

		white.addChooseButton({
			.id = u"whitegram/musicIslandExpandMode"_q,
			.title = trWhitegram(
				QString::fromUtf8("Раскрывать"),
				QString("Expand")),
			.boxTitle = trWhitegram(
				QString::fromUtf8("Когда раскрывать островок"),
				QString("When to expand the island")),
			.initialSelection = settings->musicIslandExpandMode(),
			.options = MusicIslandExpandModeOptions(),
			.setter = [](int index) {
				WhiteSettings::getInstance().setMusicIslandExpandMode(index);
			},
			.icon = { &st::menuIconExpand },
			.keywords = {
				u"hover"_q,
				u"click"_q,
				u"expand"_q,
				QString::fromUtf8("наведение"),
				QString::fromUtf8("клик"),
				QString::fromUtf8("раскрытие"),
			},
		});
		white.addSettingToggle({
			.id = u"whitegram/musicIslandClickOpensTrack"_q,
			.title = trWhitegram(
				QString::fromUtf8("Клик по названию открывает трек"),
				QString("Clicking the title opens the track")),
			.getter = &WhiteSettings::musicIslandClickOpensTrack,
			.setter = &WhiteSettings::setMusicIslandClickOpensTrack,
			.icon = { &st::menuIconShowInChat },
			.keywords = {
				u"click"_q,
				u"track"_q,
				QString::fromUtf8("клик"),
				QString::fromUtf8("трек"),
			},
		});
		white.addSettingToggle({
			.id = u"whitegram/musicIslandHideTopBar"_q,
			.title = trWhitegram(
				QString::fromUtf8("Скрывать обычный плеер"),
				QString("Hide the regular player bar")),
			.getter = &WhiteSettings::musicIslandHideTopBar,
			.setter = &WhiteSettings::setMusicIslandHideTopBar,
			.icon = { &st::menuIconCaptionHide },
			.keywords = {
				u"topbar"_q,
				u"player"_q,
				QString::fromUtf8("полоса"),
				QString::fromUtf8("плеер"),
			},
		});

		builder.addSkip();
		builder.addDivider();
		builder.addSkip();

		builder.addSubsectionTitle(trWhitegram(
			QString::fromUtf8("Оформление"),
			QString("Look")));

		white.addSettingToggle({
			.id = u"whitegram/musicIslandCoverColors"_q,
			.title = trWhitegram(
				QString::fromUtf8("Цвета из обложки"),
				QString("Colours from the cover art")),
			.getter = &WhiteSettings::musicIslandCoverColors,
			.setter = &WhiteSettings::setMusicIslandCoverColors,
			.icon = { &st::menuIconChangeColors },
			.keywords = {
				u"cover"_q,
				u"color"_q,
				QString::fromUtf8("обложка"),
				QString::fromUtf8("цвет"),
			},
		});
		white.addSettingToggle({
			.id = u"whitegram/musicIslandBlurCover"_q,
			.title = trWhitegram(
				QString::fromUtf8("Размытая обложка на фоне"),
				QString("Blurred cover behind the player")),
			.getter = &WhiteSettings::musicIslandBlurCover,
			.setter = &WhiteSettings::setMusicIslandBlurCover,
			.icon = { &st::menuIconPhoto },
			.keywords = {
				u"blur"_q,
				u"background"_q,
				QString::fromUtf8("размытие"),
				QString::fromUtf8("фон"),
			},
			.shown = MusicIslandFlagValue(
				&WhiteSettings::musicIslandCoverColors,
				&WhiteSettings::musicIslandCoverColorsChanges),
		});
		white.addSettingToggle({
			.id = u"whitegram/musicIslandEqualizer"_q,
			.title = trWhitegram(
				QString::fromUtf8("Эквалайзер в свёрнутом виде"),
				QString("Equalizer when collapsed")),
			.getter = &WhiteSettings::musicIslandEqualizer,
			.setter = &WhiteSettings::setMusicIslandEqualizer,
			.icon = { &st::menuIconStats },
			.keywords = {
				u"equalizer"_q,
				u"visualizer"_q,
				QString::fromUtf8("эквалайзер"),
				QString::fromUtf8("визуализатор"),
			},
		});

		builder.addButton({
			.id = u"whitegram/musicIslandCustomBackgroundChoose"_q,
			.altIds = { u"whitegram/musicIslandCustomBackgroundEnabled"_q },
			.title = MusicIslandHasBackgroundValue(
			) | rpl::map([](bool has) {
				const auto id = Lang::GetInstance().id();
				const auto ru = id.startsWith(u"ru"_q)
					|| Lang::GetInstance().baseId().startsWith(u"ru"_q);
				return has
					? (ru ? QString::fromUtf8("Свой фон") : QString("Custom background"))
					: (ru ? QString::fromUtf8("Выбрать свой фон…") : QString("Choose a background…"));
			}),
			.icon = { &st::menuIconPhotoSet },
			.label = rpl::combine(
				MusicIslandHasBackgroundValue(),
				WhiteSettings::getInstance().musicIslandCustomBackgroundPathValue()
			) | rpl::map([](bool has, const QString &path) {
				return has ? MusicIslandBackgroundCurrentText(path) : QString();
			}),
			.onClick = [=] {
				const auto filters = u"Image files (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;"_q
					+ FileDialog::AllFilesFilter();
				FileDialog::GetOpenPath(
					Core::App().getFileDialogParent(),
					u"Open image file"_q,
					filters,
					[=](const FileDialog::OpenResult &result) {
						if (result.paths.isEmpty()) {
							return;
						}
						auto &backgroundSettings = WhiteSettings::getInstance();
						backgroundSettings.setMusicIslandCustomBackgroundPath(
							result.paths.front());
						backgroundSettings.setMusicIslandCustomBackgroundEnabled(true);
					});
			},
			.keywords = {
				u"background"_q,
				u"image"_q,
				u"photo"_q,
				QString::fromUtf8("фон"),
				QString::fromUtf8("картинка"),
				QString::fromUtf8("фото"),
			},
		});

		builder.scope([&] {
			white.addChooseButton({
				.id = u"whitegram/musicIslandCustomBackgroundMode"_q,
				.title = trWhitegram(
					QString::fromUtf8("Размещение фона"),
					QString("Background fit")),
				.boxTitle = trWhitegram(
					QString::fromUtf8("Размещение фона"),
					QString("Background fit")),
				.initialSelection = settings->musicIslandCustomBackgroundMode(),
				.options = MusicIslandBackgroundModeOptions(),
				.setter = [](int index) {
					WhiteSettings::getInstance().setMusicIslandCustomBackgroundMode(index);
				},
				.icon = { &st::menuIconPhoto },
				.keywords = {
					u"fit"_q,
					u"fill"_q,
					u"stretch"_q,
					QString::fromUtf8("заполнение"),
					QString::fromUtf8("вписать"),
					QString::fromUtf8("растянуть"),
				},
			});
			white.addSettingToggle({
				.id = u"whitegram/musicIslandCustomBackgroundBlur"_q,
				.title = trWhitegram(
					QString::fromUtf8("Размыть фон"),
					QString("Blur the background")),
				.getter = &WhiteSettings::musicIslandCustomBackgroundBlur,
				.setter = &WhiteSettings::setMusicIslandCustomBackgroundBlur,
				.icon = { &st::menuIconPhoto },
				.keywords = {
					u"blur"_q,
					QString::fromUtf8("размытие"),
					QString::fromUtf8("блюр"),
				},
			});
			white.addSlider({
				.id = u"whitegram/musicIslandCustomBackgroundOpacity"_q,
				.title = trWhitegram(
					QString::fromUtf8("Непрозрачность"),
					QString("Opacity")),
				.steps = 101,
				.current = settings->musicIslandCustomBackgroundOpacity(),
				.indexToValue = [](int index) { return index; },
				.onFinalChanged = [](int value) {
					WhiteSettings::getInstance().setMusicIslandCustomBackgroundOpacity(value);
				},
				.formatLabel = [](int value) {
					return QString::number(value) + u'%';
				},
				.keywords = { u"opacity"_q, QString::fromUtf8("прозрачность") },
			});
			white.addSlider({
				.id = u"whitegram/musicIslandCustomBackgroundDim"_q,
				.title = trWhitegram(
					QString::fromUtf8("Затемнение"),
					QString("Dimming")),
				.steps = 101,
				.current = settings->musicIslandCustomBackgroundDim(),
				.indexToValue = [](int index) { return index; },
				.onFinalChanged = [](int value) {
					WhiteSettings::getInstance().setMusicIslandCustomBackgroundDim(value);
				},
				.formatLabel = [](int value) {
					return QString::number(value) + u'%';
				},
				.keywords = { u"dim"_q, QString::fromUtf8("затемнение") },
			});
			builder.addButton({
				.id = u"whitegram/musicIslandCustomBackgroundReset"_q,
				.title = trWhitegram(
					QString::fromUtf8("Убрать свой фон"),
					QString("Remove custom background")),
				.st = &st::settingsAttentionButtonWithIcon,
				.icon = { &st::menuIconDeleteAttention },
				.onClick = [=] {
					auto &backgroundSettings = WhiteSettings::getInstance();
					backgroundSettings.setMusicIslandCustomBackgroundEnabled(false);
					backgroundSettings.setMusicIslandCustomBackgroundPath(QString());
				},
			});
		}, MusicIslandHasBackgroundValue());

		builder.addSkip();
		builder.addDivider();
		builder.addSkip();

		builder.addSubsectionTitle(trWhitegram(
			QString::fromUtf8("Текст песни"),
			QString("Lyrics")));

		white.addSettingToggle({
			.id = u"whitegram/musicLyricsEnabled"_q,
			.title = trWhitegram(
				QString::fromUtf8("Показывать текст песни"),
				QString("Show lyrics")),
			.getter = &WhiteSettings::musicLyricsEnabled,
			.setter = &WhiteSettings::setMusicLyricsEnabled,
			.icon = { &st::menuIconTranslate },
			.keywords = {
				u"lyrics"_q,
				u"text"_q,
				u"karaoke"_q,
				QString::fromUtf8("текст"),
				QString::fromUtf8("лирика"),
				QString::fromUtf8("караоке"),
			},
		});

		builder.scope([&] {
			white.addSettingToggle({
				.id = u"whitegram/musicLyricsShowInIsland"_q,
				.title = trWhitegram(
					QString::fromUtf8("Панель текста в островке"),
					QString("Lyrics panel in the island")),
				.getter = &WhiteSettings::musicLyricsShowInIsland,
				.setter = &WhiteSettings::setMusicLyricsShowInIsland,
				.icon = { &st::menuIconExpand },
				.keywords = {
					u"panel"_q,
					QString::fromUtf8("панель"),
				},
			});
			white.addSettingToggle({
				.id = u"whitegram/musicLyricsFloatingLine"_q,
				.title = trWhitegram(
					QString::fromUtf8("Строка текста в свёрнутом виде"),
					QString("Lyrics line when collapsed")),
				.getter = &WhiteSettings::musicLyricsFloatingLine,
				.setter = &WhiteSettings::setMusicLyricsFloatingLine,
				.icon = { &st::menuIconShowInChat },
				.keywords = {
					u"line"_q,
					QString::fromUtf8("строка"),
				},
			});
			white.addSettingToggle({
				.id = u"whitegram/musicLyricsKaraokeButton"_q,
				.title = trWhitegram(
					QString::fromUtf8("Кнопка окна караоке"),
					QString("Karaoke window button")),
				.getter = &WhiteSettings::musicLyricsKaraokeButton,
				.setter = &WhiteSettings::setMusicLyricsKaraokeButton,
				.icon = { &st::menuIconUnmute },
				.keywords = {
					u"window"_q,
					u"karaoke"_q,
					QString::fromUtf8("окно"),
					QString::fromUtf8("караоке"),
				},
			});
			white.addSettingToggle({
				.id = u"whitegram/musicLyricsTranslate"_q,
				.title = trWhitegram(
					QString::fromUtf8("Перевод текста"),
					QString("Lyrics translation")),
				.getter = &WhiteSettings::musicLyricsTranslate,
				.setter = &WhiteSettings::setMusicLyricsTranslate,
				.icon = { &st::menuIconTranslate },
				.keywords = {
					u"translate"_q,
					u"translation"_q,
					QString::fromUtf8("перевод"),
				},
			});
			white.addSettingToggle({
				.id = u"whitegram/musicLyricsKeyword"_q,
				.title = trWhitegram(
					QString::fromUtf8("Выделять ключевое слово"),
					QString("Highlight the key word")),
				.getter = &WhiteSettings::musicLyricsKeyword,
				.setter = &WhiteSettings::setMusicLyricsKeyword,
				.icon = { &st::menuIconPalette },
				.keywords = {
					u"keyword"_q,
					u"glitch"_q,
					QString::fromUtf8("ключевое"),
					QString::fromUtf8("слово"),
				},
			});
			white.addSettingToggle({
				.id = u"whitegram/musicLyricsBassPulse"_q,
				.title = trWhitegram(
					QString::fromUtf8("Пульсация фона под бас"),
					QString("Background pulses with the bass")),
				.getter = &WhiteSettings::musicLyricsBassPulse,
				.setter = &WhiteSettings::setMusicLyricsBassPulse,
				.icon = { &st::menuIconStats },
				.keywords = {
					u"bass"_q,
					u"pulse"_q,
					QString::fromUtf8("бас"),
					QString::fromUtf8("пульс"),
				},
			});
			white.addSettingToggle({
				.id = u"whitegram/musicLyricsChorus"_q,
				.title = trWhitegram(
					QString::fromUtf8("Находить припев"),
					QString("Detect the chorus")),
				.getter = &WhiteSettings::musicLyricsChorus,
				.setter = &WhiteSettings::setMusicLyricsChorus,
				.icon = { &st::menuIconUnmute },
				.keywords = {
					u"chorus"_q,
					u"hook"_q,
					QString::fromUtf8("припев"),
				},
			});
			white.addSlider({
				.id = u"whitegram/musicLyricsFontScale"_q,
				.title = trWhitegram(
					QString::fromUtf8("Размер текста"),
					QString("Text size")),
				.steps = 19,
				.current = settings->musicLyricsFontScale(),
				.indexToValue = [](int index) { return 70 + index * 5; },
				.onFinalChanged = [](int value) {
					WhiteSettings::getInstance().setMusicLyricsFontScale(value);
				},
				.formatLabel = [](int value) {
					return QString::number(value) + u'%';
				},
				.keywords = { u"font"_q, u"size"_q, QString::fromUtf8("размер") },
			});
			const auto weatherController = builder.controller();
			builder.addButton({
				.id = u"whitegram/musicWeatherCity"_q,
				.title = trWhitegram(
					QString::fromUtf8("Город для погоды"),
					QString("Weather city")),
				.icon = { &st::menuIconPhoto },
				.label = WhiteSettings::getInstance().musicWeatherCityValue(
				) | rpl::map([](const QString &city) {
					return city.isEmpty() ? QString::fromUtf8("По IP-адресу") : city;
				}),
				.onClick = [=] {
					if (weatherController) {
						weatherController->show(Box(ShowWeatherCityBox));
					}
				},
				.keywords = {
					u"weather"_q,
					u"city"_q,
					QString::fromUtf8("погода"),
					QString::fromUtf8("город"),
				},
			});
			builder.addButton({
				.id = u"whitegram/musicKaraokeCover"_q,
				.title = trWhitegram(
					QString::fromUtf8("Картинка «Сейчас играет»"),
					QString("\"Now playing\" image")),
				.icon = { &st::menuIconPhotoSet },
				.label = WhiteSettings::getInstance().musicKaraokeCoverPathValue(
				) | rpl::map([](const QString &path) {
					return path.isEmpty()
						? QString::fromUtf8("Обложка трека")
						: MusicIslandBackgroundCurrentText(path);
				}),
				.onClick = [=] {
					const auto filters = u"Image files (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;"_q
						+ FileDialog::AllFilesFilter();
					FileDialog::GetOpenPath(
						Core::App().getFileDialogParent(),
						u"Open image file"_q,
						filters,
						[=](const FileDialog::OpenResult &result) {
							if (!result.paths.isEmpty()) {
								WhiteSettings::getInstance().setMusicKaraokeCoverPath(
									result.paths.front());
							}
						});
				},
				.keywords = {
					u"cover"_q,
					u"image"_q,
					QString::fromUtf8("картинка"),
					QString::fromUtf8("обложка"),
					QString::fromUtf8("караоке"),
				},
			});
			builder.addButton({
				.id = u"whitegram/musicKaraokeCoverReset"_q,
				.title = trWhitegram(
					QString::fromUtf8("Вернуть обложку трека"),
					QString("Use the track cover")),
				.st = &st::settingsAttentionButtonWithIcon,
				.icon = { &st::menuIconDeleteAttention },
				.onClick = [=] {
					WhiteSettings::getInstance().setMusicKaraokeCoverPath(QString());
				},
				.shown = WhiteSettings::getInstance().musicKaraokeCoverPathValue(
				) | rpl::map([](const QString &path) { return !path.isEmpty(); }),
			});
			builder.addSkip();
			builder.addDividerText(trWhitegram(
				QString::fromUtf8(
					"Картинка для карточки «Сейчас играет» на экране караоке. "
					"Лучшее разрешение — 1440×640 (соотношение 9:4), "
					"минимум 720×320. Поверх картинки рисуется эквалайзер "
					"трека."),
				QString(
					"Image for the \"Now playing\" card on the karaoke screen. "
					"Best resolution is 1440×640 (9:4), at least 720×320. "
					"The track equalizer is drawn on top of it.")));
		}, MusicIslandFlagValue(
			&WhiteSettings::musicLyricsEnabled,
			&WhiteSettings::musicLyricsEnabledChanges));

		builder.addSkip();
		builder.addDividerText(trWhitegram(
			QString::fromUtf8(
				"Текст берётся с LRCLIB и NetEase. Припев и пульсация "
				"считаются по самому треку прямо на компьютере. Клик по "
				"панели текста открывает караоке на весь экран."),
			QString(
				"Lyrics come from LRCLIB and NetEase. The chorus and the "
				"pulse are computed from the track itself, on your "
				"computer. Click the lyrics panel for full-screen karaoke.")));
	}, WhiteSettings::getInstance().musicIslandEnabledValue());

	builder.addSkip();
});

void ShowBypassCustomMirrorBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(trWhitegram(
		QString::fromUtf8("Свой Worker"),
		QString("Custom Worker")));
	box->setWidth(st::boxWideWidth);

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		trWhitegram(
			QString::fromUtf8(
				"Домен Cloudflare Worker или полный хост. Несколько "
				"адресов — через запятую. Whitegram попробует их "
				"вместе с общими зеркалами."),
			QString(
				"Cloudflare Worker domain or full host. Separate "
				"several entries with commas. Whitegram will try them "
				"along with shared mirrors.")),
		st::boxLabel));

	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::whitegramBypassMirrorField,
			trWhitegram(
				QString::fromUtf8("my-worker.workers.dev"),
				QString("my-worker.workers.dev")),
			WhiteSettings::getInstance().connectionBypassSecret().trimmed()),
		st::whitegramBypassMirrorFieldMargin);
	field->setFocus();

	const auto apply = [=] {
		WhiteSettings::getInstance().setConnectionBypassSecret(
			field->getLastText().trimmed());
		box->closeBox();
	};

	box->addButton(trWhitegram(
		QString::fromUtf8("Подтвердить"),
		QString("Confirm")), apply);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	if (!WhiteSettings::getInstance().connectionBypassSecret().trimmed().isEmpty()) {
		box->addLeftButton(trWhitegram(
			QString::fromUtf8("Удалить"),
			QString("Remove")), [=] {
			WhiteSettings::getInstance().setConnectionBypassSecret(QString());
			box->closeBox();
		});
	}

	field->submits() | rpl::on_next([=](Qt::KeyboardModifiers) {
		apply();
	}, field->lifetime());
}

void ShowBypassAboutBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(trWhitegram(
		QString::fromUtf8("Как работает обход"),
		QString("How bypass works")));
	box->setWidth(st::boxWideWidth);

	const auto addBlock = [&](
			rpl::producer<QString> title,
			rpl::producer<QString> text) {
		Ui::AddSubsectionTitle(box->verticalLayout(), std::move(title));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::boxLabel));
	};

	addBlock(
		trWhitegram(
			QString::fromUtf8("Когда нужен"),
			QString("When you need it")),
		trWhitegram(
			QString::fromUtf8(
				"Интернет работает, сайты открываются, а Telegram висит "
				"на «Соединение…». Значит, сеть блокирует именно "
				"Telegram. Если всё и так работает, обход не нужен."),
			QString(
				"The internet works and websites open, but Telegram "
				"hangs on \"Connecting…\". The network blocks Telegram "
				"itself. If everything works, you don't need bypass.")));

	addBlock(
		trWhitegram(
			QString::fromUtf8("Что он делает"),
			QString("What it does")),
		trWhitegram(
			QString::fromUtf8(
				"Подключение к серверам Telegram идёт через WebSocket, "
				"поэтому для сети оно выглядит как обычный HTTPS-сайт. "
				"Шифрование сообщений не меняется."),
			QString(
				"The connection to Telegram servers goes over "
				"WebSocket, so to the network it looks like a regular "
				"HTTPS site. Message encryption stays the same.")));

	addBlock(
		trWhitegram(
			QString::fromUtf8("Это не VPN"),
			QString("It is not a VPN")),
		trWhitegram(
			QString::fromUtf8(
				"Обход работает только для Whitegram. IP-адрес не "
				"меняется, браузер и другие программы идут напрямую. "
				"Ваш прокси из настроек Telegram продолжит работать."),
			QString(
				"Bypass only affects Whitegram. Your IP address stays "
				"the same, the browser and other apps connect directly. "
				"Your proxy from Telegram settings keeps working.")));

	addBlock(
		trWhitegram(
			QString::fromUtf8("Режимы"),
			QString("Modes")),
		trWhitegram(
			QString::fromUtf8(
				"Выкл — обычное соединение.\n"
				"Авто — обход включается сам, если Telegram не "
				"подключился примерно за 12 секунд.\n"
				"Всегда — обход включён сразу. Выбирайте, если Telegram "
				"у вас не открывается постоянно."),
			QString(
				"Off — regular connection.\n"
				"Auto — bypass turns on by itself if Telegram doesn't "
				"connect in about 12 seconds.\n"
				"Always — bypass is on from the start. Pick it if "
				"Telegram never opens for you.")));

	addBlock(
		trWhitegram(
			QString::fromUtf8("Если не помогло"),
			QString("If it didn't help")),
		trWhitegram(
			QString::fromUtf8(
				"После смены режима подождите 10–20 секунд. Если чаты "
				"так и не загрузились, проверьте время на компьютере и "
				"попробуйте другую сеть, например раздачу с телефона. "
				"Через обход соединение может быть чуть медленнее."),
			QString(
				"After switching modes, wait 10–20 seconds. If chats "
				"still don't load, check the computer clock and try "
				"another network, like a phone hotspot. The connection "
				"may be a bit slower through bypass.")));

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

[[nodiscard]] bool IsRuLang() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	return id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
}

void AddTgWsProxyBlock(not_null<Ui::VerticalLayout*> layout) {
	using namespace WhiteFeatures::TgWsProxy;

	Ui::AddSkip(layout);
	Ui::AddDivider(layout);
	Ui::AddSkip(layout);
	Ui::AddSubsectionTitle(
		layout,
		trWhitegram(
			QString::fromUtf8("Обход 2 · TG WS Proxy"),
			QString("Bypass 2 · TG WS Proxy")));

	const auto card = layout->add(
		object_ptr<BypassMirrorCard>(layout),
		st::whitegramBypassMirrorCardMargin)->content();

	card->add(
		object_ptr<Ui::FlatLabel>(
			card,
			trWhitegram(
				QString::fromUtf8(
					"Запасной способ: встроенный tg-ws-proxy от Flowseal. "
					"Держит открытые соединения с Telegram, при сбое "
					"переключается на Cloudflare. Включайте, если обычный "
					"обход не помог. Пока он работает, обычный обход и ваш "
					"прокси отключены, после выключения они вернутся."),
				QString(
					"Fallback option: the built-in tg-ws-proxy by Flowseal. "
					"It keeps connections to Telegram open and switches to "
					"Cloudflare on failure. Turn it on if the regular bypass "
					"didn't help. While it runs, the regular bypass and your "
					"proxy are off; they come back when you turn it off.")),
			st::whitegramBypassMirrorHint));

	Ui::AddSkip(card, st::settingsCheckboxesSkip);

	const auto statusLabel = card->add(
		object_ptr<Ui::FlatLabel>(
			card,
			rpl::single(QString()),
			st::whitegramBypassMirrorStatus));
	statusValue(
	) | rpl::on_next([=](const Status &status) {
		const auto isRu = IsRuLang();
		auto text = QString();
		auto active = false;
		switch (status.state) {
		case State::Off:
			text = isRu
				? QString::fromUtf8("Статус: выключен")
				: QString("Status: off");
			break;
		case State::Starting:
			text = isRu
				? QString::fromUtf8("Статус: запускается…")
				: QString("Status: starting…");
			break;
		case State::Running:
			text = (isRu
				? QString::fromUtf8("Статус: работает на 127.0.0.1:%1")
				: QString("Status: running on 127.0.0.1:%1")
			).arg(status.port);
			active = true;
			break;
		case State::Failed:
			text = (isRu
				? QString::fromUtf8("Статус: ошибка — %1")
				: QString("Status: error — %1")
			).arg(status.error);
			break;
		}
		statusLabel->setText(text);
		statusLabel->setTextColorOverride(active
			? st::windowActiveTextFg->c
			: (status.state == State::Failed)
			? std::make_optional(st::attentionButtonFg->c)
			: std::optional<QColor>());
	}, statusLabel->lifetime());

	Ui::AddSkip(card, st::whitegramBypassMirrorButtonSkip);

	const auto button = card->add(
		object_ptr<Ui::RoundButton>(
			card,
			WhiteSettings::getInstance().tgWsProxyEnabledValue(
			) | rpl::map([](bool enabled) {
				const auto isRu = IsRuLang();
				return enabled
					? (isRu ? QString::fromUtf8("Выключить") : QString("Turn off"))
					: (isRu ? QString::fromUtf8("Включить") : QString("Turn on"));
			}),
			st::whitegramBypassMirrorButton));
	button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	button->setClickedCallback([] {
		auto &settings = WhiteSettings::getInstance();
		settings.setTgWsProxyEnabled(!settings.tgWsProxyEnabled());
	});
}

void BuildWhitegramBypass(SectionBuilder &builder) {
	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto wrap = object_ptr<Ui::VerticalLayout>(ctx.container);
		const auto layout = wrap.data();

		Ui::AddSkip(layout);

		auto icon = object_ptr<Ui::RpWidget>(layout);
		const auto iconRaw = icon.data();
		const auto iconSize = st::settingsFilterIconSize;
		iconRaw->resize(QSize(iconSize, iconSize));
		iconRaw->setNaturalWidth(iconSize);
		iconRaw->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(iconRaw);
			auto hq = PainterHighQualityEnabler(p);
			const auto rect = iconRaw->rect();
			const auto active = WhiteSettings::getInstance().connectionBypassMode()
				!= ConnectionBypassMode::Off;
			p.setPen(Qt::NoPen);
			p.setBrush(active ? st::windowBgActive : st::windowBgOver);
			p.drawEllipse(rect);
			st::menuIconUnblock.paintInCenter(
				p,
				rect,
				active ? st::windowFgActive->c : st::windowSubTextFg->c);
		}, iconRaw->lifetime());
		WhiteSettings::getInstance().connectionBypassModeChanges(
		) | rpl::on_next([=] {
			iconRaw->update();
		}, iconRaw->lifetime());
		layout->add(std::move(icon), style::al_top);

		Ui::AddSkip(layout);

		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				trWhitegram(
					QString::fromUtf8("Обход блокировок"),
					QString("Connection bypass")),
				st::boxTitle),
			style::al_top);

		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				WhiteSettings::getInstance().connectionBypassModeValue(
				) | rpl::map([](ConnectionBypassMode mode) {
					const auto id = Lang::GetInstance().id();
					const auto baseId = Lang::GetInstance().baseId();
					const auto isRu = id.startsWith(u"ru"_q)
						|| baseId.startsWith(u"ru"_q);
					switch (mode) {
					case ConnectionBypassMode::Off:
						return isRu
							? QString::fromUtf8("Прямое соединение")
							: QString("Direct connection");
					case ConnectionBypassMode::Auto:
						return isRu
							? QString::fromUtf8("Включается при сбое сети")
							: QString("Turns on if the connection fails");
					case ConnectionBypassMode::Always:
						return isRu
							? QString::fromUtf8("Обход всегда включён")
							: QString("Bypass always on");
					}
					return QString();
				}),
				st::boxDividerLabel),
			style::al_top);

		Ui::AddSkip(layout);
		Ui::AddDivider(layout);
		Ui::AddSkip(layout);
		Ui::AddSubsectionTitle(
			layout,
			trWhitegram(
				QString::fromUtf8("Режим"),
				QString("Mode")));

		const auto slider = layout->add(
			object_ptr<Ui::SettingsSlider>(layout, st::whitegramBypassSlider),
			st::whitegramBypassSliderMargin);
		const auto applySections = [=] {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q)
				|| baseId.startsWith(u"ru"_q);
			slider->setSections({
				isRu ? QString::fromUtf8("Выкл") : QString("Off"),
				isRu ? QString::fromUtf8("Авто") : QString("Auto"),
				isRu ? QString::fromUtf8("Всегда") : QString("Always"),
			});
			slider->setActiveSectionFast(static_cast<int>(
				WhiteSettings::getInstance().connectionBypassMode()));
		};
		applySections();
		Lang::GetInstance().idChanges(
		) | rpl::on_next([=] {
			applySections();
		}, slider->lifetime());
		slider->sectionActivated(
		) | rpl::on_next([=](int index) {
			WhiteSettings::getInstance().setConnectionBypassMode(
				static_cast<ConnectionBypassMode>(index));
		}, slider->lifetime());
		WhiteSettings::getInstance().connectionBypassModeChanges(
		) | rpl::on_next([=](ConnectionBypassMode mode) {
			if (slider->activeSection() != static_cast<int>(mode)) {
				slider->setActiveSectionFast(static_cast<int>(mode));
			}
		}, slider->lifetime());

		const auto cooldownHint = layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				rpl::single(QString()),
				st::boxDividerLabel),
			style::al_top);
		cooldownHint->hide();
		WhiteFeatures::ConnectionBypass::modeSwitchLockedValue(
		) | rpl::on_next([=](bool locked) {
			slider->setEnabled(!locked);
			if (locked) {
				const auto id = Lang::GetInstance().id();
				const auto baseId = Lang::GetInstance().baseId();
				const auto isRu = id.startsWith(u"ru"_q)
					|| baseId.startsWith(u"ru"_q);
				cooldownHint->setText(isRu
					? QString::fromUtf8(
						"Слишком частые переключения. Подождите несколько "
						"секунд, затем можно снова сменить режим.")
					: QString(
						"Too many mode changes. Wait a few seconds before "
						"switching again."));
				cooldownHint->show();
			} else {
				cooldownHint->hide();
			}
		}, slider->lifetime());

		Ui::AddSkip(layout);

		layout->add(
			object_ptr<Ui::FlatLabel>(
				layout,
				WhiteSettings::getInstance().connectionBypassModeValue(
				) | rpl::map([](ConnectionBypassMode mode) {
					const auto id = Lang::GetInstance().id();
					const auto baseId = Lang::GetInstance().baseId();
					const auto isRu = id.startsWith(u"ru"_q)
						|| baseId.startsWith(u"ru"_q);
					switch (mode) {
					case ConnectionBypassMode::Off:
						return isRu
							? QString::fromUtf8(
								"Whitegram подключается к Telegram напрямую. "
								"Если мессенджер недоступен, выберите Авто "
								"или Всегда.")
							: QString(
								"Whitegram connects to Telegram directly. "
								"If the messenger is unreachable, choose Auto "
								"or Always.");
					case ConnectionBypassMode::Auto:
						return isRu
							? QString::fromUtf8(
								"Сначала обычное соединение. Если за несколько "
								"секунд подключиться не удалось, включается "
								"обход через WebSocket. Если все зеркала "
								"недоступны — временно пробует обычный TCP.")
							: QString(
								"Starts with a direct connection. If it cannot "
								"connect for a few seconds, WebSocket bypass "
								"turns on. If all mirrors fail, it temporarily "
								"tries plain TCP.");
					case ConnectionBypassMode::Always:
						return isRu
							? QString::fromUtf8(
								"Обход включён постоянно. Помогает при "
								"блокировках провайдера, соединение может "
								"быть чуть медленнее.")
							: QString(
								"Bypass stays on. Helps when a provider blocks "
								"Telegram; the connection may be a bit slower.");
					}
					return QString();
				}),
				st::boxDividerLabel),
			st::boxRowPadding);

		Ui::AddSkip(layout);
		Ui::AddDivider(layout);
		Ui::AddSkip(layout);
		Ui::AddSubsectionTitle(
			layout,
			trWhitegram(
				QString::fromUtf8("Свои зеркала"),
				QString("Custom mirrors")));

		const auto mirrorCardWrap = layout->add(
			object_ptr<BypassMirrorCard>(layout),
			st::whitegramBypassMirrorCardMargin);
		const auto mirrorCard = mirrorCardWrap->content();

		mirrorCard->add(
			object_ptr<Ui::FlatLabel>(
				mirrorCard,
				trWhitegram(
					QString::fromUtf8(
						"Личный Cloudflare Worker — запасной маршрут, "
						"если общие зеркала недоступны. Адрес хранится "
						"только на этом устройстве."),
					QString(
						"A personal Cloudflare Worker is a backup route "
						"when shared mirrors are unavailable. The address "
						"is stored on this device only.")),
				st::whitegramBypassMirrorHint));

		Ui::AddSkip(mirrorCard, st::settingsCheckboxesSkip);

		const auto statusLabel = mirrorCard->add(
			object_ptr<Ui::FlatLabel>(
				mirrorCard,
				rpl::single(QString()),
				st::whitegramBypassMirrorStatus));
		WhiteSettings::getInstance().connectionBypassSecretValue(
		) | rpl::on_next([=](const QString &secret) {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q)
				|| baseId.startsWith(u"ru"_q);
			const auto configured = !secret.trimmed().isEmpty();
			statusLabel->setText(configured
				? (isRu
					? QString::fromUtf8("Статус: личный Worker подключён")
					: QString("Status: custom Worker is active"))
				: (isRu
					? QString::fromUtf8("Статус: Worker не задан")
					: QString("Status: no Worker configured")));
			statusLabel->setTextColorOverride(configured
				? st::windowActiveTextFg->c
				: std::optional<QColor>());
		}, statusLabel->lifetime());

		Ui::AddSkip(mirrorCard, st::whitegramBypassMirrorButtonSkip);

		const auto controller = ctx.controller;
		const auto mirrorButton = mirrorCard->add(
			object_ptr<Ui::RoundButton>(
				mirrorCard,
				WhiteSettings::getInstance().connectionBypassSecretValue(
				) | rpl::map([](const QString &secret) {
					const auto id = Lang::GetInstance().id();
					const auto baseId = Lang::GetInstance().baseId();
					const auto isRu = id.startsWith(u"ru"_q)
						|| baseId.startsWith(u"ru"_q);
					const auto configured = !secret.trimmed().isEmpty();
					if (configured) {
						return isRu
							? QString::fromUtf8("Изменить Worker")
							: QString("Edit Worker");
					}
					return isRu
						? QString::fromUtf8("Добавить Worker")
						: QString("Add Worker");
				}),
				st::whitegramBypassMirrorButton));
		mirrorButton->setTextTransform(
			Ui::RoundButtonTextTransform::NoTransform);
		mirrorButton->setClickedCallback([=] {
			controller->show(Box(ShowBypassCustomMirrorBox));
		});

		AddTgWsProxyBlock(layout);

		Ui::AddSkip(layout);

		const auto more = AddButtonWithIcon(
			layout,
			trWhitegram(
				QString::fromUtf8("Подробнее"),
				QString("Learn more")),
			st::settingsButton,
			{ &st::menuIconInfo });
		more->setClickedCallback([=] {
			controller->show(Box(ShowBypassAboutBox));
		});

		return { .widget = std::move(wrap) };
	}, [] {
		return SearchEntry{
			.id = u"whitegram/connectionBypassMode"_q,
			.title = QString::fromUtf8("Обход блокировок"),
			.keywords = {
				u"bypass"_q,
				u"websocket"_q,
				QString::fromUtf8("обход"),
				QString::fromUtf8("блокиров"),
			},
			.section = WhitegramBypass::Id(),
			.icon = { &st::menuIconUnblock },
		};
	});
}

const auto kWhitegramBypassMeta = BuildHelper({
	.id = WhitegramBypass::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Обход блокировок"),
	.icon = &st::menuIconUnblock,
}, [](SectionBuilder &builder) {
	BuildWhitegramBypass(builder);
});

const auto kWhitegramSelfDestructMeta = BuildHelper({
	.id = WhitegramSelfDestruct::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Самоуничтожение"),
	.icon = &st::menuIconDeleteAttention,
}, [](SectionBuilder &builder) {
	BuildWhitegramSelfDestructPage(builder);
});

const auto kWhitegramPluginsMeta = BuildHelper({
	.id = WhitegramPlugins::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Плагины"),
	.icon = &st::menuIconPermissions,
}, [](SectionBuilder &builder) {
	Whitegram::Plugins::BuildPluginsPage(builder);
});

QString FormatScheduleTime(int minutes) {
	int h = (minutes / 60) % 24;
	int m = minutes % 60;
	return QString("%1:%2").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0'));
}

QString FormatScheduleDays(int weekdays, bool isRu) {
	if ((weekdays & 0x7F) == 0x7F) {
		return isRu ? QString::fromUtf8("Каждый день") : QString("Every day");
	}
	if ((weekdays & 0x7F) == 0x1F) {
		return isRu ? QString::fromUtf8("Будни (Пн-Пт)") : QString("Weekdays (Mon-Fri)");
	}
	if ((weekdays & 0x7F) == 0x60) {
		return isRu ? QString::fromUtf8("Выходные (Сб-Вс)") : QString("Weekends (Sat-Sun)");
	}
	const QString ruDays[] = {
		QString::fromUtf8("Пн"), QString::fromUtf8("Вт"),
		QString::fromUtf8("Ср"), QString::fromUtf8("Чт"),
		QString::fromUtf8("Пт"), QString::fromUtf8("Сб"),
		QString::fromUtf8("Вс")
	};
	const QString enDays[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
	QStringList selected;
	for (int i = 0; i < 7; ++i) {
		if (weekdays & (1 << i)) {
			selected.push_back(isRu ? ruDays[i] : enDays[i]);
		}
	}
	return selected.isEmpty()
		? (isRu ? QString::fromUtf8("Нет дней") : QString("No days"))
		: selected.join(u", "_q);
}

void ShowGhostScheduleIntervalBox(
		not_null<Window::SessionController*> controller,
		uint64 userId,
		std::optional<int> editingIndex = std::nullopt) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);

		box->setTitle(rpl::single(editingIndex.has_value()
			? (isRu ? QString::fromUtf8("Редактировать интервал") : QString("Edit interval"))
			: (isRu ? QString::fromUtf8("Новый интервал") : QString("New interval"))));

		const auto entries = WhiteSettings::ghost(userId).ghostScheduleEntries();
		GhostScheduleEntry initialEntry;
		if (editingIndex.has_value() && *editingIndex >= 0 && *editingIndex < int(entries.size())) {
			initialEntry = entries[*editingIndex];
		}

		const auto fromField = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(isRu ? QString::fromUtf8("Время начала (ЧЧ:ММ)") : QString("Start time (HH:MM)")),
			FormatScheduleTime(initialEntry.from)));

		const auto toField = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(isRu ? QString::fromUtf8("Время окончания (ЧЧ:ММ)") : QString("End time (HH:MM)")),
			FormatScheduleTime(initialEntry.to)));

		struct DaysState {
			std::vector<Ui::Checkbox*> checkboxes;
			int weekdays = 0x7F;
		};
		const auto daysState = box->lifetime().make_state<DaysState>();
		daysState->weekdays = initialEntry.weekdays;

		const QString dayNamesRu[] = {
			QString::fromUtf8("Понедельник"),
			QString::fromUtf8("Вторник"),
			QString::fromUtf8("Среда"),
			QString::fromUtf8("Четверг"),
			QString::fromUtf8("Пятница"),
			QString::fromUtf8("Суббота"),
			QString::fromUtf8("Воскресенье")
		};
		const QString dayNamesEn[] = {
			"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
		};

		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(isRu ? QString::fromUtf8("Дни недели:") : QString("Days of week:")),
			st::boxTitle));

		for (int i = 0; i < 7; ++i) {
			const auto bit = (1 << i);
			const auto checked = (daysState->weekdays & bit) != 0;
			const auto dayName = isRu ? dayNamesRu[i] : dayNamesEn[i];
			const auto cb = box->addRow(object_ptr<Ui::Checkbox>(
				box,
				dayName,
				checked,
				st::defaultBoxCheckbox));
			daysState->checkboxes.push_back(cb);
			cb->checkedChanges(
			) | rpl::on_next([=](bool isChecked) {
				if (isChecked) {
					daysState->weekdays |= bit;
				} else {
					daysState->weekdays &= ~bit;
				}
			}, box->lifetime());
		}

		const auto setAllDays = [=](int mask) {
			daysState->weekdays = mask;
			for (int i = 0; i < 7; ++i) {
				daysState->checkboxes[i]->setChecked((mask & (1 << i)) != 0);
			}
		};

		const auto presetsWrap = box->addRow(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)));
		const auto presetsInner = presetsWrap->entity();

		const auto btnAll = presetsInner->add(object_ptr<Ui::LinkButton>(
			presetsInner,
			isRu ? QString::fromUtf8("Выбрать все дни") : QString("Select all days")));
		btnAll->setClickedCallback([=] { setAllDays(0x7F); });

		const auto btnWork = presetsInner->add(object_ptr<Ui::LinkButton>(
			presetsInner,
			isRu ? QString::fromUtf8("Только будни (Пн-Пт)") : QString("Weekdays only (Mon-Fri)")));
		btnWork->setClickedCallback([=] { setAllDays(0x1F); });

		const auto btnWeekend = presetsInner->add(object_ptr<Ui::LinkButton>(
			presetsInner,
			isRu ? QString::fromUtf8("Только выходные (Сб-Вс)") : QString("Weekends only (Sat-Sun)")));
		btnWeekend->setClickedCallback([=] { setAllDays(0x60); });

		auto parseTime = [](const QString &str, int defaultVal) -> int {
			const auto trimmed = str.trimmed();
			const auto parts = trimmed.split(':');
			if (parts.size() == 2) {
				bool okH = false, okM = false;
				int h = parts[0].toInt(&okH);
				int m = parts[1].toInt(&okM);
				if (okH && okM && h >= 0 && h < 24 && m >= 0 && m < 60) {
					return h * 60 + m;
				}
			}
			return defaultVal;
		};

		box->addButton(tr::lng_box_ok(), [=] {
			int fromMin = parseTime(fromField->getLastText(), 540);
			int toMin = parseTime(toField->getLastText(), 1080);
			int mask = daysState->weekdays;
			if (mask == 0) mask = 0x7F;

			GhostScheduleEntry newEntry{ fromMin, toMin, mask };
			auto &g = WhiteSettings::ghost(userId);
			if (editingIndex.has_value()) {
				g.updateGhostScheduleEntry(*editingIndex, newEntry);
			} else {
				g.addGhostScheduleEntry(newEntry);
			}
			box->closeBox();
		});

		if (editingIndex.has_value()) {
			box->addLeftButton(rpl::single(isRu ? QString::fromUtf8("Удалить") : QString("Delete")), [=] {
				auto &g = WhiteSettings::ghost(userId);
				g.removeGhostScheduleEntry(*editingIndex);
				box->closeBox();
			});
		}

		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void AddGhostTargetModeControls(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		uint64 userId) {
	const auto targetOptions = std::vector<QString>{
		tr::white_GhostTargetModeGlobal(tr::now),
		tr::white_GhostTargetModeWhitelist(tr::now),
		tr::white_GhostTargetModeBlacklist(tr::now),
	};
	const auto targetOptionText = WhiteSettings::ghost(userId).ghostTargetModeValue(
	) | rpl::map([=](GhostTargetMode value) {
		return targetOptions[static_cast<int>(value)];
	});
	const auto targetButton = AddButtonWithLabel(
		container,
		tr::white_GhostTargetMode(),
		std::move(targetOptionText),
		st::settingsButtonNoIcon);
	targetButton->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			const auto save = [=](int index) {
				WhiteSettings::ghost(userId).setGhostTargetMode(
					static_cast<GhostTargetMode>(index));
			};
			SingleChoiceBox(box, {
				.title = tr::white_GhostTargetMode(),
				.options = targetOptions,
				.initialSelection = static_cast<int>(
					WhiteSettings::ghost(userId).ghostTargetMode()),
				.callback = save,
			});
		}));
	});

	const auto managePeersCountLabel = [=] {
		auto &ghostSettings = WhiteSettings::ghost(userId);
		const auto mode = ghostSettings.ghostTargetMode();
		if (mode == GhostTargetMode::Whitelist) {
			const auto count = int(ghostSettings.ghostTargetPeers().size());
			return count > 0
				? tr::white_GhostTargetPeersCount(tr::now, lt_count, count)
				: tr::white_GhostNoPeersSelected(tr::now);
		} else if (mode == GhostTargetMode::Blacklist) {
			const auto count = int(ghostSettings.ghostExcludedPeers().size());
			return count > 0
				? tr::white_GhostExcludedPeersCount(tr::now, lt_count, count)
				: tr::white_GhostNoPeersSelected(tr::now);
		}
		return QString();
	};

	const auto managePeersLabelProducer = WhiteSettings::ghost(userId).ghostTargetModeValue(
	) | rpl::map([=](GhostTargetMode) {
		return managePeersCountLabel();
	});

	const auto manageWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto manageInner = manageWrap->entity();
	const auto manageButton = AddButtonWithLabel(
		manageInner,
		tr::white_GhostManageTargetPeers(),
		rpl::duplicate(managePeersLabelProducer),
		st::settingsButtonNoIcon);
	manageButton->addClickHandler([=] {
		Window::ShowChooseRecipientBox(
			controller,
			[=](not_null<Data::Thread*> thread) {
				auto &ghostSettings = WhiteSettings::ghost(userId);
				const auto peerId = thread->peer()->id.value;
				const auto mode = ghostSettings.ghostTargetMode();
				if (mode == GhostTargetMode::Whitelist) {
					ghostSettings.togglePeerInTargetList(peerId);
				} else if (mode == GhostTargetMode::Blacklist) {
					ghostSettings.togglePeerInExcludedList(peerId);
				}
				return true;
			},
			tr::white_GhostSelectChat());
	});
	AddSkip(manageInner);
	AddDividerText(manageInner,
		WhiteSettings::ghost(userId).ghostTargetModeValue(
	) | rpl::map([=](GhostTargetMode mode) {
		return (mode == GhostTargetMode::Whitelist)
			? tr::white_GhostManageTargetPeersDescription(tr::now)
			: tr::white_GhostManageExcludedPeersDescription(tr::now);
	}));

	WhiteSettings::ghost(userId).ghostTargetModeValue(
	) | rpl::on_next([=](GhostTargetMode mode) {
		manageWrap->toggle(mode != GhostTargetMode::Global, anim::type::normal);
	}, container->lifetime());
	manageWrap->toggle(
		WhiteSettings::ghost(userId).ghostTargetMode() != GhostTargetMode::Global,
		anim::type::instant);
	manageWrap->finishAnimating();

	AddSkip(container);
	AddDividerText(container, tr::white_GhostTargetModeDescription());
}

void BuildWhitegramGhostMode(SectionBuilder &builder) {
	builder.addSkip();
	BuildAyuGhostEssentials(builder);

	builder.addSkip();
	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Где работает"),
		QString("Where it works")));

	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			const auto userId = WhiteSettings::getInstance().useGlobalGhostMode()
				? uint64(0)
				: wctx.controller->session().userId().bare;
			AddGhostTargetModeControls(
				wctx.container,
				wctx.controller,
				userId);
		}, [&](const SearchContext &sctx) {
			sctx.entries->push_back({
				.id = u"whitegram/ghostTargetMode"_q,
				.title = tr::white_GhostTargetMode(tr::now),
				.section = sctx.sectionId,
			});
			sctx.entries->push_back({
				.id = u"whitegram/ghostManageTargetPeers"_q,
				.title = tr::white_GhostManageTargetPeers(tr::now),
				.section = sctx.sectionId,
			});
		});
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addSubsectionTitle(trWhitegram(
		QString::fromUtf8("Расписание"),
		QString("Schedule")));

	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			const auto container = wctx.container;
			const auto controller = wctx.controller;
			const auto userId = WhiteSettings::getInstance().useGlobalGhostMode()
				? uint64(0)
				: controller->session().userId().bare;

			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);

			const auto scheduleToggle = AddButtonWithIcon(
				container,
				trWhitegram(
					QString::fromUtf8("Включить расписание"),
					QString("Enable schedule")),
				st::settingsButtonNoIcon);
			scheduleToggle->toggleOn(
				WhiteSettings::ghost(userId).ghostScheduleEnabledValue()
			)->toggledValue(
			) | rpl::filter([=](bool enabled) {
				return enabled != WhiteSettings::ghost(userId).ghostScheduleEnabled();
			}) | on_next([=](bool enabled) {
				WhiteSettings::ghost(userId).setGhostScheduleEnabled(enabled);
			}, container->lifetime());

			AddSkip(container);
			AddDividerText(container, trWhitegram(
				QString::fromUtf8("Режим призрака сам включается в выбранные часы и дни недели и выключается вне их."),
				QString("Ghost mode turns on by itself during the chosen hours and days and turns off outside them.")));

			const auto scheduleWrap = container->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					container,
					object_ptr<Ui::VerticalLayout>(container)));
			const auto scheduleInner = scheduleWrap->entity();

			const auto intervalsList = scheduleInner->add(
				object_ptr<Ui::VerticalLayout>(scheduleInner));

			struct ScheduleItemsHolder {
				std::vector<base::unique_qptr<QWidget>> widgets;
			};
			const auto holder = scheduleInner->lifetime().make_state<ScheduleItemsHolder>();

			const auto rebuildIntervals = [=] {
				holder->widgets.clear();
				const auto &entries = WhiteSettings::ghost(userId).ghostScheduleEntries();
				if (entries.empty()) {
					auto emptyLabel = object_ptr<Ui::FlatLabel>(
						intervalsList,
						trWhitegram(
							QString::fromUtf8("Интервалов пока нет."),
							QString("No intervals yet.")),
						st::boxDividerLabel);
					holder->widgets.push_back(base::make_unique_q<QWidget>(intervalsList->add(std::move(emptyLabel), st::boxRowPadding)));
				} else {
					for (int i = 0; i < int(entries.size()); ++i) {
						const auto &entry = entries[i];
						const auto timeText = FormatScheduleTime(entry.from) + u" — "_q + FormatScheduleTime(entry.to);
						const auto daysText = FormatScheduleDays(entry.weekdays, isRu);
						const auto btn = AddButtonWithLabel(
							intervalsList,
							rpl::single(timeText),
							rpl::single(daysText),
							st::settingsButtonNoIcon);
						btn->addClickHandler([=] {
							ShowGhostScheduleIntervalBox(controller, userId, i);
						});
						holder->widgets.push_back(base::make_unique_q<QWidget>(btn));
					}
				}
				intervalsList->resizeToWidth(intervalsList->width());
			};

			rebuildIntervals();

			WhiteSettings::ghost(userId).ghostScheduleEntriesChanges(
			) | rpl::on_next([=] {
				rebuildIntervals();
			}, scheduleInner->lifetime());

			AddSkip(scheduleInner);

			const auto addIntervalBtn = AddButtonWithIcon(
				scheduleInner,
				trWhitegram(
					QString::fromUtf8("Добавить интервал"),
					QString("Add interval")),
				st::settingsButtonLightNoIcon);
			addIntervalBtn->addClickHandler([=] {
				ShowGhostScheduleIntervalBox(controller, userId, std::nullopt);
			});

			WhiteSettings::ghost(userId).ghostScheduleEnabledValue(
			) | rpl::on_next([=](bool enabled) {
				scheduleWrap->toggle(enabled, anim::type::normal);
			}, container->lifetime());
			scheduleWrap->toggle(
				WhiteSettings::ghost(userId).ghostScheduleEnabled(),
				anim::type::instant);
			scheduleWrap->finishAnimating();
		}, [&](const SearchContext &sctx) {
			sctx.entries->push_back({
				.id = u"whitegram/ghostSchedule"_q,
				.title = QString::fromUtf8("Расписание режима призрака"),
				.section = sctx.sectionId,
			});
		});
	});

	builder.addSkip();
	builder.addDivider();
	builder.addSkip();
	BuildAyuSpyContent(builder);
	builder.addSkip();
}

const auto kWhitegramGhostModeMeta = BuildHelper({
	.id = WhitegramGhostMode::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Режим призрака"),
	.icon = &st::menuIconStealth,
}, [](SectionBuilder &builder) {
	BuildWhitegramGhostMode(builder);
});

const auto kWhitegramChatsMeta = BuildHelper({
	.id = WhitegramChats::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Чаты и сообщения"),
	.icon = &st::menuIconChatBubble,
}, [](SectionBuilder &builder) {
	BuildAyuChatsContent(builder);
});

const auto kWhitegramMeta = BuildHelper({
	.id = WhitegramMain::Id(),
	.parentId = MainId(),
	.title = QString::fromUtf8("Whitegram"),
	.icon = &st::menuIconPremium,
}, [](SectionBuilder &builder) {
	BuildLogo(builder);

	builder.addSkip();
	BuildWhitegramVersionInfo(builder);
	builder.addSkip();

	builder.addSubsectionTitle(
		trWhitegram(
			QString::fromUtf8("Категории"),
			QString("Categories")));

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Основное"),
			QString("General")),
		.targetSection = WhitegramGeneral::Id(),
		.icon = { &st::menuIconShowAll },
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Режим призрака"),
			QString("Ghost Mode")),
		.targetSection = WhitegramGhostMode::Id(),
		.icon = { &st::menuIconStealth },
		.keywords = {
			u"ghost"_q,
			u"stealth"_q,
			QString::fromUtf8("призрак"),
			QString::fromUtf8("невидим"),
			QString::fromUtf8("расписание"),
			QString::fromUtf8("таймер"),
			QString::fromUtf8("область"),
			QString::fromUtf8("whitelist"),
			QString::fromUtf8("blacklist"),
		},
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Чаты и сообщения"),
			QString("Chats and messages")),
		.targetSection = WhitegramChats::Id(),
		.icon = { &st::menuIconChatBubble },
		.keywords = {
			u"chats"_q,
			u"messages"_q,
			QString::fromUtf8("стикеры"),
			QString::fromUtf8("контекстное меню"),
			QString::fromUtf8("сообщения"),
		},
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Фильтры сообщений"),
			QString("Message filters")),
		.targetSection = WhiteFilters::Id(),
		.icon = { &st::menuIconTagFilter },
		.keywords = {
			u"filters"_q,
			u"regex"_q,
			QString::fromUtf8("фильтр"),
		},
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Папки чатов"),
			QString("Chat folders")),
		.targetSection = WhitegramArchives::Id(),
		.icon = { &st::menuIconArchive },
	});
	
	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Оформление"),
			QString("Appearance")),
		.targetSection = WhitegramAppearance::Id(),
		.icon = { &st::menuIconPalette },
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Музыкальный островок"),
			QString("Music island")),
		.targetSection = WhitegramMusicIsland::Id(),
		.icon = { &st::menuIconSoundOn },
		.keywords = {
			u"music"_q,
			u"island"_q,
			u"player"_q,
			QString::fromUtf8("музыка"),
			QString::fromUtf8("островок"),
			QString::fromUtf8("плеер"),
		},
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Обход блокировок"),
			QString("Connection bypass")),
		.targetSection = WhitegramBypass::Id(),
		.icon = { &st::menuIconUnblock },
		.keywords = {
			u"bypass"_q,
			u"websocket"_q,
			QString::fromUtf8("обход"),
		},
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Плагины"),
			QString("Plugins")),
		.targetSection = WhitegramPlugins::Id(),
		.icon = { &st::menuIconPermissions },
		.keywords = {
			u"python"_q,
			u"plugin"_q,
			QString::fromUtf8("плагины"),
			QString::fromUtf8("скрипты"),
		},
	});

	builder.addSectionButton({
		.title = trWhitegram(
			QString::fromUtf8("Самоуничтожение"),
			QString("Self-destruct")),
		.targetSection = WhitegramSelfDestruct::Id(),
		.icon = { &st::menuIconDeleteAttention },
		.keywords = {
			u"self-destruct"_q,
			u"wipe"_q,
			QString::fromUtf8("самоуничтожение"),
			QString::fromUtf8("удалить"),
		},
	});

	BuildWhitegramLinks(builder);
});

} // namespace

rpl::producer<QString> trWhitegram(const QString &ru, const QString &en) {
	return rpl::single(rpl::empty) | rpl::then(
		Lang::GetInstance().idChanges() | rpl::to_empty
	) | rpl::map([ru, en] {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
		return isRu ? ru : en;
	});
}

rpl::producer<QString> WhitegramMain::title() {
	return rpl::single(QString("Whitegram"));
}

WhitegramMain::WhitegramMain(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramMain::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramMainId() {
	return WhitegramMain::Id();
}

rpl::producer<QString> WhitegramGeneral::title() {
	return trWhitegram(
		QString::fromUtf8("Основное"),
		QString("General"));
}

WhitegramGeneral::WhitegramGeneral(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramGeneral::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramGeneralMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramGeneralId() {
	return WhitegramGeneral::Id();
}

rpl::producer<QString> WhitegramUpdates::title() {
	return trWhitegram(
		QString::fromUtf8("Обновление"),
		QString("Updates"));
}

WhitegramUpdates::WhitegramUpdates(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramUpdates::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramUpdatesMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramUpdatesId() {
	return WhitegramUpdates::Id();
}

rpl::producer<QString> WhitegramGhostMode::title() {
	return trWhitegram(
		QString::fromUtf8("Режим призрака"),
		QString("Ghost Mode"));
}

WhitegramGhostMode::WhitegramGhostMode(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramGhostMode::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramGhostModeMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramGhostModeId() {
	return WhitegramGhostMode::Id();
}

rpl::producer<QString> WhitegramCrypto::title() {
	return trWhitegram(
		QString::fromUtf8("Курсы криптовалют"),
		QString("Cryptocurrency Rates"));
}

WhitegramCrypto::WhitegramCrypto(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramCrypto::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramCryptoMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramCryptoId() {
	return WhitegramCrypto::Id();
}

rpl::producer<QString> WhitegramTimeSpent::title() {
	return trWhitegram(
		QString::fromUtf8("Счетчик времени"),
		QString("Time Spent Counter"));
}

WhitegramTimeSpent::WhitegramTimeSpent(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramTimeSpent::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramTimeSpentMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramTimeSpentId() {
	return WhitegramTimeSpent::Id();
}

Type WhitegramStartupSoundId() { return WhitegramStartupSound::Id(); }
Type WhitegramAppearanceId() { return WhitegramAppearance::Id(); }
Type WhitegramMusicIslandId() { return WhitegramMusicIsland::Id(); }
Type WhitegramProfileBackgroundId() {
	return WhitegramProfileBackground::Id();
}
Type WhitegramStartupAnimId() { return WhitegramStartupAnim::Id(); }
Type WhitegramArchivesId() { return WhitegramArchives::Id(); }
Type WhitegramBypassId() { return WhitegramBypass::Id(); }

rpl::producer<QString> WhitegramArchives::title() {
	return trWhitegram(
		QString::fromUtf8("Папки чатов"),
		QString("Chat folders"));
}

WhitegramArchives::WhitegramArchives(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramArchives::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramArchivesMeta.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> WhitegramBypass::title() {
	return trWhitegram(
		QString::fromUtf8("Обход блокировок"),
		QString("Connection bypass"));
}

WhitegramBypass::WhitegramBypass(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramBypass::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramBypassMeta.build);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> WhitegramSelfDestruct::title() {
	return trWhitegram(
		QString::fromUtf8("Самоуничтожение"),
		QString("Self-destruct"));
}

WhitegramSelfDestruct::WhitegramSelfDestruct(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramSelfDestruct::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramSelfDestructMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramSelfDestructId() {
	return WhitegramSelfDestruct::Id();
}

rpl::producer<QString> WhitegramPlugins::title() {
	return trWhitegram(
		QString::fromUtf8("Плагины"),
		QString("Plugins"));
}

WhitegramPlugins::WhitegramPlugins(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramPlugins::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramPluginsMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramPluginsId() {
	return WhitegramPlugins::Id();
}

rpl::producer<QString> WhitegramChats::title() {
	return trWhitegram(
		QString::fromUtf8("Чаты и сообщения"),
		QString("Chats and messages"));
}

WhitegramChats::WhitegramChats(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramChats::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramChatsMeta.build);
	Ui::ResizeFitChild(this, content);
}

WhitegramStartupSound::WhitegramStartupSound(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramStartupSound::title() {
	return trWhitegram(
		QString::fromUtf8("Звук при запуске"),
		QString("Startup Sound"));
}
void WhitegramStartupSound::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramStartupSoundMeta.build);
	Ui::ResizeFitChild(this, content);
}

WhitegramAppearance::WhitegramAppearance(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramAppearance::title() {
	return trWhitegram(
		QString::fromUtf8("Оформление"),
		QString("Appearance"));
}
void WhitegramAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramAppearanceMeta.build);
	Ui::ResizeFitChild(this, content);
}

WhitegramMusicIsland::WhitegramMusicIsland(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramMusicIsland::title() {
	return trWhitegram(
		QString::fromUtf8("Музыкальный островок"),
		QString("Music island"));
}
void WhitegramMusicIsland::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramMusicIslandMeta.build);
	Ui::ResizeFitChild(this, content);
}

WhitegramProfileBackground::WhitegramProfileBackground(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramProfileBackground::title() {
	return trWhitegram(
		QString::fromUtf8("Фон профиля"),
		QString("Profile background"));
}
void WhitegramProfileBackground::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramProfileBackgroundMeta.build);
	Ui::ResizeFitChild(this, content);
}

WhitegramStartupAnim::WhitegramStartupAnim(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramStartupAnim::title() {
	return trWhitegram(
		QString::fromUtf8("Анимация при входе"),
		QString("Startup Animation"));
}
void WhitegramStartupAnim::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramStartupAnimMeta.build);
	Ui::ResizeFitChild(this, content);
}

void ShowConnectionBypassBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(trWhitegram(
		QString::fromUtf8("Обход блокировок"),
		QString("Connection bypass")));
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });

	const auto content = box->verticalLayout();

	Ui::AddSubsectionTitle(
		content,
		trWhitegram(
			QString::fromUtf8("Режим"),
			QString("Mode")));

	const auto slider = content->add(
		object_ptr<Ui::SettingsSlider>(content, st::whitegramBypassSlider),
		st::whitegramBypassSliderMargin);
	const auto applySections = [=] {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q)
			|| baseId.startsWith(u"ru"_q);
		slider->setSections({
			isRu ? QString::fromUtf8("Выкл") : QString("Off"),
			isRu ? QString::fromUtf8("Авто") : QString("Auto"),
			isRu ? QString::fromUtf8("Всегда") : QString("Always"),
		});
		slider->setActiveSectionFast(static_cast<int>(
			WhiteSettings::getInstance().connectionBypassMode()));
	};
	applySections();
	Lang::GetInstance().idChanges(
	) | rpl::on_next([=] {
		applySections();
	}, slider->lifetime());
	slider->sectionActivated(
	) | rpl::on_next([=](int index) {
		WhiteSettings::getInstance().setConnectionBypassMode(
			static_cast<ConnectionBypassMode>(index));
	}, slider->lifetime());
	WhiteSettings::getInstance().connectionBypassModeChanges(
	) | rpl::on_next([=](ConnectionBypassMode mode) {
		if (slider->activeSection() != static_cast<int>(mode)) {
			slider->setActiveSectionFast(static_cast<int>(mode));
		}
	}, slider->lifetime());

	const auto cooldownHint = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			rpl::single(QString()),
			st::boxDividerLabel),
		style::al_top);
	cooldownHint->hide();
	WhiteFeatures::ConnectionBypass::modeSwitchLockedValue(
	) | rpl::on_next([=](bool locked) {
		slider->setEnabled(!locked);
		if (locked) {
			const auto id = Lang::GetInstance().id();
			const auto baseId = Lang::GetInstance().baseId();
			const auto isRu = id.startsWith(u"ru"_q)
				|| baseId.startsWith(u"ru"_q);
			cooldownHint->setText(isRu
				? QString::fromUtf8(
					"Слишком частые переключения. Подождите несколько "
					"секунд, затем можно снова сменить режим.")
				: QString(
					"Too many mode changes. Wait a few seconds before "
					"switching again."));
			cooldownHint->show();
		} else {
			cooldownHint->hide();
		}
	}, slider->lifetime());

	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			WhiteSettings::getInstance().connectionBypassModeValue(
			) | rpl::map([](ConnectionBypassMode mode) {
				const auto id = Lang::GetInstance().id();
				const auto baseId = Lang::GetInstance().baseId();
				const auto isRu = id.startsWith(u"ru"_q)
					|| baseId.startsWith(u"ru"_q);
				switch (mode) {
				case ConnectionBypassMode::Off:
					return isRu
						? QString::fromUtf8(
							"Whitegram подключается к Telegram напрямую. "
							"Если мессенджер недоступен, выберите Авто "
							"или Всегда.")
						: QString(
							"Whitegram connects to Telegram directly. "
							"If the messenger is unreachable, choose Auto "
							"or Always.");
				case ConnectionBypassMode::Auto:
					return isRu
						? QString::fromUtf8(
							"Сначала обычное соединение. Если за несколько "
							"секунд подключиться не удалось, включается "
							"обход через WebSocket. Если все зеркала "
							"недоступны — временно пробует обычный TCP.")
						: QString(
							"Starts with a direct connection. If it cannot "
							"connect for a few seconds, WebSocket bypass "
							"turns on. If all mirrors fail, it temporarily "
							"tries plain TCP.");
				case ConnectionBypassMode::Always:
					return isRu
						? QString::fromUtf8(
							"Обход включён постоянно. Помогает при "
							"блокировках провайдера, соединение может "
							"быть чуть медленнее.")
						: QString(
							"Bypass stays on. Helps when a provider blocks "
							"Telegram; the connection may be a bit slower.");
				}
				return QString();
			}),
			st::boxDividerLabel),
		st::boxRowPadding);

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(
		content,
		trWhitegram(
			QString::fromUtf8("Свои зеркала"),
			QString("Custom mirrors")));

	const auto mirrorCardWrap = content->add(
		object_ptr<BypassMirrorCard>(content),
		st::whitegramBypassMirrorCardMargin);
	const auto mirrorCard = mirrorCardWrap->content();

	mirrorCard->add(
		object_ptr<Ui::FlatLabel>(
			mirrorCard,
			trWhitegram(
				QString::fromUtf8(
					"Личный Cloudflare Worker — запасной маршрут, "
					"если общие зеркала недоступны. Адрес хранится "
					"только на этом устройстве."),
				QString(
					"A personal Cloudflare Worker is a backup route "
					"when shared mirrors are unavailable. The address "
					"is stored on this device only.")),
			st::whitegramBypassMirrorHint));

	Ui::AddSkip(mirrorCard, st::settingsCheckboxesSkip);

	const auto statusLabel = mirrorCard->add(
		object_ptr<Ui::FlatLabel>(
			mirrorCard,
			rpl::single(QString()),
			st::whitegramBypassMirrorStatus));
	WhiteSettings::getInstance().connectionBypassSecretValue(
	) | rpl::on_next([=](const QString &secret) {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q)
			|| baseId.startsWith(u"ru"_q);
		const auto configured = !secret.trimmed().isEmpty();
		statusLabel->setText(configured
			? (isRu
				? QString::fromUtf8("Статус: личный Worker подключён")
				: QString("Status: custom Worker is active"))
			: (isRu
				? QString::fromUtf8("Статус: Worker не задан")
				: QString("Status: no Worker configured")));
		statusLabel->setTextColorOverride(configured
			? st::windowActiveTextFg->c
			: std::optional<QColor>());
	}, statusLabel->lifetime());

	Ui::AddSkip(mirrorCard, st::whitegramBypassMirrorButtonSkip);

	const auto mirrorButton = mirrorCard->add(
		object_ptr<Ui::RoundButton>(
			mirrorCard,
			WhiteSettings::getInstance().connectionBypassSecretValue(
			) | rpl::map([](const QString &secret) {
				const auto id = Lang::GetInstance().id();
				const auto baseId = Lang::GetInstance().baseId();
				const auto isRu = id.startsWith(u"ru"_q)
					|| baseId.startsWith(u"ru"_q);
				const auto configured = !secret.trimmed().isEmpty();
				if (configured) {
					return isRu
						? QString::fromUtf8("Изменить Worker")
						: QString("Edit Worker");
				}
				return isRu
					? QString::fromUtf8("Добавить Worker")
					: QString("Add Worker");
			}),
			st::whitegramBypassMirrorButton));
	mirrorButton->setTextTransform(
		Ui::RoundButtonTextTransform::NoTransform);
	mirrorButton->setClickedCallback([=] {
		box->uiShow()->showBox(Box(ShowBypassCustomMirrorBox));
	});

	Ui::AddSkip(content);

	const auto more = AddButtonWithIcon(
		content,
		trWhitegram(
			QString::fromUtf8("Подробнее"),
			QString("Learn more")),
		st::settingsButton,
		{ &st::menuIconInfo });
	more->setClickedCallback([=] {
		box->uiShow()->showBox(Box(ShowBypassAboutBox));
	});
}

void SetupConnectionBypass(
		not_null<Window::Controller*> controller,
		not_null<Ui::VerticalLayout*> container) {
	const auto label = WhiteSettings::getInstance().connectionBypassModeValue(
	) | rpl::map([](ConnectionBypassMode mode) {
		const auto id = Lang::GetInstance().id();
		const auto baseId = Lang::GetInstance().baseId();
		const auto isRu = id.startsWith(u"ru"_q)
			|| baseId.startsWith(u"ru"_q);
		switch (mode) {
		case ConnectionBypassMode::Off:
			return isRu
				? QString::fromUtf8("Выкл")
				: QString("Off");
		case ConnectionBypassMode::Auto:
			return isRu
				? QString::fromUtf8("Авто")
				: QString("Auto");
		case ConnectionBypassMode::Always:
			return isRu
				? QString::fromUtf8("Всегда")
				: QString("Always");
		}
		return QString();
	});

	const auto button = AddButtonWithLabel(
		container,
		trWhitegram(
			QString::fromUtf8("Обход блокировок"),
			QString("Connection bypass")),
		std::move(label),
		st::settingsButton,
		{ &st::menuIconUnblock });

	button->addClickHandler([=] {
		controller->show(Box(ShowConnectionBypassBox));
	});
}

QString RunWhitegramDraftPlugin(const QString &text) {
	if (text.isEmpty() || !Whitegram::Plugins::Started()) {
		return QString();
	}
	return Whitegram::Plugins::TransformDraft(text);
}

} // namespace Settings
