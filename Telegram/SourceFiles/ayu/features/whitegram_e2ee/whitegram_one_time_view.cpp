// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#include "ayu/features/whitegram_e2ee/whitegram_one_time_view.h"

#include "ayu/features/whitegram_e2ee/whitegram_e2ee.h"
#include "ayu/features/whitegram_e2ee/whitegram_view_once_box.h"
#include "base/qthelp_url.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Whitegram::E2EE {
namespace {

class ViewOnceClickHandler final : public ClickHandler {
public:
	explicit ViewOnceClickHandler(not_null<HistoryItem*> item)
	: _item(item) {
	}

	explicit ViewOnceClickHandler(QString url)
	: _url(std::move(url)) {
	}

	void onClick(ClickContext context) const override {
		if (context.button != Qt::LeftButton) {
			return;
		}
		auto controller = context.other.value<ClickHandlerContext>().sessionWindow.get();
		if (!controller) {
			if (const auto window = Core::App().activeWindow()) {
				controller = window->sessionController();
			}
		}
		if (!controller) {
			return;
		}

		if (_item) {
			ShowViewOnceBox(controller, _item);
			return;
		}

		if (!_url.isEmpty()) {
			const auto query = _url.mid(_url.indexOf('?') + 1);
			const auto params = qthelp::url_parse_params(
				query,
				qthelp::UrlParamNameTransform::ToLower);
			const auto peerId = params.value(u"peer"_q).toLongLong();
			const auto msgId = params.value(u"id"_q).toInt();

			const auto contextItemId = context.other.value<ClickHandlerContext>().itemId;
			const auto targetMsgId = (contextItemId && contextItemId.peer == PeerId(peerId))
				? contextItemId.msg
				: MsgId(msgId);

			if (const auto item = controller->session().data().message(PeerId(peerId), targetMsgId)) {
				ShowViewOnceBox(controller, item);
			}
		}
	}

	QString tooltip() const override {
		return u"Одноразовое сообщение"_q;
	}

private:
	HistoryItem *_item = nullptr;
	QString _url;
};

} // namespace

ClickHandlerPtr MakeViewOnceClickHandler(not_null<HistoryItem*> item) {
	return std::make_shared<ViewOnceClickHandler>(item);
}

ClickHandlerPtr MakeViewOnceClickHandler(QString url) {
	return std::make_shared<ViewOnceClickHandler>(std::move(url));
}

TextWithEntities FormatOneTimeMessageBubbleText(not_null<const HistoryItem*> item) {
	auto result = TextWithEntities();
	result.text = u"🔒 Одноразовое сообщение\n👁️ Нажмите, чтобы просмотреть"_q;

	const auto peerId = item->history()->peer->id.value;
	const auto msgId = item->id.bare;
	const auto viewOnceUrl = QString(u"tg://whitegram_view_once?peer=%1&id=%2"_q)
		.arg(peerId)
		.arg(msgId);

	result.entities.push_back(EntityInText(
		EntityType::CustomUrl,
		0,
		result.text.size(),
		viewOnceUrl));

	return result;
}

} // namespace Whitegram::E2EE
