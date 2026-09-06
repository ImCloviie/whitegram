// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/ayu_settings.h"
#include "base/basic_types.h"
#include "base/weak_ptr.h"
#include "media/clip/media_clip_reader.h"

#include <optional>

class QPainter;
class QWidget;

namespace Window {
class SessionController;
} // namespace Window

namespace AyuFeatures::CustomProfileBackground {

using Zone = ::CustomProfileBackground::Zone;

[[nodiscard]] bool IsVideoFile(const QString &path);
[[nodiscard]] QString StoreBackgroundFile(
	uint64 peerId,
	Zone zone,
	const QString &sourcePath);

[[nodiscard]] bool HasBackground(uint64 peerId, Zone zone);
[[nodiscard]] std::optional<Zone> TopRendererZone(uint64 peerId);
[[nodiscard]] std::optional<Zone> BottomRendererZone(uint64 peerId);
[[nodiscard]] bool UsesFullBackground(uint64 peerId);

void PickBackground(
	not_null<Window::SessionController*> controller,
	uint64 peerId,
	Zone zone);
void RemoveBackground(uint64 peerId, Zone zone);
void EnsureRemoteBackgrounds(uint64 peerId);

class Renderer : public base::has_weak_ptr {
public:
	explicit Renderer(Fn<void()> repaint);

	void setPeer(uint64 peerId, Zone zone);
	void clear();
	void paint(
		QPainter &p,
		const QRect &rect,
		int coverHeight,
		int sourceY = 0);
	[[nodiscard]] bool active() const;

private:
	void loadImage(const QString &path);
	void loadVideo(const QString &path);
	void handleClipNotification(Media::Clip::Notification notification);
	void ensureVideoStarted(QSize frame);

	Fn<void()> _repaint;
	uint64 _peerId = 0;
	Zone _zone = Zone::Full;
	QImage _image;
	Media::Clip::ReaderPointer _video;
	QSize _videoFrameSize;
};

} // namespace AyuFeatures::CustomProfileBackground
