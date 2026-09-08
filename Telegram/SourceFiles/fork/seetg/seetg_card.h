/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "fork/seetg/seetg_visuals.h"
#include "ui/abstract_button.h"

namespace Fork::SeeTg {

// A gift card drawn the way the client draws its own - the backdrop's
// gradient, the pattern scattered over it, the model in the middle, the
// number on a ribbon - from changes.tg assets alone. For a collectible the
// client has no data for: one that left the profile, or one in a history
// row. A regular gift gets the plain card: the collection's art on the
// window background.
struct CardData {
	uint64 giftId = 0;
	QString title;
	QString model;
	QString backdrop;
	QString pattern;
	int num = 0;
	QString saleAmount;
	QString saleCurrency;
	QString saleMarket;

	[[nodiscard]] bool unique() const {
		return !model.isEmpty();
	}
};

class Card final : public Ui::AbstractButton {
public:
	Card(QWidget *parent, CardData data);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void paintPattern(QPainter &p, const QRect &inner);
	void paintRibbon(QPainter &p, const QRect &inner);
	void paintSale(QPainter &p, const QRect &inner);
	bool showSale() const;

	const CardData _data;
	std::optional<Visuals::Backdrop> _backdrop;
	QImage _model;
	QImage _pattern;
	QImage _patternTinted;
	QImage _ribbon;
	QString _salePrice;
	QImage _saleLogo;
	QImage _saleTon;

};

} // namespace Fork::SeeTg
