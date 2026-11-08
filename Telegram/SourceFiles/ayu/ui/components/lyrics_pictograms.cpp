// This file is part of Whitegram for Desktop.
#include "ayu/ui/components/lyrics_pictograms.h"

#include <QtGui/QBrush>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPen>
#include <QtGui/QTransform>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace AyuUi::Pictograms {
namespace {

constexpr auto kPi = 3.14159265358979323846;

enum Kind {
	kNone = 0,
	kBone,
	kCigar,
	kMoney,
	kFire,
	kBolt,
	kBroken,
	kStar,
	kSun,
	kMoon,
	kCrown,
	kDiamond,
	kCar,
	kPhone,
	kClock,
	kRose,
	kWings,
	kSkull,
	kBullet,
	kKey,
	kPill,
	kBottle,
	kNote,
	kPlane,
	kGhost,
	kDice,
	kChain,
	kRocket,
	kBalloon,
	kButterfly,
	kCoffee,
	kLetter,
	kCamera,
	kGift,
	kBulb,
	kSnowflake,
};

enum class Motion {
	Pop,
	Drop,
	Fly,
	Glide,
	Shot,
	Launch,
	Rise,
	Swarm,
	Fall,
};

struct Rule {
	int kind = kNone;
	Motion motion = Motion::Pop;
	std::vector<const char*> stems;
};

const std::vector<Rule> &Rules() {
	static const auto result = std::vector<Rule>{
		{ kBone, Motion::Fly, { "кость=", "кости=", "костей=", "костями=", "костях=", "косточк", "bone=", "bones=" } },
		{ kCigar, Motion::Pop, { "труба=", "трубу=", "трубой=", "трубка=", "сигар", "курю=", "курит", "курим", "курил", "курить", "закур", "затяж", "дым=", "дыма=", "дымом", "косяк", "блант", "smoke", "smoking", "cigar", "blunt" } },
		{ kMoney, Motion::Fall, { "деньг", "денег", "бабки=", "бабок=", "бабло", "бабла", "кэш", "налик", "купюр", "доллар", "баксы", "баксов", "money", "cash=", "dollar", "bucks" } },
		{ kFire, Motion::Pop, { "огонь", "огня", "огнем", "горю=", "горит", "горим", "сгор", "пожар", "пламя", "пламен", "fire", "burn", "flame" } },
		{ kBolt, Motion::Pop, { "молни", "гром=", "грома=", "гроза", "разряд", "lightning", "thunder" } },
		{ kBroken, Motion::Pop, { "разбит", "разбил", "broken", "heartbreak" } },
		{ kStar, Motion::Pop, { "звезд", "star=", "stars=" } },
		{ kSun, Motion::Pop, { "солнц", "рассвет", "sun=", "sunshine", "sunrise" } },
		{ kMoon, Motion::Pop, { "луна=", "луну=", "луной=", "луне=", "moon" } },
		{ kCrown, Motion::Drop, { "корон", "король", "короля", "королев", "царь=", "царя=", "царица", "king=", "queen", "crown" } },
		{ kDiamond, Motion::Pop, { "бриллиант", "брилл", "алмаз", "карат", "diamond" } },
		{ kCar, Motion::Pop, { "машин", "тачк", "мерс=", "бмв", "bmw", "ламбо", "порше", "car=", "cars=", "porsche" } },
		{ kPhone, Motion::Pop, { "телефон", "трубку=", "звоню", "позвон", "звонил", "звонк", "phone" } },
		{ kClock, Motion::Pop, { "часы=", "часов=", "время=", "времени=", "минут", "секунд", "clock", "time=" } },
		{ kRose, Motion::Pop, { "роза=", "розы=", "розу=", "розами", "цветок", "цветы=", "цветов=", "букет", "rose=", "roses", "flower" } },
		{ kWings, Motion::Pop, { "крыль", "крылья", "ангел", "angel", "wings" } },
		{ kSkull, Motion::Pop, { "череп", "skull" } },
		{ kBullet, Motion::Shot, { "пуля", "пули=", "пулю=", "пулей", "выстрел", "стреля", "стрельн", "bullet", "shoot", "shot=" } },
		{ kKey, Motion::Pop, { "ключ", "key=", "keys=" } },
		{ kPill, Motion::Pop, { "таблет", "колеса=", "колес=", "pill" } },
		{ kBottle, Motion::Pop, { "бутыл", "водк", "вино=", "вина=", "пиво", "пьян", "бухать", "бухаю", "текил", "виски", "wine", "beer", "vodka", "drunk", "whiskey", "henny" } },
		{ kNote, Motion::Rise, { "музык", "песн", "мелоди", "music", "song=", "songs=", "melody" } },
		{ kPlane, Motion::Glide, { "самолет", "полет", "лечу=", "летим", "улета", "plane", "fly=", "flying" } },
		{ kGhost, Motion::Swarm, { "призрак", "привиден", "ghost" } },
		{ kDice, Motion::Pop, { "казино", "ставк", "кубик", "азарт", "рулетк", "casino", "dice", "gamble" } },
		{ kChain, Motion::Pop, { "цепь=", "цепи=", "цепью=", "цепочк", "chain" } },
		{ kRocket, Motion::Launch, { "ракет", "космос", "rocket", "space=" } },
		{ kBalloon, Motion::Swarm, { "шарик", "шары=", "balloon" } },
		{ kButterfly, Motion::Swarm, { "бабочк", "butterfl" } },
		{ kCoffee, Motion::Pop, { "кофе", "coffee" } },
		{ kLetter, Motion::Glide, { "письм", "конверт", "letter" } },
		{ kCamera, Motion::Pop, { "камер", "фотк", "снимок", "camera" } },
		{ kGift, Motion::Pop, { "подар", "gift" } },
		{ kBulb, Motion::Pop, { "идея", "идеи=", "лампоч", "idea" } },
		{ kSnowflake, Motion::Fall, { "снежин", "снег=", "снега=", "снегом", "снегу=", "snow=", "snowflake" } },
	};
	return result;
}

const Rule *FindRule(int kind) {
	for (const auto &rule : Rules()) {
		if (rule.kind == kind) {
			return &rule;
		}
	}
	return nullptr;
}

double Clamp01(double value) {
	return std::clamp(value, 0., 1.);
}

double Smooth(double t) {
	t = Clamp01(t);
	return t * t * (3. - 2. * t);
}

double Spring(double age) {
	return (age <= 0.) ? 0. : (1. - std::exp(-7. * age) * std::cos(9. * age));
}

double Hash(int seed, int k) {
	auto x = std::uint32_t(seed) * 2654435761u ^ std::uint32_t(k + 1) * 40503u;
	x ^= x >> 13;
	x *= 0x5bd1e995u;
	x ^= x >> 15;
	return (x & 0xFFFFu) / 65535.;
}

QLinearGradient Vertical(double top, double bottom, QColor from, QColor to) {
	auto result = QLinearGradient(QPointF(0., top), QPointF(0., bottom));
	result.setColorAt(0., from);
	result.setColorAt(1., to);
	return result;
}

void Glow(QPainter &p, QPointF center, double radius, QColor color) {
	if (radius <= 0.) {
		return;
	}
	auto gradient = QRadialGradient(center, radius);
	gradient.setColorAt(0., color);
	auto edge = color;
	edge.setAlpha(0);
	gradient.setColorAt(1., edge);
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawEllipse(center, radius, radius);
}

void Fill(QPainter &p, const QPainterPath &path, const QBrush &brush, double S, bool shadow = true) {
	if (shadow) {
		p.save();
		p.translate(0., S * 0.05);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 70));
		p.drawPath(path);
		p.restore();
	}
	p.setPen(Qt::NoPen);
	p.setBrush(brush);
	p.drawPath(path);
}

void Sparkle(QPainter &p, QPointF c, double r, QColor color) {
	if (r <= 0.) {
		return;
	}
	auto star = QPainterPath();
	star.moveTo(c.x(), c.y() - r);
	star.quadTo(c, QPointF(c.x() + r, c.y()));
	star.quadTo(c, QPointF(c.x(), c.y() + r));
	star.quadTo(c, QPointF(c.x() - r, c.y()));
	star.quadTo(c, QPointF(c.x(), c.y() - r));
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPath(star);
}

QPainterPath HeartPath(double S) {
	auto heart = QPainterPath();
	heart.moveTo(0., S * 0.36);
	heart.cubicTo(-S * 0.62, -S * 0.02, -S * 0.27, -S * 0.52, 0., -S * 0.17);
	heart.cubicTo(S * 0.27, -S * 0.52, S * 0.62, -S * 0.02, 0., S * 0.36);
	return heart;
}

QPainterPath StarPath(double outer, double inner, int points) {
	auto star = QPainterPath();
	for (auto k = 0; k != points * 2; ++k) {
		const auto a = -kPi / 2. + k * kPi / points;
		const auto r = (k % 2) ? inner : outer;
		const auto point = QPointF(std::cos(a) * r, std::sin(a) * r);
		if (k) {
			star.lineTo(point);
		} else {
			star.moveTo(point);
		}
	}
	star.closeSubpath();
	return star;
}

void DrawBone(QPainter &p, double S) {
	auto path = QPainterPath();
	path.setFillRule(Qt::WindingFill);
	path.addRoundedRect(QRectF(-S * 0.42, -S * 0.085, S * 0.84, S * 0.17), S * 0.06, S * 0.06);
	for (const auto x : { -S * 0.42, S * 0.42 }) {
		for (const auto y : { -S * 0.1, S * 0.1 }) {
			path.addEllipse(QPointF(x, y), S * 0.12, S * 0.12);
		}
	}
	const auto shape = path.simplified();
	Fill(p, shape, Vertical(-S * 0.22, S * 0.22, QColor(255, 252, 242), QColor(214, 204, 182)), S);
	p.setPen(QPen(QColor(150, 138, 114, 150), S * 0.025));
	p.setBrush(Qt::NoBrush);
	p.drawPath(shape);
	p.setPen(QPen(QColor(255, 255, 255, 170), S * 0.03, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(QPointF(-S * 0.3, -S * 0.04), QPointF(S * 0.28, -S * 0.04));
}

void DrawCigar(QPainter &p, double S, double t, int seed) {
	const auto angle = -14.;
	const auto body = QRectF(-S * 0.52, -S * 0.1, S * 0.9, S * 0.2);
	const auto ash = QRectF(body.right() - S * 0.02, body.top() + S * 0.012, S * 0.1, body.height() - S * 0.024);
	const auto tip = QPointF(ash.right(), body.center().y());
	p.save();
	p.rotate(angle);
	auto path = QPainterPath();
	path.addRoundedRect(body, S * 0.1, S * 0.1);
	Fill(p, path, Vertical(body.top(), body.bottom(), QColor(158, 104, 64), QColor(86, 52, 28)), S);
	p.setPen(QPen(QColor(60, 36, 20, 90), S * 0.012));
	for (auto k = 1; k != 5; ++k) {
		const auto x = body.left() + body.width() * k / 5.;
		p.drawLine(QPointF(x, body.top() + S * 0.03), QPointF(x - S * 0.03, body.bottom() - S * 0.03));
	}
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 50));
	p.drawRoundedRect(QRectF(body.left() + S * 0.05, body.top() + S * 0.025, body.width() - S * 0.1, S * 0.035), S * 0.02, S * 0.02);
	const auto band = QRectF(body.left() + S * 0.16, body.top() - S * 0.006, S * 0.13, body.height() + S * 0.012);
	p.setBrush(Vertical(band.top(), band.bottom(), QColor(255, 228, 140), QColor(190, 132, 36)));
	p.drawRect(band);
	p.setBrush(Vertical(ash.top(), ash.bottom(), QColor(210, 210, 210), QColor(132, 132, 132)));
	p.drawRoundedRect(ash, S * 0.03, S * 0.03);
	const auto pulse = 0.72 + 0.28 * std::sin(t * 4.5 + seed);
	Glow(p, tip, S * 0.24 * pulse, QColor(255, 120, 40, int(170 * pulse)));
	p.setBrush(QColor(255, 180, 90));
	p.drawEllipse(tip, S * 0.03, S * 0.06);
	p.restore();
	const auto radians = angle * kPi / 180.;
	const auto origin = QPointF(
		tip.x() * std::cos(radians) - tip.y() * std::sin(radians),
		tip.x() * std::sin(radians) + tip.y() * std::cos(radians));
	for (auto k = 0; k != 6; ++k) {
		const auto phase = std::fmod(t * 0.42 + k / 6., 1.);
		const auto at = origin + QPointF(
			std::sin(phase * 5. + k + seed) * S * 0.12 + phase * S * 0.18,
			-phase * S * 1.15);
		const auto alpha = int(105 * (1. - phase) * Clamp01(phase * 5.));
		Glow(p, at, S * (0.08 + 0.2 * phase), QColor(232, 232, 240, alpha));
	}
}

void DrawBill(QPainter &p, double S) {
	const auto r = QRectF(-S * 0.45, -S * 0.24, S * 0.9, S * 0.48);
	auto path = QPainterPath();
	path.addRoundedRect(r, S * 0.05, S * 0.05);
	Fill(p, path, Vertical(r.top(), r.bottom(), QColor(160, 226, 170), QColor(64, 152, 96)), S);
	p.setPen(QPen(QColor(232, 255, 236, 150), S * 0.02));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(r.marginsRemoved(QMarginsF(S * 0.05, S * 0.05, S * 0.05, S * 0.05)), S * 0.03, S * 0.03);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(52, 128, 78));
	p.drawEllipse(QPointF(), S * 0.14, S * 0.14);
	auto dollar = QPainterPath();
	dollar.moveTo(S * 0.055, -S * 0.065);
	dollar.cubicTo(-S * 0.08, -S * 0.11, -S * 0.09, -S * 0.01, 0., 0.);
	dollar.cubicTo(S * 0.09, S * 0.01, S * 0.08, S * 0.11, -S * 0.055, S * 0.065);
	p.setPen(QPen(QColor(232, 255, 236), S * 0.03, Qt::SolidLine, Qt::RoundCap));
	p.setBrush(Qt::NoBrush);
	p.drawPath(dollar);
	p.drawLine(QPointF(0., -S * 0.11), QPointF(0., S * 0.11));
}

void DrawFire(QPainter &p, double S, double t, int seed) {
	Glow(p, QPointF(0., S * 0.08), S * 0.6, QColor(255, 120, 40, 120));
	const auto flicker = [&](double k) {
		return 1. + 0.07 * std::sin(t * 13. + k) + 0.04 * std::sin(t * 21. + k * 2.);
	};
	const auto flame = [&](double w, double h, double sway) {
		auto result = QPainterPath();
		result.moveTo(0., S * 0.3);
		result.cubicTo(-w, S * 0.26, -w * 0.95, -h * 0.15, sway, -h);
		result.cubicTo(w * 0.95, -h * 0.15, w, S * 0.26, 0., S * 0.3);
		return result;
	};
	Fill(p, flame(S * 0.32, S * 0.55 * flicker(seed), S * 0.07 * std::sin(t * 7.)),
		Vertical(-S * 0.55, S * 0.3, QColor(255, 206, 90), QColor(232, 56, 40)), S, false);
	Fill(p, flame(S * 0.18, S * 0.3 * flicker(seed + 3.), S * 0.04 * std::sin(t * 9. + 1.)),
		Vertical(-S * 0.3, S * 0.3, QColor(255, 255, 232), QColor(255, 196, 70)), S, false);
	for (auto k = 0; k != 3; ++k) {
		const auto phase = std::fmod(t * 0.9 + k / 3., 1.);
		p.setBrush(QColor(255, 200, 90, int(220 * (1. - phase))));
		p.drawEllipse(QPointF(std::sin(phase * 7. + k) * S * 0.18, -S * 0.35 - phase * S * 0.5), S * 0.025, S * 0.025);
	}
}

void DrawBolt(QPainter &p, double S, double t) {
	const auto flash = 0.6 + 0.4 * std::abs(std::sin(t * 9.)) * ((t < 0.8) ? 1. : 0.4);
	Glow(p, QPointF(), S * 0.62, QColor(255, 230, 120, int(150 * flash)));
	auto bolt = QPainterPath();
	bolt.moveTo(S * 0.08, -S * 0.48);
	bolt.lineTo(-S * 0.22, S * 0.04);
	bolt.lineTo(-S * 0.02, S * 0.04);
	bolt.lineTo(-S * 0.1, S * 0.48);
	bolt.lineTo(S * 0.24, -S * 0.08);
	bolt.lineTo(S * 0.03, -S * 0.08);
	bolt.closeSubpath();
	Fill(p, bolt, Vertical(-S * 0.48, S * 0.48, QColor(255, 252, 200), QColor(255, 186, 40)), S);
}

void DrawBroken(QPainter &p, double S, double t) {
	const auto split = Smooth((t - 0.35) / 0.45);
	const auto heart = HeartPath(S);
	auto crack = QPainterPath();
	crack.moveTo(0., -S * 0.17);
	crack.lineTo(-S * 0.06, -S * 0.02);
	crack.lineTo(S * 0.05, S * 0.08);
	crack.lineTo(-S * 0.03, S * 0.2);
	crack.lineTo(0., S * 0.4);
	auto left = crack;
	left.lineTo(-S, S * 0.4);
	left.lineTo(-S, -S);
	left.lineTo(0., -S);
	left.closeSubpath();
	auto right = crack;
	right.lineTo(S, S * 0.4);
	right.lineTo(S, -S);
	right.lineTo(0., -S);
	right.closeSubpath();
	const auto fill = Vertical(-S * 0.45, S * 0.36, QColor(255, 112, 132), QColor(196, 32, 62));
	for (const auto &[part, side] : { std::pair{ heart.intersected(left), -1. }, std::pair{ heart.intersected(right), 1. } }) {
		p.save();
		p.translate(side * split * S * 0.08, split * S * 0.03);
		p.translate(0., S * 0.36);
		p.rotate(side * 9. * split);
		p.translate(0., -S * 0.36);
		Fill(p, part, fill, S);
		p.restore();
	}
}

void DrawStar(QPainter &p, double S, double t) {
	Glow(p, QPointF(), S * 0.6, QColor(255, 220, 120, 120));
	p.save();
	p.rotate(std::sin(t * 1.5) * 10.);
	const auto star = StarPath(S * 0.42, S * 0.18, 5);
	Fill(p, star, Vertical(-S * 0.42, S * 0.42, QColor(255, 244, 170), QColor(255, 174, 46)), S);
	p.restore();
	Sparkle(p, QPointF(S * 0.34, -S * 0.32), S * (0.06 + 0.05 * std::sin(t * 4.)), QColor(255, 255, 255, 230));
}

void DrawSun(QPainter &p, double S, double t) {
	Glow(p, QPointF(), S * 0.7, QColor(255, 210, 90, 130));
	p.save();
	p.rotate(std::fmod(t * 25., 360.));
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 200, 70, 230));
	for (auto k = 0; k != 12; ++k) {
		auto ray = QPainterPath();
		ray.moveTo(-S * 0.035, -S * 0.3);
		ray.lineTo(0., -S * ((k % 2) ? 0.42 : 0.5));
		ray.lineTo(S * 0.035, -S * 0.3);
		ray.closeSubpath();
		p.drawPath(ray);
		p.rotate(30.);
	}
	p.restore();
	auto disc = QPainterPath();
	disc.addEllipse(QPointF(), S * 0.26, S * 0.26);
	Fill(p, disc, Vertical(-S * 0.26, S * 0.26, QColor(255, 246, 170), QColor(255, 168, 40)), S, false);
}

void DrawMoon(QPainter &p, double S, double t) {
	Glow(p, QPointF(), S * 0.6, QColor(180, 160, 255, 110));
	auto disc = QPainterPath();
	disc.addEllipse(QPointF(), S * 0.32, S * 0.32);
	auto bite = QPainterPath();
	bite.addEllipse(QPointF(S * 0.15, -S * 0.09), S * 0.28, S * 0.28);
	Fill(p, disc.subtracted(bite), Vertical(-S * 0.32, S * 0.32, QColor(252, 250, 255), QColor(196, 192, 236)), S);
	Sparkle(p, QPointF(S * 0.32, -S * 0.3), S * (0.05 + 0.03 * std::sin(t * 3.)), QColor(255, 255, 255, 230));
	Sparkle(p, QPointF(S * 0.42, S * 0.05), S * (0.035 + 0.02 * std::sin(t * 3. + 2.)), QColor(255, 255, 255, 200));
}

void DrawCrown(QPainter &p, double S) {
	const auto w = S * 0.8, h = S * 0.52;
	const auto x = -w / 2., y = -h / 2.;
	auto crown = QPainterPath();
	crown.moveTo(x, y + h);
	crown.lineTo(x, y + h * 0.25);
	crown.lineTo(x + w * 0.25, y + h * 0.58);
	crown.lineTo(x + w * 0.5, y);
	crown.lineTo(x + w * 0.75, y + h * 0.58);
	crown.lineTo(x + w, y + h * 0.25);
	crown.lineTo(x + w, y + h);
	crown.closeSubpath();
	Fill(p, crown, Vertical(y, y + h, QColor(255, 238, 160), QColor(206, 146, 36)), S);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(150, 96, 20, 120));
	p.drawRect(QRectF(x, y + h * 0.8, w, h * 0.08));
	const auto jewels = { std::pair{ QPointF(x + w * 0.5, y + h * 0.62), QColor(255, 70, 100) },
		std::pair{ QPointF(x + w * 0.22, y + h * 0.7), QColor(90, 170, 255) },
		std::pair{ QPointF(x + w * 0.78, y + h * 0.7), QColor(90, 220, 150) } };
	for (const auto &[at, color] : jewels) {
		p.setBrush(color);
		p.drawEllipse(at, S * 0.05, S * 0.05);
		p.setBrush(QColor(255, 255, 255, 170));
		p.drawEllipse(at + QPointF(-S * 0.015, -S * 0.015), S * 0.015, S * 0.015);
	}
	for (const auto &tip : { QPointF(x, y + h * 0.25), QPointF(x + w * 0.5, y), QPointF(x + w, y + h * 0.25) }) {
		p.setBrush(QColor(255, 246, 200));
		p.drawEllipse(tip, S * 0.035, S * 0.035);
	}
}

void DrawDiamond(QPainter &p, double S, double t) {
	Glow(p, QPointF(), S * 0.55, QColor(140, 220, 255, 110));
	auto gem = QPainterPath();
	gem.moveTo(-S * 0.22, -S * 0.3);
	gem.lineTo(S * 0.22, -S * 0.3);
	gem.lineTo(S * 0.38, -S * 0.1);
	gem.lineTo(0., S * 0.42);
	gem.lineTo(-S * 0.38, -S * 0.1);
	gem.closeSubpath();
	auto fill = QLinearGradient(QPointF(-S * 0.38, -S * 0.3), QPointF(S * 0.38, S * 0.42));
	fill.setColorAt(0., QColor(240, 252, 255));
	fill.setColorAt(0.5, QColor(150, 220, 250));
	fill.setColorAt(1., QColor(90, 160, 230));
	Fill(p, gem, fill, S);
	p.setPen(QPen(QColor(255, 255, 255, 150), S * 0.018));
	p.setBrush(Qt::NoBrush);
	p.drawLine(QPointF(-S * 0.38, -S * 0.1), QPointF(S * 0.38, -S * 0.1));
	auto facets = QPainterPath();
	facets.moveTo(-S * 0.22, -S * 0.3);
	facets.lineTo(-S * 0.1, -S * 0.1);
	facets.lineTo(0., S * 0.42);
	facets.lineTo(S * 0.1, -S * 0.1);
	facets.lineTo(S * 0.22, -S * 0.3);
	facets.moveTo(-S * 0.1, -S * 0.1);
	facets.lineTo(0., -S * 0.3);
	facets.lineTo(S * 0.1, -S * 0.1);
	p.drawPath(facets);
	Sparkle(p, QPointF(S * 0.3, -S * 0.34), S * (0.04 + 0.07 * std::abs(std::sin(t * 3.))), QColor(255, 255, 255, 240));
}

void DrawCar(QPainter &p, double S, double t) {
	auto body = QPainterPath();
	body.moveTo(-S * 0.5, S * 0.1);
	body.lineTo(-S * 0.5, -S * 0.02);
	body.quadTo(-S * 0.48, -S * 0.1, -S * 0.36, -S * 0.12);
	body.lineTo(-S * 0.2, -S * 0.13);
	body.quadTo(-S * 0.12, -S * 0.3, S * 0.06, -S * 0.3);
	body.lineTo(S * 0.16, -S * 0.3);
	body.quadTo(S * 0.28, -S * 0.29, S * 0.34, -S * 0.14);
	body.quadTo(S * 0.5, -S * 0.11, S * 0.5, 0.);
	body.lineTo(S * 0.5, S * 0.1);
	body.closeSubpath();
	Fill(p, body, Vertical(-S * 0.3, S * 0.1, QColor(255, 255, 255), QColor(190, 196, 210)), S);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(60, 72, 96, 220));
	auto glass = QPainterPath();
	glass.moveTo(-S * 0.14, -S * 0.14);
	glass.lineTo(-S * 0.06, -S * 0.26);
	glass.lineTo(S * 0.05, -S * 0.26);
	glass.lineTo(S * 0.05, -S * 0.14);
	glass.closeSubpath();
	glass.moveTo(S * 0.09, -S * 0.14);
	glass.lineTo(S * 0.09, -S * 0.26);
	glass.lineTo(S * 0.16, -S * 0.26);
	glass.lineTo(S * 0.26, -S * 0.14);
	glass.closeSubpath();
	p.drawPath(glass);
	Glow(p, QPointF(S * 0.5, -S * 0.03), S * 0.12, QColor(255, 230, 140, 200));
	for (const auto x : { -S * 0.3, S * 0.3 }) {
		const auto c = QPointF(x, S * 0.1);
		p.setBrush(QColor(28, 28, 34));
		p.drawEllipse(c, S * 0.11, S * 0.11);
		p.setBrush(QColor(196, 198, 210));
		p.drawEllipse(c, S * 0.05, S * 0.05);
		p.save();
		p.translate(c);
		p.rotate(std::fmod(t * 400., 360.));
		p.setPen(QPen(QColor(120, 122, 136), S * 0.015));
		p.drawLine(QPointF(-S * 0.05, 0.), QPointF(S * 0.05, 0.));
		p.drawLine(QPointF(0., -S * 0.05), QPointF(0., S * 0.05));
		p.restore();
	}
}

void DrawPhone(QPainter &p, double S, double t) {
	const auto buzzing = std::fmod(t, 1.6) < 0.55;
	if (buzzing) {
		p.setPen(QPen(QColor(255, 255, 255, 170), S * 0.03, Qt::SolidLine, Qt::RoundCap));
		p.setBrush(Qt::NoBrush);
		for (auto k = 0; k != 2; ++k) {
			const auto r = S * (0.32 + 0.1 * k);
			p.drawArc(QRectF(-r, -r, 2 * r, 2 * r), -30 * 16, 60 * 16);
			p.drawArc(QRectF(-r, -r, 2 * r, 2 * r), 150 * 16, 60 * 16);
		}
	}
	p.save();
	p.rotate(buzzing ? std::sin(t * 70.) * 7. : 0.);
	const auto r = QRectF(-S * 0.2, -S * 0.38, S * 0.4, S * 0.76);
	auto body = QPainterPath();
	body.addRoundedRect(r, S * 0.07, S * 0.07);
	Fill(p, body, Vertical(r.top(), r.bottom(), QColor(74, 76, 90), QColor(28, 28, 36)), S);
	const auto screen = r.marginsRemoved(QMarginsF(S * 0.03, S * 0.03, S * 0.03, S * 0.03));
	auto glass = QLinearGradient(screen.topLeft(), screen.bottomRight());
	glass.setColorAt(0., QColor(130, 176, 255));
	glass.setColorAt(1., QColor(176, 124, 255));
	p.setPen(Qt::NoPen);
	p.setBrush(glass);
	p.drawRoundedRect(screen, S * 0.05, S * 0.05);
	p.setBrush(QColor(28, 28, 36));
	p.drawRoundedRect(QRectF(-S * 0.06, screen.top() + S * 0.02, S * 0.12, S * 0.03), S * 0.015, S * 0.015);
	p.setBrush(QColor(255, 255, 255, 200));
	p.drawEllipse(QPointF(0., screen.center().y() + S * 0.06), S * 0.07, S * 0.07);
	p.restore();
}

void DrawClock(QPainter &p, double S, double t) {
	auto face = QPainterPath();
	face.addEllipse(QPointF(), S * 0.38, S * 0.38);
	Fill(p, face, Vertical(-S * 0.38, S * 0.38, QColor(255, 255, 255), QColor(214, 218, 230)), S);
	p.setPen(QPen(QColor(40, 40, 50), S * 0.04));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(QPointF(), S * 0.36, S * 0.36);
	p.setPen(QPen(QColor(40, 40, 50, 200), S * 0.02, Qt::SolidLine, Qt::RoundCap));
	for (auto k = 0; k != 12; ++k) {
		const auto a = k * kPi / 6.;
		const auto inner = (k % 3) ? 0.28 : 0.24;
		p.drawLine(QPointF(std::cos(a), std::sin(a)) * S * inner, QPointF(std::cos(a), std::sin(a)) * S * 0.31);
	}
	const auto hand = [&](double degrees, double length, double width, QColor color) {
		const auto a = (degrees - 90.) * kPi / 180.;
		p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(QPointF(), QPointF(std::cos(a), std::sin(a)) * length);
	};
	hand(t * 30., S * 0.17, S * 0.045, QColor(40, 40, 50));
	hand(t * 240., S * 0.26, S * 0.03, QColor(40, 40, 50));
	hand(t * 720., S * 0.28, S * 0.015, QColor(255, 70, 80));
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 70, 80));
	p.drawEllipse(QPointF(), S * 0.03, S * 0.03);
}

void DrawRose(QPainter &p, double S, double t) {
	p.setPen(QPen(QColor(80, 160, 90), S * 0.045, Qt::SolidLine, Qt::RoundCap));
	p.setBrush(Qt::NoBrush);
	auto stem = QPainterPath();
	stem.moveTo(0., S * 0.46);
	stem.cubicTo(S * 0.06, S * 0.3, -S * 0.05, S * 0.14, 0., -S * 0.02);
	p.drawPath(stem);
	for (const auto side : { -1., 1. }) {
		auto leaf = QPainterPath();
		const auto base = QPointF(side * S * 0.01, S * 0.26);
		leaf.moveTo(base);
		leaf.quadTo(base + QPointF(side * S * 0.12, -S * 0.12), base + QPointF(side * S * 0.24, -S * 0.04));
		leaf.quadTo(base + QPointF(side * S * 0.12, S * 0.06), base);
		Fill(p, leaf, Vertical(S * 0.14, S * 0.3, QColor(130, 210, 130), QColor(60, 140, 70)), S, false);
	}
	const auto bloom = std::min(1.08, Spring(t * 0.8));
	p.save();
	p.translate(0., -S * 0.14);
	p.scale(bloom, bloom);
	const auto petal = Vertical(-S * 0.24, S * 0.2, QColor(255, 120, 140), QColor(176, 22, 56));
	for (auto k = 0; k != 5; ++k) {
		p.save();
		p.rotate(k * 72. + 10.);
		auto path = QPainterPath();
		path.addEllipse(QPointF(0., -S * 0.1), S * 0.13, S * 0.15);
		Fill(p, path, petal, S, k == 0);
		p.restore();
	}
	auto heart = QPainterPath();
	heart.addEllipse(QPointF(), S * 0.11, S * 0.11);
	Fill(p, heart, Vertical(-S * 0.11, S * 0.11, QColor(255, 150, 166), QColor(200, 40, 70)), S, false);
	p.setPen(QPen(QColor(140, 16, 44, 180), S * 0.02, Qt::SolidLine, Qt::RoundCap));
	p.setBrush(Qt::NoBrush);
	p.drawArc(QRectF(-S * 0.07, -S * 0.07, S * 0.14, S * 0.14), 30 * 16, 250 * 16);
	p.restore();
}

void DrawWings(QPainter &p, double S, double t) {
	Glow(p, QPointF(0., -S * 0.08), S * 0.62, QColor(255, 255, 255, 70));
	const auto beat = 12. * std::sin(t * 6.5) - 4.;
	for (const auto side : { -1., 1. }) {
		p.save();
		p.scale(side, 1.);
		p.rotate(beat);
		auto wing = QPainterPath();
		wing.moveTo(S * 0.04, S * 0.02);
		wing.cubicTo(S * 0.06, -S * 0.24, S * 0.3, -S * 0.44, S * 0.58, -S * 0.42);
		wing.cubicTo(S * 0.55, -S * 0.3, S * 0.52, -S * 0.24, S * 0.48, -S * 0.2);
		wing.quadTo(S * 0.53, -S * 0.1, S * 0.41, -S * 0.08);
		wing.quadTo(S * 0.44, S * 0.03, S * 0.31, S * 0.04);
		wing.quadTo(S * 0.31, S * 0.14, S * 0.19, S * 0.13);
		wing.quadTo(S * 0.15, S * 0.2, S * 0.04, S * 0.02);
		wing.closeSubpath();
		Fill(p, wing, Vertical(-S * 0.44, S * 0.2, QColor(255, 255, 255), QColor(204, 210, 232)), S);
		p.setPen(QPen(QColor(168, 176, 204, 170), S * 0.016, Qt::SolidLine, Qt::RoundCap));
		p.setBrush(Qt::NoBrush);
		p.drawLine(QPointF(S * 0.1, -S * 0.05), QPointF(S * 0.46, -S * 0.3));
		p.drawLine(QPointF(S * 0.1, -S * 0.01), QPointF(S * 0.4, -S * 0.12));
		p.drawLine(QPointF(S * 0.1, S * 0.03), QPointF(S * 0.29, S * 0.0));
		p.restore();
	}
}

void DrawSkull(QPainter &p, double S) {
	auto head = QPainterPath();
	head.setFillRule(Qt::WindingFill);
	head.addEllipse(QPointF(0., -S * 0.08), S * 0.32, S * 0.3);
	head.addRoundedRect(QRectF(-S * 0.17, S * 0.06, S * 0.34, S * 0.24), S * 0.07, S * 0.07);
	const auto shape = head.simplified();
	Fill(p, shape, Vertical(-S * 0.38, S * 0.3, QColor(252, 252, 250), QColor(200, 200, 208)), S);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(34, 34, 42));
	p.drawEllipse(QPointF(-S * 0.12, -S * 0.05), S * 0.085, S * 0.1);
	p.drawEllipse(QPointF(S * 0.12, -S * 0.05), S * 0.085, S * 0.1);
	auto nose = QPainterPath();
	nose.moveTo(0., S * 0.04);
	nose.lineTo(-S * 0.04, S * 0.12);
	nose.lineTo(S * 0.04, S * 0.12);
	nose.closeSubpath();
	p.drawPath(nose);
	p.setPen(QPen(QColor(34, 34, 42), S * 0.022, Qt::SolidLine, Qt::RoundCap));
	for (auto k = -2; k <= 2; ++k) {
		p.drawLine(QPointF(k * S * 0.05, S * 0.19), QPointF(k * S * 0.05, S * 0.27));
	}
}

void DrawBullet(QPainter &p, double S) {
	const auto casing = QRectF(-S * 0.32, -S * 0.09, S * 0.36, S * 0.18);
	auto shell = QPainterPath();
	shell.addRoundedRect(casing, S * 0.02, S * 0.02);
	Fill(p, shell, Vertical(casing.top(), casing.bottom(), QColor(255, 222, 130), QColor(176, 128, 44)), S);
	auto tip = QPainterPath();
	tip.moveTo(S * 0.04, -S * 0.09);
	tip.quadTo(S * 0.34, -S * 0.08, S * 0.42, 0.);
	tip.quadTo(S * 0.34, S * 0.08, S * 0.04, S * 0.09);
	tip.closeSubpath();
	Fill(p, tip, Vertical(-S * 0.09, S * 0.09, QColor(240, 166, 116), QColor(160, 84, 52)), S, false);
	p.setPen(QPen(QColor(120, 84, 30, 200), S * 0.02));
	p.drawLine(QPointF(casing.left() + S * 0.04, casing.top()), QPointF(casing.left() + S * 0.04, casing.bottom()));
	p.setPen(QPen(QColor(255, 255, 255, 130), S * 0.02, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(QPointF(casing.left() + S * 0.08, -S * 0.05), QPointF(S * 0.2, -S * 0.05));
}

void DrawKey(QPainter &p, double S, double t) {
	p.save();
	p.rotate(-30. + std::sin(t * 2.) * 8.);
	auto key = QPainterPath();
	key.setFillRule(Qt::WindingFill);
	key.addEllipse(QPointF(-S * 0.25, 0.), S * 0.16, S * 0.16);
	key.addRect(QRectF(-S * 0.12, -S * 0.04, S * 0.52, S * 0.08));
	key.addRect(QRectF(S * 0.24, S * 0.04, S * 0.06, S * 0.1));
	key.addRect(QRectF(S * 0.34, S * 0.04, S * 0.05, S * 0.07));
	auto hole = QPainterPath();
	hole.addEllipse(QPointF(-S * 0.25, 0.), S * 0.07, S * 0.07);
	Fill(p, key.simplified().subtracted(hole), Vertical(-S * 0.16, S * 0.16, QColor(255, 236, 160), QColor(200, 144, 44)), S);
	p.restore();
}

void DrawPill(QPainter &p, double S, double t, int seed) {
	p.save();
	p.rotate(-35. + std::sin(t * 1.6 + seed) * 12.);
	const auto r = QRectF(-S * 0.38, -S * 0.15, S * 0.76, S * 0.3);
	auto capsule = QPainterPath();
	capsule.addRoundedRect(r, S * 0.15, S * 0.15);
	auto left = QPainterPath();
	left.addRect(QRectF(r.left() - 1., r.top() - 1., r.width() / 2. + 1., r.height() + 2.));
	auto right = QPainterPath();
	right.addRect(QRectF(r.center().x(), r.top() - 1., r.width() / 2. + 1., r.height() + 2.));
	Fill(p, capsule, QColor(0, 0, 0, 0), S);
	Fill(p, capsule.intersected(left), Vertical(r.top(), r.bottom(), QColor(255, 120, 120), QColor(206, 44, 60)), S, false);
	Fill(p, capsule.intersected(right), Vertical(r.top(), r.bottom(), QColor(255, 255, 255), QColor(206, 208, 220)), S, false);
	p.setPen(QPen(QColor(255, 255, 255, 170), S * 0.03, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(QPointF(r.left() + S * 0.12, r.top() + S * 0.07), QPointF(r.right() - S * 0.12, r.top() + S * 0.07));
	p.restore();
}

void DrawBottle(QPainter &p, double S, double t) {
	p.save();
	p.rotate(std::sin(t * 1.5) * 8.);
	auto bottle = QPainterPath();
	bottle.moveTo(-S * 0.06, -S * 0.46);
	bottle.lineTo(S * 0.06, -S * 0.46);
	bottle.lineTo(S * 0.06, -S * 0.28);
	bottle.quadTo(S * 0.18, -S * 0.22, S * 0.18, -S * 0.1);
	bottle.lineTo(S * 0.18, S * 0.36);
	bottle.quadTo(S * 0.18, S * 0.44, S * 0.1, S * 0.44);
	bottle.lineTo(-S * 0.1, S * 0.44);
	bottle.quadTo(-S * 0.18, S * 0.44, -S * 0.18, S * 0.36);
	bottle.lineTo(-S * 0.18, -S * 0.1);
	bottle.quadTo(-S * 0.18, -S * 0.22, -S * 0.06, -S * 0.28);
	bottle.closeSubpath();
	auto glass = QLinearGradient(QPointF(-S * 0.18, 0.), QPointF(S * 0.18, 0.));
	glass.setColorAt(0., QColor(46, 120, 70, 235));
	glass.setColorAt(0.45, QColor(120, 206, 140, 235));
	glass.setColorAt(1., QColor(30, 96, 54, 235));
	Fill(p, bottle, glass, S);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(242, 234, 214));
	p.drawRect(QRectF(-S * 0.18, S * 0.02, S * 0.36, S * 0.2));
	p.setBrush(QColor(200, 60, 70));
	p.drawRect(QRectF(-S * 0.18, S * 0.08, S * 0.36, S * 0.04));
	p.setBrush(QColor(220, 180, 80));
	p.drawRoundedRect(QRectF(-S * 0.07, -S * 0.5, S * 0.14, S * 0.07), S * 0.02, S * 0.02);
	p.setBrush(QColor(255, 255, 255, 110));
	p.drawRoundedRect(QRectF(-S * 0.13, -S * 0.12, S * 0.04, S * 0.46), S * 0.02, S * 0.02);
	p.restore();
}

void DrawNote(QPainter &p, double S) {
	auto note = QPainterPath();
	note.setFillRule(Qt::WindingFill);
	auto head = QPainterPath();
	head.addEllipse(QPointF(), S * 0.13, S * 0.09);
	QTransform rotate;
	rotate.translate(-S * 0.1, S * 0.24);
	rotate.rotate(-22.);
	note.addPath(rotate.map(head));
	note.addRect(QRectF(S * 0.0, -S * 0.36, S * 0.045, S * 0.6));
	auto flag = QPainterPath();
	flag.moveTo(S * 0.045, -S * 0.36);
	flag.cubicTo(S * 0.12, -S * 0.26, S * 0.28, -S * 0.22, S * 0.2, -S * 0.04);
	flag.cubicTo(S * 0.22, -S * 0.18, S * 0.12, -S * 0.2, S * 0.045, -S * 0.24);
	flag.closeSubpath();
	note.addPath(flag);
	Fill(p, note.simplified(), Vertical(-S * 0.36, S * 0.33, QColor(255, 255, 255), QColor(214, 210, 236)), S);
}

void DrawPlane(QPainter &p, double S) {
	auto top = QPainterPath();
	top.moveTo(S * 0.45, -S * 0.14);
	top.lineTo(-S * 0.42, -S * 0.02);
	top.lineTo(-S * 0.06, S * 0.08);
	top.closeSubpath();
	auto bottom = QPainterPath();
	bottom.moveTo(S * 0.45, -S * 0.14);
	bottom.lineTo(-S * 0.06, S * 0.08);
	bottom.lineTo(-S * 0.22, S * 0.3);
	bottom.closeSubpath();
	Fill(p, top, Vertical(-S * 0.14, S * 0.08, QColor(255, 255, 255), QColor(230, 232, 240)), S);
	Fill(p, bottom, Vertical(-S * 0.14, S * 0.3, QColor(214, 218, 232), QColor(176, 182, 204)), S, false);
	p.setPen(QPen(QColor(150, 156, 180, 160), S * 0.015));
	p.drawLine(QPointF(S * 0.45, -S * 0.14), QPointF(-S * 0.06, S * 0.08));
}

void DrawGhost(QPainter &p, double S, double t) {
	Glow(p, QPointF(), S * 0.6, QColor(220, 230, 255, 70));
	auto ghost = QPainterPath();
	ghost.moveTo(-S * 0.3, S * 0.3);
	ghost.lineTo(-S * 0.3, -S * 0.08);
	ghost.arcTo(QRectF(-S * 0.3, -S * 0.38, S * 0.6, S * 0.6), 180., -180.);
	ghost.lineTo(S * 0.3, S * 0.3);
	for (auto k = 0; k != 3; ++k) {
		const auto x0 = S * 0.3 - k * S * 0.2;
		const auto wobble = std::sin(t * 5. + k) * S * 0.03;
		ghost.quadTo(QPointF(x0 - S * 0.05, S * 0.38 + wobble), QPointF(x0 - S * 0.1, S * 0.3));
		ghost.quadTo(QPointF(x0 - S * 0.15, S * 0.22 - wobble), QPointF(x0 - S * 0.2, S * 0.3));
	}
	ghost.closeSubpath();
	Fill(p, ghost, Vertical(-S * 0.38, S * 0.36, QColor(255, 255, 255, 240), QColor(210, 216, 240, 220)), S);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(34, 34, 48));
	p.drawEllipse(QPointF(-S * 0.1, -S * 0.06), S * 0.05, S * 0.07);
	p.drawEllipse(QPointF(S * 0.1, -S * 0.06), S * 0.05, S * 0.07);
	p.drawEllipse(QPointF(0., S * 0.08), S * 0.04, S * 0.05);
}

void DrawDice(QPainter &p, double S, double t, int seed) {
	const auto rolling = t < 0.7;
	p.save();
	p.rotate((1. - Smooth(t / 0.7)) * 540. + 8.);
	const auto r = QRectF(-S * 0.3, -S * 0.3, S * 0.6, S * 0.6);
	auto die = QPainterPath();
	die.addRoundedRect(r, S * 0.1, S * 0.1);
	Fill(p, die, Vertical(r.top(), r.bottom(), QColor(255, 255, 255), QColor(206, 210, 222)), S);
	const auto face = rolling ? (1 + int(t * 12.) % 6) : (1 + std::abs(seed) % 6);
	const auto d = S * 0.15;
	auto pips = std::vector<QPointF>();
	if (face % 2) {
		pips.push_back(QPointF());
	}
	if (face >= 2) {
		pips.push_back(QPointF(-d, -d));
		pips.push_back(QPointF(d, d));
	}
	if (face >= 4) {
		pips.push_back(QPointF(d, -d));
		pips.push_back(QPointF(-d, d));
	}
	if (face == 6) {
		pips.push_back(QPointF(-d, 0.));
		pips.push_back(QPointF(d, 0.));
	}
	p.setPen(Qt::NoPen);
	p.setBrush((face == 1) ? QColor(220, 40, 60) : QColor(34, 34, 42));
	for (const auto &pip : pips) {
		p.drawEllipse(pip, S * 0.05, S * 0.05);
	}
	p.restore();
}

void DrawChain(QPainter &p, double S, double t) {
	p.save();
	p.rotate(std::sin(t * 2.) * 10.);
	auto pen = QPen(QBrush(Vertical(-S * 0.2, S * 0.2, QColor(250, 250, 255), QColor(150, 156, 172))), S * 0.07);
	p.setBrush(Qt::NoBrush);
	for (auto k = -1; k <= 1; ++k) {
		p.save();
		p.translate(k * S * 0.28, 0.);
		auto shadow = QPen(QColor(0, 0, 0, 60), S * 0.07);
		p.setPen(shadow);
		const auto link = (k == 0) ? QRectF(-S * 0.08, -S * 0.17, S * 0.16, S * 0.34) : QRectF(-S * 0.2, -S * 0.1, S * 0.4, S * 0.2);
		p.drawRoundedRect(link.translated(0., S * 0.03), S * 0.09, S * 0.09);
		p.setPen(pen);
		p.drawRoundedRect(link, S * 0.09, S * 0.09);
		p.restore();
	}
	p.restore();
}

void DrawRocket(QPainter &p, double S, double t, bool flame) {
	if (flame) {
		const auto flicker = 1. + 0.15 * std::sin(t * 30.);
		auto fire = QPainterPath();
		fire.moveTo(-S * 0.08, S * 0.28);
		fire.quadTo(0., S * (0.28 + 0.36 * flicker), S * 0.08, S * 0.28);
		fire.closeSubpath();
		Glow(p, QPointF(0., S * 0.4), S * 0.3, QColor(255, 140, 50, 150));
		Fill(p, fire, Vertical(S * 0.28, S * 0.64, QColor(255, 250, 200), QColor(255, 110, 40)), S, false);
	}
	for (const auto side : { -1., 1. }) {
		auto fin = QPainterPath();
		fin.moveTo(side * S * 0.1, S * 0.06);
		fin.lineTo(side * S * 0.24, S * 0.3);
		fin.lineTo(side * S * 0.08, S * 0.26);
		fin.closeSubpath();
		Fill(p, fin, Vertical(S * 0.06, S * 0.3, QColor(255, 110, 110), QColor(196, 40, 56)), S);
	}
	auto body = QPainterPath();
	body.moveTo(0., -S * 0.46);
	body.cubicTo(S * 0.16, -S * 0.32, S * 0.13, S * 0.1, S * 0.1, S * 0.28);
	body.lineTo(-S * 0.1, S * 0.28);
	body.cubicTo(-S * 0.13, S * 0.1, -S * 0.16, -S * 0.32, 0., -S * 0.46);
	body.closeSubpath();
	auto metal = QLinearGradient(QPointF(-S * 0.14, 0.), QPointF(S * 0.14, 0.));
	metal.setColorAt(0., QColor(206, 210, 224));
	metal.setColorAt(0.4, QColor(255, 255, 255));
	metal.setColorAt(1., QColor(170, 176, 196));
	Fill(p, body, metal, S);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(60, 90, 140));
	p.drawEllipse(QPointF(0., -S * 0.1), S * 0.07, S * 0.07);
	p.setBrush(QColor(150, 210, 255));
	p.drawEllipse(QPointF(-S * 0.015, -S * 0.115), S * 0.035, S * 0.035);
}

void DrawBalloon(QPainter &p, double S, double t, int seed) {
	auto string = QPainterPath();
	string.moveTo(0., S * 0.22);
	for (auto k = 1; k <= 6; ++k) {
		string.lineTo(std::sin(k * 1.4 + t * 3. + seed) * S * 0.04, S * 0.22 + k * S * 0.06);
	}
	p.setPen(QPen(QColor(255, 255, 255, 170), S * 0.015));
	p.setBrush(Qt::NoBrush);
	p.drawPath(string);
	const auto colors = { QColor(255, 90, 110), QColor(120, 160, 255), QColor(255, 196, 70), QColor(170, 110, 255) };
	const auto color = *(colors.begin() + (std::abs(seed) % 4));
	auto ball = QPainterPath();
	ball.addEllipse(QPointF(0., -S * 0.08), S * 0.24, S * 0.3);
	auto shade = QRadialGradient(QPointF(-S * 0.08, -S * 0.2), S * 0.4);
	shade.setColorAt(0., color.lighter(150));
	shade.setColorAt(1., color.darker(130));
	Fill(p, ball, shade, S);
	p.setPen(Qt::NoPen);
	p.setBrush(color.darker(120));
	auto knot = QPainterPath();
	knot.moveTo(-S * 0.04, S * 0.24);
	knot.lineTo(S * 0.04, S * 0.24);
	knot.lineTo(0., S * 0.2);
	knot.closeSubpath();
	p.drawPath(knot);
	p.setBrush(QColor(255, 255, 255, 150));
	p.drawEllipse(QPointF(-S * 0.09, -S * 0.2), S * 0.05, S * 0.08);
}

void DrawButterfly(QPainter &p, double S, double t, int seed) {
	const auto flap = 0.25 + 0.75 * std::abs(std::cos(t * 9. + seed));
	auto wing = QLinearGradient(QPointF(0., -S * 0.3), QPointF(0., S * 0.3));
	wing.setColorAt(0., QColor(196, 140, 255));
	wing.setColorAt(1., QColor(90, 170, 255));
	for (const auto side : { -1., 1. }) {
		p.save();
		p.scale(side * flap, 1.);
		auto upper = QPainterPath();
		upper.moveTo(0., -S * 0.02);
		upper.cubicTo(S * 0.1, -S * 0.36, S * 0.42, -S * 0.36, S * 0.34, -S * 0.06);
		upper.cubicTo(S * 0.3, S * 0.02, S * 0.12, S * 0.02, 0., -S * 0.02);
		auto lower = QPainterPath();
		lower.moveTo(0., S * 0.02);
		lower.cubicTo(S * 0.24, S * 0.02, S * 0.3, S * 0.24, S * 0.16, S * 0.28);
		lower.cubicTo(S * 0.06, S * 0.3, S * 0.02, S * 0.12, 0., S * 0.02);
		Fill(p, upper, wing, S);
		Fill(p, lower, wing, S, false);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 150));
		p.drawEllipse(QPointF(S * 0.22, -S * 0.16), S * 0.04, S * 0.04);
		p.restore();
	}
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(40, 36, 56));
	p.drawEllipse(QPointF(), S * 0.03, S * 0.18);
	p.setPen(QPen(QColor(40, 36, 56), S * 0.015, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(QPointF(0., -S * 0.16), QPointF(-S * 0.06, -S * 0.28));
	p.drawLine(QPointF(0., -S * 0.16), QPointF(S * 0.06, -S * 0.28));
}

void DrawCoffee(QPainter &p, double S, double t) {
	for (auto k = 0; k != 2; ++k) {
		const auto phase = std::fmod(t * 0.5 + k * 0.5, 1.);
		auto steam = QPainterPath();
		const auto x = (k ? S * 0.07 : -S * 0.07);
		steam.moveTo(x, -S * 0.14);
		steam.cubicTo(x - S * 0.08, -S * 0.24, x + S * 0.08, -S * 0.32, x, -S * 0.44);
		p.setPen(QPen(QColor(255, 255, 255, int(150 * std::sin(phase * kPi))), S * 0.03, Qt::SolidLine, Qt::RoundCap));
		p.setBrush(Qt::NoBrush);
		p.save();
		p.translate(0., -phase * S * 0.08);
		p.drawPath(steam);
		p.restore();
	}
	p.setPen(QPen(QColor(230, 232, 240), S * 0.05));
	p.setBrush(Qt::NoBrush);
	p.drawArc(QRectF(S * 0.14, -S * 0.04, S * 0.2, S * 0.2), -80 * 16, 170 * 16);
	auto cup = QPainterPath();
	cup.moveTo(-S * 0.26, -S * 0.1);
	cup.lineTo(S * 0.26, -S * 0.1);
	cup.lineTo(S * 0.2, S * 0.24);
	cup.quadTo(S * 0.19, S * 0.3, S * 0.12, S * 0.3);
	cup.lineTo(-S * 0.12, S * 0.3);
	cup.quadTo(-S * 0.19, S * 0.3, -S * 0.2, S * 0.24);
	cup.closeSubpath();
	Fill(p, cup, Vertical(-S * 0.1, S * 0.3, QColor(255, 255, 255), QColor(206, 210, 222)), S);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(120, 76, 46));
	p.drawEllipse(QPointF(0., -S * 0.1), S * 0.25, S * 0.05);
	p.setBrush(QColor(255, 255, 255, 70));
	p.drawEllipse(QPointF(0., S * 0.34), S * 0.34, S * 0.05);
}

void DrawLetter(QPainter &p, double S) {
	const auto r = QRectF(-S * 0.4, -S * 0.26, S * 0.8, S * 0.52);
	auto body = QPainterPath();
	body.addRoundedRect(r, S * 0.04, S * 0.04);
	Fill(p, body, Vertical(r.top(), r.bottom(), QColor(255, 255, 255), QColor(214, 218, 232)), S);
	p.setPen(QPen(QColor(170, 176, 200), S * 0.02, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	auto flap = QPainterPath();
	flap.moveTo(r.topLeft());
	flap.lineTo(0., S * 0.04);
	flap.lineTo(r.topRight());
	p.drawPath(flap);
	p.save();
	p.translate(0., S * 0.04);
	p.scale(0.32, 0.32);
	Fill(p, HeartPath(S), Vertical(-S * 0.4, S * 0.36, QColor(255, 120, 140), QColor(200, 30, 60)), S, false);
	p.restore();
}

void DrawCamera(QPainter &p, double S, double t) {
	const auto flash = (std::fmod(t, 2.4) < 0.18) ? (1. - std::fmod(t, 2.4) / 0.18) : 0.;
	if (flash > 0.) {
		Glow(p, QPointF(S * 0.2, -S * 0.22), S * 0.9 * flash + S * 0.2, QColor(255, 255, 255, int(220 * flash)));
	}
	auto top = QPainterPath();
	top.addRoundedRect(QRectF(-S * 0.18, -S * 0.32, S * 0.24, S * 0.12), S * 0.03, S * 0.03);
	const auto r = QRectF(-S * 0.42, -S * 0.24, S * 0.84, S * 0.5);
	auto body = QPainterPath();
	body.addRoundedRect(r, S * 0.08, S * 0.08);
	Fill(p, top.united(body), Vertical(r.top(), r.bottom(), QColor(76, 78, 92), QColor(30, 30, 38)), S);
	auto lens = QRadialGradient(QPointF(-S * 0.04, -S * 0.02), S * 0.2);
	lens.setColorAt(0., QColor(150, 190, 255));
	lens.setColorAt(0.5, QColor(40, 60, 110));
	lens.setColorAt(1., QColor(16, 16, 22));
	p.setPen(QPen(QColor(200, 204, 220), S * 0.025));
	p.setBrush(lens);
	p.drawEllipse(QPointF(0., S * 0.02), S * 0.16, S * 0.16);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 170));
	p.drawEllipse(QPointF(-S * 0.05, -S * 0.03), S * 0.035, S * 0.035);
	p.setBrush(QColor(255, 220, 120));
	p.drawEllipse(QPointF(S * 0.3, -S * 0.15), S * 0.035, S * 0.035);
}

void DrawGift(QPainter &p, double S, double t) {
	const auto hop = std::pow(std::max(0., std::sin(t * 3.4)), 6.) * S * 0.09;
	const auto box = QRectF(-S * 0.3, -S * 0.06, S * 0.6, S * 0.38);
	auto body = QPainterPath();
	body.addRoundedRect(box, S * 0.03, S * 0.03);
	Fill(p, body, Vertical(box.top(), box.bottom(), QColor(255, 110, 120), QColor(190, 34, 56)), S);
	p.setPen(Qt::NoPen);
	p.setBrush(Vertical(box.top(), box.bottom(), QColor(255, 236, 150), QColor(214, 160, 50)));
	p.drawRect(QRectF(-S * 0.05, box.top(), S * 0.1, box.height()));
	p.save();
	p.translate(0., -hop);
	const auto lid = QRectF(-S * 0.34, -S * 0.18, S * 0.68, S * 0.13);
	auto cover = QPainterPath();
	cover.addRoundedRect(lid, S * 0.03, S * 0.03);
	Fill(p, cover, Vertical(lid.top(), lid.bottom(), QColor(255, 130, 140), QColor(206, 50, 70)), S, false);
	p.setBrush(Vertical(lid.top(), lid.bottom(), QColor(255, 236, 150), QColor(214, 160, 50)));
	p.drawRect(QRectF(-S * 0.05, lid.top(), S * 0.1, lid.height()));
	for (const auto side : { -1., 1. }) {
		p.save();
		p.translate(0., lid.top());
		p.rotate(side * 30.);
		p.drawEllipse(QPointF(side * S * 0.09, -S * 0.04), S * 0.1, S * 0.06);
		p.restore();
	}
	p.restore();
}

void DrawBulb(QPainter &p, double S, double t) {
	const auto pulse = 0.75 + 0.25 * std::sin(t * 3.);
	Glow(p, QPointF(0., -S * 0.1), S * 0.62 * pulse, QColor(255, 226, 120, int(160 * pulse)));
	auto glass = QPainterPath();
	glass.addEllipse(QPointF(0., -S * 0.12), S * 0.26, S * 0.26);
	auto neck = QPainterPath();
	neck.addRect(QRectF(-S * 0.1, S * 0.06, S * 0.2, S * 0.08));
	Fill(p, glass.united(neck), Vertical(-S * 0.38, S * 0.14, QColor(255, 252, 220), QColor(255, 214, 110)), S);
	p.setPen(QPen(QColor(255, 140, 40), S * 0.02, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	auto filament = QPainterPath();
	filament.moveTo(-S * 0.06, S * 0.04);
	filament.lineTo(-S * 0.06, -S * 0.08);
	filament.lineTo(-S * 0.02, -S * 0.14);
	filament.lineTo(S * 0.02, -S * 0.08);
	filament.lineTo(S * 0.06, -S * 0.14);
	filament.lineTo(S * 0.06, S * 0.04);
	p.drawPath(filament);
	p.setPen(Qt::NoPen);
	p.setBrush(Vertical(S * 0.14, S * 0.28, QColor(200, 204, 216), QColor(120, 124, 140)));
	p.drawRoundedRect(QRectF(-S * 0.1, S * 0.14, S * 0.2, S * 0.14), S * 0.03, S * 0.03);
	p.setPen(QPen(QColor(90, 94, 110), S * 0.015));
	p.drawLine(QPointF(-S * 0.1, S * 0.19), QPointF(S * 0.1, S * 0.19));
	p.drawLine(QPointF(-S * 0.1, S * 0.23), QPointF(S * 0.1, S * 0.23));
}

void DrawSnowflake(QPainter &p, double S, double t) {
	Glow(p, QPointF(), S * 0.5, QColor(190, 220, 255, 90));
	p.save();
	p.rotate(std::fmod(t * 30., 360.));
	p.setPen(QPen(QColor(250, 252, 255), S * 0.05, Qt::SolidLine, Qt::RoundCap));
	for (auto k = 0; k != 6; ++k) {
		p.drawLine(QPointF(), QPointF(0., -S * 0.4));
		p.drawLine(QPointF(0., -S * 0.24), QPointF(-S * 0.1, -S * 0.34));
		p.drawLine(QPointF(0., -S * 0.24), QPointF(S * 0.1, -S * 0.34));
		p.rotate(60.);
	}
	p.restore();
}

void Draw(QPainter &p, int kind, double S, double t, int seed, bool flag) {
	switch (kind) {
	case kBone: DrawBone(p, S); break;
	case kCigar: DrawCigar(p, S, t, seed); break;
	case kMoney: DrawBill(p, S); break;
	case kFire: DrawFire(p, S, t, seed); break;
	case kBolt: DrawBolt(p, S, t); break;
	case kBroken: DrawBroken(p, S, t); break;
	case kStar: DrawStar(p, S, t); break;
	case kSun: DrawSun(p, S, t); break;
	case kMoon: DrawMoon(p, S, t); break;
	case kCrown: DrawCrown(p, S); break;
	case kDiamond: DrawDiamond(p, S, t); break;
	case kCar: DrawCar(p, S, t); break;
	case kPhone: DrawPhone(p, S, t); break;
	case kClock: DrawClock(p, S, t); break;
	case kRose: DrawRose(p, S, t); break;
	case kWings: DrawWings(p, S, t); break;
	case kSkull: DrawSkull(p, S); break;
	case kBullet: DrawBullet(p, S); break;
	case kKey: DrawKey(p, S, t); break;
	case kPill: DrawPill(p, S, t, seed); break;
	case kBottle: DrawBottle(p, S, t); break;
	case kNote: DrawNote(p, S); break;
	case kPlane: DrawPlane(p, S); break;
	case kGhost: DrawGhost(p, S, t); break;
	case kDice: DrawDice(p, S, t, seed); break;
	case kChain: DrawChain(p, S, t); break;
	case kRocket: DrawRocket(p, S, t, flag); break;
	case kBalloon: DrawBalloon(p, S, t, seed); break;
	case kButterfly: DrawButterfly(p, S, t, seed); break;
	case kCoffee: DrawCoffee(p, S, t); break;
	case kLetter: DrawLetter(p, S); break;
	case kCamera: DrawCamera(p, S, t); break;
	case kGift: DrawGift(p, S, t); break;
	case kBulb: DrawBulb(p, S, t); break;
	case kSnowflake: DrawSnowflake(p, S, t); break;
	default: break;
	}
}

} // namespace

int Find(const QString &normalized) {
	if (normalized.size() < 3) {
		return kNone;
	}
	for (const auto &rule : Rules()) {
		for (const auto stem : rule.stems) {
			auto value = QString::fromUtf8(stem);
			const auto exact = value.endsWith(QChar(u'='));
			if (exact) {
				value.chop(1);
			}
			if (exact ? (normalized == value) : normalized.startsWith(value)) {
				return rule.kind;
			}
		}
	}
	return kNone;
}

void Paint(QPainter &p, int kind, const QRectF &word, double rowHeight, double age, int seed) {
	const auto rule = FindRule(kind);
	if (!rule || age < 0.) {
		return;
	}
	const auto S = rowHeight * 0.6;
	const auto anchor = QPointF(word.right() + S * 0.12, word.top() + rowHeight * 0.06 - S * 0.28);
	const auto base = p.opacity();
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	const auto place = [&](QPointF center, double scale, double angle, double alpha, bool flag = false) {
		alpha = Clamp01(alpha);
		if (alpha <= 0.01 || scale <= 0.01) {
			return;
		}
		p.save();
		p.setOpacity(base * alpha);
		p.translate(center);
		p.rotate(angle);
		p.scale(scale, scale);
		Draw(p, kind, S, age, seed, flag);
		p.restore();
	};
	switch (rule->motion) {
	case Motion::Pop: {
		const auto bob = std::sin(age * 2.2 + seed) * S * 0.05;
		place(anchor + QPointF(0., bob), std::min(1.12, Spring(age)), std::sin(age * 1.4 + seed) * 5., age / 0.12);
	} break;
	case Motion::Drop: {
		const auto land = QPointF(word.left() + word.width() * 0.72, word.top() + rowHeight * 0.1 - S * 0.22);
		const auto fall = Spring(age * 0.9);
		place(land + QPointF(0., -S * 1.6 * (1. - fall)), 1., 14. - 10. * (1. - std::min(1., fall)), age / 0.15);
	} break;
	case Motion::Fly: {
		const auto start = QPointF(word.center().x(), word.top() + rowHeight * 0.2);
		const auto direction = (Hash(seed, 1) > 0.5) ? 1. : -1.;
		const auto pos = start + QPointF(direction * S * 2.4 * age, -S * 4.4 * age + S * 3.4 * age * age);
		const auto alpha = std::min(1., age / 0.08) * (1. - Smooth((age - 1.0) / 0.7));
		place(pos, 0.6 + 0.4 * Smooth(age / 0.25), direction * age * (300. + 160. * Hash(seed, 2)), alpha);
	} break;
	case Motion::Glide: {
		const auto pos = anchor + QPointF(S * 2.6 * age, -S * 0.9 * age + std::sin(age * 4.) * S * 0.12);
		const auto alpha = std::min(1., age / 0.12) * (1. - Smooth((age - 1.2) / 0.8));
		place(pos, std::min(1.1, Spring(age)), -8. + std::sin(age * 4.) * 6., alpha);
	} break;
	case Motion::Shot: {
		const auto start = QPointF(word.right() + S * 0.15, word.top() + rowHeight * 0.45);
		const auto pos = start + QPointF(S * 7. * age, 0.);
		const auto alpha = 1. - Smooth((age - 0.35) / 0.35);
		if (age < 0.18) {
			p.setOpacity(base);
			Glow(p, start, S * 0.5 * (1. - age / 0.18), QColor(255, 220, 140, 220));
		}
		if (alpha > 0.) {
			p.setOpacity(base * alpha);
			for (auto k = 0; k != 3; ++k) {
				const auto y = pos.y() + (k - 1) * S * 0.06;
				const auto length = std::min(pos.x() - start.x(), S * (1.2 - 0.3 * std::abs(k - 1)));
				auto trail = QLinearGradient(QPointF(pos.x() - length, 0.), QPointF(pos.x(), 0.));
				trail.setColorAt(0., QColor(255, 255, 255, 0));
				trail.setColorAt(1., QColor(255, 255, 255, 170));
				p.setPen(QPen(QBrush(trail), S * 0.025, Qt::SolidLine, Qt::RoundCap));
				p.drawLine(QPointF(pos.x() - length, y), QPointF(pos.x() - S * 0.3, y));
			}
		}
		place(pos, 1., 0., alpha);
	} break;
	case Motion::Launch: {
		const auto lift = std::max(0., age - 0.7);
		const auto shake = (age > 0.4 && age < 0.7) ? std::sin(age * 80.) * 3. : 0.;
		const auto pos = anchor + QPointF(0., -S * 2.2 * lift * lift - S * 0.4 * lift);
		place(pos, std::min(1.1, Spring(age)), shake, std::min(1., age / 0.12) * (1. - Smooth((lift - 0.6) / 0.5)), age > 0.45);
	} break;
	case Motion::Rise: {
		for (auto k = 0; k != 3; ++k) {
			const auto phase = std::fmod(age * 0.42 + k / 3., 1.);
			const auto pos = anchor + QPointF(std::sin(phase * 6. + k) * S * 0.22 + k * S * 0.12, S * 0.25 - phase * S * 1.6);
			place(pos, 0.55 + 0.35 * std::sin(phase * kPi), std::sin(phase * 5. + k) * 14., std::sin(phase * kPi) * std::min(1., age / 0.3));
		}
	} break;
	case Motion::Swarm: {
		const auto pos = anchor + QPointF(std::sin(age * 1.6 + seed) * S * 0.18, -S * 0.45 * Smooth(age / 1.2) + std::sin(age * 2.3) * S * 0.06);
		place(pos, std::min(1.1, Spring(age)), std::sin(age * 1.8 + seed) * 9., age / 0.2);
	} break;
	case Motion::Fall: {
		for (auto k = 0; k != 3; ++k) {
			const auto phase = std::fmod(age * 0.38 + k / 3., 1.);
			const auto pos = QPointF(
				word.left() + word.width() * (0.15 + 0.35 * k) + std::sin(phase * 7. + k) * S * 0.2,
				word.top() - S * 1.1 + phase * S * 1.7);
			place(pos, 0.6, std::sin(age * 3. + k) * 28., std::sin(phase * kPi) * std::min(1., age / 0.3));
		}
	} break;
	}
	p.restore();
}

} // namespace AyuUi::Pictograms
