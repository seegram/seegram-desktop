/*
This file is part of SeeGram Desktop, a Telegram Desktop fork.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/self_destruct_badge.h"

#include "fork/spy_mode.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "ui/cached_round_corners.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "styles/style_chat.h"

namespace Fork::SpyUi {

void PaintSelfDestructBadge(Painter &p, QPoint position, int outerWidth,
		not_null<HistoryItem*> item, const Ui::ChatPaintContext &context) {
	if (!Spy::PreviewSelfDestructMedia(item)) {
		return;
	}
	const auto media = item->media();
	const auto single = media->ttlSecondsSingleView();
	const auto text = single ? QString() : tr::lng_seconds_tiny(
		tr::now, lt_count, media->ttlSeconds());
	const auto sti = context.imageStyle();
	const auto &icon = sti->historyVideoMessageTtlIcon;
	const auto padding = st::msgDateImgPadding;
	const auto iconSize = single
		? std::max(icon.height(), st::normalFont->height)
		: st::normalFont->height;
	const auto textWidth = st::normalFont->width(text);
	const auto width = iconSize + (text.isEmpty() ? 0 : padding.x() + textWidth)
		+ 2 * padding.x();
	const auto height = iconSize + 2 * padding.y();
	const auto left = position.x() + st::msgDateImgDelta;
	const auto top = position.y() + st::msgDateImgDelta;
	const auto rect = style::rtlrect(left, top, width, height, outerWidth);
	p.save();
	Ui::FillRoundRect(p, rect, sti->msgDateImgBg, sti->msgDateImgBgCorners);
	const auto iconRect = style::rtlrect(left + padding.x(), top + padding.y(),
		iconSize, iconSize, outerWidth);
	if (single) {
		icon.paintInCenter(p, iconRect);
	} else {
		auto hq = PainterHighQualityEnabler(p);
		const auto center = QRectF(iconRect).center();
		const auto radius = iconSize * .35;
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(context.st->msgDateImgFg(), style::ConvertScale(1.5)));
		p.drawEllipse(center, radius, radius);
		p.drawLine(center, center + QPointF(0, -radius * .65));
		p.drawLine(center, center + QPointF(radius * .5, radius * .25));
	}
	if (!text.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(context.st->msgDateImgFg());
		p.drawTextLeft(left + 2 * padding.x() + iconSize, top + padding.y(),
			outerWidth, text);
	}
	p.restore();
}

} // namespace Fork::SpyUi
