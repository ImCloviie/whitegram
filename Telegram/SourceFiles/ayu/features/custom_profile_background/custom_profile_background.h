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

#include <QtCore/QDateTime>

#include <optional>

class PeerData;
class QColor;
class QPainter;
class QRect;
class QWidget;

namespace style {
struct SettingsButton;
struct SettingsCountButton;
struct DividerBar;
struct DividerLabel;
struct PeerList;
struct SubTabs;
} // namespace style

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Media::Streaming {
class Instance;
} // namespace Media::Streaming

namespace AyuFeatures::CustomProfileBackground {

using Zone = ::CustomProfileBackground::Zone;

[[nodiscard]] bool IsVideoFile(const QString &path);
[[nodiscard]] QString StoreBackgroundFile(
	uint64 peerId,
	Zone zone,
	const QString &sourcePath);

[[nodiscard]] bool HasBackground(uint64 peerId, Zone zone);
[[nodiscard]] QString BackgroundFileName(uint64 peerId, Zone zone);
[[nodiscard]] bool BackgroundIsVideo(uint64 peerId, Zone zone);
[[nodiscard]] std::optional<Zone> TopRendererZone(uint64 peerId);
[[nodiscard]] std::optional<Zone> BottomRendererZone(uint64 peerId);
[[nodiscard]] bool UsesFullBackground(uint64 peerId);
[[nodiscard]] bool UsesInfoBackground(uint64 peerId);
[[nodiscard]] bool BackgroundEnabledFor(not_null<PeerData*> peer);

[[nodiscard]] rpl::producer<> BackgroundChanges(uint64 peerId);

void PickBackground(
	not_null<Window::SessionController*> controller,
	uint64 peerId,
	Zone zone);
void RemoveBackground(uint64 peerId, Zone zone);
// Asks first, then removes the background and says so.
void ConfirmRemoveBackground(
	not_null<Window::SessionController*> controller,
	uint64 peerId,
	Zone zone);
void EnsureRemoteBackgrounds(uint64 peerId);

void PaintFullOverlay(
	QPainter &p,
	const QRect &target,
	int panelTop,
	int panelHeight,
	int headerHeight,
	const QColor &tint);
// The tint over the information block background.
void PaintInfoOverlay(QPainter &p, const QRect &target, const QColor &tint);

class Renderer final : public base::has_weak_ptr {
public:
	explicit Renderer(Fn<void()> repaint);
	~Renderer();

	void setPeer(uint64 peerId, Zone zone);
	void clear();
	// The paneHeight is the height of the pane the background is painted in.
	// When it is set the image is pinned to the pane and stays visible down to
	// its end, instead of being cut off after the cover block. The
	// headerHeight is where the lighter header tint turns into the content
	// one, it should be the full height of the profile top bar.
	void setPaintMetrics(
		int scrollTop,
		int topSkip,
		int contentHeight,
		int paneHeight = 0,
		int headerHeight = 0);
	[[nodiscard]] int paintCoverHeight() const;
	[[nodiscard]] int paintVisibleBottom() const;
	[[nodiscard]] int paintHeaderHeight() const;
	bool paint(
		QPainter &p,
		const QRect &rect,
		int coverHeight,
		int sourceY = 0,
		int overlayAlpha = 72);
	// Whether paint() can paint, a playing video takes its frames there.
	[[nodiscard]] bool active() const;
	// Whether there is a picture to show already, a video may be playing
	// before its first frame is painted.
	[[nodiscard]] bool hasFrame() const;
	// Follows hasFrame(). The first frame of a video is reported right
	// after it is painted, not while painting.
	[[nodiscard]] rpl::producer<bool> hasFrameValue() const;

private:
	// Like clear(), but leaves _hasFrame to the caller.
	void reset();
	void loadImage(const QString &path);
	void loadVideo(const QString &path);
	void startVideo(const QString &path, const QByteArray &bytes);
	void videoFailed();
	void invalidateCover();
	void advanceVideo();
	[[nodiscard]] bool videoReady() const;
	[[nodiscard]] QImage coverForTarget(QSize target);
	[[nodiscard]] bool paintVideoFrame(
		QPainter &p,
		const QRect &destination,
		QSize target,
		int sliceTop);

	Fn<void()> _repaint;
	int _scrollTop = 0;
	int _topSkip = 0;
	int _contentHeight = 0;
	int _paneHeight = 0;
	int _headerHeight = 0;
	uint64 _peerId = 0;
	Zone _zone = Zone::Full;
	QString _path;
	QDateTime _modified;
	qint64 _size = -1;
	QImage _image;
	std::unique_ptr<Media::Streaming::Instance> _video;
	QString _videoPath;
	int _videoGeneration = 0;
	QImage _lastVideoFrame;
	QImage _coverCache;
	QSize _coverTarget;
	int _coverRatio = 0;
	rpl::variable<bool> _hasFrame = false;

};

// Paints the full profile background below every other child of the host.
// The renderer's hasFrameValue() tells whether it paints anything.
[[nodiscard]] base::weak_ptr<Renderer> SetupFullBackgroundLayer(
	not_null<Ui::RpWidget*> host,
	Fn<PeerData*()> peer,
	Fn<bool()> roundTopCorners);

// Paints the information block background below every other child of the
// host. Returns whether it paints anything.
[[nodiscard]] rpl::producer<bool> AttachInfoBackgroundLayer(
	not_null<Ui::RpWidget*> host,
	Fn<PeerData*()> peer);

// A profile page with a background painted under its content marks itself
// as a host, so that the rows built inside it can find it through their
// parents and drop their opaque fills while the background is shown. The
// nearest marked parent wins. The shown value should follow what the
// layer paints, not whether the file is there: a file that can't be shown
// would leave the rows see-through over a plain page.
void MarkBackgroundHost(
	not_null<Ui::RpWidget*> host,
	rpl::producer<bool> shown);
[[nodiscard]] rpl::producer<bool> BackgroundShownValue(
	not_null<QWidget*> widget);

// See-through versions of the given styles while the background found by
// BackgroundShownValue(owner) is shown, the styles themselves otherwise.
// The result lives as long as the owner, so only the owner and its children
// may use it.
[[nodiscard]] const style::SettingsButton &AdaptiveStyle(
	not_null<Ui::RpWidget*> owner,
	const style::SettingsButton &st);
[[nodiscard]] const style::SettingsCountButton &AdaptiveStyle(
	not_null<Ui::RpWidget*> owner,
	const style::SettingsCountButton &st);
[[nodiscard]] const style::DividerBar &AdaptiveStyle(
	not_null<Ui::RpWidget*> owner,
	const style::DividerBar &st);
[[nodiscard]] const style::DividerLabel &AdaptiveStyle(
	not_null<Ui::RpWidget*> owner,
	const style::DividerLabel &st);
[[nodiscard]] const style::PeerList &AdaptiveStyle(
	not_null<Ui::RpWidget*> owner,
	const style::PeerList &st);
[[nodiscard]] const style::SubTabs &AdaptiveStyle(
	not_null<Ui::RpWidget*> owner,
	const style::SubTabs &st);

} // namespace AyuFeatures::CustomProfileBackground
