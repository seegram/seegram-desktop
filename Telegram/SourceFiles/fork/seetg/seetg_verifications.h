/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
#include "ui/text/text_entity.h"

#include <QtCore/QJsonObject>

namespace Info::Profile {
class SectionStack;
} // namespace Info::Profile

namespace Fork::SeeTg::Verification {

struct Entry {
	QString type;
	QString slot;
	TextWithEntities description;
	bool warning = false;
};
using Entries = std::vector<Entry>;

[[nodiscard]] Entries Parse(const QJsonObject &owner);
[[nodiscard]] rpl::producer<Entries> Value(not_null<PeerData*> peer);

class Badges final : public Ui::AbstractButton {
public:
	Badges(
		QWidget *parent,
		not_null<PeerData*> peer,
		QString slot,
		bool enabled = true);

	void fitToWidth(int available);
	[[nodiscard]] int extent() const;
	[[nodiscard]] rpl::producer<> updated() const;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void updateSize();

	Entries _entries;
	int _available = 0;
	int _visible = 0;
	rpl::event_stream<> _updated;

};

void AddDescriptions(
	not_null<Info::Profile::SectionStack*> stack,
	not_null<PeerData*> peer);

} // namespace Fork::SeeTg::Verification
