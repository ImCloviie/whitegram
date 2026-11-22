/*
This file is part of Whitegram for Desktop, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_media_types.h"
#include "history/view/media/history_view_media.h"

namespace Ui {
class SpoilerAnimation;
} // namespace Ui

namespace Whitegram::E2EE {

class OneTimeMedia final : public Data::Media {
public:
	explicit OneTimeMedia(not_null<HistoryItem*> parent);

	std::unique_ptr<Data::Media> clone(not_null<HistoryItem*> parent) override;

	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;
	bool allowsForward() const override;
	bool allowsEdit() const override;
	bool consumeMessageText(const TextWithEntities &text) override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

};

class OneTimeView final : public HistoryView::Media {
public:
	explicit OneTimeView(not_null<HistoryView::Element*> parent);
	~OneTimeView();

	void draw(
		Painter &p,
		const HistoryView::PaintContext &context) const override;
	HistoryView::TextState textState(
		QPoint point,
		HistoryView::StateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return false;
	}
	bool needsBubble() const override {
		return false;
	}
	bool customInfoLayout() const override {
		return true;
	}
	QPoint resolveCustomInfoRightBottom() const override;
	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

private:
	QSize countOptimalSize() override;

	struct Status {
		QString badge;
		QString subtitle;
		double progress = -1.;
	};

	[[nodiscard]] Status status() const;
	void resolve() const;

	ClickHandlerPtr _open;
	mutable std::unique_ptr<Ui::SpoilerAnimation> _spoiler;
	mutable QString _hash;
	mutable int _ttl = 0;
	mutable bool _resolved = false;
	mutable bool _revoking = false;
	mutable base::Timer _tick;
	rpl::lifetime _lifetime;

};

} // namespace Whitegram::E2EE
