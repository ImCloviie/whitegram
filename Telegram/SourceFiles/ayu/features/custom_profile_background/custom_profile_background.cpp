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
#include "lang/lang_instance.h"
#include "media/clip/media_clip_reader.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeDatabase>
#include <QtGui/QImageReader>
#include <QtGui/QPainter>

#include <optional>

namespace AyuFeatures::CustomProfileBackground {
namespace {

using Zone = ::CustomProfileBackground::Zone;

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
	const auto scaled = source.scaled(
		target,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	const auto left = (scaled.width() - target.width()) / 2;
	const auto top = (scaled.height() - target.height()) / 2;
	return scaled.copy(
		left,
		top,
		target.width(),
		target.height());
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
	return mime.inherits(u"video/"_q);
}

bool HasBackground(uint64 peerId, Zone zone) {
	return ResolveBackground(peerId, zone).has_value();
}

std::optional<Zone> TopRendererZone(uint64 peerId) {
	if (HasBackground(peerId, Zone::Top)) {
		return Zone::Top;
	}
	return std::nullopt;
}

std::optional<Zone> BottomRendererZone(uint64 peerId) {
	return std::nullopt;
}

bool UsesFullBackground(uint64 peerId) {
	return false;
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
		: info.suffix();
	const auto destination = dir
		+ QString::number(peerId)
		+ u'_'
		+ ZoneTag(zone)
		+ u'.'
		+ suffix;
	if (QFile::exists(destination)) {
		QFile::remove(destination);
	}
	if (!QFile::copy(sourcePath, destination)) {
		return QString();
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

Renderer::Renderer(Fn<void()> repaint)
: _repaint(std::move(repaint)) {
}

void Renderer::setPeer(uint64 peerId, Zone zone) {
	_peerId = peerId;
	_zone = zone;
	clear();
	const auto background = ResolveBackground(peerId, zone);
	if (!background) {
		return;
	}
	if (background->isVideo) {
		loadVideo(background->path);
	} else {
		loadImage(background->path);
	}
}

void Renderer::clear() {
	_image = QImage();
	_video = Media::Clip::ReaderPointer();
	_videoFrameSize = QSize();
}

void Renderer::loadImage(const QString &path) {
	QImageReader reader(path);
	reader.setAutoTransform(true);
	const auto size = reader.size();
	if (size.isValid()) {
		constexpr auto kMaxSide = 4096;
		if (size.width() > kMaxSide || size.height() > kMaxSide) {
			reader.setScaledSize(size.scaled(
				kMaxSide,
				kMaxSide,
				Qt::KeepAspectRatio));
		}
	}
	_image = reader.read();
	if (_image.isNull()) {
		_image = QImage(path);
	}
	if (!_image.isNull()) {
		_repaint();
	}
}

void Renderer::loadVideo(const QString &path) {
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

void Renderer::handleClipNotification(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case Notification::Reinit:
		if (_video && _video->state() == State::Error) {
			_video = Media::Clip::ReaderPointer();
		} else {
			ensureVideoStarted(_videoFrameSize);
		}
		_repaint();
		break;
	case Notification::Repaint:
		if (_video && !_video->currentDisplayed()) {
			_repaint();
		}
		break;
	}
}

void Renderer::ensureVideoStarted(QSize frame) {
	if (!_video || !_video->ready() || _video->started()) {
		return;
	} else if (frame.isEmpty()) {
		return;
	}
	_video->start({ .frame = frame });
}

bool Renderer::active() const {
	return !_image.isNull() || (_video && _video->ready());
}

void Renderer::paint(
		QPainter &p,
		const QRect &rect,
		int coverHeight,
		int sourceY) {
	if (!active() || coverHeight <= 0 || rect.isEmpty()) {
		return;
	}
	const auto target = QSize(rect.width(), coverHeight);
	QImage frame;
	if (_video) {
		_videoFrameSize = target;
		if (_video->ready() && !_video->started()) {
			ensureVideoStarted(target);
		}
		if (_video->started()) {
			frame = _video->current({ .frame = target }, crl::now());
		}
	}
	if (frame.isNull()) {
		frame = _image;
	}
	const auto prepared = PrepareCoverImage(frame, target);
	if (prepared.isNull()) {
		return;
	}
	const auto sliceTop = std::clamp(
		sourceY,
		0,
		std::max(prepared.height() - 1, 0));
	const auto sliceBottom = std::min(sliceTop + rect.height(), prepared.height());
	const auto sliceHeight = sliceBottom - sliceTop;
	if (sliceHeight <= 0) {
		return;
	}
	p.drawImage(
		rect.topLeft(),
		prepared,
		QRect(0, sliceTop, prepared.width(), sliceHeight));
	auto hq = PainterHighQualityEnabler(p);
	p.fillRect(rect, QColor(0, 0, 0, 72));
}

} // namespace AyuFeatures::CustomProfileBackground
