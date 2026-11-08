// This is the source code of Whitegram for Desktop.
//
// Copyright @Whitegram, 2026
#include "ayu/ui/components/lyrics_window.h"

#include "ayu/ui/components/lyrics_pictograms.h"

#include "ayu/ui/utils/thread_cpu.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>

#include "ayu/ayu_settings.h"
#include "settings.h"
#include "base/event_filter.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_instance.h"
#include "media/audio/media_audio.h"
#include "media/media_common.h"
#include "media/player/media_player_instance.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "logs.h"
#include "styles/style_basic.h"
#include "styles/style_music_island.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QGuiApplication>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QTime>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QKeyEvent>
#include <QtGui/QTextDocument>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>
#include <QtGui/QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>

namespace AyuUi {
namespace {

using namespace Ayu::Ui;

constexpr auto kShowDuration = crl::time(650);
constexpr auto kHideDuration = crl::time(380);
constexpr auto kScrollLeadMs = 280;
constexpr auto kWordLeadMs = 40;
constexpr auto kSpring = 150.;
constexpr auto kDamping = 24.5;
constexpr auto kEmphasisTime = 0.12;
constexpr auto kManualHoldMs = crl::time(3000);
constexpr auto kToastDuration = crl::time(4200);
constexpr auto kInactiveScale = 1.;
constexpr auto kEcgRate = 60.;
constexpr auto kEcgSamples = 240;
constexpr auto kParticles = 90;
constexpr auto kAlarmHeart = 124.;
constexpr auto kTimerLeadMs = 3600;
constexpr auto kNextLines = 3;
constexpr auto kPi = 3.14159265358979323846;

const auto kRed = QColor(255, 46, 66);
const auto kOrange = QColor(255, 159, 10);

[[nodiscard]] double Smooth(double t) {
	t = std::clamp(t, 0., 1.);
	return t * t * (3. - 2. * t);
}

[[nodiscard]] double Hash01(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
	auto h = a * 2654435761u ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
	h ^= c * 0x85ebca6bu;
	h ^= h >> 15;
	h *= 0x2c1b3c6du;
	h ^= h >> 12;
	return (h % 10000) / 10000.;
}

[[nodiscard]] QColor Mix(QColor a, QColor b, double t) {
	t = std::clamp(t, 0., 1.);
	return QColor(
		int(a.red() + (b.red() - a.red()) * t),
		int(a.green() + (b.green() - a.green()) * t),
		int(a.blue() + (b.blue() - a.blue()) * t),
		int(a.alpha() + (b.alpha() - a.alpha()) * t));
}

void DrawToFill(QPainter &p, const QRectF &target, const QPixmap &pixmap, double zoom = 1.) {
	if (pixmap.isNull()) {
		return;
	}
	const auto source = QSizeF(pixmap.size());
	const auto scaled = source.scaled(target.size() * zoom, Qt::KeepAspectRatioByExpanding);
	p.drawPixmap(
		QRectF(target.center() - QPointF(scaled.width() / 2., scaled.height() / 2.), scaled),
		pixmap,
		QRectF(QPointF(), source));
}

void DrawImageToFill(QPainter &p, const QRectF &target, const QImage &image) {
	if (image.isNull()) {
		return;
	}
	const auto source = QSizeF(image.size());
	const auto scaled = source.scaled(target.size(), Qt::KeepAspectRatioByExpanding);
	p.drawImage(
		QRectF(target.center() - QPointF(scaled.width() / 2., scaled.height() / 2.), scaled),
		image,
		QRectF(QPointF(), source));
}

struct Backdrop {
	QImage background;
	QImage frost;
	QImage glow;
};

[[nodiscard]] Backdrop BuildBackdrop(
		QSize size,
		int ratio,
		bool purple,
		bool blue,
		const QImage &blurred) {
	auto result = Backdrop();
	const auto width = size.width();
	const auto height = size.height();
	auto background = QImage(size * ratio, QImage::Format_RGB32);
	background.setDevicePixelRatio(ratio);
	{
		auto q = QPainter(&background);
		auto hq = PainterHighQualityEnabler(q);
		const auto full = QRectF(QPointF(), QSizeF(size));
		const auto side = std::max(width, height);
		auto base = QRadialGradient(QPointF(width * 0.35, height * 0.35), side * 0.9);
		base.setColorAt(0., purple ? QColor(74, 48, 120) : blue ? QColor(44, 62, 112) : QColor(62, 62, 70));
		base.setColorAt(1., purple ? QColor(22, 14, 48) : blue ? QColor(14, 22, 48) : QColor(30, 30, 35));
		q.fillRect(full, base);
		if (!blurred.isNull()) {
			q.setOpacity(0.28);
			DrawImageToFill(q, full, blurred);
			q.setOpacity(1.);
		}
		auto corner = QRadialGradient(QPointF(width * 0.92, height * 1.05), side * 0.55);
		corner.setColorAt(0., purple ? QColor(200, 90, 255, 70) : blue ? QColor(90, 70, 220, 70) : QColor(170, 18, 34, 60));
		corner.setColorAt(1., QColor(0, 0, 0, 0));
		q.fillRect(full, corner);
		auto vignette = QRadialGradient(full.center(), side * 0.72);
		vignette.setColorAt(0.5, QColor(0, 0, 0, 0));
		vignette.setColorAt(1., QColor(0, 0, 0, 150));
		q.fillRect(full, vignette);
	}
	{
		auto small = background.scaled(
			std::max(1, background.width() / 8),
			std::max(1, background.height() / 8),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		small.setDevicePixelRatio(1.);
		{
			auto q = QPainter(&small);
			q.fillRect(small.rect(), QColor(255, 255, 255, blue ? 26 : 20));
		}
		result.frost = Images::BlurLargeImage(std::move(small), 3);
	}
	result.background = std::move(background);

	const auto glowSize = size * ratio;
	auto glow = QImage(glowSize, QImage::Format_ARGB32_Premultiplied);
	glow.fill(Qt::transparent);
	{
		auto q = QPainter(&glow);
		const auto full = QRectF(QPointF(), QSizeF(glowSize));
		const auto side = std::max(glowSize.width(), glowSize.height());
		auto red = QRadialGradient(QPointF(full.width() * 0.5, full.height() * 1.15), side * 1.05);
		red.setColorAt(0., purple ? QColor(210, 100, 255, 150) : blue ? QColor(90, 120, 255, 150) : QColor(200, 20, 38, 160));
		red.setColorAt(0.6, purple ? QColor(130, 60, 220, 60) : blue ? QColor(60, 60, 200, 60) : QColor(140, 10, 26, 60));
		red.setColorAt(1., QColor(0, 0, 0, 0));
		q.fillRect(full, red);
		auto corner = QRadialGradient(QPointF(full.width() * 0.92, full.height() * 1.05), side * 0.55);
		corner.setColorAt(0., purple ? QColor(255, 120, 220, 120) : blue ? QColor(150, 110, 255, 120) : QColor(190, 20, 38, 120));
		corner.setColorAt(1., QColor(0, 0, 0, 0));
		q.fillRect(full, corner);
	}
	glow.setDevicePixelRatio(ratio);
	result.glow = std::move(glow);
	return result;
}

QString OffsetsPath() {
	return cWorkingDir() + u"tdata/whitegram/lyrics_offsets.json"_q;
}

QJsonObject &Offsets() {
	static auto result = [] {
		auto file = QFile(OffsetsPath());
		return file.open(QIODevice::ReadOnly)
			? QJsonDocument::fromJson(file.readAll()).object()
			: QJsonObject();
	}();
	return result;
}

QString OffsetKey(const QString &artist, const QString &title) {
	return (artist.trimmed() + u" - "_q + title.trimmed()).toLower();
}

void SaveOffsets() {
	QDir().mkpath(cWorkingDir() + u"tdata/whitegram"_q);
	auto file = QFile(OffsetsPath());
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(Offsets()).toJson(QJsonDocument::Compact));
	}
}

[[nodiscard]] std::vector<Lyrics::Word> SplitWords(const QString &text) {
	auto result = std::vector<Lyrics::Word>();
	const auto parts = text.split(QChar(u' '), Qt::SkipEmptyParts);
	for (auto i = 0; i != parts.size(); ++i) {
		auto word = Lyrics::Word();
		word.text = parts[i] + ((i + 1 < parts.size()) ? u" "_q : QString());
		result.push_back(std::move(word));
	}
	return result;
}

[[nodiscard]] QString Time(std::int64_t ms) {
	return Ui::FormatDurationText(std::max<std::int64_t>(ms, 0) / 1000);
}

[[nodiscard]] std::vector<float> HeartbeatPattern() {
	return {
		0.05f, 0.10f, 0.06f, 0.f, 0.f, -0.14f, 1.f, 0.55f, -0.38f, -0.1f,
		0.f, 0.03f, 0.10f, 0.18f, 0.22f, 0.17f, 0.08f, 0.02f,
	};
}

void FillProgress(QPainter &p, const QRectF &rect, double progress, QColor active) {
	const auto radius = rect.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 46));
	p.drawRoundedRect(rect, radius, radius);
	if (progress > 0.) {
		p.setBrush(active);
		p.drawRoundedRect(
			QRectF(rect.left(), rect.top(), std::max(rect.height(), rect.width() * progress), rect.height()),
			radius,
			radius);
	}
}


enum Category {
	kNoCategory = 0,
	kSnow,
	kAlone,
	kNight,
	kWords,
	kFriends,
	kAnger,
	kQueen,
	kFace,
	kSwear,
	kCute,
	kHealth,
	kRain,
	kMemory,
	kEnemy,
	kGrave,
	kForever,
	kSleep,
	kWish,
	kNear,
	kMeaning,
	kMissed,
	kRun,
	kNumber,
	kCategoryCount,
};

enum FxKind {
	kFxNone = 0,
	kFxUnderline,
	kFxDashed,
	kFxCircle,
	kFxTarget,
	kFxNumber,
	kFxQuestion,
	kFxEye,
	kFxSleep,
	kFxTears,
	kFxCheck,
	kFxGold,
	kFxRainbow,
	kFxDissolve,
	kFxSink,
	kFxGlitch,
	kFxItalic,
	kFxRun,
	kFxAsk,
	kFxLeave,
	kFxHang,
	kFxShine,
	kFxSlash,
	kFxImpact,
	kFxLove,
	kFxSquiggle,
	kFxDetect,
	kFxName,
};

const std::vector<std::pair<int, std::vector<const char*>>> &FxStems() {
	static const auto result = std::vector<std::pair<int, std::vector<const char*>>>{
		{ kFxDissolve, { "умру", "умрем", "умрет", "умер", "умир", "смерт", "пепел", "прах", "die=", "died", "dead", "death", "dying" } },
		{ kFxSink, { "могил", "grave", "утон", "drown", "похорон", "гроб", "coffin", "bury", "buried" } },
		{ kFxHang, { "повес", "вешал", "вешай", "висел", "виселиц", "петля", "петлю", "петле", "hang=", "hanging", "hanged", "noose" } },
		{ kFxRainbow, { "навечно", "вечно", "навсегда", "forever", "eternal", "always" } },
		{ kFxShine, { "вера=", "веру=", "вере=", "верой=", "верю", "бог=", "бога=", "богу=", "богом=", "боже", "свят", "рай=", "рая=", "раю=", "небес", "faith", "heaven", "god=", "holy", "pray", "angel", "ангел" } },
		{ kFxGold, { "желан", "мечт", "надежд", "чудо", "золот", "wish", "dream", "hope", "magic", "gold" } },
		{ kFxTarget, { "враг", "enemy", "enemies", "цель=", "цели=", "target", "прицел", "мишен" } },
		{ kFxDetect, { "воин", "солдат", "бойц", "армия", "армии", "армию", "отряд", "толп", "warrior", "soldier", "army", "crowd" } },
		{ kFxImpact, { "томпсон", "калаш", "автомат", "взрыв", "взорв", "бомб", "boom", "bang=", "explod", "bomb" } },
		{ kFxGlitch, { "глок", "glock", "пушк", "ствол", "пистолет", "gun=", "guns", "pistol", "война", "войну", "войне", "war=", "glitch" } },
		{ kFxSlash, { "меч=", "мечи=", "мечом", "мечей", "нож", "клинок", "клинк", "порез", "sword", "knife", "blade", "slash" } },
		{ kFxCircle, { "круг", "кольц", "circle" } },
		{ kFxAsk, { "зачем", "почему", "why=" } },
		{ kFxQuestion, { "который", "которая", "которые", "которого", "who=" } },
		{ kFxEye, { "глаз", "смотр", "взгляд", "eyes", "watch" } },
		{ kFxSleep, { "спит", "сплю", "спать", "усну", "уснул", "sleep", "asleep" } },
		{ kFxTears, { "слез", "плач", "плак", "cry", "tears" } },
		{ kFxCheck, { "готов", "ready" } },
		{ kFxDashed, { "границ", "border" } },
		{ kFxLove, { "руках", "руками", "обним", "целу", "поцел", "люблю", "любим", "любов", "сердц", "love", "kiss", "heart", "hug=" } },
		{ kFxLeave, { "ухожу", "уйду", "уйти", "ушел", "ушла", "уходи", "прощай", "leave", "leaving", "goodbye", "gone=" } },
		{ kFxRun, { "бежать", "беги=", "бегу=", "бегут", "бежим", "убега", "побег", "run=", "running", "runnin", "ran=" } },
		{ kFxItalic, { "бой=", "бою=", "драк", "битв", "fight", "battle", "хитр", "быстр", "fast=", "faster" } },
		{ kFxSquiggle, { "дура", "дурак", "тупой", "тупая", "туповат", "идиот", "глуп", "stupid", "dumb", "fool", "idiot" } },
		{ kFxUnderline, { "первым", "первый", "первая", "лучш", "единствен", "first=", "best=", "only=" } },
	};
	return result;
}

int NumberValue(const QString &word) {
	if (word.isEmpty()) {
		return 0;
	}
	auto digits = true;
	for (const auto ch : word) {
		digits = digits && ch.isDigit();
	}
	if (digits) {
		const auto value = word.toInt();
		return (value >= 2 && value <= 100000) ? value : 0;
	}
	static const auto map = std::map<QString, int>{
		{ QString::fromUtf8("два"), 2 }, { QString::fromUtf8("две"), 2 }, { QString::fromUtf8("три"), 3 },
		{ QString::fromUtf8("четыре"), 4 }, { QString::fromUtf8("пять"), 5 }, { QString::fromUtf8("шесть"), 6 },
		{ QString::fromUtf8("семь"), 7 }, { QString::fromUtf8("восемь"), 8 }, { QString::fromUtf8("девять"), 9 },
		{ QString::fromUtf8("десять"), 10 }, { QString::fromUtf8("двенадцать"), 12 }, { QString::fromUtf8("пятнадцать"), 15 },
		{ QString::fromUtf8("двадцать"), 20 }, { QString::fromUtf8("тридцать"), 30 }, { QString::fromUtf8("сорок"), 40 },
		{ QString::fromUtf8("пятьдесят"), 50 }, { QString::fromUtf8("шестьдесят"), 60 }, { QString::fromUtf8("семьдесят"), 70 },
		{ QString::fromUtf8("восемьдесят"), 80 }, { QString::fromUtf8("девяносто"), 90 }, { QString::fromUtf8("сто"), 100 },
		{ QString::fromUtf8("тысяча"), 1000 }, { QString::fromUtf8("тысячи"), 1000 }, { QString::fromUtf8("миллион"), 1000000 },
		{ u"two"_q, 2 }, { u"three"_q, 3 }, { u"four"_q, 4 }, { u"five"_q, 5 }, { u"six"_q, 6 }, { u"seven"_q, 7 },
		{ u"eight"_q, 8 }, { u"nine"_q, 9 }, { u"ten"_q, 10 }, { u"twelve"_q, 12 }, { u"twenty"_q, 20 },
		{ u"fifty"_q, 50 }, { u"hundred"_q, 100 }, { u"thousand"_q, 1000 }, { u"million"_q, 1000000 },
	};
	const auto it = map.find(word);
	return (it != map.end()) ? it->second : 0;
}

enum StickerKind {
	kNoSticker = 0,
	kStickerMoon,
	kStickerCrown,
	kStickerSnow,
	kStickerSquiggle,
	kStickerHeart,
};

struct CategoryInfo {
	int id = 0;
	std::vector<const char*> stems;
	QColor accent;
	int sticker = kNoSticker;
};

const std::vector<CategoryInfo> &Categories() {
	static const auto result = std::vector<CategoryInfo>{
		{ kSwear, { "матер", "сука", "бля", "хуй", "пизд", "ебат", "еба", "fuck", "shit", "bitch" }, QColor(255, 80, 96), kNoSticker },
		{ kFace, { "лицо", "лица", "глаз", "улыб", "взгляд", "face", "eyes", "smile" }, QColor(255, 140, 200), kNoSticker },
		{ kAnger, { "злиш", "злюсь", "злая", "злой", "бесиш", "бесит", "ненавиж", "набить", "драк", "руга", "крич", "тупые", "тупая", "hate", "angry", "mad" }, QColor(255, 60, 80), kNoSticker },
		{ kQueen, { "царица", "царицу", "королев", "принцесс", "бледн", "queen", "princess", "pale" }, QColor(150, 200, 255), kStickerCrown },
		{ kSnow, { "снег", "снеж", "зима", "зиму", "зимн", "мороз", "холод", "лед", "вьюг", "метел", "winter", "snow", "frost", "ice" }, QColor(170, 215, 255), kStickerSnow },
		{ kNight, { "ночь", "ночью", "ночн", "луна", "луну", "звезд", "ждал", "жду", "ждать", "утро", "night", "moon", "midnight" }, QColor(175, 150, 255), kStickerMoon },
		{ kFriends, { "друз", "друг", "люди", "толп", "friends", "people" }, QColor(255, 130, 200), kNoSticker },
		{ kWords, { "сказать", "слова", "скажу", "напиш", "сообщ", "звон", "смс", "words", "call", "text" }, QColor(), kNoSticker },
		{ kAlone, { "одна", "один", "одино", "никого", "пусто", "alone", "lonely" }, QColor(), kNoSticker },
		{ kCute, { "мило", "милая", "милое", "милый", "любл", "любов", "нежн", "cute", "love", "sweet" }, QColor(255, 150, 210), kStickerHeart },
		{ kHealth, { "болен", "больн", "таблет", "препарат", "лекарств", "врач", "пульс", "sick", "pill", "hospital" }, QColor(), kNoSticker },
		{ kRain, { "дожд", "ливн", "слез", "rain", "tears", "cry" }, QColor(140, 180, 255), kNoSticker },
		{ kMemory, { "помн", "память", "фото", "воспомин", "remember", "photo" }, QColor(), kNoSticker },
		{ kEnemy, { "враг", "enemy", "enemies", "цель=", "цели=", "target" }, QColor(), kNoSticker },
		{ kGrave, { "могил", "grave", "кладбищ", "гроб", "маршрут", "route" }, QColor(), kNoSticker },
		{ kForever, { "навечно", "вечно", "навсегда", "forever", "eternal", "young" }, QColor(), kNoSticker },
		{ kSleep, { "спит", "сплю", "спать", "разбуж", "будить", "sleep", "asleep" }, QColor(), kNoSticker },
		{ kWish, { "желан", "мечт", "wish", "dream" }, QColor(), kNoSticker },
		{ kNear, { "рядом", "руках", "обним", "близко", "near", "arms" }, QColor(), kNoSticker },
		{ kMeaning, { "смысл", "потерял", "искать", "ищу", "lost", "meaning", "search" }, QColor(), kNoSticker },
		{ kMissed, { "захот", "пропущ", "звонят", "звонки", "missed", "calling" }, QColor(), kNoSticker },
		{ kRun, { "беж", "бег=", "run=", "running" }, QColor(), kNoSticker },
	};
	return result;
}

int WordCategory(const QString &normalized) {
	if (normalized.size() < 3) {
		return kNoCategory;
	}
	for (const auto &info : Categories()) {
		for (const auto stem : info.stems) {
			auto value = QString::fromUtf8(stem).trimmed();
			const auto exact = value.endsWith(QChar(u'='));
			if (exact) {
				value.chop(1);
			}
			if (exact ? (normalized == value) : normalized.startsWith(value)) {
				return info.id;
			}
		}
	}
	return kNoCategory;
}

const CategoryInfo *FindCategory(int id) {
	for (const auto &info : Categories()) {
		if (info.id == id) {
			return &info;
		}
	}
	return nullptr;
}

} // namespace

LyricsWindow::LyricsWindow(not_null<QWidget*> parent)
: RpWidget(parent)
, _ecg(kEcgSamples, 0.f)
, _ecgPattern(HeartbeatPattern()) {
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_OpaquePaintEvent);
	hide();
	setGeometry(parent->rect());
	for (auto i = 0; i != kParticles; ++i) {
		const auto big = (i % 9 == 0);
		_particles.push_back({
			.x = Hash01(i, 1, 7),
			.y = Hash01(i, 2, 7),
			.radius = big ? (5. + 9. * Hash01(i, 3, 7)) : (0.8 + 1.8 * Hash01(i, 3, 7)),
			.speed = 0.008 + 0.03 * Hash01(i, 4, 7),
			.phase = 6.28 * Hash01(i, 5, 7),
			.alpha = big ? (0.04 + 0.05 * Hash01(i, 6, 7)) : (0.25 + 0.5 * Hash01(i, 6, 7)),
		});
	}
	base::install_event_filter(this, parent, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Resize) {
			setGeometry(parentWidget()->rect());
			_layoutSize = QSize();
		}
		return base::EventFilterResult::Continue;
	});
	_animation.init([=](crl::time now) {
		tick(now);
		return isVisible();
	});
	AyuSettings::getInstance().musicKaraokeCoverPathValue(
	) | rpl::on_next([=](const QString &path) {
		loadCustomCover(path);
	}, lifetime());
	AyuSettings::getInstance().musicLyricsTranslateChanges(
	) | rpl::on_next([=](bool) {
		_translation = {};
		requestTranslation();
		_layoutSize = QSize();
		update();
	}, lifetime());
	AyuSettings::getInstance().musicWeatherCityChanges(
	) | rpl::on_next([=](const QString &) {
		_weather = {};
		requestWeather(true);
	}, lifetime());
	rpl::merge(
		AyuSettings::getInstance().musicLyricsFontScaleChanges() | rpl::to_empty,
		AyuSettings::getInstance().musicLyricsKeywordChanges() | rpl::to_empty
	) | rpl::on_next([=] {
		_layoutSize = QSize();
		update();
	}, lifetime());
}

void LyricsWindow::toggle(bool shown) {
	if (_shown == shown) {
		return;
	}
	_shown = shown;
	_transitionSharp = QPixmap();
	_transitionBlur = QPixmap();
	setAttribute(Qt::WA_OpaquePaintEvent, false);
	if (shown) {
		setGeometry(parentWidget()->rect());
		if (!_everShown) {
			_everShown = true;
			requestTranslation();
		}
		prewarm();
		show();
		raise();
		setFocus();
		_lastTick = 0;
		if (!_animation.animating()) {
			_animation.start();
		}
		requestWeather(false);
		showToast(
			ToastKind::Music,
			QString::fromUtf8("МУЗЫКА"),
			QString::fromUtf8("Сейчас играет"),
			_artist.isEmpty() ? _title : (_artist + QString::fromUtf8(" — ") + _title));
	}
	_showAnimation.start([=] {
		update();
		if (_showAnimation.animating()) {
			return;
		} else if (_shown) {
			setAttribute(Qt::WA_OpaquePaintEvent, true);
			_transitionSharp = QPixmap();
			_transitionBlur = QPixmap();
		} else {
			hide();
			_animation.stop();
		}
	}, shown ? 0. : 1., shown ? 1. : 0., shown ? kShowDuration : kHideDuration, anim::easeOutCubic);
}

bool LyricsWindow::shown() const {
	return _shown;
}

void LyricsWindow::setTrack(const QString &artist, const QString &title) {
	const auto changed = (artist != _artist) || (title != _title);
	_artist = artist;
	_title = title;
	if (changed) {
		_lyricsOffset = std::int64_t(Offsets().value(OffsetKey(artist, title)).toDouble());
	}
	if (changed && _shown && !title.isEmpty()) {
		showToast(
			ToastKind::Music,
			QString::fromUtf8("МУЗЫКА"),
			QString::fromUtf8("Сейчас играет"),
			artist.isEmpty() ? title : (artist + QString::fromUtf8(" — ") + title));
	}
	update();
}

void LyricsWindow::setCover(QPixmap art, QPixmap blurred, QColor accent) {
	if (art.cacheKey() == _art.cacheKey()
		&& blurred.cacheKey() == _blurred.cacheKey()
		&& accent == _accent) {
		return;
	}
	_art = std::move(art);
	_blurred = std::move(blurred);
	_accent = accent;
	_cardArt = QPixmap();
	requestBackdrop(false);
	update();
}

void LyricsWindow::setLyrics(const Lyrics::Data &data, bool loading) {
	const auto same = (data.hasData == _data.hasData)
		&& (data.synced == _data.synced)
		&& (data.revision == _data.revision)
		&& (data.autoSynced == _data.autoSynced)
		&& (data.title == _data.title)
		&& (data.artist == _data.artist)
		&& (data.source == _data.source)
		&& (data.lines.size() == _data.lines.size())
		&& (data.lines.empty()
			|| (data.lines.front().text == _data.lines.front().text
				&& data.lines.front().ms == _data.lines.front().ms
				&& data.lines.back().text == _data.lines.back().text));
	if (same && loading == _loading) {
		return;
	}
	_data = data;
	_loading = loading;
	_scroll = 0.;
	_velocity = 0.;
	_manualOffset = 0.;
	_active = -1;
	_layoutSize = QSize();
	detectTheme();
	buildScenes();
	_translation = {};
	requestTranslation();
	requestBackdrop(false);
	scheduleHiddenLayout();
	update();
}

void LyricsWindow::setAnalysis(std::shared_ptr<const Analysis::Result> analysis) {
	_analysis = std::move(analysis);
	_wasChorus = false;
	_lastBeatMs = -1;
	update();
}

void LyricsWindow::setPlayback(
		std::int64_t positionMs,
		std::int64_t durationMs,
		bool playing,
		double speed) {
	_basePosition = positionMs;
	_baseAt = crl::now();
	_duration = durationMs;
	_playing = playing;
	_speed = (speed > 0.) ? speed : 1.;
}

void LyricsWindow::setSeekCallback(Fn<void(std::int64_t)> callback) {
	_seek = std::move(callback);
}

void LyricsWindow::setSaveCallbacks(Fn<int()> state, Fn<void()> toggle) {
	_profileState = std::move(state);
	_toggleProfile = std::move(toggle);
}

std::int64_t LyricsWindow::position() const {
	if (!_playing || !_baseAt) {
		return _basePosition;
	}
	const auto elapsed = std::min<crl::time>(crl::now() - _baseAt, 2000);
	return _basePosition + std::int64_t(elapsed * _speed);
}

int LyricsWindow::activeIndex(std::int64_t position) const {
	return _data.synced ? Lyrics::ActiveLineIndex(_data, position) : -1;
}

QFont LyricsWindow::makeFont(double size, int weight) const {
	const auto pixel = std::max(8, int(std::round(size)));
	const auto key = (pixel << 10) | (weight & 0x3FF);
	if (const auto i = _fontCache.find(key); i != _fontCache.end()) {
		return i->second;
	}
	if (_fontCache.size() > 512) {
		_fontCache.clear();
	}
	// WHY: a live QFont keeps its engine and glyph cache referenced, so Qt's
	// global font cache cannot evict them between frames in a busy client.
	auto result = st::normalFont->f;
	result.setFamilies({
		u"SF Pro Display"_q,
		u"Segoe UI"_q,
		u"Helvetica Neue"_q,
		result.family(),
	});
	result.setPixelSize(pixel);
	result.setWeight(QFont::Weight(weight));
	_fontCache.emplace(key, result);
	return result;
}

QFont LyricsWindow::lyricFont() const {
	const auto scale = AyuSettings::getInstance().musicLyricsFontScale() / 100.;
	const auto narrow = _geometry.nowCard.isEmpty();
	const auto size = narrow
		? std::clamp(_geometry.lyrics.width() * 0.08, 24., 64.)
		: 72. * _geometry.unit;
	return makeFont(size * scale, QFont::Bold);
}

double LyricsWindow::anchorY() const {
	return _geometry.lyrics.top() + (_geometry.nowCard.isEmpty()
		? _geometry.lyrics.height() * 0.18
		: 132. * _geometry.unit);
}

bool LyricsWindow::inChorus(std::int64_t position) const {
	return _analysis && _analysis->inChorus(position);
}

bool LyricsWindow::alarm() const {
	return _heart >= kAlarmHeart;
}

std::int64_t LyricsWindow::nextChorusStart(std::int64_t position) const {
	if (!_analysis) {
		return -1;
	}
	for (const auto &section : _analysis->chorus) {
		if (section.startMs > position) {
			return section.startMs;
		}
	}
	return -1;
}

float LyricsWindow::pulse(std::int64_t position) const {
	if (!_analysis || !_analysis->ok
		|| !AyuSettings::getInstance().musicLyricsBassPulse()) {
		return 0.f;
	}
	return _analysis->pulseAt(position) * (inChorus(position) ? 1.f : 0.5f);
}

QColor LyricsWindow::wordColor(const Lyrics::Word &word, double lit) const {
	const auto keywordOn = AyuSettings::getInstance().musicLyricsKeyword();
	const auto bleeding = word.keyword && keywordOn && (_theme == Theme::Health);
	const auto info = (keywordOn && word.accent) ? FindCategory(word.accent) : nullptr;
	auto result = bleeding
		? kRed
		: (info && info->accent.isValid())
		? Mix(QColor(255, 255, 255), info->accent, 0.42)
		: QColor(255, 255, 255);
	const auto dim = (bleeding || (info && info->accent.isValid())) ? 0.42 : 0.34;
	result.setAlphaF(std::clamp(dim + (1. - dim) * lit, 0., 1.));
	return result;
}

void LyricsWindow::computeGeometry() {
	auto &g = _geometry;
	const auto w = double(width());
	const auto h = double(height());
	const auto s = std::max(0.45, std::min(w / 1920., h / 1080.));
	g.unit = s;
	const auto ox = std::max(0., (w - 1920. * s) / 2.);
	const auto oy = std::max(0., (h - 1080. * s) / 2.);
	const auto R = [&](double x, double y, double width, double height) {
		return QRectF(ox + x * s, oy + y * s, width * s, height * s);
	};
	g.close = QRectF(w - 58. * s, 18. * s, 38. * s, 38. * s);
	g.pill = R(960. - 162., 18., 324., 58.);
	g.nowCard = g.playerCard = g.leftCard = g.rightCard = g.wideCard = QRectF();
	if (w >= 900.) {
		g.nowCard = R(100., 122., 715., 315.);
		g.playerCard = R(100., 457., 715., 197.);
		g.leftCard = R(100., 682., 350., 328.);
		g.rightCard = R(466., 682., 350., 328.);
		const auto left = ox + 895. * s;
		g.lyrics = QRectF(left, oy + 168. * s, w - left - 60. * s, h - oy - 168. * s);
	} else {
		g.lyrics = QRectF(40. * s, oy + 168. * s, w - 80. * s, h - oy - 168. * s);
	}
	if (!g.playerCard.isEmpty()) {
		const auto &c = g.playerCard;
		const auto P = [&](double x, double y, double width, double height) {
			return QRectF(c.left() + x * s, c.top() + y * s, width * s, height * s);
		};
		g.save = P(715. - 84., 26., 46., 46.);
		g.more = QRectF();
		g.seek = P(92., 126., 715. - 184., 6.);
		g.volumeIcon = P(26., 152., 32., 32.);
		g.volume = P(62., 166., 92., 5.);
		g.previous = P(275. - 26., 142., 52., 52.);
		g.playPause = P(357. - 28., 140., 56., 56.);
		g.next = P(432. - 26., 142., 52., 52.);
		g.repeat = P(485. - 22., 146., 44., 44.);
		g.order = P(538. - 22., 146., 44., 44.);
	}
}

void LyricsWindow::relayout() {
	_layoutSize = size();
	computeGeometry();
	_font = lyricFont();
	const auto metrics = QFontMetricsF(_font);
	_rowHeight = metrics.height() * 1.04;
	_ascent = metrics.ascent();
	const auto maxWidth = _geometry.nowCard.isEmpty()
		? _geometry.lyrics.width()
		: std::min(_geometry.lyrics.width(), 640. * _geometry.unit);
	const auto gap = _rowHeight * 0.28;
	_translationFont = makeFont(std::max(13., _font.pixelSize() * 0.36), QFont::Normal);
	_translationChip = std::max(16., _font.pixelSize() * 0.3);
	_lines.clear();
	_lines.reserve(_data.lines.size());
	auto titleNames = QSet<QString>();
	{
		static const auto split = QRegularExpression(uR"([^\p{L}\p{N}]+)"_q);
		const auto parts = _title.split(split, Qt::SkipEmptyParts);
		if (parts.size() <= 3) {
			for (const auto &part : parts) {
				const auto normalized = Lyrics::NormalizedWord(part);
				if (normalized.size() >= 3) {
					titleNames.insert(normalized);
				}
			}
		}
	}
	auto detected = std::map<QString, int>();
	auto iconsUsed = std::map<int, int>();
	_blooms.clear();
	_textSprites.clear();
	auto top = 0.;
	for (const auto &source : _data.lines) {
		auto line = LineLayout();
		line.words = source.words.empty() ? SplitWords(source.text) : source.words;
		auto x = 0., y = 0.;
		const auto count = int(line.words.size());
		line.boxes.resize(count);
		for (auto i = 0; i < count;) {
			auto till = i;
			while (till + 1 < count && !line.words[till].text.endsWith(QChar(u' '))) {
				++till;
			}
			auto groupWidth = 0.;
			for (auto k = i; k <= till; ++k) {
				groupWidth += metrics.horizontalAdvance(
					(k == till) ? line.words[k].text.trimmed() : line.words[k].text);
			}
			if (x > 0. && x + groupWidth > maxWidth) {
				x = 0.;
				y += _rowHeight;
			}
			for (auto k = i; k <= till; ++k) {
				const auto visible = metrics.horizontalAdvance(line.words[k].text.trimmed());
				line.boxes[k] = QRectF(x, y, visible, _rowHeight);
				x += metrics.horizontalAdvance(line.words[k].text);
			}
			i = till + 1;
		}
		line.fx.assign(count, kFxNone);
		line.numbers.assign(count, 0);
		for (auto k = 0; k != count; ++k) {
			auto number = 0;
			const auto normalized = Lyrics::NormalizedWord(line.words[k].text);
			line.fx[k] = wordFx(normalized, number);
			line.numbers[k] = number;
			if (line.fx[k] == kFxDetect) {
				line.numbers[k] = ++detected[normalized];
			} else if (!line.fx[k] && !titleNames.isEmpty()) {
				static const auto split = QRegularExpression(uR"([^\p{L}\p{N}]+)"_q);
				const auto trimmed = line.words[k].text.trimmed();
				if (!trimmed.isEmpty() && trimmed.front().isUpper()) {
					for (const auto &part : trimmed.split(split, Qt::SkipEmptyParts)) {
						if (titleNames.contains(Lyrics::NormalizedWord(part))) {
							line.fx[k] = kFxName;
							break;
						}
					}
				}
			}
			if (!line.fx[k] && line.words[k].keyword && _theme != Theme::Health) {
				line.fx[k] = kFxUnderline;
			}
		}
		line.icons.assign(count, 0);
		auto lineIcons = 0;
		for (auto k = 0; k != count && lineIcons < 2; ++k) {
			const auto fx = line.fx[k];
			const auto allowed = !fx
				|| fx == kFxUnderline
				|| fx == kFxItalic
				|| fx == kFxRun
				|| fx == kFxGlitch
				|| fx == kFxGold
				|| fx == kFxShine
				|| fx == kFxRainbow;
			if (!allowed) {
				continue;
			}
			const auto icon = Pictograms::Find(Lyrics::NormalizedWord(line.words[k].text));
			if (icon && iconsUsed[icon] < 3) {
				++iconsUsed[icon];
				++lineIcons;
				line.icons[k] = icon;
			}
		}
		auto seen = std::uint32_t(0);
		for (auto k = 0; k != count; ++k) {
			const auto category = line.words[k].accent;
			const auto info = category ? FindCategory(category) : nullptr;
			if (info && info->sticker && !line.icons[k] && !(seen & (1u << category))) {
				seen |= (1u << category);
				line.stickers.emplace_back(k, info->sticker);
			}
		}
		line.top = top;
		line.height = y + _rowHeight;
		const auto index = int(_lines.size());
		if (_translation.ok && index < int(_translation.lines.size())) {
			auto translated = _translation.lines[index].trimmed();
			if (!translated.isEmpty() && translated != source.text.trimmed()) {
				auto stems = QStringList();
				for (const auto &key : _translation.keys) {
					const auto normalized = Lyrics::NormalizedWord(key.section(QChar(u' '), 0, 0));
					if (normalized.size() >= 3) {
						stems.push_back(normalized.left(std::max(3, std::min(5, int(normalized.size()) - 1))));
					}
				}
				auto html = QString();
				auto last = 0;
				static const auto token = QRegularExpression(uR"(\w+)"_q, QRegularExpression::UseUnicodePropertiesOption);
				auto it = token.globalMatch(translated);
				while (it.hasNext()) {
					const auto match = it.next();
					const auto normalized = Lyrics::NormalizedWord(match.captured());
					auto bold = false;
					for (const auto &stem : stems) {
						if (normalized.startsWith(stem)) {
							bold = true;
							break;
						}
					}
					html += translated.mid(last, match.capturedStart() - last).toHtmlEscaped();
					html += bold
						? (u"<b>"_q + match.captured().toHtmlEscaped() + u"</b>"_q)
						: match.captured().toHtmlEscaped();
					last = match.capturedEnd();
				}
				html += translated.mid(last).toHtmlEscaped();
				line.translationHtml = html;
				auto document = QTextDocument();
				document.setDefaultFont(_translationFont);
				document.setDocumentMargin(0.);
				document.setTextWidth(maxWidth - 18. * _geometry.unit);
				document.setHtml(html);
				line.translationTop = line.height + _rowHeight * 0.08;
				line.translationTextTop = line.translationTop + _translationChip + _rowHeight * 0.08;
				line.height = line.translationTextTop + document.size().height();
			}
		}
		top += line.height + gap;
		_lines.push_back(std::move(line));
	}
	_contentHeight = std::max(0., top - gap);
	_emphasis.assign(_lines.size(), 0.);

	_glassCache.clear();
	_cardArt = QPixmap();
	_cardFrame = QImage();
	const auto ratio = style::DevicePixelRatio();
	requestBackdrop(_backgroundCache.isNull()
		|| _backgroundCache.size() != size() * ratio);
}

void LyricsWindow::scheduleHiddenLayout() {
	if (_shown || !_everShown || _hiddenLayoutScheduled) {
		return;
	}
	_hiddenLayoutScheduled = true;
	const auto weak = QPointer<LyricsWindow>(this);
	QTimer::singleShot(400, this, [=] {
		const auto strong = weak.data();
		if (!strong) {
			return;
		}
		strong->_hiddenLayoutScheduled = false;
		if (!strong->_shown && strong->_layoutSize != strong->size()) {
			strong->relayout();
		}
	});
}

void LyricsWindow::setLyricsOffset(std::int64_t offset, bool announce) {
	_lyricsOffset = std::clamp<std::int64_t>(offset, -30000, 30000);
	const auto key = OffsetKey(_artist, _title);
	if (_lyricsOffset) {
		Offsets().insert(key, double(_lyricsOffset));
	} else {
		Offsets().remove(key);
	}
	SaveOffsets();
	LOG(("Karaoke sync: manual offset %1 ms for %2").arg(QString::number(_lyricsOffset), key));
	if (announce) {
		const auto value = (_lyricsOffset > 0 ? u"+"_q : (_lyricsOffset < 0 ? QString::fromUtf8("−") : QString()))
			+ QString::number(std::abs(_lyricsOffset) / 1000., 'f', 1) + QString::fromUtf8(" с");
		showToast(
			ToastKind::Music,
			QString::fromUtf8("СИНХРОНИЗАЦИЯ"),
			_lyricsOffset ? (QString::fromUtf8("Текст сдвинут на ") + value) : QString::fromUtf8("Сдвиг текста сброшен"),
			QString::fromUtf8("← → — сдвиг, Ctrl+клик по строке — «звучит сейчас», 0 — сброс"));
	}
	update();
}

std::uint64_t LyricsWindow::backdropKey() const {
	auto result = std::uint64_t(_blurred.isNull() ? 0 : _blurred.cacheKey());
	result = result * 31 + std::uint64_t(width());
	result = result * 31 + std::uint64_t(height());
	result = result * 31 + std::uint64_t(style::DevicePixelRatio());
	result = result * 31 + (_purplePalette ? 1 : 0) + (_bluePalette ? 2 : 0);
	return result ? result : 1;
}

void LyricsWindow::requestBackdrop(bool sync) {
	if (width() <= 0 || height() <= 0) {
		return;
	}
	const auto key = backdropKey();
	if (key == _backdropKey || (!sync && key == _backdropPending)) {
		return;
	}
	const auto size = this->size();
	const auto ratio = style::DevicePixelRatio();
	const auto purple = _purplePalette;
	const auto blue = _bluePalette;
	auto blurred = _blurred.isNull() ? QImage() : _blurred.toImage();
	if (sync) {
		_backdropPending = 0;
		auto result = BuildBackdrop(size, ratio, purple, blue, blurred);
		applyBackdrop(
			key,
			std::move(result.background),
			std::move(result.frost),
			std::move(result.glow));
		return;
	}
	_backdropPending = key;
	const auto weak = QPointer<LyricsWindow>(this);
	crl::async([=, blurred = std::move(blurred)] {
		auto result = BuildBackdrop(size, ratio, purple, blue, blurred);
		crl::on_main([=, result = std::move(result)]() mutable {
			const auto strong = weak.data();
			if (!strong || strong->_backdropPending != key) {
				return;
			}
			strong->_backdropPending = 0;
			strong->applyBackdrop(
				key,
				std::move(result.background),
				std::move(result.frost),
				std::move(result.glow));
		});
	});
}

void LyricsWindow::applyBackdrop(
		std::uint64_t key,
		QImage background,
		QImage frost,
		QImage glow) {
	_backgroundCache = Ui::PixmapFromImage(std::move(background));
	_frost = Ui::PixmapFromImage(std::move(frost));
	_glowCache = Ui::PixmapFromImage(std::move(glow));
	_backdropKey = key;
	_glassCache.clear();
	_widgetFrameKind = { Widget::None, Widget::None };
	update();
}

void LyricsWindow::ensureCache(int index) {
	auto &line = _lines[index];
	if (!line.sharp.isNull()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto radius = std::max(3, int(_rowHeight * 0.14));
	const auto pad = radius * 3;
	const auto size = QSize(
		int(std::ceil(_geometry.lyrics.width())) + 2 * pad,
		int(std::ceil(line.height)) + 2 * pad);
	auto image = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto q = QPainter(&image);
		auto hq = PainterHighQualityEnabler(q);
		q.setFont(_font);
		for (auto i = 0; i != int(line.words.size()); ++i) {
			auto color = wordColor(line.words[i], 1.);
			color.setAlpha(255);
			q.setPen(color);
			q.drawText(
				QPointF(pad + line.boxes[i].x(), pad + line.boxes[i].y() + _ascent),
				line.words[i].text.trimmed());
		}
		if (!line.translationHtml.isEmpty()) {
			renderTranslation(q, line, QPointF(pad, pad + line.translationTextTop), QColor(255, 255, 255, 170));
		}
	}
	if (!line.translationHtml.isEmpty()) {
		const auto height = int(std::ceil(line.height - line.translationTextTop)) + 4;
		auto bright = QImage(QSize(int(std::ceil(_geometry.lyrics.width())) + 4, height) * ratio, QImage::Format_ARGB32_Premultiplied);
		bright.setDevicePixelRatio(ratio);
		bright.fill(Qt::transparent);
		{
			auto q = QPainter(&bright);
			auto hq = PainterHighQualityEnabler(q);
			renderTranslation(q, line, QPointF(), QColor(255, 255, 255, 235));
		}
		line.translation = Ui::PixmapFromImage(std::move(bright));
	}
	line.sharp = Ui::PixmapFromImage(QImage(image));
	constexpr auto kDownscale = 6;
	auto small = image.scaled(
		std::max(1, image.width() / kDownscale),
		std::max(1, image.height() / kDownscale),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	small.setDevicePixelRatio(1.);
	small = Images::BlurLargeImage(std::move(small), 2);
	auto soft = small.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	soft.setDevicePixelRatio(ratio);
	line.blurred = Ui::PixmapFromImage(std::move(soft));
}

void LyricsWindow::showToast(ToastKind kind, QString label, QString title, QString text) {
	_toast = Toast{
		.kind = kind,
		.label = std::move(label),
		.title = std::move(title),
		.text = std::move(text),
		.shownAt = crl::now(),
	};
}

void LyricsWindow::tickHeart(double dt, std::int64_t position) {
	const auto analysed = _analysis && _analysis->ok;
	const auto now = crl::now();
	const auto seconds = now / 1000.;

	auto trigger = false;
	auto amplitude = 0.;
	if (_playing && analysed) {
		const auto value = _analysis->pulseAt(position);
		_pulseAverage += (value - _pulseAverage) * (1. - std::exp(-dt / 0.6));
		const auto threshold = std::max(0.3, _pulseAverage * 1.18 + 0.06);
		const auto rising = (value > threshold) && (_lastPulse <= threshold || value > _lastPulse + 0.12);
		if (rising && now - _lastKickAt > 200) {
			_lastKickAt = now;
			_kicks.push_back(now);
			trigger = true;
			amplitude = std::clamp(0.3 + 0.85 * value, 0.3, 1.15);
		}
		_lastPulse = value;
		if (!trigger && now - _lastKickAt > 1300) {
			_lastKickAt = now;
			trigger = true;
			amplitude = 0.3 + 0.1 * Hash01(std::uint32_t(now), 3, 3);
		}
	} else {
		const auto period = crl::time(60000. / std::max(40., _heart));
		if (!_syntheticBeatAt || now - _syntheticBeatAt >= period) {
			_syntheticBeatAt = now;
			trigger = true;
			amplitude = (_playing ? 0.6 : 0.42) + 0.3 * Hash01(std::uint32_t(now), 5, 5);
		}
	}
	while (!_kicks.empty() && now - _kicks.front() > 5000) {
		_kicks.pop_front();
	}

	const auto energy = analysed
		? _analysis->energyAt(position)
		: (0.35 + 0.08 * std::sin(seconds * 0.7));
	auto target = 64. + 50. * energy + (inChorus(position) ? 14. : 0.) + 30. * _redBoost;
	if (_playing && analysed && _kicks.size() >= 3) {
		const auto span = std::max<crl::time>(800, _kicks.back() - _kicks.front());
		auto rate = (_kicks.size() - 1) * 60000. / span;
		while (rate > 175.) {
			rate /= 2.;
		}
		target = 0.6 * std::clamp(rate, 55., 175.) + 0.4 * target;
	}
	if (!_playing) {
		target = 66. + 3. * std::sin(seconds * 0.4);
	}
	if (trigger) {
		target += 4. * amplitude;
	}
	_heart += (target - _heart) * (1. - std::exp(-dt / 0.55));
	_heart = std::clamp(_heart, 52., 178.);

	const auto shown = int(std::round(_heart));
	if (_heartHistory.empty() || now - _heartHistory.back().first >= 100) {
		_heartHistory.emplace_back(now, shown);
	}
	while (!_heartHistory.empty() && now - _heartHistory.front().first > 30000) {
		_heartHistory.pop_front();
	}
	_heartMin = shown;
	_heartMax = shown;
	for (const auto &[at, value] : _heartHistory) {
		_heartMin = std::min(_heartMin, value);
		if (now - at <= 6000) {
			_heartMax = std::max(_heartMax, value);
		}
	}

	if (trigger) {
		const auto pattern = HeartbeatPattern();
		const auto jitter = Hash01(std::uint32_t(now), 7, 1);
		for (auto i = 0; i != int(pattern.size()); ++i) {
			const auto wave = (i >= 11) ? (0.7 + 0.6 * jitter) : 1.;
			_ecgPattern[i] = pattern[i] * float(amplitude * wave);
		}
		_ecgPatternAt = 0;
	}
	_baselinePhase += dt;
	_ecgClock += dt;
	while (_ecgClock >= 1. / kEcgRate) {
		_ecgClock -= 1. / kEcgRate;
		const auto wander = 0.035 * std::sin(_baselinePhase * 1.7) + 0.02 * std::sin(_baselinePhase * 4.3);
		auto value = float(wander + (Hash01(std::uint32_t(_ecgHead), std::uint32_t(seconds * 10), 3) - 0.5) * 0.045);
		if (_ecgPatternAt >= 0) {
			value += _ecgPattern[_ecgPatternAt];
			if (++_ecgPatternAt >= int(_ecgPattern.size())) {
				_ecgPatternAt = -1;
			}
		}
		_ecg[_ecgHead] = value;
		_ecgHead = (_ecgHead + 1) % kEcgSamples;
	}
}

void LyricsWindow::tickParticles(double dt, std::int64_t position) {
	const auto boost = (1. + 1.6 * pulse(position)) * ((_theme == Theme::Rain) ? 9. : 1.);
	const auto seconds = crl::now() / 1000.;
	for (auto &particle : _particles) {
		particle.y += particle.speed * dt * boost;
		particle.x += std::sin(seconds * 0.35 + particle.phase) * 0.0006;
		if (particle.y > 1.05) {
			particle.y = -0.05;
			particle.x = Hash01(std::uint32_t(seconds * 1000), std::uint32_t(particle.phase * 100), 9);
		}
		if (particle.x < -0.05) {
			particle.x += 1.1;
		} else if (particle.x > 1.05) {
			particle.x -= 1.1;
		}
	}
}

void LyricsWindow::tick(crl::time now) {
	const auto dt = _lastTick ? std::clamp((now - _lastTick) / 1000., 0., 0.05) : 0.016;
	_lastTick = now;
	if (_layoutSize != size()) {
		relayout();
	}
	const auto current = position();
	_active = activeIndex(current + _lyricsOffset + kScrollLeadMs);
	if (!_lines.empty()) {
		const auto from = std::max(0, _active - 3);
		const auto till = std::min(int(_lines.size()), std::max(_active, 0) + 9);
		for (auto i = from; i < till; ++i) {
			if (_lines[i].sharp.isNull()) {
				ensureCache(i);
				break;
			}
		}
	}
	if (now > _manualUntil) {
		_manualOffset *= std::exp(-dt / 0.25);
	}
	auto target = 0.;
	if (_active >= 0 && _active < int(_lines.size())) {
		target = _lines[_active].top;
	} else if (!_data.synced && _duration > 0 && !_lines.empty()) {
		const auto progress = std::clamp(double(current) / _duration, 0., 1.);
		target = progress * std::max(0., _contentHeight - _lines.back().height);
	}
	target = std::clamp(target + _manualOffset, -_rowHeight * 2., std::max(0., _contentHeight));
	_velocity += (kSpring * (target - _scroll) - kDamping * _velocity) * dt;
	_scroll += _velocity * dt;
	const auto blend = 1. - std::exp(-dt / kEmphasisTime);
	for (auto i = 0; i != int(_emphasis.size()); ++i) {
		_emphasis[i] += (((i == _active) ? 1. : 0.) - _emphasis[i]) * blend;
	}

	auto keyword = false;
	if (_active >= 0
		&& _active < int(_lines.size())
		&& AyuSettings::getInstance().musicLyricsKeyword()) {
		for (const auto &word : _lines[_active].words) {
			if (word.keyword && current >= word.startMs && current < word.endMs + 600) {
				keyword = true;
				break;
			}
		}
	}
	const auto boostTime = keyword ? 0.12 : 0.9;
	_redBoost += ((keyword ? 1. : 0.) - _redBoost) * (1. - std::exp(-dt / boostTime));

	tickHeart(dt, current);
	tickParticles(dt, current);
	if (_playing) {
		_listenedSeconds += dt;
	}
	if (_weatherAt && now - _weatherAt > (_weather.ok ? 15 * 60 * 1000 : 60 * 1000)) {
		requestWeather(false);
	}
	{
		const auto energy = (_analysis && _analysis->ok) ? double(_analysis->energyAt(current)) : 0.3;
		_energySmooth += (energy - _energySmooth) * (1. - std::exp(-dt / 0.5));
	}
	constexpr auto kEqBars = 28;
	if (int(_eqLevels.size()) != kEqBars) {
		_eqLevels.assign(kEqBars, 0.f);
	}
	const auto analysed = _analysis && _analysis->ok && _analysis->bands > 0;
	for (auto i = 0; i != kEqBars; ++i) {
		auto target = 0.f;
		if (_playing && analysed) {
			const auto position = double(i) * (_analysis->bands - 1) / (kEqBars - 1);
			const auto low = int(position);
			const auto mix = float(position - low);
			const auto a = _analysis->spectrumAt(current, low);
			const auto b = _analysis->spectrumAt(current, std::min(low + 1, _analysis->bands - 1));
			const auto flicker = 0.85f + 0.3f * float(Hash01(std::uint32_t(now / 70), std::uint32_t(i), 13));
			target = std::clamp((a + (b - a) * mix) * flicker, 0.f, 1.f);
		} else if (_playing) {
			target = 0.2f + 0.25f * float(std::abs(std::sin(now / 260. + i * 0.7)));
		} else {
			target = 0.06f;
		}
		auto &level = _eqLevels[i];
		const auto time = (target > level) ? 0.045 : 0.2;
		level += (target - level) * float(1. - std::exp(-dt / time));
	}

	const auto chorus = inChorus(current);
	const auto alarmed = alarm();
	if (alarmed && !_wasAlarm && _playing) {
		showToast(
			ToastKind::Health,
			QString::fromUtf8("ЗДОРОВЬЕ"),
			QString::fromUtf8("Тревога: высокий пульс"),
			QString::number(int(std::round(_heart)))
				+ QString::fromUtf8(" уд/мин в покое. Вы в порядке?"));
	} else if (!alarmed && _wasAlarm && _playing && _toast.kind == ToastKind::Health) {
		showToast(
			ToastKind::Calm,
			QString::fromUtf8("ЗДОРОВЬЕ"),
			QString::fromUtf8("Пульс снижается"),
			QString::number(int(std::round(_heart)))
				+ QString::fromUtf8(" уд/мин. Дышите ровно."));
	}
	_wasAlarm = alarmed;
	if (chorus && !_wasChorus && _playing && now - _themeToastAt > 15000) {
		_themeToastAt = now;
		const auto line = chorusLine(current);
		const auto clock = QTime::currentTime().toString(u"HH:mm"_q);
		switch (_theme) {
		case Theme::Health:
			break;
		case Theme::Love:
			showToast(ToastKind::Message, QString::fromUtf8("СООБЩЕНИЯ"), QString::fromUtf8("Она"), line.isEmpty() ? QString::fromUtf8("Я рядом. Держись.") : line);
			break;
		case Theme::Night:
			showToast(ToastKind::Calm, QString::fromUtf8("БУДИЛЬНИК"), QString::fromUtf8("Не спится"), clock + QString::fromUtf8(" — пора бы уснуть"));
			break;
		case Theme::Rain:
			showToast(ToastKind::Calm, QString::fromUtf8("ПОГОДА"), QString::fromUtf8("Ливень усиливается"), QString::fromUtf8("+5°, ощущается как +1°"));
			break;
		default:
			if (!line.isEmpty()) {
				showToast(ToastKind::Music, QString::fromUtf8("МУЗЫКА"), QString::fromUtf8("Припев"), line);
			}
			break;
		}
	}
	_wasChorus = chorus;
	updateScene(now, current);
	if (_theme == Theme::Health) {
		const auto taken = pillsTaken(current);
		if (_lastPillsTaken >= 0 && taken == 3 && _lastPillsTaken < 3 && _playing) {
			showToast(
				ToastKind::Pills,
				QString::fromUtf8("ЛЕКАРСТВА"),
				QString::fromUtf8("Приём завершён"),
				QString::fromUtf8("Препараты приняты. Улучшений нет."));
		}
		_lastPillsTaken = taken;
	}

	const auto next = nextChorusStart(current);
	const auto mode = alarmed
		? PillMode::Alert
		: (_playing && next >= 0 && next - current <= kTimerLeadMs && !chorus)
		? ((_theme == Theme::Love) ? PillMode::Call : PillMode::Timer)
		: PillMode::Track;
	_pillMode = mode;
	const auto pillTarget = ((mode == PillMode::Track) ? 324. : 420.) * _geometry.unit;
	_pillWidth = _pillWidth
		? (_pillWidth + (pillTarget - _pillWidth) * (1. - std::exp(-dt / 0.12)))
		: pillTarget;
	if (now >= _nextFrameAt) {
		update();
	}
}

void LyricsWindow::paintEvent(QPaintEvent *e) {
	const auto started = crl::now();
	auto frameTimer = QElapsedTimer();
	frameTimer.start();
	const auto cpuStarted = Ayu::Ui::ThreadCpuMs();
	const auto guard = gsl::finally([&] {
		++_textFrame;
		if (_textSprites.size() > 1500) {
			for (auto i = _textSprites.begin(); i != _textSprites.end();) {
				if (i->used < _textFrame - 240) {
					i = _textSprites.erase(i);
				} else {
					++i;
				}
			}
		}
		const auto spent = frameTimer.nsecsElapsed() / 1e6;
		_paintCpu += std::max(0., Ayu::Ui::ThreadCpuMs() - cpuStarted);
		_paintTime += crl::time(std::lround(spent));
		++_paintFrames;
		_avgPaint = _avgPaint ? (_avgPaint * 0.9 + spent * 0.1) : spent;
		if (!_lowQuality && _avgPaint > 20.) {
			_lowQuality = true;
		} else if (_lowQuality && _avgPaint < 8.) {
			_lowQuality = false;
		}
		// WHY: a heavy frame must leave the main thread room for the rest of the app,
		// otherwise every Telegram animation and scroll stutters while karaoke is open.
		_nextFrameAt = crl::now() + crl::time(spent * 0.6);
		if (started - _statsAt >= 3000) {
			if (_statsAt) {
				const auto frames = std::max(1, _paintFrames);
				LOG(("Karaoke: %1 fps, %2 ms per frame, %3x%4 x%5%6, back %7 cards %8 widgets %9").arg(
					QString::number(_paintFrames * 1000. / (started - _statsAt), 'f', 1),
					QString::number(double(_paintTime) / frames, 'f', 1),
					QString::number(width()),
					QString::number(height()),
					QString::number(style::DevicePixelRatio()),
					_lowQuality ? u" low"_q : QString(),
					QString::number(_sectionTime[0] / frames, 'f', 1),
					QString::number(_sectionTime[1] / frames, 'f', 1),
					QString::number(_sectionTime[2] / frames, 'f', 1))
					+ u" lyrics "_q + QString::number(_sectionTime[3] / frames, 'f', 1)
					+ u" top "_q + QString::number(_sectionTime[4] / frames, 'f', 1)
					+ u", thread cpu "_q + QString::number(_paintCpu / frames, 'f', 1) + u" ms"_q);
			}
			_statsAt = started;
			_paintTime = 0;
			_paintFrames = 0;
			_sectionTime = {};
			_paintCpu = 0.;
		}
	});
	auto p = QPainter(this);
	const auto shown = _showAnimation.value(_shown ? 1. : 0.);
	if (shown >= 1.) {
		paintContent(p);
		return;
	}
	if (_transitionSharp.isNull()) {
		prepareTransition();
	}
	p.fillRect(rect(), QColor(0, 0, 0, int(170 * shown)));
	p.setOpacity(std::min(1., shown * 1.8));
	p.drawPixmap(0, 0, _transitionBlur);
	p.setOpacity(shown * shown);
	p.drawPixmap(0, 0, _transitionSharp);
}

void LyricsWindow::paintContent(QPainter &p) {
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);
	if (_layoutSize != size()) {
		relayout();
	}
	const auto current = position();
	auto timer = QElapsedTimer();
	timer.start();
	auto mark = qint64(0);
	const auto section = [&](int index) {
		const auto now = timer.nsecsElapsed();
		_sectionTime[index] += (now - mark) / 1e6;
		mark = now;
	};
	paintBackground(p, current);
	paintParticles(p, false);
	section(0);
	p.save();
	if (!_geometry.nowCard.isEmpty()) {
		paintNowCard(p, current);
		paintPlayerCard(p, current);
	}
	section(1);
	paintSceneWidget(p, true, _geometry.leftCard, current);
	paintSceneWidget(p, false, _geometry.rightCard, current);
	section(2);
	paintLyrics(p, current + _lyricsOffset);
	section(3);
	p.restore();
	paintParticles(p, true);
	paintStatusBar(p);
	paintPill(p, current);
	paintWeatherChip(p);
	paintToast(p);
	section(4);
}

void LyricsWindow::paintBackground(QPainter &p, std::int64_t position) {
	p.drawPixmap(0, 0, _backgroundCache);
	const auto strength = std::min(1., std::max(double(pulse(position)) * 0.8, _redBoost * 0.9)
		+ (inChorus(position) ? 0.12 : 0.)
		+ (alarm() ? 0.2 : 0.));
	if (strength > (_lowQuality ? 0.3 : 0.01)) {
		p.save();
		p.setOpacity(strength);
		p.drawPixmap(0, 0, _glowCache);
		p.restore();
	}
}

void LyricsWindow::paintParticles(QPainter &p, bool front) {
	if (_lowQuality) {
		return;
	}
	const auto u = _geometry.unit * 1.5;
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != int(_particles.size()); ++i) {
		if ((i % 4 == 0) != front) {
			continue;
		}
		const auto &particle = _particles[i];
		const auto twinkle = 0.75 + 0.25 * std::sin(crl::now() / 600. + particle.phase);
		p.setBrush(QColor(255, 255, 255, int(255 * particle.alpha * twinkle * (front ? 0.7 : 1.))));
		const auto r = particle.radius * u;
		const auto center = QPointF(particle.x * width(), particle.y * height());
		if (_theme == Theme::Rain && particle.radius < 4.) {
			p.setPen(QPen(QColor(200, 215, 255, int(255 * particle.alpha * 0.6)), std::max(1., r * 0.7)));
			p.drawLine(center, center + QPointF(-3. * u, 16. * u * (0.6 + particle.speed * 20.)));
			p.setPen(Qt::NoPen);
		} else {
			p.drawEllipse(center, r, r);
		}
	}
}

void LyricsWindow::paintGlass(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	const auto radius = 30. * s;
	auto cached = (const QPixmap*)nullptr;
	for (const auto &[area, pixmap] : _glassCache) {
		if (area == rect) {
			cached = &pixmap;
			break;
		}
	}
	if (!cached) {
		const auto ratio = style::DevicePixelRatio();
		auto image = QImage(rect.size().toSize() * ratio, QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::transparent);
		{
			auto q = QPainter(&image);
			auto hq = PainterHighQualityEnabler(q);
			const auto local = QRectF(QPointF(), rect.size());
			auto path = QPainterPath();
			path.addRoundedRect(local, radius, radius);
			q.setClipPath(path);
			const auto sx = double(_frost.width()) / std::max(1, width());
			const auto sy = double(_frost.height()) / std::max(1, height());
			q.drawPixmap(
				local,
				_frost,
				QRectF(rect.left() * sx, rect.top() * sy, rect.width() * sx, rect.height() * sy));
			q.fillRect(local, QColor(255, 255, 255, 26));
			auto sheen = QLinearGradient(local.topLeft(), local.bottomLeft());
			sheen.setColorAt(0., QColor(255, 255, 255, 34));
			sheen.setColorAt(0.35, QColor(255, 255, 255, 8));
			sheen.setColorAt(1., QColor(0, 0, 0, 30));
			q.fillRect(local, sheen);
			q.setClipping(false);
			auto border = QLinearGradient(local.topLeft(), local.bottomLeft());
			border.setColorAt(0., QColor(255, 255, 255, 70));
			border.setColorAt(1., QColor(255, 255, 255, 18));
			q.setPen(QPen(QBrush(border), 1.4 * s));
			q.setBrush(Qt::NoBrush);
			q.drawRoundedRect(local.marginsRemoved(QMarginsF(0.7, 0.7, 0.7, 0.7)), radius, radius);
			auto inner = QLinearGradient(local.topLeft(), local.topRight());
			inner.setColorAt(0., QColor(255, 255, 255, 0));
			inner.setColorAt(0.5, QColor(255, 255, 255, 90));
			inner.setColorAt(1., QColor(255, 255, 255, 0));
			q.setPen(QPen(QBrush(inner), 1.2 * s));
			q.drawLine(QPointF(radius, 1.6 * s), QPointF(local.width() - radius, 1.6 * s));
		}
		_glassCache.emplace_back(rect, Ui::PixmapFromImage(std::move(image)));
		cached = &_glassCache.back().second;
	}
	p.drawPixmap(rect.topLeft().toPoint(), *cached);
	const auto red = std::max(double(pulse(position)) * 0.6, _redBoost);
	if (red > 0.01) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(210, 22, 40, int(55 * red)));
		p.drawRoundedRect(rect, radius, radius);
	}
}

void LyricsWindow::paintIconButton(
		QPainter &p,
		const QRectF &rect,
		const style::icon &icon,
		Control control,
		QColor color) const {
	if (_hovered == control || _pressed == control) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, (_pressed == control) ? 40 : 24));
		p.drawEllipse(rect);
	}
	icon.paintInCenter(p, rect.toRect(), color);
}

void LyricsWindow::paintStar(QPainter &p, const QRectF &rect, bool filled) const {
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, (_hovered == Control::Save) ? 46 : 26));
	p.drawEllipse(rect);
	auto star = QPainterPath();
	const auto center = rect.center();
	const auto outer = rect.width() * 0.27;
	const auto inner = outer * 0.45;
	for (auto i = 0; i != 10; ++i) {
		const auto angle = -kPi / 2. + i * kPi / 5.;
		const auto r = (i % 2) ? inner : outer;
		const auto point = center + QPointF(std::cos(angle) * r, std::sin(angle) * r);
		if (i) {
			star.lineTo(point);
		} else {
			star.moveTo(point);
		}
	}
	star.closeSubpath();
	const auto peak = (_redBoost > 0.35) || alarm();
	const auto color = peak ? kRed : QColor(255, 255, 255, 230);
	if (filled) {
		p.setPen(Qt::NoPen);
		p.setBrush(color);
	} else {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color, 1.3 * _geometry.unit, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	}
	p.drawPath(star);
}

void LyricsWindow::paintWeatherChip(QPainter &p) {
	const auto u = _geometry.unit * 1.7;
	const auto pillWidth = std::max(_pillWidth, 200. * _geometry.unit);
	const auto pillRight = _geometry.pill.center().x() + pillWidth / 2.;
	const auto height = _geometry.pill.height() * 0.72;
	const auto real = _weather.ok;
	const auto title = real
		? Weather::FormatDegrees(_weather.temperature)
		: (_weatherLoading ? QString::fromUtf8("Погода…") : QString::fromUtf8("Нет связи с погодой"));
	const auto subtitle = real
		? (_weather.city.isEmpty() ? Weather::Describe(_weather.code) : _weather.city)
		: QString();
	const auto titleFont = makeFont(12. * u, QFont::DemiBold);
	const auto subtitleFont = makeFont(8.5 * u);
	const auto icon = height * 0.62;
	const auto textWidth = std::max(
		QFontMetrics(titleFont).horizontalAdvance(title),
		QFontMetrics(subtitleFont).horizontalAdvance(subtitle));
	const auto width = 10. * u + icon + 7. * u + textWidth + 12. * u;
	const auto rect = QRectF(
		pillRight + 12. * u,
		_geometry.pill.center().y() - height / 2.,
		width,
		height);
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(QColor(255, 255, 255, 36), 1.));
	p.setBrush(QColor(0, 0, 0, 200));
	p.drawRoundedRect(rect, height / 2., height / 2.);
	const auto box = QRectF(rect.left() + 10. * u, rect.center().y() - icon / 2., icon, icon);
	const auto sky = real ? Weather::SkyOf(_weather.code) : Weather::Sky::Cloudy;
	const auto c = box.center();
	if (sky == Weather::Sky::Clear) {
		const auto color = _weather.day ? QColor(255, 204, 64) : QColor(220, 225, 255);
		p.setPen(QPen(color, 1.6 * u, Qt::SolidLine, Qt::RoundCap));
		p.setBrush(color);
		if (_weather.day) {
			p.drawEllipse(c, icon * 0.2, icon * 0.2);
			for (auto i = 0; i != 8; ++i) {
				const auto angle = i * 0.7853981633974483;
				const auto dir = QPointF(std::cos(angle), std::sin(angle));
				p.drawLine(c + dir * icon * 0.32, c + dir * icon * 0.45);
			}
		} else {
			auto moon = QPainterPath();
			moon.addEllipse(c, icon * 0.36, icon * 0.36);
			auto bite = QPainterPath();
			bite.addEllipse(c + QPointF(icon * 0.2, -icon * 0.14), icon * 0.32, icon * 0.32);
			p.setPen(Qt::NoPen);
			p.drawPath(moon.subtracted(bite));
		}
	} else {
		const auto cloudColor = real ? QColor(235, 238, 245) : QColor(255, 255, 255, 120);
		auto cloud = QPainterPath();
		cloud.addEllipse(QPointF(c.x() - icon * 0.18, c.y() + icon * 0.02), icon * 0.2, icon * 0.18);
		cloud.addEllipse(QPointF(c.x() + icon * 0.06, c.y() - icon * 0.08), icon * 0.25, icon * 0.24);
		cloud.addRoundedRect(QRectF(c.x() - icon * 0.4, c.y() - icon * 0.02, icon * 0.8, icon * 0.22), icon * 0.11, icon * 0.11);
		cloud.setFillRule(Qt::WindingFill);
		p.setPen(Qt::NoPen);
		p.setBrush(cloudColor);
		p.drawPath(cloud.simplified());
		const auto bottom = c.y() + icon * 0.3;
		if (sky == Weather::Sky::Rain || sky == Weather::Sky::Storm) {
			p.setPen(QPen(QColor(100, 180, 255), 1.4 * u, Qt::SolidLine, Qt::RoundCap));
			for (auto i = -1; i != 2; ++i) {
				const auto x = c.x() + i * icon * 0.22;
				p.drawLine(QPointF(x, bottom), QPointF(x - icon * 0.06, bottom + icon * 0.16));
			}
		} else if (sky == Weather::Sky::Snow) {
			p.setBrush(QColor(255, 255, 255));
			for (auto i = -1; i != 2; ++i) {
				p.drawEllipse(QPointF(c.x() + i * icon * 0.22, bottom + icon * 0.1), 1.3 * u, 1.3 * u);
			}
		} else if (sky == Weather::Sky::Fog) {
			p.setPen(QPen(QColor(255, 255, 255, 150), 1.2 * u, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(QPointF(c.x() - icon * 0.36, bottom + icon * 0.06), QPointF(c.x() + icon * 0.36, bottom + icon * 0.06));
		}
	}
	const auto textLeft = box.right() + 7. * u;
	if (subtitle.isEmpty()) {
		p.setFont(titleFont);
		p.setPen(QColor(255, 255, 255, real ? 255 : 170));
		drawText(p, QRectF(textLeft, rect.top(), textWidth + 2., rect.height()), Qt::AlignLeft | Qt::AlignVCenter, title);
	} else {
		p.setFont(titleFont);
		p.setPen(QColor(255, 255, 255));
		drawText(p, QPointF(textLeft, rect.top() + rect.height() * 0.48), title);
		p.setFont(subtitleFont);
		p.setPen(QColor(255, 255, 255, 150));
		drawText(p, QPointF(textLeft, rect.top() + rect.height() * 0.82), subtitle);
	}
	p.restore();
}

void LyricsWindow::paintPill(QPainter &p, std::int64_t position) {
	const auto u = _geometry.unit * 1.7;
	const auto width = std::max(_pillWidth, 200. * _geometry.unit);
	const auto rect = QRectF(_geometry.pill.center().x() - width / 2., _geometry.pill.top(), width, _geometry.pill.height());
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 240));
	p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
	p.save();
	p.setClipRect(rect.adjusted(4. * u, 0, -4. * u, 0));
	const auto pad = 6. * u;
	if (_pillMode == PillMode::Alert) {
		const auto heart = QRectF(rect.left() + 12. * u, rect.top() + 9. * u, 16. * u, 16. * u);
		p.setFont(makeFont(15. * u, QFont::Bold));
		p.setPen(kRed);
		drawText(p, heart, Qt::AlignCenter, QString::fromUtf8("♥"));
		p.setFont(makeFont(9. * u, QFont::Bold));
		drawText(p, QPointF(heart.right() + 8. * u, rect.top() + 15. * u), QString::fromUtf8("ПУЛЬС"));
		p.setFont(makeFont(9. * u));
		p.setPen(QColor(255, 255, 255, 150));
		drawText(p, QPointF(heart.right() + 8. * u, rect.top() + 26. * u), QString::fromUtf8("Тревога"));
		p.setFont(makeFont(18. * u, QFont::Bold));
		p.setPen(kRed);
		drawText(p, 
			rect.adjusted(0, 0, -14. * u, 0),
			Qt::AlignRight | Qt::AlignVCenter,
			QString::number(int(std::round(_heart))));
	} else if (_pillMode == PillMode::Call) {
		const auto avatar = QRectF(rect.left() + 7. * u, rect.top() + 6. * u, 22. * u, 22. * u);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(120, 120, 128));
		p.drawEllipse(avatar);
		p.setFont(makeFont(11. * u, QFont::Bold));
		p.setPen(QColor(255, 255, 255));
		drawText(p, avatar, Qt::AlignCenter, QString::fromUtf8("О"));
		p.setFont(makeFont(10.5 * u, QFont::DemiBold));
		drawText(p, QPointF(avatar.right() + 8. * u, rect.top() + 15. * u), QString::fromUtf8("Она"));
		p.setFont(makeFont(8.5 * u));
		p.setPen(QColor(255, 255, 255, 140));
		drawText(p, QPointF(avatar.right() + 8. * u, rect.top() + 27. * u), QString::fromUtf8("FaceTime Audio…"));
		const auto button = 24. * u;
		const auto accept = QRectF(rect.right() - 6. * u - button, rect.top() + 5. * u, button, button);
		const auto decline = accept.translated(-button - 8. * u, 0.);
		const auto ring = 1. + 0.06 * std::sin(crl::now() / 90.);
		for (const auto &[circle, color, angle] : {
				std::tuple{ decline, QColor(255, 59, 48), 135. },
				std::tuple{ accept, QColor(52, 199, 89), 0. } }) {
			const auto scaled = QRectF(
				circle.center() - QPointF(circle.width() * ring / 2., circle.height() * ring / 2.),
				circle.size() * ring);
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			p.drawEllipse(scaled);
			p.save();
			p.translate(circle.center());
			p.rotate(angle);
			p.setPen(QPen(QColor(255, 255, 255), 2.6 * u, Qt::SolidLine, Qt::RoundCap));
			p.setBrush(Qt::NoBrush);
			p.drawArc(QRectF(-6. * u, -4. * u, 12. * u, 12. * u), 30 * 16, 120 * 16);
			p.restore();
		}
	} else if (_pillMode == PillMode::Timer) {
		const auto next = nextChorusStart(position);
		const auto left = std::max<std::int64_t>(0, next - position);
		const auto ring = QRectF(rect.left() + 10. * u, rect.top() + 8. * u, 18. * u, 18. * u);
		p.setPen(QPen(QColor(255, 159, 10, 70), 2.2 * u));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(ring);
		p.setPen(QPen(kOrange, 2.2 * u, Qt::SolidLine, Qt::RoundCap));
		p.drawArc(ring, 90 * 16, int(360 * 16 * (double(left) / kTimerLeadMs)));
		p.setFont(makeFont(9. * u, QFont::Bold));
		p.setPen(kOrange);
		drawText(p, QPointF(ring.right() + 8. * u, rect.top() + 15. * u), QString::fromUtf8("ТАЙМЕР"));
		auto subtitle = QString();
		if (_data.synced) {
			const auto index = Lyrics::ActiveLineIndex(_data, next + 50);
			if (index >= 0) {
				subtitle = _data.lines[index].text;
			}
		}
		p.setFont(makeFont(9. * u));
		p.setPen(QColor(255, 255, 255, 150));
		drawText(p, 
			QPointF(ring.right() + 8. * u, rect.top() + 26. * u),
			QFontMetrics(makeFont(9. * u)).elidedText(subtitle, Qt::ElideRight, int(width * 0.45)));
		p.setFont(makeFont(18. * u, QFont::DemiBold));
		p.setPen(QColor(255, 255, 255));
		drawText(p, 
			rect.adjusted(0, 0, -14. * u, 0),
			Qt::AlignRight | Qt::AlignVCenter,
			u"0:0"_q + QString::number(int(std::ceil(left / 1000.))));
	} else {
		const auto cover = QRectF(rect.left() + pad, rect.top() + pad, rect.height() - 2 * pad, rect.height() - 2 * pad);
		auto path = QPainterPath();
		path.addRoundedRect(cover, 6. * u, 6. * u);
		p.save();
		p.setClipPath(path);
		paintArt(p, cover, 1., QPointF());
		p.restore();
		const auto dots = 5;
		const auto now = crl::now() / 1000.;
		const auto bass = pulse(position);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 230));
		const auto dotsRight = rect.right() - 14. * u;
		for (auto i = 0; i != dots; ++i) {
			const auto level = _playing
				? (0.35 + 0.65 * std::abs(std::sin(now * (2.3 + 0.5 * i) + i * 1.4)) * (0.6 + 0.4 * bass))
				: 0.3;
			const auto h = std::max(3. * u, 14. * u * level);
			const auto x = dotsRight - (dots - i) * 5. * u;
			p.drawRoundedRect(QRectF(x, rect.center().y() - h / 2., 3. * u, h), 1.5 * u, 1.5 * u);
		}
		const auto textLeft = cover.right() + 8. * u;
		const auto textWidth = dotsRight - dots * 5. * u - 8. * u - textLeft;
		p.setFont(makeFont(10.5 * u, QFont::DemiBold));
		p.setPen(QColor(255, 255, 255));
		drawText(p, 
			QPointF(textLeft, rect.top() + 15. * u),
			QFontMetrics(makeFont(10.5 * u, QFont::DemiBold)).elidedText(_title, Qt::ElideRight, int(textWidth)));
		p.setFont(makeFont(9. * u));
		p.setPen(QColor(255, 255, 255, 140));
		drawText(p, 
			QPointF(textLeft, rect.top() + 27. * u),
			QFontMetrics(makeFont(9. * u)).elidedText(_sceneStatus.isEmpty() ? _artist : _sceneStatus, Qt::ElideRight, int(textWidth)));
	}
	p.restore();
}

void LyricsWindow::paintNowCard(QPainter &p, std::int64_t position) {
	const auto s = _geometry.unit;
	const auto rect = _geometry.nowCard;
	const auto radius = 30. * s;
	const auto &source = !_customCover.isNull() ? _customCover : _art;
	if (source.isNull()) {
		auto path = QPainterPath();
		path.addRoundedRect(rect, radius, radius);
		p.save();
		p.setClipPath(path);
		paintGeneratedArt(p, rect);
		paintCardContent(p, rect, position);
		p.restore();
	} else {
		if (_cardArt.isNull()) {
			buildCardArt();
		}
		const auto ratio = style::DevicePixelRatio();
		const auto frameSize = (rect.size() * ratio).toSize();
		if (_cardFrame.size() != frameSize || _cardMask.isNull()) {
			_cardFrame = QImage(frameSize, QImage::Format_ARGB32_Premultiplied);
			_cardFrame.setDevicePixelRatio(ratio);
			auto mask = QImage(frameSize, QImage::Format_ARGB32_Premultiplied);
			mask.setDevicePixelRatio(ratio);
			mask.fill(Qt::transparent);
			{
				auto q = QPainter(&mask);
				q.setRenderHint(QPainter::Antialiasing);
				q.setPen(Qt::NoPen);
				q.setBrush(QColor(255, 255, 255));
				q.drawRoundedRect(QRectF(QPointF(), QSizeF(frameSize) / ratio), radius, radius);
			}
			_cardMask = Ui::PixmapFromImage(std::move(mask));
		}
		_cardFrame.fill(Qt::transparent);
		{
			auto q = QPainter(&_cardFrame);
			q.setRenderHint(QPainter::Antialiasing);
			const auto t = crl::now() / 1000.;
			const auto marginX = (_cardArt.width() - frameSize.width()) / 2.;
			const auto marginY = (_cardArt.height() - frameSize.height()) / 2.;
			const auto dx = int(std::lround(marginX + (_lowQuality ? 0. : std::sin(t / 9.) * marginX * 0.8)));
			const auto dy = int(std::lround(marginY + (_lowQuality ? 0. : std::cos(t / 11.) * marginY * 0.5)));
			const auto local = QRectF(QPointF(), QSizeF(frameSize) / ratio);
			q.drawPixmap(local, _cardArt, QRectF(dx, dy, frameSize.width(), frameSize.height()));
			const auto beat = pulse(position);
			if (beat > 0.02 && !_lowQuality) {
				q.fillRect(local, QColor(255, 255, 255, int(22 * beat)));
			}
			paintCardContent(q, local, position);
			q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			q.drawPixmap(0, 0, _cardMask);
		}
		p.drawImage(rect.topLeft().toPoint(), _cardFrame);
	}
	auto border = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	border.setColorAt(0., QColor(255, 255, 255, 80));
	border.setColorAt(1., QColor(255, 255, 255, 20));
	p.setPen(QPen(QBrush(border), 1.4 * s));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(rect.marginsRemoved(QMarginsF(0.7, 0.7, 0.7, 0.7)), radius, radius);

	const auto chipFont = makeFont(16. * s, QFont::DemiBold);
	const auto chipText = QString::fromUtf8("♫  Сейчас играет");
	const auto chip = QRectF(
		rect.left() + 16. * s,
		rect.top() + 16. * s,
		QFontMetricsF(chipFont).horizontalAdvance(chipText) + 30. * s,
		34. * s);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 95));
	p.drawRoundedRect(chip, 12. * s, 12. * s);
	p.setFont(chipFont);
	p.setPen(QColor(255, 255, 255, 235));
	drawText(p, chip, Qt::AlignCenter, chipText);
	const auto blink = (std::fmod(crl::now() / 1000., 1.4) < 0.9) ? 235 : 70;
	p.setBrush(QColor(255, 255, 255, _playing ? blink : 90));
	p.setPen(Qt::NoPen);
	p.drawEllipse(QPointF(rect.right() - 27. * s, rect.top() + 30. * s), 5. * s, 5. * s);

	const auto small = makeFont(15. * s, QFont::DemiBold);
	auto spaced = small;
	spaced.setLetterSpacing(QFont::AbsoluteSpacing, 1.4 * s);
	p.setFont(spaced);
	p.setPen(QColor(255, 255, 255, 185));
	const auto frames = int((position % 1000) * 30 / 1000);
	const auto total = position / 1000;
	const auto timecode = QString::asprintf("%02d:%02d:%02d", int(total / 60), int(total % 60), frames);
	const auto timeWidth = QFontMetricsF(spaced).horizontalAdvance(timecode);
	const auto caption = (_artist.isEmpty() ? _title : _artist).toUpper().replace(u", "_q, u" · "_q);
	const auto baseline = rect.bottom() - 26. * s;
	drawText(p, 
		QPointF(rect.left() + 24. * s, baseline),
		QFontMetrics(spaced).elidedText(caption, Qt::ElideRight, int(rect.width() - 80. * s - timeWidth)));
	drawText(p, QPointF(rect.right() - 24. * s - timeWidth, baseline), timecode);
}

void LyricsWindow::buildCardArt() {
	const auto &source = !_customCover.isNull() ? _customCover : _art;
	const auto rect = _geometry.nowCard;
	if (source.isNull() || rect.isEmpty()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto margin = 10. * _geometry.unit;
	const auto size = QSizeF(rect.width() + 2 * margin, rect.height() + 2 * margin);
	auto image = QImage((size * ratio).toSize(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(QColor(20, 20, 24));
	{
		auto q = QPainter(&image);
		q.setRenderHint(QPainter::SmoothPixmapTransform);
		const auto local = QRectF(QPointF(), size);
		DrawToFill(q, local, source, 1.06);
		auto shade = QLinearGradient(local.topLeft(), local.bottomLeft());
		shade.setColorAt(0., QColor(0, 0, 0, 70));
		shade.setColorAt(0.4, QColor(0, 0, 0, 0));
		shade.setColorAt(1., QColor(0, 0, 0, 150));
		q.fillRect(local, shade);
	}
	_cardArt = Ui::PixmapFromImage(std::move(image));
}

void LyricsWindow::paintCardContent(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	if (_customCover.isNull() && _art.isNull()) {
		auto shade = QLinearGradient(rect.topLeft(), rect.bottomLeft());
		shade.setColorAt(0., QColor(0, 0, 0, 70));
		shade.setColorAt(0.4, QColor(0, 0, 0, 0));
		shade.setColorAt(1., QColor(0, 0, 0, 150));
		p.fillRect(rect, shade);
	}
	if (!_lowQuality) {
		p.setPen(Qt::NoPen);
		for (auto i = 0; i != 26; ++i) {
			const auto speed = 0.03 + 0.06 * Hash01(i, 21, 3);
			const auto y = std::fmod(Hash01(i, 22, 3) + crl::now() / 1000. * speed, 1.);
			const auto x = Hash01(i, 23, 3) + 0.02 * std::sin(crl::now() / 900. + i);
			const auto size = (0.8 + 1.4 * Hash01(i, 25, 3)) * s;
			p.setBrush(QColor(255, 255, 255, int(60 + 140 * Hash01(i, 24, 3))));
			p.drawEllipse(QPointF(rect.left() + rect.width() * x, rect.top() + rect.height() * y), size, size);
		}
	}
	paintEqualizer(
		p,
		QRectF(rect.left() + 24. * s, rect.bottom() - 112. * s, rect.width() - 48. * s, 62. * s),
		position);
}

void LyricsWindow::paintPlayerCard(QPainter &p, std::int64_t position) {
	const auto &g = _geometry;
	const auto s = g.unit;
	const auto rect = g.playerCard;
	paintGlass(p, rect, position);
	const auto pad = 34. * s;
	const auto textWidth = int(g.save.left() - rect.left() - pad - 12. * s);
	const auto titleFont = makeFont(38. * s, QFont::Bold);
	p.setFont(titleFont);
	p.setPen(QColor(255, 255, 255));
	drawText(p, 
		QPointF(rect.left() + pad, rect.top() + 62. * s),
		QFontMetrics(titleFont).elidedText(_title, Qt::ElideRight, textWidth));
	const auto artistFont = makeFont(24. * s);
	p.setFont(artistFont);
	p.setPen(QColor(255, 255, 255, 150));
	drawText(p, 
		QPointF(rect.left() + pad, rect.top() + 100. * s),
		QFontMetrics(artistFont).elidedText(_artist, Qt::ElideRight, textWidth));

	const auto profile = _profileState ? _profileState() : -1;
	if (profile >= 0) {
		paintStar(p, g.save, profile > 0);
	}

	const auto progress = (_pressed == Control::Seek && _dragValue >= 0.)
		? _dragValue
		: (_duration > 0 ? std::clamp(double(position) / _duration, 0., 1.) : 0.);
	FillProgress(p, g.seek, progress, QColor(255, 255, 255, 240));
	const auto knob = ((_hovered == Control::Seek || _pressed == Control::Seek) ? 20. : 16.) * s;
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255));
	p.drawEllipse(QPointF(g.seek.left() + g.seek.width() * progress, g.seek.center().y()), knob / 2., knob / 2.);
	const auto timeFont = makeFont(16. * s);
	p.setFont(timeFont);
	p.setPen(QColor(255, 255, 255, 140));
	const auto shownPosition = std::int64_t(progress * _duration);
	drawText(p, 
		QRectF(rect.left() + pad, g.seek.center().y() - 12. * s, 60. * s, 24. * s),
		Qt::AlignLeft | Qt::AlignVCenter,
		Time(shownPosition));
	drawText(p, 
		QRectF(rect.right() - pad - 70. * s, g.seek.center().y() - 12. * s, 70. * s, 24. * s),
		Qt::AlignRight | Qt::AlignVCenter,
		u"-"_q + Time(_duration - shownPosition));

	const auto hoverDisc = [&](Control control, const QRectF &area) {
		if (_hovered == control || _pressed == control) {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, (_pressed == control) ? 40 : 22));
			p.drawEllipse(area);
		}
	};
	const auto white = QColor(255, 255, 255, 240);
	hoverDisc(Control::Previous, g.previous);
	paintGlyph(p, Control::Previous, g.previous, white);
	hoverDisc(Control::PlayPause, g.playPause);
	paintGlyph(p, Control::PlayPause, g.playPause, white);
	hoverDisc(Control::Next, g.next);
	paintGlyph(p, Control::Next, g.next, white);

	const auto &settings = Core::App().settings();
	const auto repeat = settings.playerRepeatMode();
	const auto order = settings.playerOrderMode();
	const auto modeButton = [&](const QRectF &area, const style::icon &, Control control, bool active) {
		const auto chip = area.marginsRemoved(QMarginsF(3. * s, 3. * s, 3. * s, 3. * s));
		p.setPen(Qt::NoPen);
		if (active) {
			p.setBrush(QColor(255, 255, 255, 235));
			p.drawRoundedRect(chip, 10. * s, 10. * s);
		} else if (_hovered == control) {
			p.setBrush(QColor(255, 255, 255, 30));
			p.drawRoundedRect(chip, 10. * s, 10. * s);
		}
		paintModeIcon(
			p,
			control,
			area,
			active ? QColor(30, 30, 36) : QColor(255, 255, 255, 200),
			(control == Control::Repeat) && (repeat == Media::RepeatMode::One) ? true
				: (control == Control::Order) && (order == Media::OrderMode::Shuffle));
	};
	modeButton(
		g.repeat,
		(repeat == Media::RepeatMode::One) ? st::musicIslandIconRepeatOne : st::musicIslandIconRepeat,
		Control::Repeat,
		repeat != Media::RepeatMode::None);
	modeButton(
		g.order,
		(order == Media::OrderMode::Shuffle) ? st::musicIslandIconShuffle : st::musicIslandIconOrder,
		Control::Order,
		order != Media::OrderMode::Default);
	const auto tagFont = makeFont(15. * s, QFont::DemiBold);
	p.setFont(tagFont);
	p.setPen(QColor(255, 255, 255, 110));
	drawText(p, 
		QRectF(g.order.right() + 8. * s, g.order.top(), rect.right() - g.order.right() - 20. * s, g.order.height()),
		Qt::AlignLeft | Qt::AlignVCenter,
		QString::number(_speed, 'g', 3) + u"x · Whitegram"_q);

	const auto volume = (_pressed == Control::Volume && _dragValue >= 0.)
		? _dragValue
		: settings.songVolume();
	const auto &volumeIcon = (volume <= 0.)
		? st::musicIslandIconVolume0
		: (volume < 0.66)
		? st::musicIslandIconVolume1
		: st::musicIslandIconVolume2;
	paintIconButton(p, g.volumeIcon, volumeIcon, Control::VolumeIcon, QColor(255, 255, 255, 170));
	FillProgress(p, g.volume, volume, QColor(255, 255, 255, 220));
}

void LyricsWindow::paintPulseWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto u = _geometry.unit * 2.;
	paintGlass(p, rect, position);
	const auto intensity = std::clamp((_heart - 100.) / 30., 0., 1.);
	const auto pad = 12. * u;
	p.setFont(makeFont(12. * u, QFont::Bold));
	p.setPen(kRed);
	drawText(p, QPointF(rect.left() + pad, rect.top() + pad + 10. * u), QString::fromUtf8("♥"));
	p.setFont(makeFont(10.5 * u, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255, 235));
	drawText(p, QPointF(rect.left() + pad + 15. * u, rect.top() + pad + 10. * u), QString::fromUtf8("Пульс"));
	p.setFont(makeFont(8.5 * u));
	p.setPen(QColor(255, 255, 255, 120));
	const auto now = QString::fromUtf8("Сейчас");
	drawText(p, 
		QPointF(rect.right() - pad - QFontMetricsF(makeFont(8.5 * u)).horizontalAdvance(now), rect.top() + pad + 10. * u),
		now);

	const auto number = QString::number(int(std::round(_heart)));
	const auto big = makeFont(std::min(40. * u, rect.height() * 0.27), QFont::Bold);
	const auto numberY = rect.top() + pad + 20. * u + QFontMetricsF(big).ascent();
	p.setFont(big);
	p.setPen(Mix(QColor(255, 255, 255), QColor(255, 70, 85), intensity));
	drawText(p, QPointF(rect.left() + pad, numberY), number);
	p.setFont(makeFont(9. * u, QFont::DemiBold));
	p.setPen(Mix(QColor(255, 255, 255, 170), QColor(255, 70, 85), intensity));
	drawText(p, 
		QPointF(rect.left() + pad + QFontMetricsF(big).horizontalAdvance(number) + 5. * u, numberY),
		QString::fromUtf8("уд/мин"));

	const auto footer = 18. * u;
	const auto chart = QRectF(
		rect.left() + pad,
		numberY + 6. * u,
		rect.width() - 2 * pad,
		rect.bottom() - numberY - 6. * u - footer - pad * 0.5);
	if (chart.height() > 8.) {
		auto path = QPainterPath();
		const auto mid = chart.center().y() + chart.height() * 0.15;
		for (auto i = 0; i != kEcgSamples; ++i) {
			const auto value = _ecg[(_ecgHead + i) % kEcgSamples];
			const auto point = QPointF(
				chart.left() + chart.width() * i / (kEcgSamples - 1),
				mid - value * chart.height() * 0.55);
			if (i) {
				path.lineTo(point);
			} else {
				path.moveTo(point);
			}
		}
		const auto line = Mix(QColor(255, 255, 255, 190), kRed, std::max(intensity, _redBoost));
		auto glow = line;
		glow.setAlpha(50);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(glow, 4. * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPath(path);
		p.setPen(QPen(line, 1.5 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPath(path);
		const auto last = _ecg[(_ecgHead + kEcgSamples - 1) % kEcgSamples];
		p.setPen(Qt::NoPen);
		p.setBrush(line);
		p.drawEllipse(QPointF(chart.right(), mid - last * chart.height() * 0.55), 2.6 * u, 2.6 * u);
	}
	p.setFont(makeFont(8. * u));
	p.setPen(QColor(255, 255, 255, 95));
	drawText(p, 
		QPointF(rect.left() + pad, rect.bottom() - pad + 2. * u),
		QString::fromUtf8("Диапазон ") + QString::number(_heartMin) + QString::fromUtf8("–") + QString::number(_heartMax));
}

void LyricsWindow::paintLinesWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto u = _geometry.unit * 2.;
	paintGlass(p, rect, position);
	const auto pad = 12. * u;
	const auto synced = _data.synced && !_data.lines.empty();
	const auto lineCount = int(_data.lines.size());
	const auto active = synced ? Lyrics::ActiveLineIndex(_data, position) : -1;
	const auto capacity = std::clamp(int((rect.height() - 70. * u) / (28. * u)), 1, 5);
	const auto perPage = std::max(kNextLines, capacity);
	const auto first = synced
		? std::clamp((std::max(active, 0) / perPage) * perPage, 0, std::max(0, lineCount - perPage))
		: 0;
	const auto rows = synced ? std::min(perPage, lineCount - first) : 0;
	auto done = 0;
	for (auto i = 0; i != rows; ++i) {
		if (position >= _data.lines[first + i].endMs) {
			++done;
		}
	}

	p.setFont(makeFont(10.5 * u, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255, 235));
	drawText(p, QPointF(rect.left() + pad, rect.top() + pad + 10. * u), QString::fromUtf8("✎  Строки"));
	if (rows) {
		p.setFont(makeFont(8.5 * u));
		p.setPen(QColor(255, 255, 255, 120));
		const auto counter = QString::number(done) + QString::fromUtf8(" из ") + QString::number(rows);
		drawText(p, 
			QPointF(rect.right() - pad - QFontMetricsF(makeFont(8.5 * u)).horizontalAdvance(counter), rect.top() + pad + 10. * u),
			counter);
	}

	const auto footer = 22. * u;
	const auto listTop = rect.top() + pad + 20. * u;
	const auto listBottom = rect.bottom() - pad - footer - 4. * u;
	if (!rows) {
		p.setFont(makeFont(9.5 * u));
		p.setPen(QColor(255, 255, 255, 130));
		drawText(p, 
			QRectF(rect.left() + pad, listTop, rect.width() - 2 * pad, listBottom - listTop),
			Qt::AlignCenter | Qt::TextWordWrap,
			_loading
				? QString::fromUtf8("Ищем текст…")
				: _data.hasData
				? QString::fromUtf8("Текст без таймингов")
				: QString::fromUtf8("Текст не найден"));
	}
	const auto rowHeight = rows ? std::min(30. * u, (listBottom - listTop) / perPage) : 0.;
	for (auto i = 0; i != rows; ++i) {
		const auto &line = _data.lines[first + i];
		const auto y = listTop + rowHeight * i;
		const auto current = (first + i == active) && position < line.endMs;
		const auto finished = position >= line.endMs;
		const auto icon = QRectF(rect.left() + pad, y + rowHeight / 2. - 8. * u, 16. * u, 16. * u);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, current ? 60 : 30));
		p.drawEllipse(icon);
		p.setFont(makeFont(8. * u, QFont::Bold));
		p.setPen(QColor(255, 255, 255, 220));
		drawText(p, icon, Qt::AlignCenter, QString::fromUtf8("♪"));
		const auto mark = QRectF(rect.right() - pad - 14. * u, y + rowHeight / 2. - 7. * u, 14. * u, 14. * u);
		const auto textLeft = icon.right() + 7. * u;
		const auto textWidth = int(mark.left() - 6. * u - textLeft);
		p.setFont(makeFont(9.5 * u, QFont::DemiBold));
		p.setPen(QColor(255, 255, 255, current ? 255 : 200));
		drawText(p, 
			QPointF(textLeft, y + rowHeight * 0.48),
			QFontMetrics(makeFont(9.5 * u, QFont::DemiBold)).elidedText(line.text, Qt::ElideRight, textWidth));
		p.setFont(makeFont(7.5 * u));
		p.setPen(QColor(255, 255, 255, 105));
		drawText(p, QPointF(textLeft, y + rowHeight * 0.86), Time(line.ms));
		if (finished) {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 235));
			p.drawEllipse(mark);
			auto check = QPainterPath();
			check.moveTo(mark.left() + mark.width() * 0.28, mark.top() + mark.height() * 0.52);
			check.lineTo(mark.left() + mark.width() * 0.45, mark.top() + mark.height() * 0.68);
			check.lineTo(mark.left() + mark.width() * 0.74, mark.top() + mark.height() * 0.36);
			p.setPen(QPen(QColor(25, 25, 30), 1.6 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			p.setBrush(Qt::NoBrush);
			p.drawPath(check);
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, current ? 70 : 34));
			p.drawEllipse(mark);
		}
	}

	const auto pill = QRectF(rect.left() + pad, rect.bottom() - pad - footer, rect.width() - 2 * pad, footer);
	auto text = QString();
	auto red = false;
	if (!synced) {
		text = QString::fromUtf8("Ожидание…");
	} else if (inChorus(position) || _redBoost > 0.35) {
		red = true;
		text = (active >= 0) ? _data.lines[active].text : QString();
	} else if (active < 0) {
		text = QString::fromUtf8("Начало через ")
			+ Time(_data.lines.front().ms - position);
	} else if (active + 1 < lineCount) {
		const auto wait = _data.lines[active + 1].ms - position;
		text = (wait > 1500)
			? (QString::fromUtf8("Дальше через ") + Time(wait))
			: QString::fromUtf8("Звучит…");
	} else {
		text = QString::fromUtf8("Финал");
	}
	p.setPen(Qt::NoPen);
	p.setBrush(red ? QColor(225, 30, 48) : QColor(255, 255, 255, 34));
	p.drawRoundedRect(pill, pill.height() / 2., pill.height() / 2.);
	p.setFont(makeFont(8.5 * u, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255, red ? 255 : 190));
	drawText(p, 
		pill,
		Qt::AlignCenter,
		QFontMetrics(makeFont(8.5 * u, QFont::DemiBold)).elidedText(text, Qt::ElideRight, int(pill.width() - 16. * u)));
}

void LyricsWindow::paintToast(QPainter &p) {
	if (!_toast.shownAt) {
		return;
	}
	const auto elapsed = crl::now() - _toast.shownAt;
	if (elapsed > kToastDuration) {
		_toast.shownAt = 0;
		_toastCache = QPixmap();
		return;
	}
	const auto s = _geometry.unit;
	const auto narrow = _geometry.nowCard.isEmpty();
	const auto w = narrow ? std::min(width() - 60. * s, 640. * s) : std::min(660. * s, width() - _geometry.lyrics.left() - 40. * s);
	const auto h = 112. * s;
	const auto x = narrow ? (width() - w) / 2. : (width() - 70. * s - w);
	const auto rect = QRectF(x, _geometry.lyrics.top() - 96. * s, w, h);
	const auto pad = 4.;
	if (_toastCacheAt != _toast.shownAt || _toastCache.isNull()) {
		const auto ratio = style::DevicePixelRatio();
		auto image = QImage(
			QSize(int(std::ceil(w + 2 * pad)), int(std::ceil(h + 2 * pad))) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::transparent);
		{
			auto q = QPainter(&image);
			q.setRenderHint(QPainter::Antialiasing);
			q.setRenderHint(QPainter::TextAntialiasing);
			q.translate(pad - rect.left(), pad - rect.top());
			paintToastContent(q, rect);
		}
		_toastCache = Ui::PixmapFromImage(std::move(image));
		_toastCacheAt = _toast.shownAt;
	}
	const auto t = elapsed / 1000.;
	const auto spring = 1. - std::exp(-7.5 * t) * std::cos(11. * t);
	const auto leaving = std::clamp((elapsed - (kToastDuration - 420)) / 420., 0., 1.);
	const auto visible = Smooth(elapsed / 220.) * (1. - Smooth(leaving));
	const auto lift = (1. - spring) * 90. * s + Smooth(leaving) * 60. * s;
	const auto scale = (0.88 + 0.12 * std::min(spring, 1.04)) * (1. - 0.06 * Smooth(leaving));
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.setOpacity(visible);
	p.translate(rect.center().x(), rect.top() - lift);
	p.scale(scale, scale);
	p.drawPixmap(QPointF(-w / 2. - pad, -pad), _toastCache);
	p.restore();
}

void LyricsWindow::paintToastContent(QPainter &p, const QRectF &rect) {
	const auto s = _geometry.unit;
	const auto t = 10.;
	const auto h = rect.height();
	const auto radius = 30. * s;
	p.save();
	auto path = QPainterPath();
	path.addRoundedRect(rect, radius, radius);
	p.save();
	p.setClipPath(path);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	const auto sx = double(_frost.width()) / std::max(1, width());
	const auto sy = double(_frost.height()) / std::max(1, height());
	p.drawPixmap(rect, _frost, QRectF(rect.left() * sx, rect.top() * sy, rect.width() * sx, rect.height() * sy));
	p.fillRect(rect, QColor(255, 255, 255, 22));
	auto sheen = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	sheen.setColorAt(0., QColor(255, 255, 255, 30));
	sheen.setColorAt(1., QColor(0, 0, 0, 25));
	p.fillRect(rect, sheen);
	p.restore();
	auto border = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	border.setColorAt(0., QColor(255, 255, 255, 80));
	border.setColorAt(1., QColor(255, 255, 255, 20));
	p.setPen(QPen(QBrush(border), 1.4 * s));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(rect.marginsRemoved(QMarginsF(0.7, 0.7, 0.7, 0.7)), radius, radius);

	const auto icon = QRectF(rect.left() + 22. * s, rect.top() + (h - 66. * s) / 2., 66. * s, 66. * s);
	const auto light = (_toast.kind == ToastKind::Health || _toast.kind == ToastKind::Calm);
	const auto tile = (_toast.kind == ToastKind::Weather)
		? QColor(60, 140, 255)
		: (_toast.kind == ToastKind::Focus)
		? QColor(110, 90, 255)
		: (_toast.kind == ToastKind::Photo)
		? QColor(255, 255, 255)
		: light
		? QColor(255, 255, 255)
		: QColor(255, 255, 255, 28);
	p.setPen(Qt::NoPen);
	p.setBrush(tile);
	p.drawRoundedRect(icon, 16. * s, 16. * s);
	const auto pop = 1. + 0.25 * std::exp(-6. * t) * std::sin(14. * t);
	p.save();
	p.translate(icon.center());
	p.scale(pop, pop);
	if (_toast.kind == ToastKind::Pills) {
		p.rotate(-40.);
		const auto capsule = QRectF(-20. * s, -8.5 * s, 40. * s, 17. * s);
		auto whole = QPainterPath();
		whole.addRoundedRect(capsule, 8.5 * s, 8.5 * s);
		p.setBrush(QColor(255, 255, 255, 120));
		p.drawPath(whole);
		p.save();
		p.setClipRect(QRectF(capsule.left(), capsule.top(), capsule.width() / 2., capsule.height()));
		p.setBrush(QColor(255, 255, 255));
		p.drawPath(whole);
		p.restore();
	} else {
		const auto glyph = [&] {
			switch (_toast.kind) {
			case ToastKind::Music: return QString::fromUtf8("♫");
			case ToastKind::Saved: return QString::fromUtf8("★");
			case ToastKind::Removed: return QString::fromUtf8("☆");
			case ToastKind::Message: return QString::fromUtf8("●");
			case ToastKind::Weather: return QString::fromUtf8("☁");
			case ToastKind::Clock: return QString::fromUtf8("◷");
			case ToastKind::Focus: return QString::fromUtf8("☾");
			case ToastKind::Photo: return QString::fromUtf8("✿");
			case ToastKind::Recorder: return QString::fromUtf8("●");
			case ToastKind::Search: return QString::fromUtf8("⌕");
			case ToastKind::Map: return QString::fromUtf8("➤");
			case ToastKind::Phone: return QString::fromUtf8("✆");
			case ToastKind::Reminder: return QString::fromUtf8("★");
			default: return QString::fromUtf8("♥");
			}
		}();
		p.setFont(makeFont(32. * s, QFont::Bold));
		p.setPen((light || _toast.kind == ToastKind::Recorder)
			? kRed
			: (_toast.kind == ToastKind::Saved)
			? QColor(255, 159, 10)
			: (_toast.kind == ToastKind::Photo)
			? QColor(255, 120, 170)
			: QColor(255, 255, 255));
		if (_toast.kind == ToastKind::Message) {
			auto bubble = QPainterPath();
			bubble.addRoundedRect(QRectF(-17. * s, -14. * s, 34. * s, 25. * s), 12. * s, 12. * s);
			auto tail = QPainterPath();
			tail.moveTo(-9. * s, 8. * s);
			tail.lineTo(-13. * s, 17. * s);
			tail.lineTo(-2. * s, 10. * s);
			tail.closeSubpath();
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255));
			p.drawPath(bubble.united(tail));
		} else {
			drawText(p, QRectF(-33. * s, -33. * s, 66. * s, 66. * s), Qt::AlignCenter, glyph);
		}
	}
	p.restore();

	const auto textLeft = icon.right() + 22. * s;
	const auto labelFont = makeFont(17. * s, QFont::DemiBold);
	p.setFont(labelFont);
	p.setPen(QColor(255, 255, 255, 150));
	drawText(p, QPointF(textLeft, rect.top() + 34. * s), _toast.label);
	const auto when = QString::fromUtf8("сейчас");
	drawText(p, 
		QPointF(rect.right() - 26. * s - QFontMetricsF(labelFont).horizontalAdvance(when), rect.top() + 34. * s),
		when);
	p.setFont(makeFont(25. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(textLeft, rect.top() + 66. * s), _toast.title);
	const auto textFont = makeFont(20. * s);
	p.setFont(textFont);
	p.setPen(QColor(255, 255, 255, 205));
	drawText(p, 
		QPointF(textLeft, rect.top() + 94. * s),
		QFontMetrics(textFont).elidedText(_toast.text, Qt::ElideRight, int(rect.right() - textLeft - 26. * s)));
	p.restore();
}

void LyricsWindow::paintBleedingWord(
		QPainter &p,
		const QString &text,
		QPointF baseline,
		double width,
		QColor color,
		double strength,
		double age,
		int seed) const {
	const auto step = std::uint32_t(crl::now() / 80);
	const auto shake = (Hash01(step, std::uint32_t(seed), 1) > 0.55)
		? (Hash01(step, std::uint32_t(seed), 2) - 0.5) * 5. * strength
		: 0.;
	const auto at = baseline + QPointF(shake, 0.);
	p.setPen(QColor(255, 0, 40, int(90 * strength)));
	drawText(p, at + QPointF(1.8 * strength, 0.), text);
	p.setPen(QColor(120, 0, 10, int(120 * strength)));
	drawText(p, at + QPointF(-1.2 * strength, 1.), text);

	const auto drops = 3 + int(text.size() / 2);
	const auto grow = Smooth(age / 1.1);
	for (auto k = 0; k != drops; ++k) {
		const auto x = at.x() + width * (0.08 + 0.84 * Hash01(std::uint32_t(seed), std::uint32_t(k), 5));
		const auto length = _rowHeight * (0.18 + 0.75 * Hash01(std::uint32_t(seed), std::uint32_t(k), 6)) * grow;
		const auto thickness = std::max(1.5, _rowHeight * (0.035 + 0.03 * Hash01(std::uint32_t(seed), std::uint32_t(k), 7)));
		const auto top = at.y() - _ascent * 0.15;
		auto gradient = QLinearGradient(QPointF(x, top), QPointF(x, top + length));
		auto solid = color;
		solid.setAlpha(int(230 * strength));
		auto clear = color;
		clear.setAlpha(int(60 * strength));
		gradient.setColorAt(0., solid);
		gradient.setColorAt(1., clear);
		p.setPen(QPen(QBrush(gradient), thickness, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(QPointF(x, top), QPointF(x, top + length));
		if (length > thickness * 2.) {
			p.setPen(Qt::NoPen);
			p.setBrush(solid);
			p.drawEllipse(QPointF(x, top + length), thickness * 0.9, thickness * 1.1);
		}
	}
	p.setPen(color);
	drawText(p, at, text);
}

void LyricsWindow::paintActiveLine(
		QPainter &p,
		int index,
		QPointF origin,
		double emphasis,
		std::int64_t position) {
	const auto &line = _lines[index];
	const auto synced = _data.synced;
	const auto keywordOn = AyuSettings::getInstance().musicLyricsKeyword();
	p.setFont(_font);
	for (auto i = 0; i != int(line.words.size()); ++i) {
		const auto &word = line.words[i];
		const auto &box = line.boxes[i];
		const auto progress = synced ? Lyrics::WordProgress(word, position + kWordLeadMs) : 1.;
		const auto lit = Smooth(progress) * emphasis;
		const auto lift = -_rowHeight * 0.04 * Smooth(progress) * emphasis;
		const auto baseline = origin + QPointF(box.x(), box.y() + _ascent + lift);
		const auto text = word.text.trimmed();
		if (word.keyword && keywordOn && _theme == Theme::Health && progress > 0.) {
			const auto age = std::max(0., double(position - word.startMs) / 1000.);
			paintBleedingWord(
				p,
				text,
				baseline,
				box.width(),
				wordColor(word, lit),
				emphasis * std::max(0.45, _redBoost),
				age,
				index * 31 + i);
			continue;
		}
		const auto fx = (i < int(line.fx.size())) ? line.fx[i] : int(kFxNone);
		if (fx && keywordOn && progress > 0.) {
			const auto age = synced ? std::max(0., (position - word.startMs) / 1000.) : 10.;
			if (paintFxWord(p, fx, text, baseline, box, emphasis, progress, age, index * 31 + i)) {
				continue;
			}
		}
		if (word.accent && keywordOn && progress > 0.) {
			const auto info = FindCategory(word.accent);
			if (info && info->accent.isValid()) {
				paintBloom(p, text, baseline, info->accent, 0.85 * emphasis * std::min(1., progress * 2.), index * 31 + i);
			}
		}
		if (progress > 0. && progress < 1.) {
			const auto pop = 1. + 0.05 * std::sin(kPi * progress) * emphasis;
			const auto center = QPointF(baseline.x() + box.width() / 2., baseline.y() - _ascent * 0.35);
			p.save();
			p.translate(center);
			p.scale(pop, pop);
			p.translate(-center);
			p.setPen(wordColor(word, 0.));
			drawText(p, baseline, text);
			p.setPen(wordColor(word, emphasis));
			const auto edge = baseline.x() + box.width() * std::clamp(progress, 0., 1.);
			const auto feather = std::max(4., box.width() * 0.18);
			const auto top = baseline.y() - _ascent - _rowHeight;
			const auto tall = _rowHeight * 3.;
			const auto solid = edge - feather / 2.;
			if (solid > baseline.x() - _rowHeight) {
				p.save();
				p.setClipRect(QRectF(baseline.x() - _rowHeight, top, solid - (baseline.x() - _rowHeight), tall), Qt::IntersectClip);
				drawText(p, baseline, text);
				p.restore();
			}
			for (auto k = 0; k != 4; ++k) {
				const auto from = solid + feather * k / 4.;
				p.save();
				p.setOpacity(p.opacity() * (1. - (k + 0.5) / 4.));
				p.setClipRect(QRectF(from, top, feather / 4., tall), Qt::IntersectClip);
				drawText(p, baseline, text);
				p.restore();
			}
			p.restore();
		} else {
			p.setPen(wordColor(word, lit));
			drawText(p, baseline, text);
		}
	}
}

void LyricsWindow::paintLyrics(QPainter &p, std::int64_t position) {
	const auto &g = _geometry;
	const auto u = g.unit * 1.8;
	const auto area = g.lyrics;
	const auto labelPoint = QPointF(area.left(), area.top() - 12. * u);
	p.setFont(makeFont(10.5 * u, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255, 190));
	const auto label = QString::fromUtf8("Текст песни");
	drawText(p, labelPoint, label);
	if (_data.synced) {
		const auto labelWidth = QFontMetricsF(makeFont(10.5 * u, QFont::DemiBold)).horizontalAdvance(label);
		const auto liveFont = makeFont(7. * u, QFont::Bold);
		auto live = _data.autoSynced ? QString::fromUtf8("АВТО") : u"LIVE"_q;
		if (_lyricsOffset) {
			live += QString::fromUtf8(" · ") + ((_lyricsOffset > 0) ? u"+"_q : QString::fromUtf8("−"))
				+ QString::number(std::abs(_lyricsOffset) / 1000., 'f', 1) + QString::fromUtf8(" с");
		}
		const auto liveRect = QRectF(
			labelPoint.x() + labelWidth + 6. * u,
			labelPoint.y() - 8. * u,
			QFontMetricsF(liveFont).horizontalAdvance(live) + 8. * u,
			11. * u);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 40));
		p.drawRoundedRect(liveRect, 3. * u, 3. * u);
		p.setFont(liveFont);
		p.setPen(QColor(255, 255, 255, 210));
		drawText(p, liveRect, Qt::AlignCenter, live);
	}

	if (!_data.hasData) {
		p.setFont(makeFont(22. * u, QFont::Bold));
		p.setPen(QColor(255, 255, 255, 120));
		drawText(p, 
			QRectF(area.left(), area.top() + 30. * u, area.width(), 120. * u),
			Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
			_loading
				? QString::fromUtf8("Ищем текст песни…")
				: QString::fromUtf8("Текст для этого трека не найден"));
		return;
	}

	const auto anchor = anchorY();
	const auto topFade = 60. * u;
	const auto bottomFade = 140. * u;
	p.save();
	p.setClipRect(QRectF(area.left() - 40. * u, area.top() - 4. * u, area.width() + 80. * u, area.height() + 4. * u));
	for (auto i = 0; i != int(_lines.size()); ++i) {
		const auto &line = _lines[i];
		const auto y = anchor + line.top - _scroll;
		if (y + line.height < area.top() - 10. || y > height()) {
			continue;
		}
		const auto distance = (_active >= 0) ? std::abs(i - _active) : (i + 1);
		const auto past = (_active >= 0) && (i < _active);
		auto opacity = past
			? 0.32
			: (distance <= 1) ? 0.55 : (distance == 2) ? 0.45 : 0.36;
		const auto fade = std::clamp((y + line.height - area.top()) / topFade, 0., 1.)
			* std::clamp((height() - y) / bottomFade, 0., 1.);
		if (i == _hoveredLine && _data.synced) {
			opacity = std::min(1., opacity + 0.2);
		}
		const auto emphasis = _emphasis[i];
		const auto scale = kInactiveScale + (1. - kInactiveScale) * emphasis;
		const auto origin = QPointF(area.left(), y);
		p.save();
		if (scale != 1.) {
			p.translate(origin);
			p.scale(scale, scale);
			p.translate(-origin);
		}
		if (emphasis < 0.99) {
			ensureCache(i);
			const auto ratio = style::DevicePixelRatio();
			const auto pad = (line.sharp.width() / double(ratio) - area.width()) / 2.;
			const auto at = QPointF(area.left() - pad, y - pad).toPoint();
			const auto blur = (past
				? std::clamp(0.5 * distance, 0., 1.)
				: std::clamp((distance - 1) * 0.55, 0., 1.)) * (1. - emphasis);
			const auto base = opacity * fade * (1. - emphasis);
			if (_lowQuality) {
				p.setOpacity(base * (1. - blur * 0.55));
				p.drawPixmap(at, line.sharp);
			} else if (blur < 0.99) {
				p.setOpacity(base * (1. - blur));
				p.drawPixmap(at, line.sharp);
			}
			if (!_lowQuality && blur > 0.01) {
				p.setOpacity(base * blur);
				p.drawPixmap(at, line.blurred);
			}
		}
		if (emphasis > 0.01) {
			p.setOpacity(fade * emphasis);
			paintActiveLine(p, i, origin, emphasis, position);
			if (!line.translationHtml.isEmpty()) {
				if (line.translation.isNull()) {
					ensureCache(i);
				}
				const auto s = _geometry.unit;
				const auto chipFont = makeFont(_translationChip * 0.62, QFont::Bold);
				const auto label = QString::fromUtf8("文A  ") + translationTarget().toUpper() + QString::fromUtf8(" · ПЕРЕВОД");
				const auto chip = QRectF(
					origin.x(),
					origin.y() + line.translationTop,
					QFontMetricsF(chipFont).horizontalAdvance(label) + 18. * s,
					_translationChip);
				p.setPen(Qt::NoPen);
				p.setBrush(QColor(255, 255, 255, 36));
				p.drawRoundedRect(chip, 6. * s, 6. * s);
				p.setFont(chipFont);
				p.setPen(QColor(255, 255, 255, 200));
				drawText(p, chip, Qt::AlignCenter, label);
				p.setOpacity(fade * Smooth(emphasis));
				p.drawPixmap(QPointF(origin.x(), origin.y() + line.translationTextTop).toPoint(), line.translation);
			}
		}
		if (distance <= 1 && AyuSettings::getInstance().musicLyricsKeyword()) {
			for (auto k = 0; k != int(line.fx.size()); ++k) {
				const auto fx = line.fx[k];
				if (!fx || fx == kFxGlitch) {
					continue;
				}
				const auto &word = line.words[k];
				if (_data.synced && position < word.startMs) {
					continue;
				}
				const auto age = _data.synced ? (position - word.startMs) / 1000. : 10.;
				p.setOpacity(fade * (past ? 0.5 : 1.));
				paintFxOverlay(p, fx, line.numbers[k], word.text.trimmed(), line.boxes[k].translated(origin), age, 1., i * 31 + k);
			}
		}
		if (distance <= 1 && AyuSettings::getInstance().musicLyricsKeyword()) {
			for (auto k = 0; k != int(line.icons.size()); ++k) {
				if (!line.icons[k]) {
					continue;
				}
				const auto &word = line.words[k];
				if (_data.synced && position < word.startMs) {
					continue;
				}
				const auto age = _data.synced ? (position - word.startMs) / 1000. : 3.;
				p.setOpacity(fade * (past ? 0.45 : 1.));
				Pictograms::Paint(p, line.icons[k], line.boxes[k].translated(origin), _rowHeight, age, i * 31 + k);
			}
		}
		if (!line.stickers.empty() && distance <= 1 && AyuSettings::getInstance().musicLyricsKeyword()) {
			for (const auto &[index, sticker] : line.stickers) {
				const auto &word = line.words[index];
				const auto started = !_data.synced || position >= word.startMs;
				if (!started) {
					continue;
				}
				const auto age = _data.synced ? (position - word.startMs) / 1000. : 1.;
				const auto pop = std::min(1.15, 1. - std::exp(-6. * age) * std::cos(9. * age));
				const auto &box = line.boxes[index];
				p.setOpacity(fade * (past ? 0.55 : 1.));
				paintSticker(p, sticker, origin + QPointF(box.right(), box.top() + _rowHeight * 0.18), pop);
			}
		}
		p.restore();
	}
	p.restore();
	p.setOpacity(1.);
}

void LyricsWindow::detectTheme() {
	struct Lexicon {
		Theme theme;
		double weight;
		std::vector<const char*> stems;
	};
	static const auto lexicons = std::vector<Lexicon>{
		{ Theme::Health, 1.3, { "болен", "больн", "боль", "болит", "таблет", "препарат", "лекарств", "врач", "больниц", "пульс", "кров", "наркот", "доз", "sick", "pain", "pill", "hurt", "hospital", "blood", "drug", "medic", "overdos" } },
		{ Theme::Love, 1., { "любл", "любов", "люби", "целу", "обним", "скуча", "нежн", "родн", "love", "baby", "babe", "kiss", "darling", "miss" } },
		{ Theme::Night, 1., { "ночь", "ночью", "ночн", "луна", "луны", "звезд", "спать", "сплю", "усну", "бессонн", "темнот", "night", "moon", "sleep", "insomnia", "midnight" } },
		{ Theme::Rain, 1., { "дожд", "ливн", "ливен", "слез", "холод", "зима", "зимн", "снег", "туман", "осен", "rain", "tears", "cry", "cold", "winter", "storm", "snow" } },
	};
	auto scores = std::map<Theme, double>();
	for (const auto &line : _data.lines) {
		for (const auto &token : line.text.split(QChar(u' '), Qt::SkipEmptyParts)) {
			const auto word = Lyrics::NormalizedWord(token);
			if (word.size() < 3) {
				continue;
			}
			for (const auto &lexicon : lexicons) {
				for (const auto stem : lexicon.stems) {
					if (word.startsWith(QString::fromUtf8(stem))) {
						scores[lexicon.theme] += lexicon.weight;
						break;
					}
				}
			}
		}
	}
	_theme = Theme::Default;
	auto best = 2.9;
	for (const auto &[theme, score] : scores) {
		if (score > best) {
			best = score;
			_theme = theme;
		}
	}
	switch (_theme) {
	case Theme::Health:
		_leftWidget = Widget::Pulse;
		_rightWidget = Widget::Pills;
		break;
	case Theme::Love:
		_leftWidget = Widget::Pulse;
		_rightWidget = Widget::Messages;
		break;
	case Theme::Night:
		_leftWidget = Widget::Sleep;
		_rightWidget = Widget::Pulse;
		break;
	case Theme::Rain:
		_leftWidget = Widget::Weather;
		_rightWidget = Widget::Pulse;
		break;
	default:
		_leftWidget = Widget::Pulse;
		_rightWidget = Widget::Sleep;
		break;
	}
	_wideWidget = Widget::Lines;
}

QString LyricsWindow::chorusLine(std::int64_t start) const {
	if (!_data.synced) {
		return QString();
	}
	const auto index = Lyrics::ActiveLineIndex(_data, start + 50);
	return (index >= 0) ? _data.lines[index].text : QString();
}

void LyricsWindow::paintWidget(
		QPainter &p,
		Widget kind,
		const QRectF &rect,
		std::int64_t position) {
	if (rect.isEmpty()) {
		return;
	}
	switch (kind) {
	case Widget::Pulse: paintPulseWidget(p, rect, position); break;
	case Widget::Lines: paintLinesWidget(p, rect, position); break;
	case Widget::Pills: paintPillsWidget(p, rect, position); break;
	case Widget::Messages: paintMessagesWidget(p, rect, position); break;
	case Widget::Weather: paintWeatherWidget(p, rect, position); break;
	case Widget::Sleep: paintSleepWidget(p, rect, position); break;
	case Widget::Radar: paintRadarWidget(p, rect, position); break;
	case Widget::Clock: paintClockWidget(p, rect, position); break;
	case Widget::Friends: paintFriendsWidget(p, rect, position); break;
	case Widget::Battery: paintBatteryWidget(p, rect, position); break;
	case Widget::Paleness: paintPalenessWidget(p, rect, position); break;
	case Widget::Emblem: paintEmblemWidget(p, rect, position); break;
	case Widget::FaceId: paintFaceWidget(p, rect, position); break;
	case Widget::Recorder: paintRecorderWidget(p, rect, position); break;
	case Widget::Ring: paintRingWidget(p, rect, position); break;
	case Widget::Spectrum: paintSpectrumWidget(p, rect, position); break;
	case Widget::Rhythm: paintRhythmWidget(p, rect, position); break;
	case Widget::Rings: paintRingsWidget(p, rect, position); break;
	case Widget::ScreenTime: paintScreenTimeWidget(p, rect, position); break;
	case Widget::Target: paintTargetWidget(p, rect, position); break;
	case Widget::Block: paintBlockWidget(p, rect, position); break;
	case Widget::Counter: paintCounterWidget(p, rect, position); break;
	case Widget::Route: paintRouteWidget(p, rect, position); break;
	case Widget::Infinity: paintInfinityWidget(p, rect, position); break;
	case Widget::StoppedClock: paintStoppedClockWidget(p, rect, position); break;
	case Widget::Alarm: paintAlarmWidget(p, rect, position); break;
	case Widget::Reminders: paintRemindersWidget(p, rect, position); break;
	case Widget::Geo: paintGeoWidget(p, rect, position); break;
	case Widget::Compass: paintCompassWidget(p, rect, position); break;
	case Widget::Search: paintSearchWidget(p, rect, position); break;
	case Widget::MissedList: paintMissedListWidget(p, rect, position); break;
	case Widget::Phone: paintPhoneWidget(p, rect, position); break;
	case Widget::Steps: paintStepsWidget(p, rect, position); break;
	case Widget::Dots: paintDotsWidget(p, rect, position); break;
	case Widget::Stopwatch: paintStopwatchWidget(p, rect, position); break;
	case Widget::None: break;
	}
}

void LyricsWindow::paintWidgetHeader(
		QPainter &p,
		const QRectF &rect,
		const QString &glyph,
		QColor glyphColor,
		const QString &title,
		const QString &corner) const {
	const auto u = _geometry.unit * 2.;
	const auto pad = 12. * u;
	const auto baseline = rect.top() + pad + 10. * u;
	p.setFont(makeFont(11. * u, QFont::Bold));
	p.setPen(glyphColor);
	drawText(p, QPointF(rect.left() + pad, baseline), glyph);
	p.setFont(makeFont(10.5 * u, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255, 235));
	drawText(p, QPointF(rect.left() + pad + 15. * u, baseline), title);
	if (!corner.isEmpty()) {
		const auto font = makeFont(8.5 * u);
		p.setFont(font);
		p.setPen(QColor(255, 255, 255, 120));
		drawText(p, 
			QPointF(rect.right() - pad - QFontMetricsF(font).horizontalAdvance(corner), baseline),
			corner);
	}
}

void LyricsWindow::paintPillsWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto u = _geometry.unit * 2.;
	paintGlass(p, rect, position);
	const auto pad = 12. * u;
	const auto duration = std::max<std::int64_t>(_duration, 1);
	const auto chorus = (_analysis && !_analysis->chorus.empty()) ? _analysis->chorus.front().startMs : -1;
	const auto base = (chorus > 8000) ? chorus : duration / 2;
	struct Dose {
		const char *name;
		const char *time;
		std::int64_t at;
	};
	const auto doses = std::array<Dose, 3>{ {
		{ "Утро", "09:00", base / 4 },
		{ "День", "14:00", base / 2 },
		{ "Вечер", "21:00", base * 85 / 100 },
	} };
	const auto taken = pillsTaken(position);
	paintWidgetHeader(
		p,
		rect,
		QString::fromUtf8("✎"),
		QColor(255, 255, 255),
		QString::fromUtf8("Лекарства"),
		QString::number(taken) + QString::fromUtf8(" из 3"));
	const auto footer = 22. * u;
	const auto listTop = rect.top() + pad + 20. * u;
	const auto rowHeight = std::min(30. * u, (rect.bottom() - pad - footer - 4. * u - listTop) / 3.);
	for (auto i = 0; i != 3; ++i) {
		const auto y = listTop + rowHeight * i;
		const auto icon = QRectF(rect.left() + pad, y + rowHeight / 2. - 8. * u, 16. * u, 16. * u);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 34));
		p.drawEllipse(icon);
		p.setFont(makeFont(8. * u, QFont::Bold));
		p.setPen(QColor(255, 255, 255, 220));
		drawText(p, icon, Qt::AlignCenter, QString::fromUtf8("✎"));
		const auto textLeft = icon.right() + 7. * u;
		p.setFont(makeFont(9.5 * u, QFont::DemiBold));
		p.setPen(QColor(255, 255, 255, 230));
		drawText(p, QPointF(textLeft, y + rowHeight * 0.48), QString::fromUtf8(doses[i].name));
		p.setFont(makeFont(7.5 * u));
		p.setPen(QColor(255, 255, 255, 105));
		drawText(p, QPointF(textLeft, y + rowHeight * 0.86), QString::fromUtf8(doses[i].time));
		const auto mark = QRectF(rect.right() - pad - 14. * u, y + rowHeight / 2. - 7. * u, 14. * u, 14. * u);
		const auto age = std::clamp((position - doses[i].at) / 260., 0., 1.);
		if (position >= doses[i].at) {
			const auto pop = 0.7 + 0.3 * Smooth(age) + 0.12 * std::sin(age * kPi);
			const auto scaled = QRectF(mark.center() - QPointF(mark.width() * pop / 2., mark.height() * pop / 2.), mark.size() * pop);
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 235));
			p.drawEllipse(scaled);
			auto check = QPainterPath();
			check.moveTo(scaled.left() + scaled.width() * 0.28, scaled.top() + scaled.height() * 0.52);
			check.lineTo(scaled.left() + scaled.width() * 0.45, scaled.top() + scaled.height() * 0.68);
			check.lineTo(scaled.left() + scaled.width() * 0.74, scaled.top() + scaled.height() * 0.36);
			p.setPen(QPen(QColor(25, 25, 30), 1.6 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			p.setBrush(Qt::NoBrush);
			p.drawPath(check);
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 34));
			p.drawEllipse(mark);
		}
	}
	const auto pill = QRectF(rect.left() + pad, rect.bottom() - pad - footer, rect.width() - 2 * pad, footer);
	const auto failed = (taken == 3) && (inChorus(position) || alarm() || _redBoost > 0.35);
	p.setPen(Qt::NoPen);
	p.setBrush(failed ? QColor(225, 30, 48) : QColor(255, 255, 255, 34));
	p.drawRoundedRect(pill, pill.height() / 2., pill.height() / 2.);
	p.setFont(makeFont(8.5 * u, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255, failed ? 255 : 190));
	drawText(p, 
		pill,
		Qt::AlignCenter,
		failed
			? QString::fromUtf8("Не помогают")
			: (taken == 3)
			? QString::fromUtf8("Эффект: проверяется…")
			: QString::fromUtf8("Эффект: ожидание…"));
}

void LyricsWindow::paintMessagesWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("●"), QColor(120, 170, 255), QString::fromUtf8("СООБЩЕНИЯ"), QString());
	p.setFont(makeFont(15. * s, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255, 150));
	drawText(p, QPointF(rect.left() + 24. * s, rect.top() + 82. * s), QString::fromUtf8("Кому: Ты"));
	auto text = QString();
	auto age = 1.;
	if (_data.synced && !_data.lines.empty()) {
		const auto active = std::max(0, Lyrics::ActiveLineIndex(_data, position));
		text = _data.lines[active].text;
		age = std::max(0., (position - _data.lines[active].ms) / 1000.);
	} else if (!_data.lines.empty()) {
		text = _data.lines.front().text;
	}
	if (!text.isEmpty() && !text.endsWith(QString::fromUtf8("…"))) {
		text += QString::fromUtf8("…");
	}
	const auto bubble = QRectF(rect.left() + 20. * s, rect.top() + 98. * s, rect.width() - 40. * s, rect.height() * 0.36);
	const auto pop = std::min(1.04, 1. - std::exp(-9. * age) * std::cos(10. * age));
	p.save();
	p.translate(bubble.bottomLeft());
	p.scale(0.9 + 0.1 * pop, 0.9 + 0.1 * pop);
	p.translate(-bubble.bottomLeft());
	p.setPen(QPen(QColor(255, 255, 255, 50), 1.2 * s));
	p.setBrush(QColor(110, 150, 255, 70));
	p.drawRoundedRect(bubble, 20. * s, 20. * s);
	const auto font = makeFont(19. * s, QFont::DemiBold);
	p.setFont(font);
	p.setPen(QColor(255, 255, 255));
	const auto inner = bubble.marginsRemoved(QMarginsF(18. * s, 12. * s, 18. * s, 12. * s));
	auto shown = text;
	const auto metrics = QFontMetricsF(font);
	while (shown.size() > 4
		&& metrics.boundingRect(inner, Qt::TextWordWrap, shown).height() > inner.height()) {
		shown = shown.left(shown.size() - 4) + QString::fromUtf8("…");
	}
	drawText(p, inner, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, shown);
	p.restore();
	p.setPen(Qt::NoPen);
	for (auto k = 0; k != 3; ++k) {
		const auto bounce = 0.5 + 0.5 * std::sin(crl::now() / 170. - k * 0.9);
		p.setBrush(QColor(255, 255, 255, int(90 + 150 * bounce)));
		p.drawEllipse(QPointF(bubble.left() + 8. * s + k * 14. * s, bubble.bottom() + 26. * s - 3. * s * bounce), 4. * s, 4. * s);
	}
	const auto pillFont = makeFont(14. * s, QFont::Bold);
	const auto label = QString::fromUtf8("!  Не отправлено");
	const auto pill = QRectF(rect.left() + 22. * s, rect.bottom() - 52. * s, QFontMetricsF(pillFont).horizontalAdvance(label) + 28. * s, 32. * s);
	p.setBrush(QColor(255, 70, 90, 50));
	p.drawRoundedRect(pill, 10. * s, 10. * s);
	p.setFont(pillFont);
	p.setPen(QColor(255, 110, 125));
	drawText(p, pill, Qt::AlignCenter, label);
}

void LyricsWindow::paintWeatherWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	const auto real = _weather.ok;
	const auto sky = real ? Weather::SkyOf(_weather.code) : Weather::Sky::Rain;
	const auto snowy = real ? (sky == Weather::Sky::Snow) : (_bluePalette || _sceneCategory == kSnow);
	const auto energy = (_analysis && _analysis->ok) ? _analysis->energyAt(position) : 0.4f;
	const auto storm = real
		? (sky == Weather::Sky::Storm || _weather.code == 65 || _weather.code == 82 || _weather.code == 75)
		: (inChorus(position) || pulse(position) > 0.6f);
	const auto falling = !real || snowy || sky == Weather::Sky::Rain || sky == Weather::Sky::Storm;
	auto path = QPainterPath();
	path.addRoundedRect(rect, 30. * s, 30. * s);
	p.save();
	p.setClipPath(path);
	const auto drops = !falling ? 0 : storm ? 28 : 16;
	for (auto i = 0; i != drops; ++i) {
		const auto speed = (snowy ? 0.18 : 0.6) + (snowy ? 0.15 : 0.6) * Hash01(i, 42, 2);
		const auto y = std::fmod(Hash01(i, 43, 2) + crl::now() / 1000. * speed, 1.);
		const auto x = Hash01(i, 44, 2) + (snowy ? 0.02 * std::sin(crl::now() / 700. + i) : 0.);
		const auto at = QPointF(rect.left() + rect.width() * x, rect.top() + rect.height() * y);
		if (snowy) {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 150));
			p.drawEllipse(at, 1.8 * s, 1.8 * s);
		} else {
			p.setPen(QPen(QColor(190, 210, 255, storm ? 90 : 55), 1.4 * s, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(at, at + QPointF(-2. * s, 10. * s));
		}
	}
	p.restore();
	const auto glyph = snowy
		? QString::fromUtf8("✱")
		: !real || sky == Weather::Sky::Rain || sky == Weather::Sky::Storm
		? QString::fromUtf8("☂")
		: (sky == Weather::Sky::Clear)
		? (_weather.day ? QString::fromUtf8("☀") : QString::fromUtf8("☾"))
		: QString::fromUtf8("☁");
	paintWidgetHeader(
		p,
		rect,
		glyph,
		QColor(150, 190, 255),
		QString::fromUtf8("ПОГОДА"),
		real ? QFontMetrics(makeFont(17. * s)).elidedText(_weather.city, Qt::ElideRight, int(rect.width() * 0.42)) : QString());
	const auto progress = sceneProgress(position, 4000);
	const auto temperature = snowy
		? -int(std::round(9. + 3. * progress + (storm ? 2. : 0.)))
		: int(std::round(9. - 6. * energy - (storm ? 3. : 0.)));
	const auto shownTemperature = real ? int(std::lround(_weather.temperature)) : temperature;
	p.setFont(makeFont(62. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 22. * s, rect.top() + 132. * s), Weather::FormatDegrees(shownTemperature));
	p.setFont(makeFont(16. * s, QFont::DemiBold));
	drawText(p, 
		QPointF(rect.left() + 24. * s, rect.top() + 164. * s),
		real
			? Weather::Describe(_weather.code)
			: snowy ? QString::fromUtf8("Снегопад") : (storm ? QString::fromUtf8("Ливень") : QString::fromUtf8("Дождь")));
	const auto feels = real ? int(std::lround(_weather.feels)) : (temperature - (snowy ? 6 : 4));
	p.setFont(makeFont(12.5 * s));
	p.setPen(QColor(255, 255, 255, 150));
	drawText(p, 
		QPointF(rect.left() + 24. * s, rect.top() + 186. * s),
		QString::fromUtf8("Ощущается как ") + ((feels > 0) ? u"+"_q : (feels < 0 ? QString::fromUtf8("−") : QString())) + QString::number(std::abs(feels)) + QString::fromUtf8("°"));
	const auto hour = QTime::currentTime().hour();
	const auto columns = 5;
	const auto cell = (rect.width() - 40. * s) / columns;
	for (auto k = 0; k != columns; ++k) {
		const auto x = rect.left() + 20. * s + cell * k;
		const auto realHour = (real && k < int(_weather.hours.size())) ? &_weather.hours[k] : nullptr;
		const auto label = (k == 0)
			? QString::fromUtf8("Сейчас")
			: QString::number(realHour ? realHour->hour : ((hour + k) % 24)).rightJustified(2, QChar(u'0'));
		const auto value = realHour ? int(std::lround(realHour->temperature)) : (temperature - k);
		const auto hourSky = realHour ? Weather::SkyOf(realHour->code) : (snowy ? Weather::Sky::Snow : Weather::Sky::Rain);
		p.setFont(makeFont(10.5 * s));
		p.setPen(QColor(255, 255, 255, 130));
		drawText(p, QRectF(x, rect.bottom() - 82. * s, cell, 16. * s), Qt::AlignCenter, label);
		p.setFont(makeFont(13. * s, QFont::Bold));
		p.setPen(QColor(255, 255, 255, 220));
		drawText(p, 
			QRectF(x, rect.bottom() - 62. * s, cell, 18. * s),
			Qt::AlignCenter,
			(hourSky == Weather::Sky::Snow)
				? QString::fromUtf8("✱")
				: (hourSky == Weather::Sky::Rain || hourSky == Weather::Sky::Storm)
				? QString::fromUtf8("☂")
				: (hourSky == Weather::Sky::Clear)
				? QString::fromUtf8("☀")
				: QString::fromUtf8("☁"));
		p.setFont(makeFont(12. * s, QFont::DemiBold));
		drawText(p, 
			QRectF(x, rect.bottom() - 40. * s, cell, 18. * s),
			Qt::AlignCenter,
			((value > 0) ? u"+"_q : (value < 0 ? QString::fromUtf8("−") : QString())) + QString::number(std::abs(value)) + QString::fromUtf8("°"));
	}
}

void LyricsWindow::paintSleepWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto u = _geometry.unit * 2.;
	paintGlass(p, rect, position);
	const auto pad = 12. * u;
	paintWidgetHeader(p, rect, QString::fromUtf8("☾"), QColor(175, 160, 255), QString::fromUtf8("Сон"), QString::fromUtf8("Сейчас"));
	const auto now = QTime::currentTime();
	const auto big = makeFont(std::min(36. * u, rect.height() * 0.25), QFont::Bold);
	const auto numberY = rect.top() + pad + 20. * u + QFontMetricsF(big).ascent();
	p.setFont(big);
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + pad, numberY), now.toString(u"HH:mm"_q));
	p.setFont(makeFont(9.5 * u, QFont::DemiBold));
	p.setPen(QColor(175, 160, 255));
	const auto late = (now.hour() < 6) || (now.hour() >= 23);
	drawText(p, QPointF(rect.left() + pad, numberY + 16. * u), late ? QString::fromUtf8("Не спится") : QString::fromUtf8("Режим сна скоро"));

	const auto footer = 14. * u;
	const auto chart = QRectF(rect.left() + pad, numberY + 24. * u, rect.width() - 2 * pad, rect.bottom() - pad - footer - numberY - 28. * u);
	if (chart.height() > 8.) {
		const auto bars = 14;
		const auto progress = (_duration > 0) ? std::clamp(double(position) / _duration, 0., 1.) : 0.;
		const auto current = std::min(bars - 1, int(progress * bars));
		const auto seed = std::uint32_t(qHash(_title));
		const auto barWidth = chart.width() / bars * 0.62;
		p.setPen(Qt::NoPen);
		for (auto i = 0; i != bars; ++i) {
			auto level = 0.25 + 0.75 * Hash01(seed, std::uint32_t(i), 31);
			if (i == current) {
				level = std::min(1., level * (0.8 + 0.6 * pulse(position)));
			}
			const auto h = chart.height() * level;
			const auto x = chart.left() + chart.width() * i / bars;
			p.setBrush((i < current)
				? QColor(175, 160, 255, 210)
				: (i == current)
				? QColor(255, 255, 255)
				: QColor(255, 255, 255, 40));
			p.drawRoundedRect(QRectF(x, chart.bottom() - h, barWidth, h), barWidth / 2., barWidth / 2.);
		}
	}
	auto wake = now.secsTo(QTime(7, 0));
	if (wake < 0) {
		wake += 24 * 3600;
	}
	p.setFont(makeFont(8. * u));
	p.setPen(QColor(255, 255, 255, 105));
	drawText(p, 
		QPointF(rect.left() + pad, rect.bottom() - pad + 2. * u),
		QString::fromUtf8("Подъём через ") + QString::number(wake / 3600) + QString::fromUtf8(" ч ") + QString::number((wake % 3600) / 60) + QString::fromUtf8(" мин"));
}

void LyricsWindow::paintArt(QPainter &p, const QRectF &rect, double zoom, QPointF drift) const {
	if (!_art.isNull()) {
		DrawToFill(p, rect.translated(drift), _art, zoom);
	} else {
		paintGeneratedArt(p, rect);
	}
}

void LyricsWindow::paintGeneratedArt(QPainter &p, const QRectF &rect) const {
	const auto accent = _accent.isValid() ? _accent : QColor(150, 20, 36);
	auto background = QLinearGradient(rect.topLeft(), rect.bottomRight());
	background.setColorAt(0., Mix(accent, QColor(0, 0, 0), 0.35));
	background.setColorAt(1., QColor(12, 12, 16));
	p.fillRect(rect, background);
	const auto radius = std::min(rect.height() * 0.42, rect.width() * 0.32);
	if (radius < 6.) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 60));
		p.drawEllipse(rect.center(), rect.width() * 0.3, rect.width() * 0.3);
		return;
	}
	const auto center = QPointF(rect.right() - radius * 1.15, rect.center().y());
	const auto angle = _playing ? std::fmod(crl::now() / 1000. * 200., 360.) : 0.;
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(8, 8, 10));
	p.drawEllipse(center, radius, radius);
	p.setBrush(Qt::NoBrush);
	for (auto k = 0; k != 7; ++k) {
		const auto r = radius * (0.48 + 0.07 * k);
		p.setPen(QPen(QColor(255, 255, 255, 14 + (k % 2) * 8), 1.));
		p.drawEllipse(center, r, r);
	}
	p.save();
	p.translate(center);
	p.rotate(angle);
	auto shine = QConicalGradient(QPointF(), 0.);
	shine.setColorAt(0., QColor(255, 255, 255, 0));
	shine.setColorAt(0.08, QColor(255, 255, 255, 40));
	shine.setColorAt(0.16, QColor(255, 255, 255, 0));
	shine.setColorAt(0.5, QColor(255, 255, 255, 0));
	shine.setColorAt(0.58, QColor(255, 255, 255, 30));
	shine.setColorAt(0.66, QColor(255, 255, 255, 0));
	p.setPen(Qt::NoPen);
	p.setBrush(shine);
	p.drawEllipse(QPointF(), radius, radius);
	p.setBrush(accent);
	p.drawEllipse(QPointF(), radius * 0.33, radius * 0.33);
	p.setFont(makeFont(radius * 0.26, QFont::Black));
	p.setPen(QColor(255, 255, 255, 230));
	const auto initial = _title.isEmpty() ? QString::fromUtf8("♫") : _title.left(1).toUpper();
	drawText(p, QRectF(-radius * 0.33, -radius * 0.33, radius * 0.66, radius * 0.66), Qt::AlignCenter, initial);
	p.setBrush(QColor(10, 10, 12));
	p.setPen(Qt::NoPen);
	p.drawEllipse(QPointF(), radius * 0.04, radius * 0.04);
	p.restore();
	if (rect.width() > radius * 3.) {
		const auto title = makeFont(std::min(rect.height() * 0.16, 28. * _geometry.unit), QFont::Black);
		p.setFont(title);
		p.setPen(QColor(255, 255, 255, 230));
		const auto textRect = QRectF(rect.left() + 16. * _geometry.unit, rect.top() + rect.height() * 0.3, center.x() - radius - rect.left() - 24. * _geometry.unit, rect.height() * 0.45);
		drawText(p, textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, _title);
	}
}


void LyricsWindow::paintStatusBar(QPainter &p) {
	const auto s = _geometry.unit;
	const auto ox = std::max(0., (width() - 1920. * s) / 2.);
	const auto oy = std::max(0., (height() - 1080. * s) / 2.);
	const auto baseline = oy + 46. * s;
	const auto now = QDateTime::currentDateTime();
	const auto timeFont = makeFont(27. * s, QFont::DemiBold);
	p.setFont(timeFont);
	p.setPen(QColor(255, 255, 255, 215));
	const auto time = now.toString(u"H:mm"_q);
	drawText(p, QPointF(ox + 70. * s, baseline), time);
	const auto dateFont = makeFont(22. * s);
	p.setFont(dateFont);
	p.setPen(QColor(255, 255, 255, 120));
	static const auto locale = QLocale(QLocale::Russian);
	auto date = locale.toString(now.date(), u"ddd d MMM"_q);
	date.remove(QChar(u'.'));
	if (!date.isEmpty()) {
		date[0] = date[0].toUpper();
	}
	const auto dateX = ox + 70. * s + QFontMetricsF(timeFont).horizontalAdvance(time) + 18. * s;
	drawText(p, QPointF(dateX, baseline), date);
	const auto moonX = dateX + QFontMetricsF(dateFont).horizontalAdvance(date) + 16. * s;
	drawText(p, QPointF(moonX, baseline), QString::fromUtf8("☾"));

	const auto right = _geometry.close.left() - 18. * s;
	const auto battery = QRectF(right - 44. * s, baseline - 19. * s, 40. * s, 19. * s);
	p.setPen(QPen(QColor(255, 255, 255, 150), 1.6 * s));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(battery, 5. * s, 5. * s);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 200));
	p.drawRoundedRect(battery.marginsRemoved(QMarginsF(3. * s, 3. * s, 13. * s, 3. * s)), 3. * s, 3. * s);
	p.drawRoundedRect(QRectF(battery.right() + 2. * s, battery.center().y() - 3.5 * s, 2.5 * s, 7. * s), 1. * s, 1. * s);
	const auto wifi = QPointF(battery.left() - 30. * s, baseline - 3. * s);
	p.setBrush(Qt::NoBrush);
	for (auto k = 0; k != 3; ++k) {
		const auto r = (6. + 6. * k) * s;
		p.setPen(QPen(QColor(255, 255, 255, 190), 2.6 * s, Qt::SolidLine, Qt::RoundCap));
		p.drawArc(QRectF(wifi.x() - r, wifi.y() - r, 2 * r, 2 * r), 50 * 16, 80 * 16);
	}
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 190));
	p.drawEllipse(wifi, 2.2 * s, 2.2 * s);
	const auto barsRight = wifi.x() - 26. * s;
	for (auto k = 0; k != 4; ++k) {
		const auto barHeight = (6. + 4.5 * k) * s;
		p.setBrush(QColor(255, 255, 255, (k < 3) ? 200 : 70));
		p.drawRoundedRect(
			QRectF(barsRight - (4 - k) * 7. * s, baseline - barHeight, 4.5 * s, barHeight),
			1.2 * s,
			1.2 * s);
	}
	const auto tagFont = makeFont(22. * s);
	p.setFont(tagFont);
	p.setPen(QColor(255, 255, 255, 120));
	const auto tag = u"@whitegram"_q;
	drawText(p, 
		QPointF(barsRight - 4 * 7. * s - 18. * s - QFontMetricsF(tagFont).horizontalAdvance(tag), baseline),
		tag);
	paintIconButton(p, _geometry.close, st::musicIslandIconClose, Control::Close, QColor(255, 255, 255, 200));
}

void LyricsWindow::paintGlyph(
		QPainter &p,
		Control control,
		const QRectF &rect,
		QColor color) const {
	const auto c = rect.center();
	const auto r = rect.width() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto triangle = [&](QPointF tip, double size, bool right) {
		auto path = QPainterPath();
		const auto dir = right ? 1. : -1.;
		path.moveTo(tip);
		path.lineTo(tip - QPointF(dir * size, size * 0.62));
		path.lineTo(tip - QPointF(dir * size, -size * 0.62));
		path.closeSubpath();
		p.drawPath(path);
	};
	switch (control) {
	case Control::PlayPause:
		if (_playing) {
			const auto bar = QSizeF(r * 0.26, r * 0.9);
			p.drawRoundedRect(QRectF(QPointF(c.x() - r * 0.36, c.y() - bar.height() / 2.), bar), r * 0.07, r * 0.07);
			p.drawRoundedRect(QRectF(QPointF(c.x() + r * 0.1, c.y() - bar.height() / 2.), bar), r * 0.07, r * 0.07);
		} else {
			triangle(QPointF(c.x() + r * 0.5, c.y()), r * 0.85, true);
		}
		break;
	case Control::Next:
		triangle(QPointF(c.x() + r * 0.02, c.y()), r * 0.5, true);
		triangle(QPointF(c.x() + r * 0.52, c.y()), r * 0.5, true);
		break;
	case Control::Previous:
		triangle(QPointF(c.x() - r * 0.02, c.y()), r * 0.5, false);
		triangle(QPointF(c.x() - r * 0.52, c.y()), r * 0.5, false);
		break;
	default:
		break;
	}
}

void LyricsWindow::loadCustomCover(const QString &path) {
	_customCover = QPixmap();
	if (!path.isEmpty()) {
		auto image = QImage(path);
		if (!image.isNull()) {
			if (image.width() > 2000 || image.height() > 1200) {
				image = image.scaled(2000, 1200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
			}
			_customCover = Ui::PixmapFromImage(std::move(image));
		}
	}
	_cardArt = QPixmap();
	update();
}

void LyricsWindow::paintEqualizer(QPainter &p, const QRectF &rect, std::int64_t position) const {
	if (_eqLevels.empty() || rect.width() <= 0.) {
		return;
	}
	const auto s = _geometry.unit;
	const auto count = int(_eqLevels.size());
	const auto step = rect.width() / count;
	const auto bar = step * 0.5;
	const auto glow = pulse(position);
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != count; ++i) {
		const auto level = std::max(0.05f, _eqLevels[i]);
		const auto height = std::max(bar, rect.height() * level);
		const auto x = rect.left() + step * i + (step - bar) / 2.;
		const auto bounds = QRectF(x, rect.bottom() - height, bar, height);
		auto gradient = QLinearGradient(bounds.bottomLeft(), bounds.topLeft());
		gradient.setColorAt(0., QColor(255, 255, 255, 150));
		gradient.setColorAt(1., Mix(QColor(255, 255, 255, 235), kRed, std::max(0., level - 0.7) / 0.3 * std::min(1., glow + 0.3)));
		p.setBrush(gradient);
		p.drawRoundedRect(bounds, bar / 2., bar / 2.);
		p.setBrush(QColor(255, 255, 255, 40));
		p.drawRoundedRect(QRectF(x, rect.bottom() + 3. * s, bar, std::max(1.5 * s, height * 0.18)), bar / 2., bar / 2.);
	}
}

void LyricsWindow::paintModeIcon(
		QPainter &p,
		Control control,
		const QRectF &rect,
		QColor color,
		bool one) const {
	const auto s = _geometry.unit;
	const auto c = rect.center();
	auto pen = QPen(color, 2.3 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const auto head = [&](QPointF tip, QPointF dir) {
		const auto len = 4.2 * s;
		const auto normal = QPointF(-dir.y(), dir.x());
		p.drawLine(tip, tip - dir * len + normal * len * 0.8);
		p.drawLine(tip, tip - dir * len - normal * len * 0.8);
	};
	if (control == Control::Repeat) {
		const auto w = 10. * s, h = 6. * s, r = 4. * s;
		auto top = QPainterPath();
		top.moveTo(c.x() - w + r * 0.2, c.y() + 1. * s);
		top.lineTo(c.x() - w, c.y() - h + r);
		top.quadTo(c.x() - w, c.y() - h, c.x() - w + r, c.y() - h);
		top.lineTo(c.x() + w, c.y() - h);
		p.drawPath(top);
		head(QPointF(c.x() + w, c.y() - h), QPointF(1., 0.));
		auto bottom = QPainterPath();
		bottom.moveTo(c.x() + w - r * 0.2, c.y() - 1. * s);
		bottom.lineTo(c.x() + w, c.y() + h - r);
		bottom.quadTo(c.x() + w, c.y() + h, c.x() + w - r, c.y() + h);
		bottom.lineTo(c.x() - w, c.y() + h);
		p.drawPath(bottom);
		head(QPointF(c.x() - w, c.y() + h), QPointF(-1., 0.));
		if (one) {
			p.setFont(makeFont(9. * s, QFont::Bold));
			drawText(p, QRectF(c.x() - 5. * s, c.y() - 5. * s, 10. * s, 10. * s), Qt::AlignCenter, u"1"_q);
		}
	} else if (one) {
		const auto w = 10. * s, h = 6. * s;
		auto first = QPainterPath();
		first.moveTo(c.x() - w, c.y() - h);
		first.cubicTo(c.x() - 2. * s, c.y() - h, c.x() + 2. * s, c.y() + h, c.x() + w, c.y() + h);
		p.drawPath(first);
		head(QPointF(c.x() + w, c.y() + h), QPointF(1., 0.));
		auto second = QPainterPath();
		second.moveTo(c.x() - w, c.y() + h);
		second.cubicTo(c.x() - 2. * s, c.y() + h, c.x() + 2. * s, c.y() - h, c.x() + w, c.y() - h);
		p.drawPath(second);
		head(QPointF(c.x() + w, c.y() - h), QPointF(1., 0.));
	} else {
		const auto dx = 4.5 * s, h = 8. * s;
		p.drawLine(QPointF(c.x() - dx, c.y() + h), QPointF(c.x() - dx, c.y() - h));
		head(QPointF(c.x() - dx, c.y() - h), QPointF(0., -1.));
		p.drawLine(QPointF(c.x() + dx, c.y() - h), QPointF(c.x() + dx, c.y() + h));
		head(QPointF(c.x() + dx, c.y() + h), QPointF(0., 1.));
	}
}

int LyricsWindow::pillsTaken(std::int64_t position) const {
	const auto duration = std::max<std::int64_t>(_duration, 1);
	const auto chorus = (_analysis && !_analysis->chorus.empty()) ? _analysis->chorus.front().startMs : -1;
	const auto base = (chorus > 8000) ? chorus : duration / 2;
	auto taken = 0;
	for (const auto at : { base / 4, base / 2, base * 85 / 100 }) {
		if (position >= at) {
			++taken;
		}
	}
	return taken;
}

void LyricsWindow::buildScenes() {
	_lineCategory.assign(_data.lines.size(), kNoCategory);
	auto counts = std::array<int, kCategoryCount>{};
	for (auto i = 0; i != int(_data.lines.size()); ++i) {
		auto &line = _data.lines[i];
		auto best = int(Categories().size());
		for (auto &word : line.words) {
			const auto category = WordCategory(Lyrics::NormalizedWord(word.text));
			const auto info = FindCategory(category);
			word.accent = (info && (info->accent.isValid() || info->sticker)) ? category : 0;
			if (!info) {
				continue;
			}
			for (auto k = 0; k != int(Categories().size()); ++k) {
				if (Categories()[k].id == category) {
					best = std::min(best, k);
				}
			}
		}
		if (best < int(Categories().size())) {
			_lineCategory[i] = Categories()[best].id;
			++counts[_lineCategory[i]];
		}
	}
	if (_theme == Theme::Health) {
		for (auto &category : _lineCategory) {
			if (category == kSwear || category == kCute) {
				category = kNoCategory;
			}
		}
	}
	_categoryCounts.assign(counts.begin(), counts.end());
	_hasPills = false;
	for (const auto &line : _data.lines) {
		for (const auto &word : line.words) {
			const auto normalized = Lyrics::NormalizedWord(word.text);
			for (const auto stem : { "таблет", "препарат", "лекарств", "пилюл", "колес", "pill", "meds", "xanax", "percocet" }) {
				if (normalized.startsWith(QString::fromUtf8(stem))) {
					_hasPills = true;
				}
			}
		}
	}
	_rotationStep = 0;
	_rotationLine = -2;
	_rotationAt = 0;
	const auto cold = counts[kSnow] + counts[kNight] + counts[kQueen] + counts[kAlone] + counts[kRain];
	const auto hot = counts[kAnger] + counts[kHealth] + counts[kSwear];
	const auto soft = counts[kForever] + counts[kSleep] + counts[kNear] + counts[kWish] + counts[kCute];
	_purplePalette = (soft >= 2 && soft > cold && soft > hot) || _theme == Theme::Love;
	_bluePalette = !_purplePalette
		&& ((cold >= 2 && cold > hot) || _theme == Theme::Night || _theme == Theme::Rain);
	_sceneToasts = 0;
	_sceneCategory = kNoCategory;
	_sceneLine = -1;
	_prevLeftWidget = _prevRightWidget = Widget::None;
}

void LyricsWindow::updateScene(crl::time now, std::int64_t position) {
	const auto line = _data.synced ? Lyrics::ActiveLineIndex(_data, position + 150) : -1;
	if (line != _sceneLine) {
		_sceneLine = line;
		auto category = int(kNoCategory);
		for (auto i = line; i >= 0 && i > line - 3 && i < int(_lineCategory.size()); --i) {
			if (_lineCategory[i]) {
				category = _lineCategory[i];
				break;
			}
		}
		if (category != _sceneCategory) {
			_sceneCategory = category;
			_sceneStartMs = position;
			if (category && !(_sceneToasts & (1u << category)) && _playing) {
				_sceneToasts |= (1u << category);
				const auto lineText = (line >= 0) ? _data.lines[line].text : QString();
				const auto heart = QString::number(int(std::round(std::max(_heart, 128.))));
				switch (category) {
				case kSnow:
					if (_weather.ok) {
						showToast(ToastKind::Weather, QString::fromUtf8("ПОГОДА"), Weather::Describe(_weather.code) + u", "_q + Weather::FormatDegrees(_weather.temperature), _weather.city + QString::fromUtf8(" · ощущается как ") + Weather::FormatDegrees(_weather.feels));
					} else {
						showToast(ToastKind::Weather, QString::fromUtf8("ПОГОДА"), QString::fromUtf8("Снегопад"), QString::fromUtf8("Ощущается как −18°"));
					}
					break;
				case kNight: showToast(ToastKind::Clock, QString::fromUtf8("ЧАСЫ"), QString::fromUtf8("Ночь прошла"), QString::fromUtf8("07:00 · будильник выключен")); break;
				case kWords: showToast(ToastKind::Message, QString::fromUtf8("СООБЩЕНИЯ"), QString::fromUtf8("Ты"), lineText); break;
				case kFriends: showToast(ToastKind::Focus, QString::fromUtf8("ФОКУС"), QString::fromUtf8("Режим «Не злись» выключен"), QString::fromUtf8("Уведомления от друзей включены")); break;
				case kAnger: showToast(ToastKind::Health, QString::fromUtf8("ЗДОРОВЬЕ"), QString::fromUtf8("Пульс ") + heart + QString::fromUtf8(" уд/мин"), QString::fromUtf8("Выше нормы")); break;
				case kQueen: showToast(ToastKind::Photo, QString::fromUtf8("ФОТО"), QString::fromUtf8("Воспоминание"), QString::fromUtf8("«Зима» · ты")); break;
				case kSwear: showToast(ToastKind::Recorder, QString::fromUtf8("ДИКТОФОН"), QString::fromUtf8("Запись отцензурирована"), QString::fromUtf8("***")); break;
				case kRain:
					if (_weather.ok) {
						showToast(ToastKind::Weather, QString::fromUtf8("ПОГОДА"), Weather::Describe(_weather.code) + u", "_q + Weather::FormatDegrees(_weather.temperature), _weather.city + QString::fromUtf8(" · ощущается как ") + Weather::FormatDegrees(_weather.feels));
					} else {
						showToast(ToastKind::Weather, QString::fromUtf8("ПОГОДА"), QString::fromUtf8("Дождь усиливается"), QString::fromUtf8("+5°, ощущается как +1°"));
					}
					break;
				case kMemory: showToast(ToastKind::Photo, QString::fromUtf8("ФОТО"), QString::fromUtf8("Воспоминание"), lineText); break;
				case kEnemy: showToast(ToastKind::Health, u"FACE ID"_q, QString::fromUtf8("Совпадение: ") + sceneTitle(QString::fromUtf8("Враг")), QString::fromUtf8("99,8% · цель захвачена")); break;
				case kGrave: showToast(ToastKind::Map, QString::fromUtf8("КАРТЫ"), QString::fromUtf8("Маршрут построен"), sceneTitle(QString::fromUtf8("Пункт назначения")) + QString::fromUtf8(" · 1 мин")); break;
				case kForever: showToast(ToastKind::Clock, QString::fromUtf8("ЧАСЫ"), QString::fromUtf8("Время остановилось"), QString::fromUtf8("навсегда")); break;
				case kSleep: showToast(ToastKind::Focus, QString::fromUtf8("ФОКУС"), QString::fromUtf8("Не беспокоить"), QString::fromUtf8("Она спит. Не будить.")); break;
				case kWish: showToast(ToastKind::Reminder, QString::fromUtf8("НАПОМИНАНИЯ"), sceneTitle(QString::fromUtf8("Одно желание")), QString::fromUtf8("Остальное — неважно")); break;
				case kNear: showToast(ToastKind::Map, QString::fromUtf8("НАЙТИ"), QString::fromUtf8("Она рядом"), QString::fromUtf8("расстояние 1 м")); break;
				case kMeaning: showToast(ToastKind::Search, QString::fromUtf8("ПОИСК"), QString::fromUtf8("Ничего не найдено"), u"«"_q + sceneWord().toLower() + QString::fromUtf8("» · 0 результатов")); break;
				case kMissed: showToast(ToastKind::Phone, QString::fromUtf8("ТЕЛЕФОН"), QString::fromUtf8("99+ пропущенных"), QString::fromUtf8("Все хотят тебя")); break;
				case kRun: showToast(ToastKind::Health, QString::fromUtf8("ФИТНЕС"), QString::fromUtf8("Пульс 168 уд/мин"), QString::fromUtf8("все в движении")); break;
				default: break;
				}
			}
		}
	}

	auto left = Widget::Pulse;
	auto right = Widget::Clock;
	switch (_sceneCategory) {
	case kSnow: left = Widget::Weather; right = Widget::Radar; break;
	case kAlone: left = Widget::Radar; right = Widget::Clock; break;
	case kNight: left = Widget::Clock; right = Widget::Messages; break;
	case kWords: left = Widget::Messages; right = Widget::Clock; break;
	case kFriends: left = Widget::Battery; right = Widget::Friends; break;
	case kAnger: left = Widget::Battery; right = Widget::Pulse; break;
	case kQueen: left = Widget::Paleness; right = Widget::Emblem; break;
	case kFace: left = Widget::FaceId; right = Widget::Pulse; break;
	case kSwear:
		left = Widget::Recorder;
		right = (int(_categoryCounts.size()) > kCute && _categoryCounts[kCute] > 0)
			? Widget::Ring
			: Widget::Pulse;
		break;
	case kCute: left = Widget::Ring; right = Widget::Messages; break;
	case kHealth: left = Widget::Pulse; right = _hasPills ? Widget::Pills : Widget::Rings; break;
	case kRain: left = Widget::Weather; right = Widget::Pulse; break;
	case kMemory: left = Widget::Messages; right = Widget::Clock; break;
	case kEnemy:
		left = (position - _sceneStartMs < 6000) ? Widget::Target : Widget::Block;
		right = Widget::Counter;
		break;
	case kGrave: left = Widget::Route; right = Widget::Pulse; break;
	case kForever: left = Widget::Infinity; right = Widget::StoppedClock; break;
	case kSleep: left = Widget::Sleep; right = Widget::Alarm; break;
	case kWish: left = Widget::Battery; right = Widget::Reminders; break;
	case kNear: left = Widget::Geo; right = Widget::Pulse; break;
	case kMeaning: left = Widget::Compass; right = Widget::Search; break;
	case kMissed: left = Widget::MissedList; right = Widget::Phone; break;
	case kRun: left = Widget::Steps; right = Widget::Pulse; break;
	case kNumber: left = Widget::Dots; right = Widget::Counter; break;
	default: {
		auto pools = std::pair<std::vector<Widget>, std::vector<Widget>>();
		switch (_theme) {
		case Theme::Health:
			pools = { { Widget::Pulse, Widget::Rings, Widget::Spectrum, Widget::Steps }, { Widget::Pills, Widget::Counter, Widget::Stopwatch, Widget::Rhythm } };
			break;
		case Theme::Love:
			pools = { { Widget::Pulse, Widget::Rings, Widget::Spectrum }, { Widget::Messages, Widget::Rhythm, Widget::Clock } };
			break;
		case Theme::Night:
			pools = { { Widget::Clock, Widget::Spectrum, Widget::Rings }, { Widget::Pulse, Widget::Sleep, Widget::ScreenTime } };
			break;
		case Theme::Rain:
			pools = { { Widget::Weather, Widget::Spectrum, Widget::Rings }, { Widget::Pulse, Widget::Rhythm, Widget::Clock } };
			break;
		default:
			pools = { { Widget::Pulse, Widget::Spectrum, Widget::Rings, Widget::Steps }, { Widget::Clock, Widget::Stopwatch, Widget::ScreenTime, Widget::Rhythm } };
			break;
		}
		if (_weather.ok) {
			const auto inLeft = std::find(pools.first.begin(), pools.first.end(), Widget::Weather);
			if (inLeft != pools.first.end()) {
				pools.first.erase(inLeft);
			}
			pools.first.insert(pools.first.begin() + std::min<std::size_t>(1, pools.first.size()), Widget::Weather);
			if (std::find(pools.second.begin(), pools.second.end(), Widget::Weather) == pools.second.end()) {
				pools.second.push_back(Widget::Weather);
			}
		}
		if (!_hasPills) {
			pools.second.erase(std::remove(pools.second.begin(), pools.second.end(), Widget::Pills), pools.second.end());
		}
		if (_data.keyword.isEmpty()) {
			pools.second.erase(std::remove(pools.second.begin(), pools.second.end(), Widget::Counter), pools.second.end());
		}
		if (pools.second.empty()) {
			pools.second = { Widget::Rings, Widget::Clock, Widget::Rhythm };
		}
		constexpr auto kMinDwell = crl::time(3500);
		constexpr auto kMaxDwell = crl::time(6500);
		const auto dwell = now - _rotationAt;
		if (!_rotationAt) {
			_rotationAt = now;
			_rotationLine = line;
		} else if ((line != _rotationLine && dwell >= kMinDwell) || dwell >= kMaxDwell) {
			++_rotationStep;
			_rotationAt = now;
			_rotationLine = line;
		}
		left = pools.first[_rotationStep % pools.first.size()];
		right = pools.second[(_rotationStep + 1) % pools.second.size()];
		if (left == right) {
			right = pools.second[(_rotationStep + 2) % pools.second.size()];
		}
		if (_weather.ok && (_rotationStep % 4 == 3) && left != Widget::Weather) {
			right = Widget::Weather;
		}
	} break;
	}
	switch (_sceneCategory) {
	case kMeaning: _sceneStatus = QString::fromUtf8("Поиск смысла…"); break;
	case kSleep: _sceneStatus = QString::fromUtf8("Не беспокоить · спит"); break;
	case kForever: _sceneStatus = QString::fromUtf8("Навечно"); break;
	case kMissed: _sceneStatus = QString::fromUtf8("Пропущено 99+"); break;
	case kNear: _sceneStatus = QString::fromUtf8("Она рядом"); break;
	case kEnemy: _sceneStatus = QString::fromUtf8("Цель захвачена"); break;
	case kGrave: _sceneStatus = QString::fromUtf8("Маршрут построен"); break;
	case kWish: _sceneStatus = QString::fromUtf8("Одно желание"); break;
	case kSnow: _sceneStatus = QString::fromUtf8("Снегопад"); break;
	case kNight: _sceneStatus = QString::fromUtf8("Ночь"); break;
	default: _sceneStatus = QString(); break;
	}
	if ((left != _leftWidget || right != _rightWidget)
		&& (!_sceneSwitchAt || now - _sceneSwitchAt >= 3000 || _leftWidget == Widget::None)) {
		_prevLeftWidget = _leftWidget;
		_prevRightWidget = _rightWidget;
		_leftWidget = left;
		_rightWidget = right;
		_sceneSwitchAt = now;
		if (!_sceneCategory) {
			_sceneStartMs = position;
		}
	}
}

double LyricsWindow::sceneProgress(std::int64_t position, std::int64_t span) const {
	return std::clamp(double(position - _sceneStartMs) / std::max<std::int64_t>(span, 1), 0., 1.);
}

QString LyricsWindow::sceneWord() const {
	if (_sceneLine < 0 || _sceneLine >= int(_data.lines.size())) {
		return QString();
	}
	for (auto i = _sceneLine; i >= 0 && i > _sceneLine - 3; --i) {
		const auto &words = _data.lines[i].words;
		for (auto k = 0; k != int(words.size()); ++k) {
			if (WordCategory(Lyrics::NormalizedWord(words[k].text)) != _sceneCategory) {
				continue;
			}
			auto result = words[k].text.trimmed();
			if (k > 0 && words[k - 1].text.trimmed().size() > 2) {
				result = words[k - 1].text.trimmed() + u" "_q + result;
			}
			result.remove(QRegularExpression(uR"([,.!?…:;"«»]+)"_q));
			if (!result.isEmpty()) {
				result[0] = result[0].toUpper();
			}
			return result;
		}
	}
	return QString();
}

QPixmap LyricsWindow::snapshotWidget(Widget kind, const QRectF &rect, std::int64_t position) {
	if (kind == Widget::None || rect.isEmpty()) {
		return QPixmap();
	}
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(rect.size().toSize() * ratio, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto q = QPainter(&image);
		q.setRenderHint(QPainter::Antialiasing);
		q.setRenderHint(QPainter::TextAntialiasing);
		q.translate(-rect.topLeft());
		paintWidget(q, kind, rect, position);
	}
	return Ui::PixmapFromImage(std::move(image));
}

void LyricsWindow::paintSceneWidget(QPainter &p, bool left, const QRectF &rect, std::int64_t position) {
	if (rect.isEmpty()) {
		return;
	}
	const auto kind = left ? _leftWidget : _rightWidget;
	const auto previous = left ? _prevLeftWidget : _prevRightWidget;
	const auto elapsed = _sceneSwitchAt ? (crl::now() - _sceneSwitchAt) / 1000. : 10.;
	const auto delay = left ? 0. : 0.09;
	const auto t = std::max(0., elapsed - delay);
	if (t >= 0.6 || previous == kind || previous == Widget::None) {
		const auto side = left ? 0 : 1;
		const auto ratio = style::DevicePixelRatio();
		const auto size = rect.size().toSize() * ratio;
		auto &frame = _widgetFrames[side];
		const auto now = crl::now();
		const auto interval = _lowQuality ? crl::time(45) : crl::time(30);
		const auto age = now - _widgetFrameAt[side];
		const auto stale = (frame.size() != size) || (_widgetFrameKind[side] != kind);
		// WHY: widgets animate slowly, so 30 fps is enough; the sides alternate
		// so both never redraw in the same frame.
		const auto due = (age >= interval && _widgetRefreshFrame != now) || age >= 2 * interval;
		if (stale || due) {
			if (frame.size() != size) {
				frame = QImage(size, QImage::Format_ARGB32_Premultiplied);
				frame.setDevicePixelRatio(ratio);
			}
			frame.fill(Qt::transparent);
			{
				auto q = QPainter(&frame);
				q.setRenderHint(QPainter::Antialiasing);
				q.setRenderHint(QPainter::TextAntialiasing);
				q.translate(-rect.topLeft());
				paintWidget(q, kind, rect, position);
			}
			_widgetFrameAt[side] = now;
			_widgetFrameKind[side] = kind;
			_widgetRefreshFrame = now;
		}
		p.drawImage(rect.topLeft().toPoint(), frame);
		return;
	}
	if (_snapshotsAt != _sceneSwitchAt) {
		_snapshotsAt = _sceneSwitchAt;
		_snapshots[0] = snapshotWidget(_prevLeftWidget, _geometry.leftCard, position);
		_snapshots[1] = snapshotWidget(_leftWidget, _geometry.leftCard, position);
		_snapshots[2] = snapshotWidget(_prevRightWidget, _geometry.rightCard, position);
		_snapshots[3] = snapshotWidget(_rightWidget, _geometry.rightCard, position);
	}
	const auto &before = _snapshots[left ? 0 : 2];
	const auto &after = _snapshots[left ? 1 : 3];
	const auto center = rect.center();
	const auto draw = [&](const QPixmap &pixmap, double scale, double opacity) {
		if (pixmap.isNull() || opacity <= 0.) {
			return;
		}
		p.save();
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.setOpacity(p.opacity() * opacity);
		p.translate(center);
		p.scale(scale, scale);
		p.translate(-center);
		p.drawPixmap(rect.topLeft(), pixmap);
		p.restore();
	};
	const auto out = Smooth(t / 0.28);
	draw(before, 1. - 0.08 * out, 1. - out);
	const auto local = std::max(0., t - 0.12);
	const auto spring = 1. - std::exp(-8. * local) * std::cos(10. * local);
	draw(after, 0.9 + 0.1 * spring, Smooth(local / 0.22));
}

const LyricsWindow::TextSprite &LyricsWindow::textSprite(
		const QFont &font,
		QRgb rgb,
		const QString &text) const {
	auto key = text;
	key += QChar(0x1F);
	key += QString::number(qHash(font), 16);
	key += QChar(0x1F);
	key += QString::number(font.letterSpacing(), 'f', 2);
	key += font.italic() ? u"i"_q : QString();
	key += QChar(0x1F);
	key += QString::number(rgb, 16);
	auto i = _textSprites.find(key);
	if (i == _textSprites.end()) {
		const auto ratio = style::DevicePixelRatio();
		const auto metrics = QFontMetricsF(font);
		const auto pad = std::ceil(std::max(2., font.pixelSize() * 0.3));
		const auto width = std::max(1., metrics.horizontalAdvance(text)) + 2 * pad;
		const auto height = metrics.ascent() + metrics.descent() + 2 * pad;
		auto image = QImage(
			QSize(int(std::ceil(width)), int(std::ceil(height))) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::transparent);
		{
			auto q = QPainter(&image);
			q.setRenderHint(QPainter::TextAntialiasing);
			q.setFont(font);
			q.setPen(QColor::fromRgb(rgb));
			q.drawText(QPointF(pad, pad + metrics.ascent()), text);
		}
		i = _textSprites.insert(key, TextSprite{
			.pixmap = Ui::PixmapFromImage(std::move(image)),
			.offset = QPointF(pad, pad + metrics.ascent()),
		});
	}
	i->used = _textFrame;
	return *i;
}

void LyricsWindow::drawText(QPainter &p, QPointF baseline, const QString &text) const {
	if (text.isEmpty() || p.pen().style() == Qt::NoPen) {
		return;
	}
	const auto brush = p.pen().brush();
	if (brush.style() != Qt::SolidPattern || text.contains(QChar(u'\n'))) {
		p.drawText(baseline, text);
		return;
	}
	const auto color = brush.color();
	if (color.alpha() == 0) {
		return;
	}
	const auto font = p.font();
	const auto opacity = p.opacity();
	const auto smooth = p.renderHints().testFlag(QPainter::SmoothPixmapTransform);
	const auto simple = p.transform().type() <= QTransform::TxTranslate;
	if (!simple && !smooth) {
		p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	}
	p.setOpacity(opacity * color.alphaF());
	const auto rgb = color.rgb();
	const auto numeric = (text.size() <= 16) && std::all_of(text.begin(), text.end(), [](QChar ch) {
		return ch.isDigit() || ch.isSpace() || QStringLiteral(":.,+-−×%°/").contains(ch);
	});
	if (numeric) {
		const auto metrics = QFontMetricsF(font);
		auto x = baseline.x();
		for (const auto ch : text) {
			const auto part = QString(ch);
			if (!ch.isSpace()) {
				const auto &sprite = textSprite(font, rgb, part);
				p.drawPixmap(QPointF(x, baseline.y()) - sprite.offset, sprite.pixmap);
			}
			x += metrics.horizontalAdvance(part);
		}
	} else {
		const auto &sprite = textSprite(font, rgb, text);
		p.drawPixmap(baseline - sprite.offset, sprite.pixmap);
	}
	p.setOpacity(opacity);
	if (!simple && !smooth) {
		p.setRenderHint(QPainter::SmoothPixmapTransform, false);
	}
}

void LyricsWindow::drawText(QPainter &p, const QRectF &rect, int flags, const QString &text) const {
	if ((flags & (Qt::TextWordWrap | Qt::TextWrapAnywhere)) || text.contains(QChar(u'\n'))) {
		p.drawText(rect, flags, text);
		return;
	}
	const auto metrics = QFontMetricsF(p.font());
	const auto width = metrics.horizontalAdvance(text);
	auto x = rect.left();
	if (flags & Qt::AlignRight) {
		x = rect.right() - width;
	} else if (flags & Qt::AlignHCenter) {
		x = rect.center().x() - width / 2.;
	}
	auto y = rect.top() + metrics.ascent();
	if (flags & Qt::AlignBottom) {
		y = rect.bottom() - metrics.descent();
	} else if (flags & Qt::AlignVCenter) {
		y = rect.center().y() - (metrics.ascent() + metrics.descent()) / 2. + metrics.ascent();
	}
	drawText(p, QPointF(x, y), text);
}

void LyricsWindow::paintBloom(
		QPainter &p,
		const QString &text,
		QPointF baseline,
		QColor color,
		double strength,
		int seed) const {
	if (strength <= 0.01 || text.isEmpty()) {
		return;
	}
	const auto pad = _rowHeight * 0.35;
	const auto key = (std::uint64_t(std::uint32_t(seed)) << 32) | std::uint64_t(color.rgb());
	auto i = _blooms.find(key);
	if (i == _blooms.end()) {
		const auto ratio = style::DevicePixelRatio();
		const auto width = QFontMetricsF(_font).horizontalAdvance(text) + 2 * pad;
		const auto height = _rowHeight + 2 * pad;
		auto image = QImage(
			QSize(int(std::ceil(width)), int(std::ceil(height))) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::transparent);
		{
			auto q = QPainter(&image);
			q.setRenderHint(QPainter::TextAntialiasing);
			q.setFont(_font);
			auto solid = color;
			solid.setAlpha(255);
			q.setPen(solid);
			q.drawText(QPointF(pad, pad + _ascent), text);
		}
		auto reduced = image.scaled(
			std::max(1, image.width() / 5),
			std::max(1, image.height() / 5),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		reduced.setDevicePixelRatio(1.);
		reduced = Images::BlurLargeImage(std::move(reduced), 3);
		auto soft = reduced.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		soft.setDevicePixelRatio(ratio);
		if (_blooms.size() > 256) {
			_blooms.clear();
		}
		i = _blooms.emplace(key, Ui::PixmapFromImage(std::move(soft))).first;
	}
	p.save();
	p.setOpacity(p.opacity() * std::min(1., strength));
	p.drawPixmap(QPointF(baseline.x() - pad, baseline.y() - _ascent - pad), i->second);
	p.restore();
}

void LyricsWindow::paintSticker(QPainter &p, int sticker, QPointF anchor, double scale) const {
	const auto size = _rowHeight * 0.5;
	p.save();
	p.translate(anchor);
	p.scale(scale, scale);
	p.setPen(Qt::NoPen);
	switch (sticker) {
	case kStickerMoon: {
		const auto center = QPointF(size * 0.55, -size * 0.75);
		auto glow = QRadialGradient(center, size * 1.1);
		glow.setColorAt(0., QColor(170, 150, 255, 110));
		glow.setColorAt(1., QColor(170, 150, 255, 0));
		p.setBrush(glow);
		p.drawEllipse(center, size * 1.1, size * 1.1);
		auto disc = QPainterPath();
		disc.addEllipse(center, size * 0.5, size * 0.5);
		auto bite = QPainterPath();
		bite.addEllipse(center + QPointF(size * 0.24, -size * 0.14), size * 0.44, size * 0.44);
		p.setBrush(QColor(245, 245, 255));
		p.drawPath(disc.subtracted(bite));
	} break;
	case kStickerCrown: {
		p.rotate(14.);
		const auto w = size * 1.3, h = size * 0.85;
		const auto x = size * 0.1, y = -size * 1.2;
		auto crown = QPainterPath();
		crown.moveTo(x, y + h);
		crown.lineTo(x, y + h * 0.25);
		crown.lineTo(x + w * 0.25, y + h * 0.6);
		crown.lineTo(x + w * 0.5, y);
		crown.lineTo(x + w * 0.75, y + h * 0.6);
		crown.lineTo(x + w, y + h * 0.25);
		crown.lineTo(x + w, y + h);
		crown.closeSubpath();
		auto fill = QLinearGradient(QPointF(x, y), QPointF(x, y + h));
		fill.setColorAt(0., QColor(255, 255, 255));
		fill.setColorAt(1., QColor(150, 200, 255));
		p.setBrush(fill);
		p.setPen(QPen(QColor(120, 170, 255, 200), size * 0.05));
		p.drawPath(crown);
	} break;
	case kStickerSnow: {
		p.setBrush(QColor(245, 250, 255, 235));
		for (auto k = 0; k != 6; ++k) {
			const auto x = -size * (0.4 + 0.45 * k);
			const auto r = size * (0.16 + 0.07 * ((k * 7) % 3));
			p.drawEllipse(QPointF(x, -size * 0.02), r * 1.3, r);
		}
		const auto t = crl::now() / 1000.;
		for (auto k = 0; k != 7; ++k) {
			const auto fall = std::fmod(t * 0.6 + k * 0.37, 1.);
			p.setBrush(QColor(255, 255, 255, int(220 * (1. - fall))));
			p.drawEllipse(QPointF(-size * (0.3 + 0.4 * k), size * (0.2 + 1.4 * fall)), size * 0.05, size * 0.05);
		}
	} break;
	case kStickerSquiggle: {
		auto path = QPainterPath();
		const auto x = size * 0.15, y = -size * 0.55;
		path.moveTo(x, y);
		for (auto k = 1; k != 6; ++k) {
			path.lineTo(x + k * size * 0.18, y + ((k % 2) ? -size * 0.22 : size * 0.22));
		}
		p.setPen(QPen(QColor(255, 60, 80), size * 0.09, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	} break;
	case kStickerHeart: {
		const auto c = QPointF(size * 0.6, -size * 0.75);
		const auto r = size * 0.28;
		auto heart = QPainterPath();
		heart.moveTo(c + QPointF(0., r * 1.4));
		heart.cubicTo(c + QPointF(-r * 2.2, -r * 0.2), c + QPointF(-r * 0.9, -r * 1.8), c + QPointF(0., -r * 0.5));
		heart.cubicTo(c + QPointF(r * 0.9, -r * 1.8), c + QPointF(r * 2.2, -r * 0.2), c + QPointF(0., r * 1.4));
		p.setBrush(QColor(255, 120, 190));
		p.drawPath(heart);
	} break;
	default:
		break;
	}
	p.restore();
}

void LyricsWindow::paintRadarWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("▲"), QColor(120, 170, 255), QString::fromUtf8("РЯДОМ"), QString());
	const auto center = QPointF(rect.center().x(), rect.top() + rect.height() * 0.43);
	const auto base = rect.height() * 0.2;
	const auto now = crl::now() / 1000.;
	p.setBrush(Qt::NoBrush);
	for (auto k = 0; k != 3; ++k) {
		const auto phase = std::fmod(now / 1.8 + k / 3., 1.);
		const auto r = base * (0.35 + 1.25 * phase);
		p.setPen(QPen(QColor(120, 170, 255, int(150 * (1. - phase))), 2. * s));
		p.drawEllipse(center, r, r);
	}
	p.setPen(QPen(QColor(120, 170, 255, 70), 1.4 * s));
	p.drawEllipse(center, base * 0.75, base * 0.75);
	p.drawEllipse(center, base * 1.3, base * 1.3);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255));
	p.drawEllipse(center, 11. * s, 11. * s);
	p.setBrush(QColor(60, 130, 255));
	p.drawEllipse(center, 7.5 * s, 7.5 * s);
	auto title = QString::fromUtf8("Никого");
	if (_sceneLine >= 0 && _sceneLine < int(_data.lines.size())) {
		const auto text = _data.lines[_sceneLine].text.toLower();
		if (text.contains(QString::fromUtf8("одна"))) {
			title = QString::fromUtf8("Одна");
		} else if (text.contains(QString::fromUtf8("один"))) {
			title = QString::fromUtf8("Один");
		}
	}
	p.setFont(makeFont(34. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 44. * s), title);
	p.setFont(makeFont(15. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 20. * s), QString::fromUtf8("никого рядом"));
}

void LyricsWindow::paintClockWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("☾"), QColor(175, 160, 255), QString::fromUtf8("НОЧЬ"), QString());
	const auto now = QTime::currentTime();
	const auto center = QPointF(rect.center().x(), rect.top() + rect.height() * 0.42);
	const auto radius = std::min(rect.width(), rect.height()) * 0.25;
	p.setPen(QPen(QColor(255, 255, 255, 50), 1.4 * s));
	p.setBrush(QColor(16, 26, 70, 220));
	p.drawEllipse(center, radius, radius);
	for (auto k = 0; k != 12; ++k) {
		const auto angle = k * kPi / 6.;
		const auto dir = QPointF(std::sin(angle), -std::cos(angle));
		p.setPen(QPen(QColor(255, 255, 255, (k % 3) ? 120 : 230), ((k % 3) ? 1.6 : 2.6) * s, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(center + dir * radius * 0.8, center + dir * radius * 0.92);
	}
	const auto seconds = now.second() + now.msec() / 1000.;
	const auto minutes = now.minute() + seconds / 60.;
	const auto hours = (now.hour() % 12) + minutes / 60.;
	const auto hand = [&](double turns, double length, double width, QColor color) {
		const auto angle = turns * 2. * kPi;
		p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(center, center + QPointF(std::sin(angle), -std::cos(angle)) * length);
	};
	hand(hours / 12., radius * 0.5, 3.6 * s, QColor(255, 255, 255));
	hand(minutes / 60., radius * 0.74, 2.8 * s, QColor(255, 255, 255));
	hand(seconds / 60., radius * 0.82, 1.4 * s, QColor(255, 159, 10));
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 159, 10));
	p.drawEllipse(center, 3.6 * s, 3.6 * s);
	p.setFont(makeFont(44. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 44. * s), now.toString(u"HH:mm"_q));
	auto waited = (now.hour() * 60 + now.minute() - 23 * 60 + 24 * 60) % (24 * 60);
	p.setFont(makeFont(15. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, 
		QPointF(rect.left() + 24. * s, rect.bottom() - 20. * s),
		QString::fromUtf8("Ждал ") + QString::number(waited / 60) + QString::fromUtf8(" ч ") + QString::number(waited % 60) + QString::fromUtf8(" мин"));
}

void LyricsWindow::paintFriendsWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("◉"), QColor(255, 255, 255), QString::fromUtf8("ДРУЗЬЯ"), QString());
	static const auto badges = std::array<const char*, 6>{ "1", "3", "1", "99+", "1", "7" };
	const auto progress = sceneProgress(position, 4200);
	const auto radius = rect.width() * 0.105;
	for (auto k = 0; k != 6; ++k) {
		const auto column = k % 3, row = k / 3;
		const auto center = QPointF(
			rect.left() + rect.width() * (0.2 + 0.3 * column),
			rect.top() + rect.height() * (0.33 + 0.24 * row));
		auto fill = QLinearGradient(center - QPointF(0., radius), center + QPointF(0., radius));
		fill.setColorAt(0., QColor(225, 230, 240));
		fill.setColorAt(1., QColor(150, 160, 180));
		p.setPen(Qt::NoPen);
		p.setBrush(fill);
		p.drawEllipse(center, radius, radius);
		auto clip = QPainterPath();
		clip.addEllipse(center, radius, radius);
		p.save();
		p.setClipPath(clip);
		p.setBrush(QColor(110, 120, 140));
		p.drawEllipse(center + QPointF(0., -radius * 0.22), radius * 0.36, radius * 0.36);
		p.drawEllipse(center + QPointF(0., radius * 0.85), radius * 0.72, radius * 0.6);
		p.restore();
		if (progress >= k / 6.) {
			const auto age = std::clamp((progress - k / 6.) * 6., 0., 1.);
			const auto text = QString::fromUtf8(badges[k]);
			const auto font = makeFont(10. * s, QFont::Bold);
			const auto w = std::max(18. * s, QFontMetricsF(font).horizontalAdvance(text) + 10. * s);
			const auto badge = QRectF(center.x() + radius * 0.35, center.y() - radius * 1.1, w, 18. * s);
			const auto pop = 0.6 + 0.4 * std::min(1.2, 1. - std::exp(-8. * age) * std::cos(10. * age));
			p.save();
			p.translate(badge.center());
			p.scale(pop, pop);
			p.translate(-badge.center());
			p.setBrush(QColor(255, 59, 48));
			p.drawRoundedRect(badge, 9. * s, 9. * s);
			p.setFont(font);
			p.setPen(QColor(255, 255, 255));
			drawText(p, badge, Qt::AlignCenter, text);
			p.restore();
		}
	}
	p.setFont(makeFont(19. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 42. * s), QString::fromUtf8("Все твои друзья"));
	p.setFont(makeFont(13. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 20. * s), QString::fromUtf8("99+ уведомлений"));
}

void LyricsWindow::paintBatteryWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("▭"), QColor(255, 255, 255), QString::fromUtf8("ТЕРПЕНИЕ"), QString());
	const auto percent = int(std::round(100. - 98. * Smooth(sceneProgress(position, 9000))));
	const auto low = (percent <= 20);
	p.setFont(makeFont(62. * s, QFont::Bold));
	p.setPen(low ? QColor(255, 80, 90) : QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 24. * s, rect.top() + rect.height() * 0.42), QString::number(percent) + u"%"_q);
	const auto bar = QRectF(rect.left() + 24. * s, rect.top() + rect.height() * 0.52, rect.width() - 48. * s, 36. * s);
	p.setPen(QPen(QColor(255, 255, 255, 90), 2. * s));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(bar, 11. * s, 11. * s);
	const auto inner = bar.marginsRemoved(QMarginsF(5. * s, 5. * s, 5. * s, 5. * s));
	auto fill = QLinearGradient(inner.topLeft(), inner.bottomLeft());
	fill.setColorAt(0., low ? QColor(255, 110, 110) : QColor(235, 245, 255));
	fill.setColorAt(1., low ? QColor(220, 40, 60) : QColor(170, 210, 255));
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawRoundedRect(QRectF(inner.left(), inner.top(), std::max(inner.height() * 0.4, inner.width() * percent / 100.), inner.height()), 7. * s, 7. * s);
	p.setFont(makeFont(17. * s, QFont::DemiBold));
	p.setPen(low ? QColor(255, 80, 90) : QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 42. * s), low ? QString::fromUtf8("Низкий заряд") : QString::fromUtf8("Заряд падает"));
	p.setFont(makeFont(13. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, 
		QPointF(rect.left() + 24. * s, rect.bottom() - 20. * s),
		low
			? (QString::fromUtf8("Осталось ") + QString::number(percent) + QString::fromUtf8("% терпения"))
			: (QString::fromUtf8("Выдержка: ") + QString::number(percent) + u"%"_q));
}

void LyricsWindow::paintPalenessWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("❄"), QColor(170, 215, 255), QString::fromUtf8("БЛЕДНОСТЬ"), QString());
	const auto progress = Smooth(sceneProgress(position, 5000));
	const auto winter = int(std::round(72. + 2. * progress));
	const auto you = int(std::round(76. + 21. * progress));
	const auto row = [&](double top, const QString &name, int value, QColor color) {
		p.setFont(makeFont(16. * s, QFont::DemiBold));
		p.setPen(QColor(255, 255, 255, 220));
		drawText(p, QPointF(rect.left() + 24. * s, top), name);
		const auto number = QString::number(value) + u"%"_q;
		drawText(p, QPointF(rect.right() - 24. * s - QFontMetricsF(makeFont(16. * s, QFont::DemiBold)).horizontalAdvance(number), top), number);
		const auto bar = QRectF(rect.left() + 24. * s, top + 12. * s, rect.width() - 48. * s, 12. * s);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 40));
		p.drawRoundedRect(bar, 6. * s, 6. * s);
		p.setBrush(color);
		p.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * value / 100., bar.height()), 6. * s, 6. * s);
	};
	row(rect.top() + rect.height() * 0.3, QString::fromUtf8("Зима"), winter, QColor(110, 170, 255));
	row(rect.top() + rect.height() * 0.53, QString::fromUtf8("Ты"), you, QColor(240, 246, 255));
	p.setFont(makeFont(22. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 42. * s), u"+"_q + QString::number(you - winter + 14) + QString::fromUtf8("% бледнее"));
	p.setFont(makeFont(13. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 20. * s), QString::fromUtf8("бледнее, чем зима"));
}

void LyricsWindow::paintEmblemWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("♛"), QColor(255, 255, 255), QString::fromUtf8("МОЯ"), QString());
	auto word = sceneWord();
	const auto crown = word.contains(QString::fromUtf8("цариц"), Qt::CaseInsensitive)
		|| word.contains(QString::fromUtf8("корол"), Qt::CaseInsensitive);
	const auto center = QPointF(rect.center().x(), rect.top() + rect.height() * 0.43);
	const auto size = rect.height() * 0.17;
	p.save();
	p.translate(center);
	p.rotate(std::fmod(crl::now() / 1000. * 12., 360.));
	auto glow = QRadialGradient(QPointF(), size * 1.6);
	glow.setColorAt(0., QColor(150, 200, 255, 90));
	glow.setColorAt(1., QColor(150, 200, 255, 0));
	p.setPen(Qt::NoPen);
	p.setBrush(glow);
	p.drawEllipse(QPointF(), size * 1.6, size * 1.6);
	p.setPen(QPen(QColor(240, 248, 255), 3.4 * s, Qt::SolidLine, Qt::RoundCap));
	for (auto k = 0; k != 6; ++k) {
		p.save();
		p.rotate(k * 60.);
		p.drawLine(QPointF(), QPointF(0., -size));
		p.drawLine(QPointF(0., -size * 0.55), QPointF(-size * 0.25, -size * 0.8));
		p.drawLine(QPointF(0., -size * 0.55), QPointF(size * 0.25, -size * 0.8));
		p.restore();
	}
	p.restore();
	if (word.isEmpty()) {
		word = crown ? QString::fromUtf8("Королева") : QString::fromUtf8("Снежная царица");
	}
	p.setFont(makeFont(21. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, 
		QRectF(rect.left() + 16. * s, rect.bottom() - 82. * s, rect.width() - 32. * s, 64. * s),
		Qt::AlignCenter | Qt::TextWordWrap,
		word);
}

void LyricsWindow::paintFaceWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("⌗"), QColor(255, 255, 255), QString::fromUtf8("FACE ID"), QString());
	const auto progress = sceneProgress(position, 1800);
	const auto box = QRectF(
		rect.center().x() - rect.height() * 0.2,
		rect.top() + rect.height() * 0.22,
		rect.height() * 0.4,
		rect.height() * 0.4);
	const auto corner = box.width() * 0.22;
	p.setPen(QPen(QColor(255, 255, 255), 3.6 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	for (const auto &[x, y, dx, dy] : {
			std::tuple{ box.left(), box.top(), 1., 1. },
			std::tuple{ box.right(), box.top(), -1., 1. },
			std::tuple{ box.left(), box.bottom(), 1., -1. },
			std::tuple{ box.right(), box.bottom(), -1., -1. } }) {
		auto path = QPainterPath();
		path.moveTo(x, y + dy * corner);
		path.lineTo(x, y);
		path.lineTo(x + dx * corner, y);
		p.drawPath(path);
	}
	const auto done = progress >= 1.;
	if (!done) {
		const auto c = box.center();
		p.setPen(QPen(QColor(255, 255, 255, 220), 3. * s, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(c + QPointF(-box.width() * 0.18, -box.height() * 0.12), c + QPointF(-box.width() * 0.18, -box.height() * 0.02));
		p.drawLine(c + QPointF(box.width() * 0.18, -box.height() * 0.12), c + QPointF(box.width() * 0.18, -box.height() * 0.02));
		p.drawArc(QRectF(c.x() - box.width() * 0.18, c.y(), box.width() * 0.36, box.height() * 0.2), 200 * 16, 140 * 16);
		const auto scan = box.top() + box.height() * std::fmod(crl::now() / 900., 1.);
		auto line = QLinearGradient(QPointF(box.left(), 0.), QPointF(box.right(), 0.));
		line.setColorAt(0., QColor(120, 200, 255, 0));
		line.setColorAt(0.5, QColor(120, 200, 255, 220));
		line.setColorAt(1., QColor(120, 200, 255, 0));
		p.setPen(QPen(QBrush(line), 2.4 * s));
		p.drawLine(QPointF(box.left(), scan), QPointF(box.right(), scan));
	} else {
		const auto age = (position - _sceneStartMs - 1800) / 1000.;
		const auto pop = std::min(1.12, 1. - std::exp(-8. * age) * std::cos(10. * age));
		const auto r = box.width() * 0.28 * std::max(0., pop);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255));
		p.drawEllipse(box.center(), r, r);
		auto check = QPainterPath();
		const auto c = box.center();
		check.moveTo(c + QPointF(-r * 0.42, 0.));
		check.lineTo(c + QPointF(-r * 0.1, r * 0.32));
		check.lineTo(c + QPointF(r * 0.45, -r * 0.3));
		p.setPen(QPen(QColor(40, 110, 255), 4. * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(check);
	}
	auto title = done ? sceneWord() : QString::fromUtf8("Сканирую…");
	if (title.isEmpty()) {
		title = QString::fromUtf8("Лицо");
	}
	p.setFont(makeFont(21. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, 
		QPointF(rect.left() + 24. * s, rect.bottom() - 42. * s),
		QFontMetrics(makeFont(21. * s, QFont::Bold)).elidedText(title, Qt::ElideRight, int(rect.width() - 48. * s)));
	p.setFont(makeFont(13. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 20. * s), done ? QString::fromUtf8("Лицо распознано") : QString::fromUtf8("Держи ровно"));
}

void LyricsWindow::paintRecorderWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	const auto seconds = std::max<std::int64_t>(0, position - _sceneStartMs) / 1000;
	paintWidgetHeader(
		p,
		rect,
		QString::fromUtf8("●"),
		kRed,
		QString::fromUtf8("ДИКТОФОН"),
		u"0:"_q + QString::number(seconds % 60).rightJustified(2, QChar(u'0')));
	auto censored = false;
	if (_active >= 0 && _active < int(_data.lines.size())) {
		for (const auto &word : _data.lines[_active].words) {
			if (WordCategory(Lyrics::NormalizedWord(word.text)) == kSwear
				&& position >= word.startMs - 200
				&& position <= word.endMs + 400) {
				censored = true;
			}
		}
	}
	const auto area = QRectF(rect.left() + 22. * s, rect.top() + rect.height() * 0.24, rect.width() - 44. * s, rect.height() * 0.36);
	const auto bars = 34;
	const auto step = area.width() / bars;
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != bars; ++i) {
		const auto at = position - (bars - 1 - i) * 90;
		auto level = (_analysis && _analysis->ok && at >= 0)
			? (0.25f + 0.75f * _analysis->energyAt(at))
			: (0.3f + 0.4f * float(std::abs(std::sin(at / 180. + i))));
		level *= 0.75f + 0.5f * float(Hash01(std::uint32_t(at / 90), std::uint32_t(i), 21));
		const auto h = std::max(4. * s, area.height() * std::min(1.f, level));
		const auto middle = (i > bars / 3 && i < bars * 2 / 3);
		p.setBrush((censored && middle) ? QColor(255, 70, 90) : QColor(190, 215, 255, 230));
		p.drawRoundedRect(QRectF(area.left() + step * i + step * 0.2, area.center().y() - h / 2., step * 0.6, h), step * 0.3, step * 0.3);
	}
	if (censored) {
		const auto chip = QRectF(rect.left() + 22. * s, area.bottom() + 8. * s, 52. * s, 24. * s);
		p.setBrush(QColor(255, 255, 255));
		p.drawRoundedRect(chip, 8. * s, 8. * s);
		p.setFont(makeFont(13. * s, QFont::Bold));
		p.setPen(QColor(20, 20, 30));
		drawText(p, chip, Qt::AlignCenter, QString::fromUtf8("БИП"));
	}
	p.setFont(makeFont(16. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, 
		QPointF(rect.left() + 24. * s, rect.bottom() - 42. * s),
		QFontMetrics(makeFont(16. * s, QFont::Bold)).elidedText(_title, Qt::ElideRight, int(rect.width() - 48. * s)));
	p.setFont(makeFont(13. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 20. * s), QString::fromUtf8("Запись · цензура ***"));
}

void LyricsWindow::paintRingWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("♥"), QColor(255, 140, 200), QString::fromUtf8("МИЛОТА"), QString());
	const auto percent = int(std::round(60. + 40. * Smooth(sceneProgress(position, 8000))));
	const auto center = QPointF(rect.center().x(), rect.top() + rect.height() * 0.42);
	const auto radius = rect.height() * 0.18;
	const auto ring = QRectF(center.x() - radius, center.y() - radius, radius * 2., radius * 2.);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(255, 255, 255, 40), 12. * s));
	p.drawEllipse(ring);
	auto gradient = QConicalGradient(center, 90.);
	gradient.setColorAt(0., QColor(255, 255, 255));
	gradient.setColorAt(0.5, QColor(255, 150, 210));
	gradient.setColorAt(1., QColor(255, 255, 255));
	p.setPen(QPen(QBrush(gradient), 12. * s, Qt::SolidLine, Qt::RoundCap));
	p.drawArc(ring, 90 * 16, -int(360 * 16 * percent / 100.));
	const auto beat = 1. + 0.08 * pulse(position);
	const auto r = radius * 0.32 * beat;
	auto heart = QPainterPath();
	heart.moveTo(center + QPointF(0., r * 1.1));
	heart.cubicTo(center + QPointF(-r * 1.8, -r * 0.1), center + QPointF(-r * 0.8, -r * 1.4), center + QPointF(0., -r * 0.4));
	heart.cubicTo(center + QPointF(r * 0.8, -r * 1.4), center + QPointF(r * 1.8, -r * 0.1), center + QPointF(0., r * 1.1));
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255));
	p.drawPath(heart);
	p.setFont(makeFont(30. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 42. * s), QString::number(percent) + u"%"_q);
	p.setFont(makeFont(13. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, 
		QPointF(rect.left() + 24. * s, rect.bottom() - 20. * s),
		(_sceneCategory == kSwear) ? QString::fromUtf8("ругается — но мило") : QString::fromUtf8("уровень милоты"));
}

void LyricsWindow::paintSpectrumWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("♪"), QColor(120, 200, 255), QString::fromUtf8("ЗВУК"), u"LIVE"_q);
	const auto db = int(std::round(-32. + 30. * _energySmooth));
	p.setFont(makeFont(54. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	const auto number = ((db < 0) ? QString::fromUtf8("−") : QString()) + QString::number(std::abs(db));
	drawText(p, QPointF(rect.left() + 22. * s, rect.top() + 122. * s), number);
	p.setFont(makeFont(17. * s, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255, 170));
	drawText(p, 
		QPointF(rect.left() + 28. * s + QFontMetricsF(makeFont(54. * s, QFont::Bold)).horizontalAdvance(number), rect.top() + 122. * s),
		QString::fromUtf8("дБ"));
	const auto area = QRectF(rect.left() + 22. * s, rect.top() + 140. * s, rect.width() - 44. * s, rect.bottom() - 52. * s - rect.top() - 140. * s);
	const auto count = std::max(1, int(_eqLevels.size()) / 2);
	const auto step = area.width() / count;
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != count; ++i) {
		const auto level = _eqLevels.empty() ? 0.1f : std::max(_eqLevels[i * 2], _eqLevels[std::min(i * 2 + 1, int(_eqLevels.size()) - 1)]);
		const auto h = std::max(4. * s, area.height() * level);
		auto fill = QLinearGradient(QPointF(0., area.bottom()), QPointF(0., area.top()));
		fill.setColorAt(0., QColor(90, 160, 255, 220));
		fill.setColorAt(1., QColor(235, 245, 255, 240));
		p.setBrush(fill);
		p.drawRoundedRect(QRectF(area.left() + step * i + step * 0.18, area.bottom() - h, step * 0.64, h), step * 0.3, step * 0.3);
	}
	p.setFont(makeFont(13. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 22. * s), QString::fromUtf8("Громкость трека сейчас"));
}

void LyricsWindow::paintRhythmWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	const auto bpm = (_analysis && _analysis->bpm > 0.) ? int(std::round(_analysis->bpm)) : 0;
	paintWidgetHeader(p, rect, QString::fromUtf8("◷"), QColor(255, 200, 120), QString::fromUtf8("РИТМ"), bpm ? (QString::number(bpm) + u" BPM"_q) : QString());
	auto phase = std::fmod(crl::now() / 1000. * 2., 2.);
	auto strong = false;
	if (_analysis && _analysis->beats.size() > 1) {
		const auto &beats = _analysis->beats;
		const auto it = std::upper_bound(beats.begin(), beats.end(), position);
		const auto index = int(it - beats.begin()) - 1;
		if (index >= 0 && index + 1 < int(beats.size())) {
			const auto span = std::max<std::int64_t>(1, beats[index + 1] - beats[index]);
			phase = (index % 2) + double(position - beats[index]) / span;
			strong = (index % 4 == 0) && (position - beats[index] < 180);
		}
	}
	const auto swing = std::sin(phase * kPi) * 0.45;
	const auto base = QPointF(rect.center().x(), rect.top() + rect.height() * 0.62);
	const auto height = rect.height() * 0.34;
	auto body = QPainterPath();
	body.moveTo(base + QPointF(-height * 0.42, 0.));
	body.lineTo(base + QPointF(-height * 0.16, -height));
	body.lineTo(base + QPointF(height * 0.16, -height));
	body.lineTo(base + QPointF(height * 0.42, 0.));
	body.closeSubpath();
	p.setPen(QPen(QColor(255, 255, 255, 60), 1.4 * s));
	p.setBrush(QColor(255, 255, 255, strong ? 60 : 26));
	p.drawPath(body);
	const auto pivot = base + QPointF(0., -height * 0.12);
	const auto tip = pivot + QPointF(std::sin(swing) * height * 0.95, -std::cos(swing) * height * 0.95);
	p.setPen(QPen(QColor(255, 255, 255), 3.4 * s, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(pivot, tip);
	p.setPen(Qt::NoPen);
	p.setBrush(strong ? QColor(255, 200, 120) : QColor(255, 255, 255));
	p.drawEllipse(pivot + (tip - pivot) * 0.62, 7. * s, 7. * s);
	p.setFont(makeFont(30. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 44. * s), bpm ? QString::number(bpm) : QString::fromUtf8("—"));
	p.setFont(makeFont(13. * s));
	p.setPen(strong ? QColor(255, 200, 120) : QColor(255, 255, 255, 140));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 20. * s), strong ? QString::fromUtf8("сильная доля") : QString::fromUtf8("ударов в минуту"));
}

void LyricsWindow::paintRingsWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("◎"), QColor(255, 90, 110), QString::fromUtf8("АКТИВНОСТЬ"), QString());
	const auto move = (_duration > 0) ? std::clamp(double(position) / _duration, 0., 1.) : 0.;
	const auto exercise = std::clamp(_energySmooth * 1.2, 0., 1.);
	auto stand = 0.;
	if (_data.synced && !_data.lines.empty()) {
		auto done = 0;
		for (const auto &line : _data.lines) {
			done += (position >= line.endMs) ? 1 : 0;
		}
		stand = double(done) / _data.lines.size();
	}
	const auto center = QPointF(rect.center().x(), rect.top() + rect.height() * 0.47);
	const auto values = std::array<double, 3>{ move, exercise, stand };
	const auto colors = std::array<QColor, 3>{ QColor(255, 55, 95), QColor(150, 255, 70), QColor(70, 220, 255) };
	const auto thickness = 13. * s;
	for (auto k = 0; k != 3; ++k) {
		const auto radius = rect.height() * 0.2 - k * (thickness + 3. * s);
		const auto ring = QRectF(center.x() - radius, center.y() - radius, radius * 2., radius * 2.);
		auto dim = colors[k];
		dim.setAlpha(50);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(dim, thickness));
		p.drawEllipse(ring);
		p.setPen(QPen(colors[k], thickness, Qt::SolidLine, Qt::RoundCap));
		p.drawArc(ring, 90 * 16, -int(360 * 16 * std::max(0.01, values[k])));
	}
	p.setFont(makeFont(13. * s, QFont::DemiBold));
	const auto labels = std::array<const char*, 3>{ "Трек", "Энергия", "Строки" };
	const auto cell = (rect.width() - 48. * s) / 3.;
	for (auto k = 0; k != 3; ++k) {
		const auto x = rect.left() + 24. * s + cell * k;
		p.setPen(colors[k]);
		drawText(p, QPointF(x, rect.bottom() - 40. * s), QString::number(int(std::round(values[k] * 100))) + u"%"_q);
		p.setPen(QColor(255, 255, 255, 140));
		drawText(p, QPointF(x, rect.bottom() - 20. * s), QString::fromUtf8(labels[k]));
	}
}

void LyricsWindow::paintScreenTimeWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("⧗"), QColor(170, 150, 255), QString::fromUtf8("ЭКРАННОЕ ВРЕМЯ"), QString());
	const auto total = int(_listenedSeconds);
	const auto text = (total >= 3600)
		? (QString::number(total / 3600) + QString::fromUtf8(" ч ") + QString::number((total % 3600) / 60) + QString::fromUtf8(" мин"))
		: (QString::number(total / 60) + QString::fromUtf8(" мин ") + QString::number(total % 60) + QString::fromUtf8(" с"));
	p.setFont(makeFont(36. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 22. * s, rect.top() + 112. * s), text);
	const auto area = QRectF(rect.left() + 22. * s, rect.top() + 134. * s, rect.width() - 44. * s, rect.bottom() - 60. * s - rect.top() - 134. * s);
	const auto bars = 12;
	const auto step = area.width() / bars;
	const auto seed = std::uint32_t(qHash(_title));
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != bars; ++i) {
		const auto last = (i == bars - 1);
		auto level = last
			? std::min(1., 0.15 + _listenedSeconds / 600.)
			: (0.15 + 0.7 * Hash01(seed, std::uint32_t(i), 41));
		const auto h = std::max(4. * s, area.height() * level);
		p.setBrush(last ? QColor(170, 150, 255) : QColor(255, 255, 255, 60));
		p.drawRoundedRect(QRectF(area.left() + step * i + step * 0.2, area.bottom() - h, step * 0.6, h), step * 0.3, step * 0.3);
	}
	p.setFont(makeFont(13. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 22. * s), QString::fromUtf8("Музыка · Whitegram"));
}

void LyricsWindow::requestWeather(bool force) {
	const auto now = crl::now();
	if (_weatherLoading && now - _weatherRequestedAt < 40000) {
		return;
	} else if (!force && _weather.ok && now - _weatherAt < 15 * 60 * 1000) {
		return;
	}
	_weatherLoading = true;
	_weatherRequestedAt = now;
	const auto generation = ++_weatherGeneration;
	const auto city = AyuSettings::getInstance().musicWeatherCity();
	LOG(("Karaoke weather: requesting (%1)").arg(city.isEmpty() ? u"by IP"_q : (u"city "_q + city)));
	const auto weak = QPointer<LyricsWindow>(this);
	crl::async([=] {
		auto data = Weather::FetchSync(city);
		crl::on_main([=, data = std::move(data)]() mutable {
			if (const auto strong = weak.data()) {
				if (generation != strong->_weatherGeneration) {
					return;
				}
				strong->_weatherLoading = false;
				strong->_weatherAt = crl::now();
				if (data.ok) {
					LOG(("Karaoke weather: %1 (%2), %3 C, code %4").arg(
						data.city,
						data.fromIp ? u"by IP"_q : u"by city"_q,
						QString::number(data.temperature, 'f', 1),
						QString::number(data.code)));
					strong->_weather = std::move(data);
				} else {
					LOG(("Karaoke weather: failed (%1)").arg(
						city.isEmpty() ? u"by IP"_q : (u"city "_q + city)));
				}
			}
		});
	});
}

int LyricsWindow::wordFx(const QString &normalized, int &number) const {
	number = NumberValue(normalized);
	if (number > 0) {
		return kFxNumber;
	}
	for (const auto &[fx, stems] : FxStems()) {
		for (const auto stem : stems) {
			auto value = QString::fromUtf8(stem);
			const auto exact = value.endsWith(QChar(u'='));
			if (exact) {
				value.chop(1);
			}
			if (exact ? (normalized == value) : normalized.startsWith(value)) {
				return fx;
			}
		}
	}
	return kFxNone;
}

bool LyricsWindow::paintFxWord(
		QPainter &p,
		int fx,
		const QString &text,
		QPointF baseline,
		const QRectF &box,
		double emphasis,
		double progress,
		double age,
		int seed) const {
	const auto s = _rowHeight / 90.;
	switch (fx) {
	case kFxRainbow: {
		auto gradient = QLinearGradient(QPointF(baseline.x(), 0.), QPointF(baseline.x() + box.width(), 0.));
		const auto shift = std::fmod(crl::now() / 2400., 1.);
		for (auto k = 0; k <= 6; ++k) {
			const auto hue = std::fmod(shift + k / 6. * 0.85, 1.);
			gradient.setColorAt(k / 6., QColor::fromHsvF(hue, 0.55, 1., std::min(1., 0.35 + progress)));
		}
		p.save();
		p.setPen(QPen(QBrush(gradient), 1.));
		p.translate(baseline);
		p.shear(-0.12 * Smooth(progress), 0.);
		drawText(p, QPointF(), text);
		p.restore();
		return true;
	}
	case kFxGold: {
		const auto glow = std::min(1., age * 2.) * emphasis;
		paintBloom(p, text, baseline, QColor(255, 176, 70), glow, seed);
		auto gradient = QLinearGradient(QPointF(0., baseline.y() - _ascent), QPointF(0., baseline.y()));
		gradient.setColorAt(0., QColor(255, 236, 170, int(255 * (0.4 + 0.6 * progress))));
		gradient.setColorAt(1., QColor(255, 170, 60, int(255 * (0.4 + 0.6 * progress))));
		p.setPen(QPen(QBrush(gradient), 1.));
		drawText(p, baseline, text);
		return true;
	}
	case kFxDissolve: {
		const auto gone = std::clamp((age - 0.35) / 1.1, 0., 1.);
		p.setPen(QColor(255, 255, 255, int(255 * (1. - gone) * std::max(0.4, progress))));
		drawText(p, baseline, text);
		if (gone > 0.) {
			p.setPen(Qt::NoPen);
			for (auto k = 0; k != 46; ++k) {
				const auto hx = Hash01(std::uint32_t(seed), std::uint32_t(k), 61);
				const auto fy = Hash01(std::uint32_t(seed), std::uint32_t(k), 62);
				const auto start = QPointF(baseline.x() + box.width() * hx, baseline.y() - _ascent * (0.1 + 0.8 * fy));
				const auto drift = QPointF((0.4 + hx) * 60. * s, -(0.3 + fy) * 46. * s) * Smooth(gone);
				const auto size = (1. + 2. * Hash01(std::uint32_t(seed), std::uint32_t(k), 63)) * s;
				p.setBrush(QColor(255, 255, 255, int(220 * (1. - gone))));
				p.drawEllipse(start + drift, size, size);
			}
		}
		return true;
	}
	case kFxSink: {
		const auto metrics = QFontMetricsF(_font);
		auto x = baseline.x();
		for (auto k = 0; k != text.size(); ++k) {
			const auto letter = QString(text[k]);
			const auto delay = 0.08 * k;
			const auto fall = Smooth((age - 0.25 - delay) / 0.7);
			const auto drop = fall * _rowHeight * 0.32;
			const auto tilt = (Hash01(std::uint32_t(seed), std::uint32_t(k), 71) - 0.5) * 24. * fall;
			p.save();
			p.translate(x + metrics.horizontalAdvance(letter) / 2., baseline.y() + drop);
			p.rotate(tilt);
			p.setPen(QColor(255, 255, 255, int(255 * std::max(0.25, (1. - 0.65 * fall)) * std::max(0.4, progress))));
			drawText(p, QPointF(-metrics.horizontalAdvance(letter) / 2., 0.), letter);
			p.restore();
			x += metrics.horizontalAdvance(letter);
		}
		return true;
	}
	case kFxGlitch: {
		const auto step = std::uint32_t(crl::now() / 90);
		const auto jitter = (Hash01(step, std::uint32_t(seed), 81) > 0.6) ? (Hash01(step, std::uint32_t(seed), 82) - 0.5) * 6. * s : 0.;
		p.save();
		p.translate(baseline + QPointF(jitter, 0.));
		p.shear(-0.22 * Smooth(progress), 0.);
		p.setPen(QColor(0, 220, 255, 90));
		drawText(p, QPointF(-2. * s, 0.), text);
		p.setPen(QColor(255, 40, 80, 90));
		drawText(p, QPointF(2. * s, 0.), text);
		p.setPen(QColor(255, 255, 255, int(255 * std::max(0.4, progress))));
		drawText(p, QPointF(), text);
		p.restore();
		return true;
	}
	case kFxItalic:
	case kFxRun:
	case kFxAsk: {
		const auto lean = Smooth(std::min(1., age / 0.22));
		p.save();
		if (fx == kFxRun) {
			const auto dash = Smooth(std::min(1., age / 0.35));
			p.setPen(QPen(QColor(255, 255, 255, int(120 * (1. - 0.5 * dash))), 2.2 * s, Qt::SolidLine, Qt::RoundCap));
			for (auto k = 0; k != 3; ++k) {
				const auto y = baseline.y() - _ascent * (0.22 + 0.22 * k);
				const auto x = baseline.x() - (8. + 5. * k) * s;
				p.drawLine(QPointF(x - (16. + 9. * k) * s * dash, y), QPointF(x, y));
			}
		}
		p.translate(baseline + QPointF((fx == kFxRun) ? 6. * s * lean : 0., 0.));
		p.shear(-0.24 * lean, 0.);
		p.setPen(QColor(255, 255, 255, int(255 * std::max(0.4, progress))));
		drawText(p, QPointF(), text);
		p.restore();
		return true;
	}
	case kFxLeave: {
		const auto lean = Smooth(std::min(1., age / 0.3));
		const auto go = Smooth(std::clamp((age - 0.25) / 1.6, 0., 1.));
		const auto alpha = 255. * std::max(0.4, progress);
		auto gradient = QLinearGradient(QPointF(0., 0.), QPointF(std::max(1., box.width()), 0.));
		gradient.setColorAt(0., QColor(255, 255, 255, int(alpha * (1. - 0.35 * go))));
		gradient.setColorAt(std::clamp(0.75 - 0.55 * go, 0.1, 0.9), QColor(255, 255, 255, int(alpha * (1. - 0.55 * go))));
		gradient.setColorAt(1., QColor(255, 255, 255, int(alpha * (1. - go))));
		p.save();
		p.translate(baseline + QPointF(go * 22. * s, 0.));
		p.shear(-0.2 * lean, 0.);
		p.setPen(QPen(QBrush(gradient), 1.));
		drawText(p, QPointF(), text);
		p.restore();
		return true;
	}
	case kFxHang: {
		const auto drop = Smooth(std::min(1., age / 0.35));
		const auto pivot = QPointF(baseline.x() + box.width() * 0.18, baseline.y() - _ascent - _rowHeight * 0.5);
		const auto knot = baseline.y() - _ascent * 0.85;
		const auto swing = 16. * std::exp(-0.9 * age) * std::cos(3.1 * age)
			+ 3.5 * std::sin(crl::now() / 700. + seed);
		p.save();
		p.translate(pivot);
		p.rotate((-8. + swing) * drop);
		p.setPen(QPen(QColor(255, 255, 255, int(170 * drop)), 1.6 * s));
		p.drawLine(QPointF(), QPointF(0., (knot - pivot.y()) * drop));
		p.translate(-pivot);
		p.setPen(QColor(255, 255, 255, int(255 * std::max(0.4, progress))));
		drawText(p, baseline, text);
		p.restore();
		p.save();
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, int(230 * drop)));
		p.drawEllipse(pivot, 3.5 * s, 3.5 * s);
		p.restore();
		return true;
	}
	case kFxShine: {
		const auto glow = Smooth(std::min(1., age / 0.45)) * emphasis;
		const auto center = QPointF(baseline.x() + box.width() * 0.5, baseline.y() - _ascent * 0.4);
		const auto reach = std::max(box.width() * 0.75, _rowHeight * 0.9);
		p.save();
		p.setPen(Qt::NoPen);
		auto halo = QRadialGradient(center, reach);
		halo.setColorAt(0., QColor(255, 255, 255, int(110 * glow)));
		halo.setColorAt(1., QColor(255, 255, 255, 0));
		p.setBrush(halo);
		p.drawEllipse(center, reach, reach);
		p.translate(center);
		p.rotate(std::fmod(crl::now() / 90., 360.));
		p.setBrush(QColor(255, 255, 255, int(110 * glow)));
		for (auto k = 0; k != 16; ++k) {
			const auto length = reach * (0.55 + 0.45 * Hash01(std::uint32_t(seed), std::uint32_t(k), 101)) * glow;
			const auto width = ((k % 2) ? 1.6 : 3.2) * s;
			auto ray = QPainterPath();
			ray.moveTo(0., -width);
			ray.lineTo(length, 0.);
			ray.lineTo(0., width);
			ray.closeSubpath();
			p.drawPath(ray);
			p.rotate(360. / 16.);
		}
		p.restore();
		paintBloom(p, text, baseline, QColor(255, 244, 214), 0.7 * glow, seed);
		p.setPen(QColor(255, 255, 255, int(255 * std::max(0.4, progress))));
		drawText(p, baseline, text);
		return true;
	}
	case kFxSlash: {
		const auto cut = Smooth(std::clamp((age - 0.1) / 0.22, 0., 1.));
		const auto split = Smooth(std::clamp((age - 0.3) / 0.3, 0., 1.)) * 4. * s;
		const auto from = QPointF(baseline.x() - 8. * s, baseline.y() + _rowHeight * 0.05);
		const auto to = QPointF(baseline.x() + box.width() + 8. * s, baseline.y() - _ascent * 1.05);
		auto upper = QPainterPath();
		upper.moveTo(from);
		upper.lineTo(to);
		upper.lineTo(to.x(), to.y() - _rowHeight * 2.);
		upper.lineTo(from.x(), to.y() - _rowHeight * 2.);
		upper.closeSubpath();
		auto lower = QPainterPath();
		lower.moveTo(from);
		lower.lineTo(to);
		lower.lineTo(to.x(), from.y() + _rowHeight);
		lower.lineTo(from.x(), from.y() + _rowHeight);
		lower.closeSubpath();
		const auto color = QColor(255, 255, 255, int(255 * std::max(0.4, progress)));
		for (const auto &[path, offset] : {
				std::pair{ upper, QPointF(-split, -split * 0.5) },
				std::pair{ lower, QPointF(split, split * 0.5) } }) {
			p.save();
			p.setClipPath(path, Qt::IntersectClip);
			p.setPen(color);
			drawText(p, baseline + offset, text);
			p.restore();
		}
		if (cut > 0.) {
			const auto fade = 1. - 0.7 * Smooth(std::clamp((age - 0.6) / 0.6, 0., 1.));
			p.save();
			p.setPen(QPen(QColor(255, 255, 255, int(230 * fade)), 2.4 * s, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(from, from + (to - from) * cut);
			p.restore();
		}
		return true;
	}
	case kFxImpact: {
		const auto center = QPointF(baseline.x() + box.width() / 2., baseline.y() - _ascent * 0.4);
		const auto flash = std::clamp(1. - age / 0.35, 0., 1.);
		const auto wave = std::clamp(age / 0.7, 0., 1.);
		if (flash > 0. || wave < 1.) {
			p.save();
			p.setClipping(false);
			if (flash > 0.) {
				p.fillRect(QRectF(rect()), QColor(255, 255, 255, int(110 * flash * flash)));
			}
			if (wave < 1.) {
				const auto r = (30. + 520. * Smooth(wave)) * s;
				p.setPen(QPen(QColor(255, 255, 255, int(200 * (1. - wave))), (14. * (1. - wave) + 2.) * s));
				p.setBrush(Qt::NoBrush);
				p.drawEllipse(center, r, r);
			}
			p.restore();
		}
		const auto scale = 1. + 0.6 * std::exp(-8. * age);
		p.save();
		p.translate(center);
		p.scale(scale, scale);
		p.translate(-center);
		paintBloom(p, text, baseline, QColor(255, 255, 255), 0.9 * emphasis, seed);
		p.setPen(QColor(255, 255, 255, int(255 * std::max(0.5, progress))));
		drawText(p, baseline, text);
		p.restore();
		return true;
	}
	case kFxLove: {
		const auto warm = Smooth(std::min(1., age / 0.3));
		const auto alpha = int(255 * std::max(0.4, progress));
		auto light = Mix(QColor(255, 255, 255), QColor(255, 175, 208), warm);
		auto deep = Mix(QColor(255, 255, 255), QColor(255, 105, 160), warm);
		light.setAlpha(alpha);
		deep.setAlpha(alpha);
		paintBloom(p, text, baseline, QColor(255, 100, 165), warm * emphasis, seed);
		auto gradient = QLinearGradient(QPointF(0., baseline.y() - _ascent), QPointF(0., baseline.y()));
		gradient.setColorAt(0., light);
		gradient.setColorAt(1., deep);
		p.setPen(QPen(QBrush(gradient), 1.));
		drawText(p, baseline, text);
		return true;
	}
	default:
		return false;
	}
}

void LyricsWindow::paintFxOverlay(
		QPainter &p,
		int fx,
		int number,
		const QString &text,
		const QRectF &box,
		double age,
		double opacity,
		int seed) const {
	const auto s = _rowHeight / 90.;
	const auto draw = 1. - std::pow(1. - std::clamp(age / 0.45, 0., 1.), 3.);
	const auto white = QColor(255, 255, 255, int(235 * opacity));
	const auto top = box.top() + _rowHeight * 0.12;
	const auto bottom = box.top() + _ascent + _rowHeight * 0.1;
	p.save();
	switch (fx) {
	case kFxUnderline: {
		auto path = QPainterPath();
		const auto y = bottom + 4. * s;
		path.moveTo(box.left(), y);
		const auto steps = 18;
		for (auto k = 1; k <= int(steps * draw); ++k) {
			const auto t = double(k) / steps;
			path.lineTo(box.left() + box.width() * t, y + std::sin(t * 6. + seed) * 1.6 * s);
		}
		auto brush = QLinearGradient(QPointF(box.left(), 0.), QPointF(box.right(), 0.));
		brush.setColorAt(0., white);
		brush.setColorAt(1., QColor(255, 255, 255, int(150 * opacity)));
		p.setPen(QPen(QBrush(brush), 3. * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	} break;
	case kFxDashed: {
		const auto y = bottom + 6. * s;
		auto pen = QPen(white, 2.6 * s, Qt::DashLine, Qt::RoundCap);
		pen.setDashPattern({ 3., 2.5 });
		p.setPen(pen);
		p.drawLine(QPointF(box.left(), y), QPointF(box.left() + box.width() * draw, y));
	} break;
	case kFxCircle: {
		auto path = QPainterPath();
		const auto center = QPointF(box.center().x(), (top + bottom) / 2.);
		const auto rx = box.width() / 2. + 14. * s, ry = (bottom - top) / 2. + 10. * s;
		const auto steps = 48;
		for (auto k = 0; k <= int(steps * 1.08 * draw); ++k) {
			const auto a = -2.4 + k * 2. * kPi / steps;
			const auto wobble = 1. + 0.04 * std::sin(k * 0.7 + seed);
			const auto point = center + QPointF(std::cos(a) * rx * wobble, std::sin(a) * ry * wobble);
			if (k) {
				path.lineTo(point);
			} else {
				path.moveTo(point);
			}
		}
		p.setPen(QPen(white, 3. * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	} break;
	case kFxTarget: {
		const auto pad = (1. - draw) * 26. * s + 6. * s;
		const auto frame = QRectF(box.left() - pad, top - pad, box.width() + 2 * pad, bottom - top + 2 * pad);
		const auto corner = 14. * s;
		p.setPen(QPen(white, 3. * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		for (const auto &[x, y, dx, dy] : {
				std::tuple{ frame.left(), frame.top(), 1., 1. },
				std::tuple{ frame.right(), frame.top(), -1., 1. },
				std::tuple{ frame.left(), frame.bottom(), 1., -1. },
				std::tuple{ frame.right(), frame.bottom(), -1., -1. } }) {
			auto path = QPainterPath();
			path.moveTo(x, y + dy * corner);
			path.lineTo(x, y);
			path.lineTo(x + dx * corner, y);
			p.drawPath(path);
		}
		if (draw >= 1.) {
			const auto chipAge = Smooth((age - 0.55) / 0.3);
			const auto font = makeFont(13. * s, QFont::Bold);
			const auto label = QString::fromUtf8("ЦЕЛЬ ЗАХВАЧЕНА");
			const auto chip = QRectF(frame.right() + 10. * s, frame.center().y() - 12. * s, QFontMetricsF(font).horizontalAdvance(label) + 26. * s, 24. * s);
			p.setOpacity(p.opacity() * chipAge);
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 40));
			p.drawRoundedRect(chip.translated((1. - chipAge) * -12. * s, 0.), 8. * s, 8. * s);
			p.setBrush(QColor(255, 60, 70));
			p.drawEllipse(QPointF(chip.left() + 11. * s, chip.center().y()), 3.5 * s, 3.5 * s);
			p.setFont(font);
			p.setPen(white);
			drawText(p, chip.adjusted(18. * s, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
		}
	} break;
	case kFxNumber: {
		{
			auto pen = QPen(white, 2.4 * s, Qt::CustomDashLine, Qt::RoundCap);
			pen.setDashPattern({ 0.1, 2.2 });
			p.setPen(pen);
			p.drawLine(QPointF(box.left(), bottom + 6. * s), QPointF(box.left() + box.width() * draw, bottom + 6. * s));
		}
		const auto pop = std::min(1.12, 1. - std::exp(-8. * age) * std::cos(10. * age));
		const auto font = makeFont(20. * s, QFont::Bold);
		const auto label = QString::number(number);
		const auto chip = QRectF(box.right() - 10. * s, top - 30. * s, QFontMetricsF(font).horizontalAdvance(label) + 22. * s, 34. * s);
		p.translate(chip.center());
		p.scale(std::max(0., pop), std::max(0., pop));
		p.translate(-chip.center());
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, int(240 * opacity)));
		p.drawRoundedRect(chip, 12. * s, 12. * s);
		p.setFont(font);
		p.setPen(QColor(20, 20, 26));
		drawText(p, chip, Qt::AlignCenter, label);
	} break;
	case kFxQuestion:
	case kFxAsk: {
		const auto bob = std::sin(crl::now() / 380. + seed) * 5. * s;
		p.setOpacity(p.opacity() * draw);
		p.setFont(makeFont(30. * s, QFont::Bold));
		p.setPen(white);
		drawText(p, QPointF(box.center().x() + box.width() * 0.2, top - 12. * s + bob - (1. - draw) * 14. * s), u"?"_q);
	} break;
	case kFxEye: {
		const auto center = QPointF(box.right() - 6. * s, top - 16. * s);
		const auto blink = (std::fmod(crl::now() / 1000. + seed * 0.1, 3.2) < 0.14) ? 0.1 : 1.;
		const auto w = 22. * s * draw, h = 13. * s * blink * draw;
		auto eye = QPainterPath();
		eye.moveTo(center + QPointF(-w, 0.));
		eye.quadTo(center + QPointF(0., -h * 1.6), center + QPointF(w, 0.));
		eye.quadTo(center + QPointF(0., h * 1.6), center + QPointF(-w, 0.));
		p.setPen(QPen(white, 2.6 * s));
		p.setBrush(Qt::NoBrush);
		p.drawPath(eye);
		p.setPen(Qt::NoPen);
		p.setBrush(white);
		p.drawEllipse(center, 5.5 * s * blink * draw, 5.5 * s * blink * draw);
	} break;
	case kFxSleep: {
		p.setPen(white);
		for (auto k = 0; k != 3; ++k) {
			const auto phase = std::fmod(crl::now() / 1800. + k / 3., 1.);
			p.setOpacity(opacity * (1. - phase) * draw);
			p.setFont(makeFont((16. + 6. * k) * s, QFont::Bold));
			drawText(p, QPointF(box.right() + (6. + 16. * phase + 6. * k) * s, top - (8. + 40. * phase + 4. * k) * s), u"z"_q);
		}
	} break;
	case kFxTears: {
		p.setPen(Qt::NoPen);
		for (auto k = 0; k != 4; ++k) {
			const auto phase = std::fmod(crl::now() / 1300. + k * 0.27, 1.);
			const auto x = box.left() + box.width() * (0.15 + 0.22 * k);
			const auto y = bottom + 8. * s + phase * 42. * s;
			p.setBrush(QColor(170, 210, 255, int(220 * (1. - phase) * opacity * draw)));
			auto drop = QPainterPath();
			drop.moveTo(x, y - 7. * s);
			drop.quadTo(x + 5. * s, y, x, y + 4. * s);
			drop.quadTo(x - 5. * s, y, x, y - 7. * s);
			p.drawPath(drop);
		}
	} break;
	case kFxCheck: {
		const auto pop = std::min(1.12, 1. - std::exp(-8. * age) * std::cos(10. * age));
		const auto r = 13. * s * std::max(0., pop);
		const auto center = QPointF(box.right() + 24. * s, (top + bottom) / 2.);
		p.setPen(Qt::NoPen);
		p.setBrush(white);
		p.drawEllipse(center, r, r);
		auto check = QPainterPath();
		check.moveTo(center + QPointF(-r * 0.45, 0.));
		check.lineTo(center + QPointF(-r * 0.1, r * 0.35));
		check.lineTo(center + QPointF(r * 0.5, -r * 0.35));
		p.setPen(QPen(QColor(25, 25, 30), 2.6 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(check);
	} break;
	case kFxSink: {
		const auto rise = Smooth((age - 0.3) / 0.5);
		const auto c = QPointF(box.center().x(), top - 18. * s - rise * 8. * s);
		p.setOpacity(p.opacity() * rise * 0.9);
		p.setPen(QPen(white, 2.6 * s, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(c + QPointF(0., -14. * s), c + QPointF(0., 16. * s));
		p.drawLine(c + QPointF(-8. * s, -5. * s), c + QPointF(8. * s, -5. * s));
	} break;
	case kFxLove: {
		p.setPen(Qt::NoPen);
		for (auto k = 0; k != 3; ++k) {
			const auto phase = std::fmod(crl::now() / 1700. + k / 3. + seed * 0.07, 1.);
			const auto x = box.right() - 4. * s + std::sin(phase * 6. + k) * 6. * s + k * 9. * s;
			const auto y = top - 4. * s - phase * 46. * s;
			const auto size = (7. + 3. * k) * s * (0.6 + 0.4 * std::sin(phase * kPi));
			p.setBrush(QColor(255, 110, 170, int(230 * (1. - phase) * draw * opacity)));
			auto heart = QPainterPath();
			heart.moveTo(x, y + size * 0.35);
			heart.cubicTo(x - size, y - size * 0.35, x - size * 0.45, y - size, x, y - size * 0.45);
			heart.cubicTo(x + size * 0.45, y - size, x + size, y - size * 0.35, x, y + size * 0.35);
			p.drawPath(heart);
		}
	} break;
	case kFxSquiggle: {
		auto path = QPainterPath();
		const auto y = bottom + 5. * s;
		const auto period = 9. * s;
		const auto steps = std::max(8, int(box.width() / (2. * s)));
		path.moveTo(box.left(), y);
		for (auto k = 1; k <= int(steps * draw); ++k) {
			const auto x = box.left() + box.width() * double(k) / steps;
			path.lineTo(x, y + std::sin((x - box.left()) / period * kPi) * 2.6 * s);
		}
		p.setPen(QPen(white, 2.2 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	} break;
	case kFxDetect: {
		const auto grow = Smooth(age / 0.3);
		const auto pad = 9. * s;
		const auto frame = QRectF(box.left() - pad, top - pad, box.width() + 2 * pad, bottom - top + 2 * pad);
		const auto zoom = 0.85 + 0.15 * grow;
		const auto shown = QRectF(
			frame.center() - QPointF(frame.width(), frame.height()) * (zoom / 2.),
			frame.size() * zoom);
		p.setOpacity(p.opacity() * grow);
		p.setPen(QPen(white, 2.4 * s));
		p.setBrush(QColor(255, 255, 255, 18));
		p.drawRoundedRect(shown, 10. * s, 10. * s);
		const auto font = makeFont(13. * s, QFont::Bold);
		const auto label = QString::number(std::max(1, number)) + u" "_q + text.toLower();
		const auto chip = QRectF(
			shown.left() + 12. * s,
			shown.top() - 12. * s,
			QFontMetricsF(font).horizontalAdvance(label) + 34. * s,
			22. * s);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, int(235 * opacity)));
		p.drawRoundedRect(chip, 7. * s, 7. * s);
		const auto dark = QColor(25, 25, 32);
		p.setBrush(dark);
		const auto icon = QPointF(chip.left() + 12. * s, chip.center().y());
		p.drawEllipse(icon + QPointF(0., -3. * s), 3. * s, 3. * s);
		p.drawPie(QRectF(icon.x() - 5.5 * s, icon.y() + 1. * s, 11. * s, 10. * s), 0, 180 * 16);
		p.setFont(font);
		p.setPen(dark);
		drawText(p, chip.adjusted(22. * s, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
	} break;
	case kFxName: {
		const auto center = QPointF(box.center().x(), (top + bottom) / 2.);
		const auto wave = std::clamp(age / 0.8, 0., 1.);
		if (wave < 1.) {
			const auto r = (bottom - top) * 0.6 + 150. * s * Smooth(wave);
			p.setPen(QPen(QColor(255, 255, 255, int(150 * (1. - wave) * opacity)), 2. * s));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(center, r, r);
		}
		p.setPen(Qt::NoPen);
		for (auto k = 0; k != 5; ++k) {
			const auto phase = std::fmod(crl::now() / 1600. + k / 5., 1.);
			const auto twinkle = std::sin(phase * kPi);
			const auto x = box.left() - 10. * s + (box.width() + 20. * s) * Hash01(std::uint32_t(seed), std::uint32_t(k), 111);
			const auto y = top - 14. * s + (bottom - top + 26. * s) * Hash01(std::uint32_t(seed), std::uint32_t(k), 112);
			const auto r = (3. + 5. * twinkle) * s;
			p.setBrush(QColor(255, 255, 255, int(230 * twinkle * draw * opacity)));
			auto star = QPainterPath();
			star.moveTo(x, y - r);
			star.quadTo(x, y, x + r, y);
			star.quadTo(x, y, x, y + r);
			star.quadTo(x, y, x - r, y);
			star.quadTo(x, y, x, y - r);
			p.drawPath(star);
		}
		const auto badgeAge = std::max(0., age - 0.15);
		const auto pop = std::min(1.12, 1. - std::exp(-8. * badgeAge) * std::cos(10. * badgeAge));
		const auto r = 11. * s * std::max(0., pop);
		const auto badge = QPointF(box.right() + 14. * s, top - 2. * s);
		p.setBrush(white);
		p.drawEllipse(badge, r, r);
		auto check = QPainterPath();
		check.moveTo(badge + QPointF(-r * 0.45, 0.));
		check.lineTo(badge + QPointF(-r * 0.1, r * 0.35));
		check.lineTo(badge + QPointF(r * 0.5, -r * 0.35));
		p.setPen(QPen(QColor(25, 25, 30), 2.2 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		p.drawPath(check);
	} break;
	case kFxGold:
	case kFxRainbow: {
		p.setPen(Qt::NoPen);
		for (auto k = 0; k != 6; ++k) {
			const auto phase = std::fmod(crl::now() / 1500. + k / 6., 1.);
			const auto twinkle = std::sin(phase * kPi);
			const auto x = box.left() + box.width() * Hash01(std::uint32_t(seed), std::uint32_t(k), 91);
			const auto y = top - 10. * s + (bottom - top + 20. * s) * Hash01(std::uint32_t(seed), std::uint32_t(k), 92);
			const auto r = (3. + 5. * twinkle) * s;
			p.setBrush((fx == kFxGold) ? QColor(255, 225, 140, int(230 * twinkle * draw)) : QColor(255, 255, 255, int(230 * twinkle * draw)));
			auto star = QPainterPath();
			star.moveTo(x, y - r);
			star.quadTo(x, y, x + r, y);
			star.quadTo(x, y, x, y + r);
			star.quadTo(x, y, x - r, y);
			star.quadTo(x, y, x, y - r);
			p.drawPath(star);
		}
	} break;
	default:
		break;
	}
	p.restore();
}

void LyricsWindow::paintWidgetFooter(QPainter &p, const QRectF &rect, const QString &title, const QString &subtitle, double size) const {
	const auto s = _geometry.unit;
	const auto font = makeFont(size * s, QFont::Bold);
	p.setFont(font);
	p.setPen(QColor(255, 255, 255));
	drawText(p, 
		QPointF(rect.left() + 24. * s, rect.bottom() - 42. * s),
		QFontMetrics(font).elidedText(title, Qt::ElideRight, int(rect.width() - 48. * s)));
	const auto small = makeFont(13. * s);
	p.setFont(small);
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, 
		QPointF(rect.left() + 24. * s, rect.bottom() - 20. * s),
		QFontMetrics(small).elidedText(subtitle, Qt::ElideRight, int(rect.width() - 48. * s)));
}

void LyricsWindow::paintToggle(QPainter &p, const QRectF &rect, double on) const {
	const auto radius = rect.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(Mix(QColor(255, 255, 255, 50), QColor(255, 255, 255, 235), on));
	p.drawRoundedRect(rect, radius, radius);
	const auto knob = rect.height() - 6.;
	const auto x = rect.left() + 3. + (rect.width() - knob - 6.) * on;
	p.setBrush(Mix(QColor(255, 255, 255), QColor(30, 30, 36), on));
	p.drawEllipse(QRectF(x, rect.top() + 3., knob, knob));
}

void LyricsWindow::paintAvatar(QPainter &p, QPointF center, double radius, QColor color) const {
	auto fill = QLinearGradient(center - QPointF(0., radius), center + QPointF(0., radius));
	fill.setColorAt(0., color.lighter(130));
	fill.setColorAt(1., color);
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawEllipse(center, radius, radius);
	auto clip = QPainterPath();
	clip.addEllipse(center, radius, radius);
	p.save();
	p.setClipPath(clip);
	p.setBrush(QColor(255, 255, 255, 170));
	p.drawEllipse(center + QPointF(0., -radius * 0.22), radius * 0.36, radius * 0.36);
	p.drawEllipse(center + QPointF(0., radius * 0.85), radius * 0.72, radius * 0.6);
	p.restore();
}

QString LyricsWindow::sceneTitle(const QString &fallback) const {
	auto result = sceneWord();
	if (result.isEmpty()) {
		return fallback;
	}
	const auto space = result.lastIndexOf(QChar(u' '));
	if (space > 0 && result.size() > 16) {
		result = result.mid(space + 1);
		result[0] = result[0].toUpper();
	}
	return result;
}

void LyricsWindow::paintTargetWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("◎"), QColor(255, 255, 255), QString::fromUtf8("КОНТАКТ"), QString());
	const auto center = QPointF(rect.center().x(), rect.top() + rect.height() * 0.43);
	const auto base = rect.height() * 0.19;
	for (auto k = 0; k != 3; ++k) {
		const auto phase = std::fmod(crl::now() / 2000. + k / 3., 1.);
		p.setPen(QPen(QColor(255, 255, 255, int(120 * (1. - phase))), 1.6 * s));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(center, base * (0.7 + 0.9 * phase), base * (0.7 + 0.9 * phase));
	}
	const auto pop = std::min(1.1, 1. - std::exp(-7. * sceneProgress(position, 1000) * 1.) * std::cos(9. * sceneProgress(position, 1000)));
	paintAvatar(p, center, base * 0.62 * std::max(0.2, pop), QColor(150, 155, 170));
	paintWidgetFooter(p, rect, sceneTitle(QString::fromUtf8("Враг")), QString::fromUtf8("Определён · рядом"), 26.);
}

void LyricsWindow::paintBlockWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("⊘"), QColor(255, 255, 255), QString::fromUtf8("БЛОКИРОВКА"), QString());
	const auto on = Smooth((sceneProgress(position, 4000) * 4. - 1.2) / 0.4);
	const auto icon = QPointF(rect.left() + 52. * s, rect.top() + 118. * s);
	p.setPen(QPen(QColor(255, 255, 255), 3.4 * s));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(icon, 22. * s, 22. * s);
	p.drawLine(icon + QPointF(-15. * s, 15. * s), icon + QPointF(15. * s, -15. * s));
	p.setFont(makeFont(28. * s, QFont::Bold));
	drawText(p, QPointF(icon.x() + 36. * s, icon.y() - 2. * s), sceneTitle(QString::fromUtf8("Враг")));
	p.setFont(makeFont(13. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, QPointF(icon.x() + 36. * s, icon.y() + 20. * s), on > 0.5 ? QString::fromUtf8("Контакт заблокирован") : QString::fromUtf8("Контакт"));
	const auto row = QRectF(rect.left() + 18. * s, rect.bottom() - 70. * s, rect.width() - 36. * s, 46. * s);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 30));
	p.drawRoundedRect(row, 14. * s, 14. * s);
	p.setFont(makeFont(15. * s, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, row.adjusted(14. * s, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("Заблокировать"));
	paintToggle(p, QRectF(row.right() - 66. * s, row.center().y() - 15. * s, 56. * s, 30. * s), on);
}

void LyricsWindow::paintCounterWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("✦"), QColor(255, 255, 255), QString::fromUtf8("СЧЁТЧИК"), QString());
	auto target = (_sceneCategory == kEnemy)
		? Lyrics::NormalizedWord(sceneWord().section(QChar(u' '), -1))
		: _data.keyword;
	if (target.isEmpty()) {
		target = _data.keyword;
	}
	auto count = 0;
	auto lastAt = std::int64_t(-1);
	auto shown = QString();
	if (!target.isEmpty()) {
		for (const auto &line : _data.lines) {
			for (const auto &word : line.words) {
				if (word.startMs <= position && Lyrics::NormalizedWord(word.text) == target) {
					++count;
					lastAt = word.startMs;
					shown = word.text.trimmed();
				}
			}
		}
	}
	const auto age = (lastAt >= 0) ? (position - lastAt) / 1000. : 10.;
	const auto pop = 1. + 0.25 * std::exp(-6. * age) * std::sin(12. * age);
	const auto anchor = QPointF(rect.left() + 24. * s, rect.top() + 140. * s);
	p.save();
	p.translate(anchor);
	p.scale(pop, pop);
	p.setFont(makeFont(72. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(), QString::fromUtf8("×") + QString::number(std::max(count, 0)));
	p.restore();
	shown.remove(QRegularExpression(uR"([,.!?…:;"«»]+)"_q));
	if (!shown.isEmpty()) {
		shown[0] = shown[0].toUpper();
	}
	paintWidgetFooter(
		p,
		rect,
		shown.isEmpty() ? QString::fromUtf8("Слово") : shown,
		QString::fromUtf8("упоминаний в этом треке"),
		22.);
}

void LyricsWindow::paintRouteWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	const auto progress = Smooth(sceneProgress(position, 7000));
	paintWidgetHeader(p, rect, QString::fromUtf8("➤"), QColor(255, 255, 255), sceneTitle(QString::fromUtf8("Маршрут")).toUpper(), QString());
	const auto map = QRectF(rect.left() + 16. * s, rect.top() + 66. * s, rect.width() - 32. * s, rect.height() - 82. * s);
	auto clip = QPainterPath();
	clip.addRoundedRect(map, 16. * s, 16. * s);
	p.save();
	p.setClipPath(clip);
	p.fillRect(map, QColor(0, 0, 0, 60));
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 22));
	const auto cell = map.width() / 5.;
	for (auto x = 0; x != 5; ++x) {
		for (auto y = 0; y != 4; ++y) {
			p.drawRoundedRect(QRectF(map.left() + cell * x + 5. * s, map.top() + cell * y * 0.8 + 5. * s, cell - 10. * s, cell * 0.8 - 10. * s), 6. * s, 6. * s);
		}
	}
	const auto points = std::array<QPointF, 5>{
		QPointF(0.1, 0.85), QPointF(0.42, 0.85), QPointF(0.42, 0.5), QPointF(0.82, 0.5), QPointF(0.82, 0.16) };
	auto full = QPainterPath();
	auto lengths = std::array<double, 4>{};
	auto total = 0.;
	for (auto k = 0; k != 5; ++k) {
		const auto point = QPointF(map.left() + map.width() * points[k].x(), map.top() + map.height() * points[k].y());
		if (k) {
			full.lineTo(point);
			const auto prev = QPointF(map.left() + map.width() * points[k - 1].x(), map.top() + map.height() * points[k - 1].y());
			lengths[k - 1] = std::hypot(point.x() - prev.x(), point.y() - prev.y());
			total += lengths[k - 1];
		} else {
			full.moveTo(point);
		}
	}
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(255, 255, 255, 60), 6. * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.drawPath(full);
	auto walked = progress * total;
	auto traveled = QPainterPath();
	auto head = QPointF(map.left() + map.width() * points[0].x(), map.top() + map.height() * points[0].y());
	traveled.moveTo(head);
	for (auto k = 0; k != 4 && walked > 0.; ++k) {
		const auto from = QPointF(map.left() + map.width() * points[k].x(), map.top() + map.height() * points[k].y());
		const auto to = QPointF(map.left() + map.width() * points[k + 1].x(), map.top() + map.height() * points[k + 1].y());
		const auto part = std::min(1., walked / lengths[k]);
		head = from + (to - from) * part;
		traveled.lineTo(head);
		walked -= lengths[k];
	}
	p.setPen(QPen(QColor(255, 255, 255), 6. * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.drawPath(traveled);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 80));
	p.drawEllipse(head, 13. * s, 13. * s);
	p.setBrush(QColor(255, 255, 255));
	p.drawEllipse(head, 7. * s, 7. * s);
	p.restore();
	if (progress >= 1.) {
		const auto age = (position - _sceneStartMs - 7000) / 1000.;
		const auto pop = std::min(1.1, 1. - std::exp(-8. * age) * std::cos(10. * age));
		const auto font = makeFont(14. * s, QFont::Bold);
		const auto label = QString::fromUtf8("✓  Вы прибыли");
		const auto chip = QRectF(map.right() - QFontMetricsF(font).horizontalAdvance(label) - 34. * s, map.bottom() - 40. * s, QFontMetricsF(font).horizontalAdvance(label) + 24. * s, 30. * s);
		p.save();
		p.translate(chip.center());
		p.scale(std::max(0., pop), std::max(0., pop));
		p.translate(-chip.center());
		p.setBrush(QColor(255, 255, 255));
		p.drawRoundedRect(chip, 15. * s, 15. * s);
		p.setFont(font);
		p.setPen(QColor(25, 25, 30));
		drawText(p, chip, Qt::AlignCenter, label);
		p.restore();
	}
}

void LyricsWindow::paintInfinityWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("∞"), QColor(255, 120, 220), QString::fromUtf8("ВОЗРАСТ"), QString());
	const auto center = QPointF(rect.center().x(), rect.top() + rect.height() * 0.44);
	const auto a = rect.width() * 0.26 * (1. + 0.04 * pulse(position));
	auto path = QPainterPath();
	for (auto k = 0; k <= 80; ++k) {
		const auto t = k * 2. * kPi / 80.;
		const auto d = 1. + std::sin(t) * std::sin(t);
		const auto point = center + QPointF(a * std::cos(t) / d, a * std::sin(t) * std::cos(t) / d);
		if (k) {
			path.lineTo(point);
		} else {
			path.moveTo(point);
		}
	}
	auto gradient = QLinearGradient(center - QPointF(a, 0.), center + QPointF(a, 0.));
	const auto shift = std::fmod(crl::now() / 3000., 1.);
	gradient.setColorAt(0., QColor::fromHsvF(std::fmod(0.8 + shift, 1.), 0.55, 1.));
	gradient.setColorAt(0.5, QColor::fromHsvF(std::fmod(0.9 + shift, 1.), 0.5, 1.));
	gradient.setColorAt(1., QColor::fromHsvF(std::fmod(0.7 + shift, 1.), 0.55, 1.));
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QBrush(gradient), 14. * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.drawPath(path);
	const auto line = (_sceneLine >= 0 && _sceneLine < int(_data.lines.size())) ? _data.lines[_sceneLine].text.toLower() : QString();
	paintWidgetFooter(p, rect, QString::fromUtf8("навсегда"), line.contains(u"young"_q) ? u"forever young"_q : QString::fromUtf8("возраст не меняется"), 22.);
}

void LyricsWindow::paintStoppedClockWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("◷"), QColor(255, 180, 90), QString::fromUtf8("ВРЕМЯ"), QString());
	const auto center = QPointF(rect.center().x(), rect.top() + rect.height() * 0.44);
	const auto radius = std::min(rect.width(), rect.height()) * 0.26;
	p.setPen(QPen(QColor(255, 255, 255, 70), 1.6 * s));
	p.setBrush(QColor(255, 255, 255, 18));
	p.drawEllipse(center, radius, radius);
	for (auto k = 0; k != 12; ++k) {
		const auto angle = k * kPi / 6.;
		const auto dir = QPointF(std::sin(angle), -std::cos(angle));
		p.setPen(QPen(QColor(255, 255, 255, 150), 2. * s, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(center + dir * radius * 0.82, center + dir * radius * 0.93);
	}
	const auto frozen = double(_sceneStartMs % 43200000) / 1000.;
	const auto tremble = std::sin(crl::now() / 60.) * 0.004 * (1. + pulse(position));
	const auto hand = [&](double turns, double length, double width, QColor color) {
		const auto angle = turns * 2. * kPi;
		p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(center, center + QPointF(std::sin(angle), -std::cos(angle)) * length);
	};
	hand(frozen / 43200., radius * 0.5, 3.6 * s, QColor(255, 255, 255));
	hand(std::fmod(frozen / 3600., 1.), radius * 0.74, 2.8 * s, QColor(255, 255, 255));
	hand(0.6 + tremble, radius * 0.84, 1.4 * s, QColor(255, 80, 90));
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 80, 90));
	p.drawEllipse(center, 3.6 * s, 3.6 * s);
	paintWidgetFooter(p, rect, QString::fromUtf8("Стоп"), QString::fromUtf8("время остановилось"), 22.);
}

void LyricsWindow::paintAlarmWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("⏰"), QColor(255, 180, 90), QString::fromUtf8("БУДИЛЬНИК"), QString());
	const auto off = Smooth((sceneProgress(position, 3000) * 3. - 1.) / 0.4);
	p.setFont(makeFont(46. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255, int(255 - 120 * off)));
	drawText(p, QPointF(rect.left() + 22. * s, rect.top() + 116. * s), u"07:00"_q);
	paintToggle(p, QRectF(rect.right() - 76. * s, rect.top() + 84. * s, 56. * s, 30. * s), 1. - off);
	const auto row = QRectF(rect.left() + 18. * s, rect.top() + 138. * s, rect.width() - 36. * s, 42. * s);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 26));
	p.drawRoundedRect(row, 13. * s, 13. * s);
	p.setFont(makeFont(14. * s, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, row.adjusted(12. * s, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("☾  Не беспокоить"));
	paintToggle(p, QRectF(row.right() - 60. * s, row.center().y() - 13. * s, 50. * s, 26. * s), Smooth((sceneProgress(position, 3000) * 3. - 1.6) / 0.4));
	const auto note = QRectF(rect.left() + 18. * s, rect.bottom() - 58. * s, rect.width() - 36. * s, 38. * s);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 18));
	p.drawRoundedRect(note, 12. * s, 12. * s);
	p.setFont(makeFont(13. * s));
	p.setPen(QColor(255, 255, 255, 190));
	drawText(p, note, Qt::AlignCenter, QString::fromUtf8("Она спит. Не будить."));
}

void LyricsWindow::paintRemindersWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("☰"), QColor(255, 180, 90), QString::fromUtf8("НАПОМИНАНИЯ"), u"1"_q);
	auto first = sceneWord();
	if (first.isEmpty()) {
		first = QString::fromUtf8("Одно желание");
	}
	const auto rows = std::array<QString, 3>{ first, QString::fromUtf8("Всё остальное"), QString::fromUtf8("Неважно") };
	const auto progress = sceneProgress(position, 3000);
	for (auto k = 0; k != 3; ++k) {
		const auto y = rect.top() + (92. + 52. * k) * s;
		const auto done = (k > 0) ? Smooth((progress * 3. - k * 0.6) / 0.5) : 0.;
		const auto mark = QPointF(rect.left() + 34. * s, y);
		p.setPen(QPen(QColor(255, 180, 90, (k == 0) ? 255 : 120), 2. * s));
		p.setBrush(done > 0.5 ? QBrush(QColor(255, 255, 255, 120)) : QBrush(Qt::NoBrush));
		p.drawEllipse(mark, 9. * s, 9. * s);
		const auto font = makeFont((k == 0 ? 17. : 15.) * s, k == 0 ? QFont::Bold : QFont::Normal);
		p.setFont(font);
		p.setPen(QColor(255, 255, 255, k == 0 ? 255 : int(200 - 100 * done)));
		const auto textLeft = mark.x() + 20. * s;
		const auto shown = QFontMetrics(font).elidedText(rows[k], Qt::ElideRight, int(rect.right() - textLeft - 44. * s));
		drawText(p, QPointF(textLeft, y + 6. * s), shown);
		if (done > 0.) {
			const auto width = QFontMetricsF(font).horizontalAdvance(shown) * done;
			p.setPen(QPen(QColor(255, 255, 255, 170), 1.6 * s));
			p.drawLine(QPointF(textLeft, y), QPointF(textLeft + width, y));
		}
		if (k == 0) {
			p.setFont(makeFont(18. * s, QFont::Bold));
			p.setPen(QColor(255, 196, 90));
			drawText(p, QPointF(rect.right() - 40. * s, y + 7. * s), QString::fromUtf8("★"));
		}
	}
}

void LyricsWindow::paintGeoWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("◉"), QColor(120, 220, 140), QString::fromUtf8("ГЕОПОЗИЦИЯ"), QString());
	const auto progress = Smooth(sceneProgress(position, 8000));
	const auto distance = std::max(0., 38. * (1. - progress));
	const auto center = QPointF(rect.center().x(), rect.top() + rect.height() * 0.45);
	for (auto k = 0; k != 2; ++k) {
		const auto phase = std::fmod(crl::now() / 2200. + k * 0.5, 1.);
		p.setPen(QPen(QColor(255, 255, 255, int(70 * (1. - phase))), 1.4 * s));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(center, rect.width() * 0.32 * phase + 8. * s, rect.width() * 0.32 * phase + 8. * s);
	}
	const auto gap = rect.width() * 0.3 * (distance / 38.);
	const auto you = center - QPointF(gap, 0.);
	const auto her = center + QPointF(gap, 0.);
	p.setFont(makeFont(12. * s, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255, 170));
	if (gap > 14. * s) {
		drawText(p, QRectF(you.x() - 30. * s, you.y() - 34. * s, 60. * s, 16. * s), Qt::AlignCenter, QString::fromUtf8("Ты"));
		drawText(p, QRectF(her.x() - 30. * s, her.y() - 34. * s, 60. * s, 16. * s), Qt::AlignCenter, QString::fromUtf8("Она"));
	}
	p.setPen(QPen(QColor(255, 255, 255), 2.4 * s));
	p.setBrush(QColor(90, 150, 255));
	p.drawEllipse(you, 11. * s, 11. * s);
	p.setBrush(QColor(255, 110, 170));
	p.drawEllipse(her, 11. * s, 11. * s);
	const auto arrived = distance < 1.;
	paintWidgetFooter(
		p,
		rect,
		arrived ? QString::fromUtf8("На руках") : (QString::fromUtf8("Она рядом · ") + QString::number(int(std::ceil(distance))) + QString::fromUtf8(" м")),
		arrived ? QString::fromUtf8("расстояние 0 м") : QString::fromUtf8("приближается"),
		20.);
}

void LyricsWindow::paintCompassWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("⊕"), QColor(255, 90, 90), QString::fromUtf8("КОМПАС"), QString());
	const auto center = QPointF(rect.center().x(), rect.top() + rect.height() * 0.45);
	const auto radius = std::min(rect.width(), rect.height()) * 0.25;
	p.setPen(QPen(QColor(255, 255, 255, 80), 1.6 * s));
	p.setBrush(QColor(0, 0, 0, 50));
	p.drawEllipse(center, radius, radius);
	p.setFont(makeFont(12. * s, QFont::Bold));
	const auto letters = std::array<const char*, 4>{ "С", "В", "Ю", "З" };
	for (auto k = 0; k != 4; ++k) {
		const auto angle = k * kPi / 2.;
		const auto at = center + QPointF(std::sin(angle), -std::cos(angle)) * radius * 0.74;
		p.setPen(k ? QColor(255, 255, 255, 170) : QColor(255, 90, 90));
		drawText(p, QRectF(at.x() - 10. * s, at.y() - 9. * s, 20. * s, 18. * s), Qt::AlignCenter, QString::fromUtf8(letters[k]));
	}
	const auto t = crl::now() / 1000.;
	const auto lost = (_sceneCategory == kMeaning);
	const auto angle = lost ? (std::sin(t * 1.7) * 0.9 + std::sin(t * 4.3) * 0.35) : (0.1 * std::sin(t * 1.3));
	const auto dir = QPointF(std::sin(angle), -std::cos(angle));
	auto north = QPainterPath();
	north.moveTo(center + dir * radius * 0.6);
	north.lineTo(center + QPointF(-dir.y(), dir.x()) * 6. * s);
	north.lineTo(center - QPointF(-dir.y(), dir.x()) * 6. * s);
	north.closeSubpath();
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 80, 90));
	p.drawPath(north);
	auto south = QPainterPath();
	south.moveTo(center - dir * radius * 0.6);
	south.lineTo(center + QPointF(-dir.y(), dir.x()) * 6. * s);
	south.lineTo(center - QPointF(-dir.y(), dir.x()) * 6. * s);
	south.closeSubpath();
	p.setBrush(QColor(255, 255, 255, 200));
	p.drawPath(south);
	paintWidgetFooter(p, rect, lost ? QString::fromUtf8("Смысл") : QString::fromUtf8("Север"), lost ? QString::fromUtf8("смысл: не найден") : QString::fromUtf8("направление верное"), 20.);
}

void LyricsWindow::paintSearchWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("⌕"), QColor(255, 255, 255), QString::fromUtf8("ПОИСК"), QString());
	auto query = sceneWord().toLower();
	if (query.isEmpty()) {
		query = QString::fromUtf8("смысл жизни");
	}
	const auto progress = sceneProgress(position, 5000);
	const auto typed = query.left(int(std::ceil(query.size() * std::min(1., progress * 2.5))));
	const auto field = QRectF(rect.left() + 18. * s, rect.top() + 72. * s, rect.width() - 36. * s, 40. * s);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 34));
	p.drawRoundedRect(field, 12. * s, 12. * s);
	p.setFont(makeFont(15. * s, QFont::DemiBold));
	p.setPen(QColor(255, 255, 255));
	const auto cursor = (std::fmod(crl::now() / 500., 2.) < 1.) ? u"|"_q : QString();
	drawText(p, field.adjusted(14. * s, 0, -10. * s, 0), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("⌕  ") + typed + cursor);
	const auto searching = progress < 0.75;
	const auto center = QPointF(rect.center().x(), rect.top() + rect.height() * 0.6);
	if (searching) {
		for (auto k = 0; k != 8; ++k) {
			const auto angle = k * kPi / 4. + crl::now() / 160.;
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 60 + k * 24));
			p.drawEllipse(center + QPointF(std::cos(angle), std::sin(angle)) * 14. * s, 3. * s, 3. * s);
		}
	}
	paintWidgetFooter(p, rect, searching ? QString::fromUtf8("Ищем…") : QString::fromUtf8("Ничего не найдено"), QString::fromUtf8("0 результатов"), 18.);
}

void LyricsWindow::paintMissedListWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("●"), QColor(120, 220, 140), QString::fromUtf8("СООБЩЕНИЯ"), u"99+"_q);
	const auto texts = std::array<const char*, 3>{ "Ты где?", "Напиши мне", "Позвони" };
	const auto colors = std::array<QColor, 3>{ QColor(90, 150, 255), QColor(255, 120, 170), QColor(255, 180, 90) };
	const auto progress = sceneProgress(position, 2500);
	for (auto k = 0; k != 3; ++k) {
		const auto appear = Smooth((progress * 2.5 - k * 0.5) / 0.6);
		if (appear <= 0.) {
			continue;
		}
		const auto row = QRectF(rect.left() + 16. * s + (1. - appear) * 30. * s, rect.top() + (70. + 58. * k) * s, rect.width() - 32. * s, 50. * s);
		p.setOpacity(appear);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 30));
		p.drawRoundedRect(row, 14. * s, 14. * s);
		paintAvatar(p, QPointF(row.left() + 24. * s, row.center().y()), 14. * s, colors[k]);
		p.setFont(makeFont(14. * s, QFont::Bold));
		p.setPen(QColor(255, 255, 255));
		drawText(p, QPointF(row.left() + 46. * s, row.top() + 21. * s), QString::fromUtf8("Все"));
		p.setFont(makeFont(13. * s));
		p.setPen(QColor(255, 255, 255, 170));
		drawText(p, QPointF(row.left() + 46. * s, row.top() + 39. * s), QString::fromUtf8(texts[k]));
		p.setOpacity(1.);
	}
}

void LyricsWindow::paintPhoneWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("✆"), QColor(255, 80, 90), QString::fromUtf8("ТЕЛЕФОН"), QString());
	const auto count = int(std::round(99. * Smooth(sceneProgress(position, 3000))));
	p.setFont(makeFont(64. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 22. * s, rect.top() + 138. * s), QString::number(count) + ((count >= 99) ? u"+"_q : QString()));
	p.setFont(makeFont(14. * s));
	p.setPen(QColor(255, 255, 255, 150));
	drawText(p, QPointF(rect.left() + 24. * s, rect.top() + 164. * s), QString::fromUtf8("пропущенных"));
	const auto colors = std::array<QColor, 6>{ QColor(90, 150, 255), QColor(255, 120, 170), QColor(255, 180, 90), QColor(150, 110, 255), QColor(90, 220, 170), QColor(255, 90, 110) };
	for (auto k = 0; k != 6; ++k) {
		const auto bob = std::sin(crl::now() / 300. + k) * 2. * s;
		p.setPen(QPen(QColor(30, 30, 40), 2. * s));
		p.setBrush(colors[k]);
		p.drawEllipse(QPointF(rect.left() + (36. + 30. * k) * s, rect.bottom() - 38. * s + bob), 13. * s, 13. * s);
	}
	const auto ring = std::fmod(crl::now() / 900., 1.);
	const auto icon = QPointF(rect.right() - 40. * s, rect.top() + 92. * s);
	p.setPen(QPen(QColor(255, 80, 90, int(150 * (1. - ring))), 2. * s));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(icon, 14. * s + ring * 10. * s, 14. * s + ring * 10. * s);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 80, 90));
	p.drawEllipse(icon, 14. * s, 14. * s);
}

void LyricsWindow::paintStepsWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("⇶"), QColor(120, 220, 140), QString::fromUtf8("ШАГИ"), QString());
	const auto steps = int(std::max<std::int64_t>(0, position) / 1000. * (1.6 + 1.4 * _energySmooth)) + 240;
	auto text = QString::number(steps);
	if (text.size() > 3) {
		text.insert(text.size() - 3, QChar(0x202F));
	}
	p.setFont(makeFont(50. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 22. * s, rect.top() + 124. * s), text);
	const auto area = QRectF(rect.left() + 22. * s, rect.top() + 140. * s, rect.width() - 44. * s, rect.bottom() - 52. * s - rect.top() - 140. * s);
	const auto bars = 18;
	const auto step = area.width() / bars;
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != bars; ++i) {
		const auto at = position - (bars - 1 - i) * 500;
		const auto level = (_analysis && _analysis->ok && at >= 0) ? _analysis->energyAt(at) : 0.3f;
		const auto h = std::max(4. * s, area.height() * (0.15 + 0.85 * level));
		p.setBrush((i == bars - 1) ? QColor(120, 220, 140) : QColor(255, 255, 255, 170));
		p.drawRoundedRect(QRectF(area.left() + step * i + step * 0.2, area.bottom() - h, step * 0.6, h), step * 0.3, step * 0.3);
	}
	p.setFont(makeFont(13. * s));
	p.setPen(QColor(255, 255, 255, 140));
	drawText(p, QPointF(rect.left() + 24. * s, rect.bottom() - 22. * s), QString::fromUtf8("шагов под этот трек"));
}

void LyricsWindow::paintDotsWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("⁘"), QColor(255, 255, 255), QString::fromUtf8("СЧЁТ"), QString());
	auto number = 0;
	if (_sceneLine >= 0 && _sceneLine < int(_data.lines.size())) {
		for (const auto &word : _data.lines[_sceneLine].words) {
			number = std::max(number, NumberValue(Lyrics::NormalizedWord(word.text)));
		}
	}
	if (number <= 0) {
		number = 12;
	}
	const auto shown = int(std::round(number * Smooth(sceneProgress(position, 2500))));
	p.setFont(makeFont(48. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	const auto label = QString::number(shown);
	drawText(p, QPointF(rect.right() - 24. * s - QFontMetricsF(makeFont(48. * s, QFont::Bold)).horizontalAdvance(label), rect.top() + 116. * s), label);
	const auto dots = std::min(number, 60);
	const auto columns = 10;
	const auto cell = (rect.width() - 44. * s) / columns;
	for (auto k = 0; k != dots; ++k) {
		const auto x = rect.left() + 22. * s + cell * (k % columns) + cell / 2.;
		const auto y = rect.top() + 140. * s + cell * (k / columns) + cell / 2.;
		const auto lit = (k * number / std::max(1, dots)) < shown;
		p.setPen(Qt::NoPen);
		p.setBrush(lit ? QColor(255, 255, 255) : QColor(255, 255, 255, 40));
		p.drawEllipse(QPointF(x, y), cell * 0.3, cell * 0.3);
	}
}

void LyricsWindow::paintStopwatchWidget(QPainter &p, const QRectF &rect, std::int64_t position) {
	const auto s = _geometry.unit;
	paintGlass(p, rect, position);
	paintWidgetHeader(p, rect, QString::fromUtf8("◴"), QColor(255, 180, 90), QString::fromUtf8("СЕКУНДОМЕР"), QString());
	const auto at = std::max<std::int64_t>(position, 0);
	const auto text = QString::number(at / 60000) + u":"_q
		+ QString::number((at / 1000) % 60).rightJustified(2, QChar(u'0')) + u"."_q
		+ QString::number((at / 10) % 100).rightJustified(2, QChar(u'0'));
	p.setFont(makeFont(46. * s, QFont::Bold));
	p.setPen(QColor(255, 255, 255));
	drawText(p, QPointF(rect.left() + 22. * s, rect.top() + 116. * s), text);
	auto laps = std::vector<std::pair<int, std::int64_t>>();
	if (_data.synced) {
		for (auto i = 0; i != int(_data.lines.size()); ++i) {
			if (_data.lines[i].endMs <= position) {
				laps.emplace_back(i + 1, _data.lines[i].endMs - _data.lines[i].ms);
			}
		}
	}
	p.setFont(makeFont(13. * s));
	for (auto k = 0; k != 4 && k < int(laps.size()); ++k) {
		const auto &[number, duration] = laps[laps.size() - 1 - k];
		const auto y = rect.top() + (146. + 24. * k) * s;
		p.setPen(QColor(255, 255, 255, k ? 120 : 220));
		drawText(p, QPointF(rect.left() + 24. * s, y), QString::fromUtf8("Круг ") + QString::number(number));
		const auto value = QString::number(duration / 1000) + u"."_q + QString::number((duration / 10) % 100).rightJustified(2, QChar(u'0'));
		drawText(p, QPointF(rect.right() - 24. * s - QFontMetricsF(makeFont(13. * s)).horizontalAdvance(value), y), value);
	}
}

QString LyricsWindow::translationTarget() const {
	const auto id = Lang::GetInstance().id();
	if (id.startsWith(u"ru"_q) || Lang::GetInstance().baseId().startsWith(u"ru"_q)) {
		return u"ru"_q;
	} else if (id.startsWith(u"uk"_q)) {
		return u"uk"_q;
	}
	return u"en"_q;
}

void LyricsWindow::renderTranslation(QPainter &p, const LineLayout &line, QPointF at, QColor color) const {
	const auto s = _geometry.unit;
	auto document = QTextDocument();
	document.setDefaultFont(_translationFont);
	document.setDocumentMargin(0.);
	document.setTextWidth(std::min(_geometry.lyrics.width(), _geometry.nowCard.isEmpty() ? _geometry.lyrics.width() : 640. * s) - 18. * s);
	document.setDefaultStyleSheet(u"b { font-weight: 700; }"_q);
	document.setHtml(line.translationHtml);
	p.save();
	p.translate(at);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(color.red(), color.green(), color.blue(), color.alpha() / 2));
	p.drawRoundedRect(QRectF(0., 2. * s, 3. * s, document.size().height() - 4. * s), 1.5 * s, 1.5 * s);
	p.translate(18. * s, 0.);
	auto context = QAbstractTextDocumentLayout::PaintContext();
	context.palette.setColor(QPalette::Text, color);
	document.documentLayout()->draw(&p, context);
	p.restore();
}

void LyricsWindow::requestTranslation() {
	if (!_everShown) {
		return;
	}
	if (!AyuSettings::getInstance().musicLyricsTranslate() || !_data.hasData || _data.lines.empty()) {
		return;
	}
	const auto target = translationTarget();
	auto lines = std::vector<QString>();
	lines.reserve(_data.lines.size());
	for (const auto &line : _data.lines) {
		lines.push_back(line.text);
	}
	if (Lyrics::LooksLike(lines, target)) {
		return;
	}
	auto keys = std::vector<QString>();
	auto seen = QSet<QString>();
	for (const auto &line : _data.lines) {
		for (const auto &word : line.words) {
			auto number = 0;
			const auto normalized = Lyrics::NormalizedWord(word.text);
			const auto special = word.keyword || word.accent || (wordFx(normalized, number) != kFxNone && !number);
			if (special && normalized.size() >= 3 && !seen.contains(normalized) && keys.size() < 40) {
				seen.insert(normalized);
				keys.push_back(word.text.trimmed());
			}
		}
	}
	static auto cache = std::map<QString, Lyrics::Translation>();
	const auto cacheKey = _artist + u"\n"_q + _title + u"\n"_q + target + u"\n"_q + QString::number(lines.size());
	if (const auto it = cache.find(cacheKey); it != cache.end()) {
		_translation = it->second;
		_layoutSize = QSize();
		return;
	}
	const auto generation = ++_translationGeneration;
	const auto weak = QPointer<LyricsWindow>(this);
	crl::async([=] {
		auto result = Lyrics::TranslateSync(lines, keys, target);
		crl::on_main([=, result = std::move(result)]() mutable {
			const auto strong = weak.data();
			if (!strong || generation != strong->_translationGeneration || !result.ok) {
				return;
			}
			cache[cacheKey] = result;
			strong->_translation = std::move(result);
			strong->_layoutSize = QSize();
			strong->scheduleHiddenLayout();
			strong->update();
		});
	});
}

void LyricsWindow::prewarm() {
	if (_layoutSize != size()) {
		relayout();
	}
	if (_lines.empty()) {
		return;
	}
	const auto active = std::max(0, activeIndex(position() + _lyricsOffset + kScrollLeadMs));
	for (auto i = std::max(0, active - 1); i < std::min(int(_lines.size()), active + 4); ++i) {
		ensureCache(i);
	}
}

void LyricsWindow::prepareTransition() {
	prewarm();
	const auto ratio = style::DevicePixelRatio();
	auto frame = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	frame.setDevicePixelRatio(ratio);
	frame.fill(Qt::transparent);
	{
		auto q = QPainter(&frame);
		paintContent(q);
	}
	auto small = frame.scaled(
		std::max(1, frame.width() / 14),
		std::max(1, frame.height() / 14),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	small.setDevicePixelRatio(1.);
	small = Images::BlurLargeImage(std::move(small), 2);
	auto soft = small.scaled(frame.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	soft.setDevicePixelRatio(ratio);
	_transitionBlur = Ui::PixmapFromImage(std::move(soft));
	_transitionSharp = Ui::PixmapFromImage(std::move(frame));
}

int LyricsWindow::lineAt(QPoint point) const {
	const auto &area = _geometry.lyrics;
	if (!area.contains(point)) {
		return -1;
	}
	const auto anchor = anchorY();
	for (auto i = 0; i != int(_lines.size()); ++i) {
		const auto y = anchor + _lines[i].top - _scroll;
		if (point.y() >= y - 6 && point.y() <= y + _lines[i].height + 6) {
			return i;
		}
	}
	return -1;
}

LyricsWindow::Control LyricsWindow::controlAt(QPoint point) const {
	const auto &g = _geometry;
	const auto at = QPointF(point);
	const auto inflate = [&](const QRectF &r) {
		return r.marginsAdded(QMarginsF(0., 8. * g.unit, 0., 8. * g.unit));
	};
	if (g.close.contains(at)) {
		return Control::Close;
	} else if (!g.playerCard.isEmpty()) {
		if (_profileState && _profileState() >= 0 && g.save.contains(at)) {
			return Control::Save;
		} else if (g.playPause.contains(at)) {
			return Control::PlayPause;
		} else if (g.previous.contains(at)) {
			return Control::Previous;
		} else if (g.next.contains(at)) {
			return Control::Next;
		} else if (g.repeat.contains(at)) {
			return Control::Repeat;
		} else if (g.order.contains(at)) {
			return Control::Order;
		} else if (g.volumeIcon.contains(at)) {
			return Control::VolumeIcon;
		} else if (inflate(g.volume).contains(at)) {
			return Control::Volume;
		} else if (inflate(g.seek).contains(at)) {
			return Control::Seek;
		}
	}
	return (lineAt(point) >= 0 && _data.synced) ? Control::Line : Control::None;
}

void LyricsWindow::updateDrag(QPoint point) {
	const auto &bar = (_pressed == Control::Seek) ? _geometry.seek : _geometry.volume;
	if (bar.width() <= 0.) {
		return;
	}
	_dragValue = std::clamp((point.x() - bar.left()) / bar.width(), 0., 1.);
	if (_pressed == Control::Volume) {
		auto &settings = Core::App().settings();
		if (_dragValue > 0.) {
			settings.setRememberedSongVolume(_dragValue);
		}
		settings.setSongVolume(_dragValue);
		if (const auto mixer = Media::Player::mixer()) {
			mixer->setSongVolume(_dragValue);
		}
	}
	update();
}

void LyricsWindow::activate(Control control, QPoint point) {
	const auto player = Media::Player::instance();
	const auto type = AudioMsgId::Type::Song;
	auto &settings = Core::App().settings();
	switch (control) {
	case Control::Close:
		toggle(false);
		break;
	case Control::Previous:
		player->previous(type);
		break;
	case Control::PlayPause:
		player->playPause(type);
		break;
	case Control::Next:
		player->next(type);
		break;
	case Control::Repeat:
		settings.setPlayerRepeatMode([&] {
			switch (settings.playerRepeatMode()) {
			case Media::RepeatMode::None: return Media::RepeatMode::One;
			case Media::RepeatMode::One: return Media::RepeatMode::All;
			case Media::RepeatMode::All: return Media::RepeatMode::None;
			}
			return Media::RepeatMode::None;
		}());
		Core::App().saveSettingsDelayed();
		break;
	case Control::Order:
		settings.setPlayerOrderMode([&] {
			switch (settings.playerOrderMode()) {
			case Media::OrderMode::Default: return Media::OrderMode::Reverse;
			case Media::OrderMode::Reverse: return Media::OrderMode::Shuffle;
			case Media::OrderMode::Shuffle: return Media::OrderMode::Default;
			}
			return Media::OrderMode::Default;
		}());
		Core::App().saveSettingsDelayed();
		break;
	case Control::Save:
		if (_toggleProfile && _profileState) {
			const auto before = _profileState();
			_toggleProfile();
			const auto track = _artist.isEmpty()
				? _title
				: (_artist + QString::fromUtf8(" — ") + _title);
			if (before == 0) {
				showToast(ToastKind::Saved, QString::fromUtf8("МУЗЫКА"), QString::fromUtf8("Добавлено в профиль"), track);
			} else if (before == 1) {
				showToast(ToastKind::Removed, QString::fromUtf8("МУЗЫКА"), QString::fromUtf8("Убрано из профиля"), track);
			}
		}
		break;
	case Control::VolumeIcon: {
		const auto volume = (settings.songVolume() > 0.) ? 0. : settings.rememberedSongVolume();
		settings.setSongVolume(volume);
		if (const auto mixer = Media::Player::mixer()) {
			mixer->setSongVolume(volume);
		}
		Core::App().saveSettingsDelayed();
	} break;
	case Control::Line: {
		const auto line = lineAt(point);
		if (line >= 0
			&& _data.lines[line].ms >= 0
			&& (QGuiApplication::keyboardModifiers() & Qt::ControlModifier)) {
			setLyricsOffset(_data.lines[line].ms - position() + 60, true);
		} else if (line >= 0 && _seek && _data.lines[line].ms >= 0) {
			_manualOffset = 0.;
			_manualUntil = 0;
			_seek(_data.lines[line].ms);
		}
	} break;
	default:
		break;
	}
	update();
}

void LyricsWindow::wheelEvent(QWheelEvent *e) {
	if (_geometry.lyrics.contains(e->position())) {
		_manualOffset -= e->angleDelta().y() * 0.9;
		_manualUntil = crl::now() + kManualHoldMs;
	}
	e->accept();
}

void LyricsWindow::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_pressed = controlAt(e->pos());
	if (_pressed == Control::Seek || _pressed == Control::Volume) {
		updateDrag(e->pos());
	}
	update();
}

void LyricsWindow::mouseMoveEvent(QMouseEvent *e) {
	if (_pressed == Control::Seek || _pressed == Control::Volume) {
		updateDrag(e->pos());
		return;
	}
	const auto control = controlAt(e->pos());
	const auto line = (control == Control::Line) ? lineAt(e->pos()) : -1;
	if (_hovered != control || _hoveredLine != line) {
		_hovered = control;
		_hoveredLine = line;
		setCursor((control != Control::None) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void LyricsWindow::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, Control::None);
	if (pressed == Control::Seek) {
		if (_seek && _duration > 0 && _dragValue >= 0.) {
			_seek(std::int64_t(_dragValue * _duration));
		}
	} else if (pressed == Control::Volume) {
		Core::App().saveSettingsDelayed();
	} else if (pressed != Control::None && controlAt(e->pos()) == pressed) {
		activate(pressed, e->pos());
	}
	_dragValue = -1.;
	update();
}

void LyricsWindow::leaveEventHook(QEvent *e) {
	if (_pressed == Control::None) {
		_hovered = Control::None;
		_hoveredLine = -1;
		update();
	}
	RpWidget::leaveEventHook(e);
}

void LyricsWindow::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		toggle(false);
		return;
	} else if (e->key() == Qt::Key_Space) {
		Media::Player::instance()->playPause(AudioMsgId::Type::Song);
		return;
	} else if ((e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) && _data.synced) {
		const auto step = (e->modifiers() & Qt::ShiftModifier) ? 500 : 100;
		setLyricsOffset(_lyricsOffset + ((e->key() == Qt::Key_Right) ? step : -step), true);
		return;
	} else if ((e->key() == Qt::Key_0 || e->key() == Qt::Key_Backspace) && _lyricsOffset) {
		setLyricsOffset(0, true);
		return;
	}
	RpWidget::keyPressEvent(e);
}

} // namespace AyuUi
