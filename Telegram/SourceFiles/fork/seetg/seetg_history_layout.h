/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QRect>
#include <algorithm>

namespace Fork::SeeTg::History {

struct RowGeometry {
	QRect block;
	QRect art;
	QRect text;
	int height = 0;
};

[[nodiscard]] inline RowGeometry ComputeRowGeometry(
		int width, int desiredArt, int minimumArt, int minimumText,
		int textHeight, int padding, int gap, int marginX, int marginY) {
	const auto innerWidth = std::max(1, width - 2 * (marginX + padding));
	const auto stacked = innerWidth < minimumArt + gap + minimumText;
	const auto art = std::min(innerWidth, stacked ? desiredArt
		: std::clamp(innerWidth - gap - minimumText, minimumArt, desiredArt));
	const auto contentHeight = stacked ? art + gap + textHeight : std::max(art, textHeight);
	const auto left = marginX + padding;
	const auto top = marginY + padding;
	return {
		QRect(marginX, marginY, std::max(1, width - 2 * marginX), contentHeight + 2 * padding),
		QRect(stacked ? left + (innerWidth - art) / 2 : left,
			stacked ? top : top + (contentHeight - art) / 2, art, art),
		QRect(stacked ? left : left + art + gap,
			stacked ? top + art + gap : top, stacked ? innerWidth : innerWidth - art - gap,
			stacked ? textHeight : contentHeight),
		contentHeight + 2 * (padding + marginY),
	};
}

} // namespace Fork::SeeTg::History
