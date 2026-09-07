/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_card.h"

#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_tag.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "ui/top_background_gradient.h"
#include "styles/style_credits.h"
#include "styles/style_widgets.h"

namespace Fork::SeeTg {
namespace {

// The client's own card is st::giftBoxGiftSmall wide with an 80px sticker;
// the card here is any size, so the sticker keeps that proportion.
[[nodiscard]] int StickerSide(const QRect &inner) {
	return std::min(inner.width(), inner.height())
		* st::giftBoxStickerSize.width()
		/ st::giftBoxGiftSmall;
}

} // namespace

Card::Card(QWidget *parent, CardData data)
: AbstractButton(parent)
, _data(std::move(data)) {
	if (_data.unique()) {
		if (!_data.backdrop.isEmpty()) {
			Visuals::Backdrops(crl::guard(this, [=] {
				_backdrop = Visuals::BackdropByName(_data.backdrop);
				_patternTinted = QImage();
				_ribbon = QImage();
				update();
			}));
		}
		Visuals::Image(
			Visuals::ModelImageUrl(_data.giftId, _data.model),
			crl::guard(this, [=](QImage image) {
				_model = std::move(image);
				update();
			}));
		if (!_data.pattern.isEmpty() && !_data.title.isEmpty()) {
			Visuals::Image(
				Visuals::PatternUrl(_data.title, _data.pattern),
				crl::guard(this, [=](QImage image) {
					_pattern = std::move(image);
					_patternTinted = QImage();
					update();
				}));
		}
	} else if (_data.giftId) {
		Visuals::Image(
			Visuals::OriginalImageUrl(_data.giftId),
			crl::guard(this, [=](QImage image) {
				_model = std::move(image);
				update();
			}));
	}
}

void Card::paintPattern(QPainter &p, const QRect &inner) {
	if (_pattern.isNull() || !_backdrop) {
		return;
	}
	// The pattern is an alpha mask, painted in the backdrop's pattern
	// colour at the same points, scales and opacities the client uses.
	const auto ratio = style::DevicePixelRatio();
	const auto size = Ui::Emoji::GetSizeNormal() / ratio;
	const auto large = Ui::Emoji::GetSizeLarge() / ratio;
	if (_patternTinted.isNull()) {
		_patternTinted = QImage(
			QSize(large, large) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_patternTinted.setDevicePixelRatio(ratio);
		_patternTinted.fill(_backdrop->pattern);
		auto q = QPainter(&_patternTinted);
		q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		q.drawImage(QRect(0, 0, large, large), _pattern);
	}
	const auto skip = inner.width() / 3;
	const auto rect = QRect(
		inner.x() - skip,
		inner.y(),
		inner.width() + 2 * skip,
		inner.height());
	const auto shift = (2 * size - large) / 2;
	p.save();
	p.setClipRect(inner);
	for (const auto &point : Ui::PatternBgPointsSmall()) {
		const auto position = rect.topLeft() + QPoint(
			int(point.position.x() * rect.width()),
			int(point.position.y() * rect.height()));
		p.save();
		p.translate(position);
		if (point.scale < 1.) {
			p.translate(size, size);
			p.scale(point.scale, point.scale);
			p.translate(-size, -size);
		}
		p.setOpacity(point.opacity);
		p.drawImage(QRect(shift, shift, large, large), _patternTinted);
		p.restore();
	}
	p.restore();
}

void Card::paintRibbon(QPainter &p, const QRect &inner) {
	if (!_data.num || !_backdrop) {
		return;
	}
	if (_ribbon.isNull()) {
		_ribbon = Info::PeerGifts::ValidateRotatedBadge({
			.text = '#' + Lang::FormatCountDecimal(_data.num),
			.bg1 = _backdrop->edge,
			.bg2 = _backdrop->pattern,
			.fg = QColor(255, 255, 255),
			.small = true,
		}, QMargins());
	}
	const auto rubberOut = st::lineWidth;
	const auto width = _ribbon.width() / _ribbon.devicePixelRatio();
	p.save();
	p.setClipRect(inner.marginsAdded(
		{ rubberOut, rubberOut, rubberOut, rubberOut }));
	p.drawImage(
		inner.x() + inner.width() + rubberOut - width,
		inner.y() - rubberOut,
		_ribbon);
	p.restore();
}

void Card::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto inner = rect();
	const auto radius = st::giftBoxGiftRadius;
	p.setPen(Qt::NoPen);
	if (_data.unique() && _backdrop) {
		auto gradient = QRadialGradient(inner.center(), inner.width() / 2);
		gradient.setStops({
			{ 0., _backdrop->center },
			{ 1., _backdrop->edge },
		});
		p.setBrush(gradient);
	} else {
		p.setBrush(st::windowBg);
	}
	p.drawRoundedRect(inner, radius, radius);
	if (_data.unique()) {
		paintPattern(p, inner);
	}
	if (!_model.isNull()) {
		const auto side = StickerSide(inner);
		p.drawImage(
			QRect(
				inner.x() + (inner.width() - side) / 2,
				inner.y() + (inner.height() - side) / 2,
				side,
				side),
			_model);
	}
	if (_data.unique()) {
		paintRibbon(p, inner);
	}
}

} // namespace Fork::SeeTg
