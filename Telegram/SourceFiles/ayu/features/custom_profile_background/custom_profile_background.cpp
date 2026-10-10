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
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "crl/crl.h"
#include "data/data_peer.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "media/streaming/media_streaming_document.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_loader_local.h"
#include "media/streaming/media_streaming_player.h"
#include "ui/boxes/confirm_box.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_scale.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

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
// Above Whitegram::Backend::kMaxProfileBackgroundSize, older versions
// allowed bigger local backgrounds.
constexpr auto kMaxVideoInMemory = 64 * 1024 * 1024;
constexpr auto kHeaderOverlayAlpha = 70;
constexpr auto kContentOverlayAlpha = 150;
constexpr auto kSeeThroughOverOpacity = 0.6;
constexpr auto kSeeThroughDividerOpacity = 0.5;
constexpr auto kHostObjectName = "whitegramProfileBackgroundHost";

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

[[nodiscard]] QColor WithOpacity(QColor color, float64 opacity) {
	color.setAlphaF(color.alphaF() * opacity);
	return color;
}

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

QString BackgroundFilePath(uint64 peerId, Zone zone) {
	if (const auto source = ResolveBackground(peerId, zone)) {
		return source->path;
	}
	return QString();
}

bool BackgroundIsVideo(uint64 peerId, Zone zone) {
	const auto source = ResolveBackground(peerId, zone);
	return source && source->isVideo;
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

rpl::producer<> BackgroundChanges(uint64 peerId) {
	return rpl::merge(
		AyuSettings::getInstance().customProfileBackgroundChanges(),
		Whitegram::Backend::profileBackgroundUpdates(
		) | rpl::filter(rpl::mappers::_1 == peerId) | rpl::to_empty);
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
			} else if (QFileInfo(source).size()
				> Whitegram::Backend::kMaxProfileBackgroundUploadSize) {
				controller->showToast(IsRussian()
					? u"Файл должен быть меньше 32 МБ."_q
					: u"The file must be smaller than 32 MB."_q);
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
			controller->showToast(IsRussian()
				? u"Фон профиля обновлён."_q
				: u"Profile background updated."_q);
		}));
}

void RemoveBackground(uint64 peerId, Zone zone) {
	// The server copy goes first. Clearing the local one first would make
	// everything showing the background load the cached server copy again
	// for a moment, and a video reader holding that file open could stop it
	// from being deleted.
	Whitegram::Backend::RemoveProfileBackground(peerId, zone);
	AyuSettings::getInstance().clearCustomProfileBackground(peerId, zone);
}

void ConfirmRemoveBackground(
		not_null<Window::SessionController*> controller,
		uint64 peerId,
		Zone zone) {
	const auto weak = base::make_weak(controller);
	controller->show(Ui::MakeConfirmBox({
		.text = (IsRussian()
			? u"Удалить этот фон? Его перестанут видеть все."_q
			: u"Remove this background? Nobody will see it anymore."_q),
		.confirmed = [=](Fn<void()> close) {
			RemoveBackground(peerId, zone);
			close();
			if (const auto strong = weak.get()) {
				strong->showToast(IsRussian()
					? u"Фон профиля удалён."_q
					: u"Profile background removed."_q);
			}
		},
		.confirmText = tr::lng_box_delete(),
		.confirmStyle = &st::attentionBoxButton,
	}));
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

void PaintInfoOverlay(QPainter &p, const QRect &target, const QColor &tint) {
	auto color = tint;
	color.setAlpha(kContentOverlayAlpha);
	p.fillRect(target, color);
}

Renderer::Renderer(Fn<void()> repaint)
: _repaint(std::move(repaint)) {
}

void Renderer::setPaintMetrics(
		int scrollTop,
		int topSkip,
		int contentHeight,
		int paneHeight,
		int headerHeight) {
	if (_scrollTop == scrollTop
		&& _topSkip == topSkip
		&& _contentHeight == contentHeight
		&& _paneHeight == paneHeight
		&& _headerHeight == headerHeight) {
		return;
	}
	// A background pinned to the pane stays in place while the profile
	// scrolls over it, so only the pane and the header change its look.
	const auto pinned = (paneHeight > 0) && (_paneHeight == paneHeight);
	const auto repaint = !pinned || (_headerHeight != headerHeight);
	_scrollTop = scrollTop;
	_topSkip = topSkip;
	_contentHeight = contentHeight;
	_paneHeight = paneHeight;
	_headerHeight = headerHeight;
	if (repaint) {
		_repaint();
	}
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

int Renderer::paintHeaderHeight() const {
	return (_headerHeight > 0)
		? _headerHeight
		: st::infoProfileTopBarHeightMax;
}

void Renderer::setPeer(uint64 peerId, Zone zone) {
	const auto background = ResolveBackground(peerId, zone);
	if (!background) {
		clear();
		return;
	}
	// A fetched background is written to the same path every time, so the
	// modification time and the size tell a new file apart from the one
	// already shown. A file that could not be shown is not tried again until
	// it changes, every reload would decode it again otherwise.
	const auto info = QFileInfo(background->path);
	const auto modified = info.lastModified();
	const auto size = info.size();
	if (_peerId == peerId
		&& _zone == zone
		&& _path == background->path
		&& _modified == modified
		&& _size == size) {
		return;
	}
	reset();
	_peerId = peerId;
	_zone = zone;
	_path = background->path;
	_modified = modified;
	_size = size;
	if (background->isVideo) {
		loadVideo(background->path);
	} else {
		loadImage(background->path);
	}
	// Set once, a picture replacing a picture keeps it true all the time.
	_hasFrame = hasFrame();
}

Renderer::~Renderer() = default;

void Renderer::clear() {
	reset();
	_hasFrame = false;
}

void Renderer::reset() {
	_peerId = 0;
	_path = QString();
	_modified = QDateTime();
	_size = -1;
	_image = QImage();
	++_videoGeneration;
	_video = nullptr;
	_videoPath = QString();
	_lastVideoFrame = QImage();
	invalidateCover();
}

void Renderer::invalidateCover() {
	_coverCache = QImage();
	_coverTarget = QSize();
	_coverRatio = 0;
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
	const auto generation = ++_videoGeneration;
	// The file is read into memory away from the main thread. Nothing keeps
	// it open while it plays then, so a newer version can replace it and a
	// removed one can be deleted, Windows refuses both for an open file.
	// Only a local file above kMaxVideoInMemory, set by an older version,
	// is played from the disk and kept open, new ones can't be that big.
	crl::async([=, weak = base::make_weak(this)] {
		auto bytes = QByteArray();
		auto file = QFile(path);
		const auto inMemory = (file.size() <= kMaxVideoInMemory);
		const auto read = inMemory
			&& file.open(QIODevice::ReadOnly)
			&& !(bytes = file.readAll()).isEmpty();
		const auto unreadable = inMemory && !read;
		crl::on_main(weak, [=, bytes = std::move(bytes)] {
			if (_videoGeneration != generation || _videoPath != path) {
				return;
			} else if (unreadable) {
				// Could be a moment when the file is being replaced, so it
				// is tried again on the next reload instead of given up.
				LOG(("Whitegram Background: could not read the video."));
				_modified = QDateTime();
				return;
			}
			startVideo(path, bytes);
		});
	});
}

void Renderer::startVideo(const QString &path, const QByteArray &bytes) {
	using namespace Media::Streaming;

	// Played with streaming, as video avatars are: the clip reader used for
	// GIFs refuses anything above 1280x720, which most phone videos are.
	// Empty bytes mean the file is too big to keep in memory.
	auto loader = bytes.isEmpty()
		? MakeFileLoader(path)
		: MakeBytesLoader(bytes);
	_video = std::make_unique<Instance>(
		std::make_shared<Document>(std::move(loader)),
		nullptr);
	if (!_video->valid()) {
		videoFailed();
		return;
	}
	_video->lockPlayer();
	_video->player().updates(
	) | rpl::on_next_error([=](Update &&update) {
		v::match(update.data, [&](Information &) {
			_repaint();
		}, [&](UpdateVideo &) {
			_repaint();
		}, [](const auto &) {
		});
	}, [=](Error &&) {
		videoFailed();
	}, _video->lifetime());

	// Every frame waits until the previous one is painted, so nothing is
	// decoded while the background is not on the screen.
	auto options = PlaybackOptions();
	options.mode = Mode::Video;
	options.loop = true;
	options.waitForMarkAsShown = true;
	options.hwAllowed = Core::App().settings().hardwareAcceleratedVideo();
	_video->play(options);
}

void Renderer::videoFailed() {
	LOG(("Whitegram Background: could not play the video."));
	// The path stays, so the same file is not tried again on every reload.
	_video = nullptr;
	_lastVideoFrame = QImage();
	_hasFrame = false;
	_repaint();
}

bool Renderer::videoReady() const {
	return _video
		&& _video->player().ready()
		&& !_video->player().videoSize().isEmpty();
}

void Renderer::advanceVideo() {
	if (!videoReady()) {
		return;
	}
	auto size = _video->player().videoSize();
	if (size.width() > kMaxVideoSide || size.height() > kMaxVideoSide) {
		size = size.scaled(kMaxVideoSide, kMaxVideoSide, Qt::KeepAspectRatio);
	}
	auto request = Media::Streaming::FrameRequest();
	request.resize = request.outer = size;
	auto frame = _video->frame(request);
	if (!frame.isNull()) {
		_lastVideoFrame = std::move(frame);
		if (!_hasFrame.current()) {
			// This runs while painting, the pages following the value
			// restyle their rows, so they learn about it right after.
			crl::on_main(base::make_weak(this), [=] {
				_hasFrame = hasFrame();
			});
		}
	}
	_video->markFrameShown();
}

QImage Renderer::coverForTarget(QSize target) {
	if (_image.isNull() || target.isEmpty()) {
		return QImage();
	}
	const auto ratio = style::DevicePixelRatio();
	if (target != _coverTarget
		|| ratio != _coverRatio
		|| _coverCache.isNull()) {
		auto pixels = QSize(target.width() * ratio, target.height() * ratio);
		if (pixels.width() > kMaxCoverSide || pixels.height() > kMaxCoverSide) {
			pixels = pixels.scaled(
				kMaxCoverSide,
				kMaxCoverSide,
				Qt::KeepAspectRatio);
		}
		_coverCache = PrepareCoverImage(_image, pixels);
		_coverTarget = target;
		_coverRatio = ratio;
	}
	return _coverCache;
}

bool Renderer::paintVideoFrame(
		QPainter &p,
		const QRect &destination,
		QSize target,
		int sliceTop) {
	const auto &frame = _lastVideoFrame;
	if (frame.isNull()) {
		return false;
	}
	// Video frames change all the time, so instead of scaling every frame
	// to the whole pane the visible part is cropped and drawn directly.
	const auto frameWidth = float64(frame.width());
	const auto frameHeight = float64(frame.height());
	const auto scale = std::min(
		frameWidth / target.width(),
		frameHeight / target.height());
	const auto left = (frameWidth - target.width() * scale) / 2.;
	const auto top = (frameHeight - target.height() * scale) / 2.;
	const auto source = QRectF(
		left,
		top + sliceTop * scale,
		destination.width() * scale,
		destination.height() * scale);
	auto hq = PainterHighQualityEnabler(p);
	p.drawImage(QRectF(destination), frame, source);
	return true;
}

bool Renderer::active() const {
	return _videoPath.isEmpty()
		? !_image.isNull()
		: (!_lastVideoFrame.isNull() || videoReady());
}

bool Renderer::hasFrame() const {
	return _videoPath.isEmpty()
		? !_image.isNull()
		: !_lastVideoFrame.isNull();
}

rpl::producer<bool> Renderer::hasFrameValue() const {
	return _hasFrame.value();
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
	if (sourceY >= target.height()) {
		return false;
	}
	const auto sliceTop = std::max(sourceY, 0);
	const auto sliceHeight = std::min(rect.height(), target.height() - sliceTop);
	if (sliceHeight <= 0) {
		return false;
	}
	const auto destination = QRect(
		rect.x(),
		rect.y(),
		rect.width(),
		sliceHeight);
	if (!_videoPath.isEmpty()) {
		advanceVideo();
		if (!paintVideoFrame(p, destination, target, sliceTop)) {
			return false;
		}
	} else {
		const auto prepared = coverForTarget(target);
		if (prepared.isNull()) {
			return false;
		}
		const auto scaleX = prepared.width() / float64(target.width());
		const auto scaleY = prepared.height() / float64(target.height());
		const auto source = QRectF(
			0.,
			sliceTop * scaleY,
			rect.width() * scaleX,
			sliceHeight * scaleY);
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
		Fn<bool()> roundTopCorners);

	[[nodiscard]] base::weak_ptr<Renderer> renderer();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void reload();

	const Fn<PeerData*()> _peer;
	const Fn<bool()> _roundTopCorners;
	Renderer _renderer;

};

FullBackgroundLayer::FullBackgroundLayer(
		not_null<Ui::RpWidget*> host,
		Fn<PeerData*()> peer,
		Fn<bool()> roundTopCorners)
: RpWidget(host)
, _peer(std::move(peer))
, _roundTopCorners(std::move(roundTopCorners))
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
			_renderer.paintHeaderHeight(),
			st::windowBg->c);
	}
}

class InfoBackgroundLayer final : public Ui::RpWidget {
public:
	InfoBackgroundLayer(
		not_null<Ui::RpWidget*> host,
		Fn<PeerData*()> peer);

	[[nodiscard]] rpl::producer<bool> paintedValue() const;

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

rpl::producer<bool> InfoBackgroundLayer::paintedValue() const {
	return _renderer.hasFrameValue();
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
		PaintInfoOverlay(p, slice, st::windowBg->c);
	}
}

// A child object of the marked widget. Being a child it goes away with the
// widget by itself, and nothing has to point at it from the outside.
class BackgroundHost final : public QObject {
public:
	BackgroundHost(not_null<QWidget*> parent, rpl::producer<bool> shown)
	: QObject(parent.get())
	, _shown(std::move(shown)) {
		setObjectName(QString::fromLatin1(kHostObjectName));
	}

	[[nodiscard]] rpl::producer<bool> value() const {
		return _shown.value();
	}

private:
	rpl::variable<bool> _shown;

};

[[nodiscard]] BackgroundHost *FindOwnHost(not_null<QWidget*> widget) {
	// Only BackgroundHost objects get this name, see its constructor.
	return static_cast<BackgroundHost*>(widget->findChild<QObject*>(
		QString::fromLatin1(kHostObjectName),
		Qt::FindDirectChildrenOnly));
}

[[nodiscard]] BackgroundHost *FindHost(not_null<QWidget*> widget) {
	for (auto w = widget.get(); w; w = w->parentWidget()) {
		if (const auto host = FindOwnHost(w)) {
			return host;
		}
	}
	return nullptr;
}

[[nodiscard]] QColor SeeThroughOverColor() {
	return WithOpacity(st::windowBgOver->c, kSeeThroughOverOpacity);
}

[[nodiscard]] QColor SeeThroughDividerColor() {
	return WithOpacity(st::boxDividerBg->c, kSeeThroughDividerOpacity);
}

void ApplySeeThrough(
		style::SettingsButton &button,
		const style::SettingsButton &base,
		bool shown,
		const style::color &over) {
	button.textBg = shown ? st::transparent : base.textBg;
	button.textBgOver = shown ? over : base.textBgOver;
}

// Keeps a copy of the style in the owner's lifetime and switches its fills
// with the background of the nearest host.
template <typename Style, typename Apply>
[[nodiscard]] const Style &MakeAdaptive(
		not_null<Ui::RpWidget*> owner,
		const Style &st,
		Fn<QColor()> seeThrough,
		Apply apply) {
	const auto host = FindHost(owner);
	if (!host) {
		return st;
	}
	struct State {
		State(const Style &base, Fn<QColor()> seeThrough)
		: style(base)
		, color(std::move(seeThrough)) {
		}

		Style style;
		style::complex_color color;
	};
	const auto state = owner->lifetime().make_state<State>(
		st,
		std::move(seeThrough));
	host->value() | rpl::on_next([=, base = &st](bool shown) {
		apply(state->style, *base, shown, state->color.color());
		owner->update();
	}, owner->lifetime());
	return state->style;
}

} // namespace

base::weak_ptr<Renderer> SetupFullBackgroundLayer(
		not_null<Ui::RpWidget*> host,
		Fn<PeerData*()> peer,
		Fn<bool()> roundTopCorners) {
	const auto layer = Ui::CreateChild<FullBackgroundLayer>(
		host.get(),
		std::move(peer),
		std::move(roundTopCorners));
	return layer->renderer();
}

rpl::producer<bool> AttachInfoBackgroundLayer(
		not_null<Ui::RpWidget*> host,
		Fn<PeerData*()> peer) {
	const auto layer = Ui::CreateChild<InfoBackgroundLayer>(
		host.get(),
		std::move(peer));
	return layer->paintedValue();
}

void MarkBackgroundHost(
		not_null<Ui::RpWidget*> host,
		rpl::producer<bool> shown) {
	// Marking a widget again replaces the earlier mark.
	delete FindOwnHost(host);
	new BackgroundHost(host, std::move(shown));
}

rpl::producer<bool> BackgroundShownValue(not_null<QWidget*> widget) {
	if (const auto host = FindHost(widget)) {
		return host->value();
	}
	return rpl::single(false);
}

const style::SettingsButton &AdaptiveStyle(
		not_null<Ui::RpWidget*> owner,
		const style::SettingsButton &st) {
	return MakeAdaptive(owner, st, SeeThroughOverColor, [](
			style::SettingsButton &button,
			const style::SettingsButton &base,
			bool shown,
			const style::color &over) {
		ApplySeeThrough(button, base, shown, over);
	});
}

const style::SettingsCountButton &AdaptiveStyle(
		not_null<Ui::RpWidget*> owner,
		const style::SettingsCountButton &st) {
	return MakeAdaptive(owner, st, SeeThroughOverColor, [](
			style::SettingsCountButton &button,
			const style::SettingsCountButton &base,
			bool shown,
			const style::color &over) {
		ApplySeeThrough(button.button, base.button, shown, over);
	});
}

const style::DividerBar &AdaptiveStyle(
		not_null<Ui::RpWidget*> owner,
		const style::DividerBar &st) {
	return MakeAdaptive(owner, st, SeeThroughDividerColor, [](
			style::DividerBar &bar,
			const style::DividerBar &base,
			bool shown,
			const style::color &bg) {
		bar.bg = shown ? bg : base.bg;
	});
}

const style::DividerLabel &AdaptiveStyle(
		not_null<Ui::RpWidget*> owner,
		const style::DividerLabel &st) {
	return MakeAdaptive(owner, st, SeeThroughDividerColor, [](
			style::DividerLabel &divider,
			const style::DividerLabel &base,
			bool shown,
			const style::color &bg) {
		divider.bar.bg = shown ? bg : base.bar.bg;
	});
}

const style::PeerList &AdaptiveStyle(
		not_null<Ui::RpWidget*> owner,
		const style::PeerList &st) {
	return MakeAdaptive(owner, st, SeeThroughOverColor, [](
			style::PeerList &list,
			const style::PeerList &base,
			bool shown,
			const style::color &over) {
		list.bg = shown ? st::transparent : base.bg;
		auto &button = list.item.button;
		button.textBg = shown ? st::transparent : base.item.button.textBg;
		button.textBgOver = shown ? over : base.item.button.textBgOver;
	});
}

const style::SubTabs &AdaptiveStyle(
		not_null<Ui::RpWidget*> owner,
		const style::SubTabs &st) {
	return MakeAdaptive(owner, st, SeeThroughDividerColor, [](
			style::SubTabs &tabs,
			const style::SubTabs &base,
			bool shown,
			const style::color &) {
		// The fades at the edges of a scrolled strip are filled with this
		// color, they would show as solid bars over the background.
		tabs.bg = shown ? st::transparent : base.bg;
	});
}

} // namespace AyuFeatures::CustomProfileBackground
