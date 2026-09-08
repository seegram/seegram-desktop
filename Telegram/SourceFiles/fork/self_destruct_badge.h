/*
This file is part of SeeGram Desktop, a Telegram Desktop fork.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class Painter;
class HistoryItem;
namespace Ui { struct ChatPaintContext; }
namespace Fork::SpyUi {
void PaintSelfDestructBadge(Painter &p, QPoint position, int outerWidth,
	not_null<HistoryItem*> item, const Ui::ChatPaintContext &context);
} // namespace Fork::SpyUi
