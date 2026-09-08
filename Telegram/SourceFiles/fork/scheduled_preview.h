/*
This file is part of SeeGram Desktop, a Telegram Desktop fork.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"
#include "ui/click_handler.h"
#include "scheme.h"

class HistoryItem;
class History;
namespace Api { struct SendOptions; }
namespace Ui { class PopupMenu; }
namespace Window { class SessionController; }

namespace Fork::ScheduledPreview {

// The server-owned scheduled item remains the only send/cancel target.
void Track(HistoryItem *item, const Api::SendOptions &options);
void TrackResult(not_null<History*> history, const MTPUpdates &result,
	const Api::SendOptions &options);
[[nodiscard]] bool Is(const HistoryItem *item);
[[nodiscard]] TimeId Deadline(not_null<const HistoryItem*> item);
[[nodiscard]] QString CountdownText(TimeId deadline);
[[nodiscard]] ClickHandlerPtr Link(not_null<const HistoryItem*> item);
bool FillMenu(not_null<Ui::PopupMenu*> menu, HistoryItem *item,
	not_null<Window::SessionController*> controller);
void Rebind(not_null<HistoryItem*> from, HistoryItem *to);
void Refresh(not_null<HistoryItem*> source);
void Forget(not_null<const HistoryItem*> item);

} // namespace Fork::ScheduledPreview
