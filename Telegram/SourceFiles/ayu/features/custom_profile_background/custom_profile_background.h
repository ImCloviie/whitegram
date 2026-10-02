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

class PeerData;
class QColor;
class QPainter;
class QRect;
class QWidget;

namespace Ui {
class RpWidget;
} // namespace Ui

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
[[nodiscard]] QString BackgroundFileName(uint64 peerId, Zone zone);
[[nodiscard]] std::optional<Zone> TopRendererZone(uint64 peerId);
[[nodiscard]] std::optional<Zone> BottomRendererZone(uint64 peerId);
[[nodiscard]] bool UsesFullBackground(uint64 peerId);
[[nodiscard]] bool UsesInfoBackground(uint64 peerId);
[[nodiscard]] bool BackgroundEnabledFor(not_null<PeerData*> peer);

void PickBackground(
	not_null<Window::SessionController*> controller,
	uint64 peerId,
	Zone zone);
void RemoveBackground(uint64 peerId, Zone zone);
void EnsureRemoteBackgrounds(uint64 peerId);

void PaintFullOverlay(
	QPainter &p,
	const QRect &target,
	int panelTop,
	int panelHeight,
	int headerHeight,
	const QColor &tint);

class Renderer final : public base::has_weak_ptr {
public:
	explicit Renderer(Fn<void()> repaint);

	void setPeer(uint64 peerId, Zone zone);
	void clear();
	// The paneHeight is the height of the pane the background is painted in.
	// When it is set the image is pinned to the pane and stays visible down to
	// its end, instead of being cut off after the cover block; pass 0 to keep
	// the old cover-only behaviour, as on the stories page.
	void setPaintMetrics(
		int scrollTop,
		int topSkip,
		int contentHeight,
		int paneHeight = 0);
	[[nodiscard]] int paintCoverHeight() const;
	[[nodiscard]] int paintVisibleBottom() const;
	bool paint(
		QPainter &p,
		const QRect &rect,
		int coverHeight,
		int sourceY = 0,
		int overlayAlpha = 72);
	[[nodiscard]] bool active() const;

private:
	void loadImage(const QString &path);
	void loadVideo(const QString &path);
	void restartVideo();
	void invalidateCover();
	void handleClipNotification(Media::Clip::Notification notification);
	[[nodiscard]] Media::Clip::FrameRequest videoRequest() const;
	[[nodiscard]] QImage coverForTarget(QSize target);

	Fn<void()> _repaint;
	int _scrollTop = 0;
	int _topSkip = 0;
	int _contentHeight = 0;
	int _paneHeight = 0;
	uint64 _peerId = 0;
	Zone _zone = Zone::Full;
	QString _path;
	QImage _image;
	Media::Clip::ReaderPointer _video;
	QString _videoPath;
	QImage _lastVideoFrame;
	QImage _coverCache;
	QSize _coverTarget;
	bool _frameAvailable = false;

};

struct FullProfileBackgroundBounds {
	Fn<int()> scrollTop;
	Fn<int()> topSkip;
	Fn<int()> contentHeight;
};

// Paints the full profile background below every other child of the host.
[[nodiscard]] base::weak_ptr<Renderer> SetupFullBackgroundLayer(
	not_null<Ui::RpWidget*> host,
	Fn<PeerData*()> peer,
	Fn<bool()> roundTopCorners,
	FullProfileBackgroundBounds bounds = {});

void AttachInfoBackgroundLayer(
	not_null<Ui::RpWidget*> host,
	Fn<PeerData*()> peer);

} // namespace AyuFeatures::CustomProfileBackground
