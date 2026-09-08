/*
This file is part of SeeGram Desktop, a Telegram Desktop fork.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
struct PreparedServiceText;
namespace Data { class Media; }
namespace Fork::ExpiredMedia {
bool Capture(not_null<HistoryItem*> item, Data::Media *media);
void Decorate(not_null<HistoryItem*> item, PreparedServiceText &text);
void Forget(not_null<const HistoryItem*> item);
} // namespace Fork::ExpiredMedia
