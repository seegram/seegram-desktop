/*
This file is part of SeeGram Desktop, a Telegram Desktop fork.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once
#include "scheme.h"
class HistoryItem;
namespace Main { class Session; }
namespace Fork::GhostNotifications {
void Remember(not_null<HistoryItem*> item);
void Refresh(not_null<HistoryItem*> item);
void Rebind(not_null<HistoryItem*> from, HistoryItem *to);
void Forget(not_null<const HistoryItem*> item);
void Prepare(not_null<Main::Session*> session, const MTPVector<MTPUpdate> &updates);
void Apply(not_null<Main::Session*> session, const MTPDupdateDeleteScheduledMessages &update);
[[nodiscard]] bool Suppress(not_null<const HistoryItem*> item);
}
