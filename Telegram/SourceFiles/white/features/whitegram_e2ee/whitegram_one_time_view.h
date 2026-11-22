// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

#include "base/basic_types.h"
#include "ui/text/text_entity.h"

class HistoryItem;
class ClickHandler;
using ClickHandlerPtr = std::shared_ptr<ClickHandler>;

namespace Whitegram::E2EE {

[[nodiscard]] ClickHandlerPtr MakeViewOnceClickHandler(not_null<HistoryItem*> item);
[[nodiscard]] ClickHandlerPtr MakeViewOnceClickHandler(QString url);
[[nodiscard]] TextWithEntities FormatOneTimeMessageBubbleText(not_null<const HistoryItem*> item);

} // namespace Whitegram::E2EE
