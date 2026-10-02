// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/custom_profile_background/custom_profile_background.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/whitegram_backend/whitegram_backend.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "crl/crl.h"
#include "data/data_peer.h"
#include "lang/lang_instance.h"
#include "media/clip/media_clip_reader.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_scale.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeDatabase>
#include <QtGui/QImageReader>
#include <QtGui/QBrush>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <optional>

namespace AyuFeatures::CustomProfileBackground {
namespace {

using Zone = ::CustomProfileBackground::Zone;

constexpr auto kMaxImageSide = 4096;
constexpr auto kMaxCoverSide = 4096;
constexpr auto kMaxVideoSide = 1280;
constexpr auto kHeaderOverlayAlpha = 70;
constexpr auto kContentOverlayAlpha = 150;

[[nodiscard]] bool IsRussian() {
	const auto id = Lang::GetInstance().id();
	const auto baseId = Lang::GetInstance().baseId();
	return id.startsWith(u"ru"_q) || baseId.startsWith(u"ru"_q);
}

[[nodiscard]] QString BackgroundsDirectory() {
	return cWorkingDir() + u"tdata/whitegram/profile_bg/"_q;
}

[[nodiscard]] QString ZoneTag(Zone zone) {
	switch (zone) {
	case Zone::Top: return u"top"_q;
	case Zone::Bottom: return u"bottom"_q;
	case Zone::Full: return u"full"_q;
	}
	Unexpected("Zone value in StoreBackgroundFile.");
}

[[nodiscard]] QImage PrepareCoverImage(
		const QImage &source,
		const QSize &target) {
	if (source.isNull() || target.isEmpty()) {
		return QImage();
	}
	auto scaled = source.scaled(
		target,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	const auto left = std::max((scaled.width() - target.width()) / 2, 0);
	const auto top = std::max((scaled.height() - target.height()) / 2, 0);
	auto result = scaled.copy(left, top, target.width(), target.height());
	result.setDevicePixelRatio(1.);
	return result;
}

struct BackgroundSource {
	QString path;
	bool isVideo = false;
};

[[nodiscard]] std::optional<BackgroundSource> ResolveBackground(
		uint64 peerId,
		Zone zone) {
	if (const auto background = AyuSettings::getInstance().customProfileBackground(
			peerId,
			zone)) {
		if (!background->path.isEmpty() && QFile::exists(background->path)) {
			return BackgroundSource{ background->path, background->isVideo };
		}
	}
	const auto path = Whitegram::Backend::CachedBackgroundPath(peerId, zone);
	if (!path.isEmpty() && QFile::exists(path)) {
		return BackgroundSource{
			path,
			Whitegram::Backend::CachedBackgroundIsVideo(peerId, zone),
		};
	}
	return std::nullopt;
}

} // namespace

bool IsVideoFile(const QString &path) {
	const auto mime = QMimeDatabase().mimeTypeForFile(path);
	return mime.isValid() && mime.name().startsWith(u"video/"_q);
}

bool HasBackground(uint64 peerId, Zone zone) {
	return ResolveBackground(peerId, zone).has_value();
}

QString BackgroundFileName(uint64 peerId, Zone zone) {
	if (const auto source = ResolveBackground(peerId, zone)) {
		return QFileInfo(source->path).fileName();
	}
	return QString();
}

std::optional<Zone> TopRendererZone(uint64 peerId) {
	if (HasBackground(peerId, Zone::Top)) {
		return Zone::Top;
	}
	return std::nullopt;
}

std::optional<Zone> BottomRendererZone(uint64 peerId) {
	if (HasBackground(peerId, Zone::Full)) {
		return Zone::Full;
	} else if (HasBackground(peerId, Zone::Bottom)) {
		return Zone::Bottom;
	}
	return std::nullopt;
}

bool UsesFullBackground(uint64 peerId) {
	return HasBackground(peerId, Zone::Full);
}

bool UsesInfoBackground(uint64 peerId) {
	return HasBackground(peerId, Zone::Bottom) && !UsesFullBackground(peerId);
}

bool BackgroundEnabledFor(not_null<PeerData*> peer) {
	return peer->isUser();
}

QString StoreBackgroundFile(
		uint64 peerId,
		Zone zone,
		const QString &sourcePath) {
	const auto info = QFileInfo(sourcePath);
	if (!info.exists() || !info.isFile()) {
		return QString();
	}
	const auto dir = BackgroundsDirectory();
	QDir().mkpath(dir);
	const auto suffix = info.suffix().isEmpty()
		? u"bin"_q
		: info.suffix().toLower().left(10);
	const auto prefix = QString::number(peerId) + u'_' + ZoneTag(zone);
	const auto destination = dir
		+ prefix
		+ u'_'
		+ QString::number(QDateTime::currentMSecsSinceEpoch())
		+ u'.'
		+ suffix;
	if (!QFile::copy(sourcePath, destination)) {
		return QString();
	}
	// A playing video may still hold an old file open, so removal can fail.
	const auto old = QDir(dir).entryInfoList(
		{ prefix + u"*"_q },
		QDir::Files);
	for (const auto &entry : old) {
		if (entry.absoluteFilePath() != QFileInfo(destination).absoluteFilePath()) {
			QFile::remove(entry.absoluteFilePath());
		}
	}
	return destination;
}

void PickBackground(
		not_null<Window::SessionController*> controller,
		uint64 peerId,
		Zone zone) {
	const auto caption = IsRussian()
		? u"Выберите фото или видео"_q
		: u"Choose photo or video"_q;
	const auto dialogParent = QPointer<QWidget>(
		Core::App().getFileDialogParent());
	FileDialog::GetOpenPath(
		dialogParent,
		caption,
		FileDialog::PhotoVideoFilesFilter(),
		crl::guard(controller, [=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty()) {
				return;
			}
			const auto source = result.paths.front();
			if (!QFileInfo(source).isFile()
				|| (!IsVideoFile(source)
					&& !QImageReader(source).canRead())) {
				controller->showToast(IsRussian()
					? u"Выберите фото или видео."_q
					: u"Choose a photo or video."_q);
				return;
			}
			const auto stored = StoreBackgroundFile(peerId, zone, source);
			if (stored.isEmpty()) {
				controller->showToast(IsRussian()
					? u"Не удалось сохранить файл."_q
					: u"Could not save the file."_q);
				return;
			}
			const auto isVideo = IsVideoFile(stored);
			AyuSettings::getInstance().setCustomProfileBackground(
				peerId,
				zone,
				stored,
				isVideo);
			Whitegram::Backend::UploadProfileBackground(
				peerId,
				zone,
				stored,
				isVideo);
		}));
}

void RemoveBackground(uint64 peerId, Zone zone) {
	AyuSettings::getInstance().clearCustomProfileBackground(peerId, zone);
	Whitegram::Backend::RemoveProfileBackground(peerId, zone);
}

void EnsureRemoteBackgrounds(uint64 peerId) {
	Whitegram::Backend::FetchProfileBackgrounds(peerId);
}

void PaintFullOverlay(
		QPainter &p,
		const QRect &target,
		int panelTop,
		int panelHeight,
		int headerHeight,
		const QColor &tint) {
	if (target.isEmpty() || panelHeight <= 0) {
		return;
	}
	auto header = tint;
	header.setAlpha(kHeaderOverlayAlpha);
	auto content = tint;
	content.setAlpha(kContentOverlayAlpha);
	const auto zero = target.y() - panelTop;
	auto gradient = QLinearGradient(
		0,
		zero,
		0,
		zero + std::max(headerHeight, 1));
	gradient.setColorAt(0., header);
	gradient.setColorAt(1., content);
	p.fillRect(target, QBrush(gradient));
}

Renderer::Renderer(Fn<void()> repaint)
: _repaint(std::move(repaint)) {
}

void Renderer::setPaintMetrics(
		int scrollTop,
		int topSkip,
		int contentHeight,
		int paneHeight) {
	if (_scrollTop == scrollTop
		&& _topSkip == topSkip
		&& _contentHeight == contentHeight
		&& _paneHeight == paneHeight) {
		return;
	}
	_scrollTop = scrollTop;
	_topSkip = topSkip;
	_contentHeight = contentHeight;
	_paneHeight = paneHeight;
	_repaint();
}

int Renderer::paintCoverHeight() const {
	// Scaling the image to the whole pane keeps the framing stable while
	// the profile content of any length scrolls over it.
	if (_paneHeight > 0) {
		return _paneHeight;
	}
	return (_contentHeight > 0)
		? (_topSkip + _contentHeight)
		: 0;
}

int Renderer::paintVisibleBottom() const {
	// A pane height means the background should fill the whole pane, so
	// it must not be clipped at the end of the cover block.
	if (_paneHeight > 0) {
		return _paneHeight;
	}
	return (_contentHeight > 0)
		? (_topSkip - _scrollTop + _contentHeight)
		: -1;
}

void Renderer::setPeer(uint64 peerId, Zone zone) {
	const auto background = ResolveBackground(peerId, zone);
	if (!background) {
		clear();
		return;
	}
	if (_peerId == peerId
		&& _zone == zone
		&& _path == background->path
		&& (!_image.isNull() || _video)) {
		return;
	}
	clear();
	_peerId = peerId;
	_zone = zone;
	_path = background->path;
	if (background->isVideo) {
		loadVideo(background->path);
	} else {
		loadImage(background->path);
	}
}

void Renderer::clear() {
	_peerId = 0;
	_path = QString();
	_image = QImage();
	_video = Media::Clip::ReaderPointer();
	_videoPath = QString();
	_lastVideoFrame = QImage();
	_frameAvailable = false;
	invalidateCover();
}

void Renderer::invalidateCover() {
	_coverCache = QImage();
	_coverTarget = QSize();
}

void Renderer::loadImage(const QString &path) {
	auto reader = QImageReader(path);
	reader.setAutoTransform(true);
	const auto size = reader.size();
	if (size.isValid()
		&& (size.width() > kMaxImageSide || size.height() > kMaxImageSide)) {
		reader.setScaledSize(size.scaled(
			kMaxImageSide,
			kMaxImageSide,
			Qt::KeepAspectRatio));
	}
	_image = reader.read();
	if (_image.isNull()) {
		auto fallback = QImage(path);
		if (!fallback.isNull()
			&& std::max(fallback.width(), fallback.height()) > kMaxImageSide) {
			fallback = fallback.scaled(
				kMaxImageSide,
				kMaxImageSide,
				Qt::KeepAspectRatio,
				Qt::SmoothTransformation);
		}
		_image = std::move(fallback);
	}
	invalidateCover();
	if (!_image.isNull()) {
		_repaint();
	} else {
		LOG(("Whitegram Background: could not decode image."));
	}
}

void Renderer::loadVideo(const QString &path) {
	_videoPath = path;
	_frameAvailable = false;
	const auto weak = base::make_weak(this);
	_video = Media::Clip::MakeReader(path, [=](
			Media::Clip::Notification notification) {
		crl::on_main([=] {
			if (const auto strong = weak.get()) {
				strong->handleClipNotification(notification);
			}
		});
	});
}

void Renderer::restartVideo() {
	if (_videoPath.isEmpty()) {
		return;
	}
	const auto path = _videoPath;
	loadVideo(path);
}

void Renderer::handleClipNotification(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case Notification::Reinit:
		if (_video && _video->state() == State::Error) {
			LOG(("Whitegram Background: video decode error, dropping."));
			_video = Media::Clip::ReaderPointer();
			_videoPath = QString();
			_lastVideoFrame = QImage();
			_frameAvailable = false;
			invalidateCover();
		}
		_repaint();
		break;
	case Notification::Repaint:
		if (_video && !_video->currentDisplayed()) {
			_frameAvailable = true;
			_repaint();
		}
		break;
	}
}

Media::Clip::FrameRequest Renderer::videoRequest() const {
	const auto ratio = style::DevicePixelRatio();
	auto size = QSize(
		std::max(_video->width(), 1),
		std::max(_video->height(), 1));
	if (size.width() > kMaxVideoSide || size.height() > kMaxVideoSide) {
		size = size.scaled(kMaxVideoSide, kMaxVideoSide, Qt::KeepAspectRatio);
	}
	return {
		.frame = QSize(
			std::max(size.width() / ratio, 1),
			std::max(size.height() / ratio, 1)),
		.factor = ratio,
	};
}

QImage Renderer::coverForTarget(QSize target) {
	const auto &source = _video ? _lastVideoFrame : _image;
	if (source.isNull() || target.isEmpty()) {
		return QImage();
	}
	if (target != _coverTarget || _coverCache.isNull()) {
		const auto ratio = style::DevicePixelRatio();
		auto pixels = QSize(target.width() * ratio, target.height() * ratio);
		if (pixels.width() > kMaxCoverSide || pixels.height() > kMaxCoverSide) {
			pixels = pixels.scaled(
				kMaxCoverSide,
				kMaxCoverSide,
				Qt::KeepAspectRatio);
		}
		_coverCache = PrepareCoverImage(source, pixels);
		_coverTarget = target;
	}
	return _coverCache;
}

bool Renderer::active() const {
	return !_image.isNull()
		|| (_video && (_video->ready() || !_lastVideoFrame.isNull()));
}

bool Renderer::paint(
		QPainter &p,
		const QRect &rect,
		int coverHeight,
		int sourceY,
		int overlayAlpha) {
	if (!active() || coverHeight <= 0 || rect.isEmpty()) {
		return false;
	}
	const auto target = QSize(rect.width(), coverHeight);
	if (_video) {
		using namespace Media::Clip;
		const auto state = _video->state();
		if (state == State::Error) {
			_video = Media::Clip::ReaderPointer();
			_lastVideoFrame = QImage();
			invalidateCover();
			return false;
		} else if (state == State::Finished) {
			const auto path = _videoPath;
			_video = Media::Clip::ReaderPointer();
			if (!path.isEmpty()) {
				crl::on_main([weak = base::make_weak(this), path] {
					if (const auto strong = weak.get()) {
						strong->loadVideo(path);
					}
				});
			}
		} else if (_video->ready() && !_video->started()) {
			const auto request = videoRequest();
			if (!request.frame.isEmpty()) {
				_video->start(request);
			}
		} else if (_frameAvailable && _video->started()) {
			_frameAvailable = false;
			auto frame = _video->current(videoRequest(), crl::now());
			if (!frame.isNull()) {
				_lastVideoFrame = std::move(frame);
				invalidateCover();
			}
		}
	}
	const auto prepared = coverForTarget(target);
	if (prepared.isNull()) {
		return false;
	}
	const auto sliceTop = std::clamp(sourceY, 0, target.height() - 1);
	const auto sliceHeight = std::min(rect.height(), target.height() - sliceTop);
	if (sliceHeight <= 0) {
		return false;
	}
	const auto scaleX = prepared.width() / float64(target.width());
	const auto scaleY = prepared.height() / float64(target.height());
	const auto destination = QRect(
		rect.x(),
		rect.y(),
		rect.width(),
		sliceHeight);
	const auto source = QRectF(
		0.,
		sliceTop * scaleY,
		rect.width() * scaleX,
		sliceHeight * scaleY);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(QRectF(destination), prepared, source);
	}
	if (overlayAlpha > 0) {
		p.fillRect(destination, QColor(0, 0, 0, overlayAlpha));
	}
	return true;
}

namespace {

class FullBackgroundLayer final : public Ui::RpWidget {
public:
	FullBackgroundLayer(
		not_null<Ui::RpWidget*> host,
		Fn<PeerData*()> peer,
		Fn<bool()> roundTopCorners,
		FullProfileBackgroundBounds bounds);

	[[nodiscard]] base::weak_ptr<Renderer> renderer();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void reload();

	const Fn<PeerData*()> _peer;
	const Fn<bool()> _roundTopCorners;
	const FullProfileBackgroundBounds _bounds;
	Renderer _renderer;

};

FullBackgroundLayer::FullBackgroundLayer(
		not_null<Ui::RpWidget*> host,
		Fn<PeerData*()> peer,
		Fn<bool()> roundTopCorners,
		FullProfileBackgroundBounds bounds)
: RpWidget(host)
, _peer(std::move(peer))
, _roundTopCorners(std::move(roundTopCorners))
, _bounds(std::move(bounds))
, _renderer([=] { update(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	host->sizeValue(
	) | rpl::on_next(crl::guard(host, [=](QSize size) {
		setGeometry(QRect(QPoint(), size));
	}), lifetime());
	AyuSettings::getInstance().customProfileBackgroundChanges(
	) | rpl::on_next([=] {
		reload();
	}, lifetime());
	Whitegram::Backend::profileBackgroundUpdates(
	) | rpl::on_next([=](uint64 userId) {
		const auto peer = _peer ? _peer() : nullptr;
		if (peer && (peer->id.value == userId)) {
			reload();
		}
	}, lifetime());
	lower();
	show();
	reload();
}

base::weak_ptr<Renderer> FullBackgroundLayer::renderer() {
	return base::make_weak(&_renderer);
}

void FullBackgroundLayer::reload() {
	const auto peer = _peer ? _peer() : nullptr;
	if (peer
		&& BackgroundEnabledFor(peer)
		&& UsesFullBackground(peer->id.value)) {
		_renderer.setPeer(peer->id.value, Zone::Full);
	} else {
		_renderer.clear();
	}
	if (const auto parent = parentWidget()) {
		parent->update();
	}
}

void FullBackgroundLayer::paintEvent(QPaintEvent *e) {
	if (!_renderer.active()) {
		return;
	}
	const auto cover = _renderer.paintCoverHeight();
	const auto extent = (cover > 0) ? cover : height();
	const auto limit = _renderer.paintVisibleBottom();
	const auto bottom = (limit >= 0) ? limit : height();
	auto p = QPainter(this);
	if (_roundTopCorners && _roundTopCorners()) {
		const auto radius = st::boxRadius;
		auto path = QPainterPath();
		path.addRoundedRect(
			rect() + QMargins{ 0, 0, 0, radius + 1 },
			radius,
			radius);
		p.setClipPath(path);
	}
	const auto clip = e->rect();
	const auto paintBottom = std::min(clip.bottom() + 1, std::max(bottom, 0));
	if (clip.top() >= paintBottom) {
		return;
	}
	const auto slice = QRect(
		0,
		clip.top(),
		width(),
		paintBottom - clip.top());
	if (_renderer.paint(p, slice, extent, slice.y(), 0)) {
		PaintFullOverlay(
			p,
			slice,
			slice.y(),
			extent,
			st::infoProfileTopBarHeightMax,
			st::windowBg->c);
	}
}

class InfoBackgroundLayer final : public Ui::RpWidget {
public:
	InfoBackgroundLayer(
		not_null<Ui::RpWidget*> host,
		Fn<PeerData*()> peer);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void reload();

	const Fn<PeerData*()> _peer;
	Renderer _renderer;

};

InfoBackgroundLayer::InfoBackgroundLayer(
		not_null<Ui::RpWidget*> host,
		Fn<PeerData*()> peer)
: RpWidget(host)
, _peer(std::move(peer))
, _renderer([=] { update(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	host->sizeValue(
	) | rpl::on_next(crl::guard(host, [=](QSize size) {
		setGeometry(QRect(QPoint(), size));
	}), lifetime());
	AyuSettings::getInstance().customProfileBackgroundChanges(
	) | rpl::on_next([=] {
		reload();
	}, lifetime());
	Whitegram::Backend::profileBackgroundUpdates(
	) | rpl::on_next([=](uint64 userId) {
		const auto peer = _peer ? _peer() : nullptr;
		if (peer && (peer->id.value == userId)) {
			reload();
		}
	}, lifetime());
	lower();
	show();
	reload();
}

void InfoBackgroundLayer::reload() {
	const auto peer = _peer ? _peer() : nullptr;
	if (peer
		&& BackgroundEnabledFor(peer)
		&& UsesInfoBackground(peer->id.value)) {
		_renderer.setPeer(peer->id.value, Zone::Bottom);
	} else {
		_renderer.clear();
	}
	if (const auto parent = parentWidget()) {
		parent->update();
	}
}

void InfoBackgroundLayer::paintEvent(QPaintEvent *e) {
	if (!_renderer.active()) {
		return;
	}
	auto p = QPainter(this);
	const auto clip = e->rect();
	const auto slice = QRect(0, clip.y(), width(), clip.height());
	if (_renderer.paint(p, slice, height(), slice.y(), 0)) {
		auto tint = st::windowBg->c;
		tint.setAlpha(kContentOverlayAlpha);
		p.fillRect(slice, tint);
	}
}

} // namespace

base::weak_ptr<Renderer> SetupFullBackgroundLayer(
		not_null<Ui::RpWidget*> host,
		Fn<PeerData*()> peer,
		Fn<bool()> roundTopCorners,
		FullProfileBackgroundBounds bounds) {
	const auto layer = Ui::CreateChild<FullBackgroundLayer>(
		host.get(),
		std::move(peer),
		std::move(roundTopCorners),
		std::move(bounds));
	return layer->renderer();
}

void AttachInfoBackgroundLayer(
		not_null<Ui::RpWidget*> host,
		Fn<PeerData*()> peer) {
	Ui::CreateChild<InfoBackgroundLayer>(host.get(), std::move(peer));
}

} // namespace AyuFeatures::CustomProfileBackground
