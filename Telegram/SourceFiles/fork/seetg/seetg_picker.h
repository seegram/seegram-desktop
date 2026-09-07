/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "fork/seetg/seetg_visuals.h"

namespace Window {
class SessionController;
} // namespace Window

// A picker with chips and a search field over a list with previews, for the
// see.tg filters: collections, models, backdrops, patterns.
namespace Fork::SeeTg::Picker {

struct Item {
	QString id;
	QString name;
	std::optional<Visuals::Backdrop> backdrop;
	QString imageUrl;
	bool tintImage = false;
};

struct Args {
	rpl::producer<QString> title;
	std::vector<Item> items;
	QStringList selected;
	Fn<void(QStringList)> done;
};

void Show(not_null<Window::SessionController*> controller, Args &&args);

} // namespace Fork::SeeTg::Picker
