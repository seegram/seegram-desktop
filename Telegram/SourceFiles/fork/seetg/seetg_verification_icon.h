/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>

namespace Fork::SeeTg::Verification {

// The black glyph becomes a transparent cutout, just like a monochrome
// custom emoji. Tint the alpha mask after rendering to retain color alpha.
[[nodiscard]] inline QImage TintIcon(
		QByteArray svg,
		QSize pixels,
		QColor color) {
	svg.replace("$SEAL", "#ffffff").replace("$GLYPH", "#000000");
	auto result = QImage(pixels, QImage::Format_ARGB32);
	result.fill(Qt::transparent);
	{
		auto painter = QPainter(&result);
		auto renderer = QSvgRenderer(svg);
		renderer.render(&painter);
	}
	for (auto y = 0; y != result.height(); ++y) {
		auto line = reinterpret_cast<QRgb*>(result.scanLine(y));
		for (auto x = 0; x != result.width(); ++x) {
			const auto alpha = qAlpha(line[x]) * qGray(line[x]) / 255;
			line[x] = qRgba(color.red(), color.green(), color.blue(),
				alpha * color.alpha() / 255);
		}
	}
	return result;
}

} // namespace Fork::SeeTg::Verification
