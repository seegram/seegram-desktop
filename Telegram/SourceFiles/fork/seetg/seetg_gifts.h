/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

// The see.tg view of a profile's gifts tab.
//
// Wraps the client's own gifts list in a widget with a "Telegram | see.tg"
// switch on top. In see.tg mode the list comes from the see.tg backend
// instead: upgraded, non-upgraded and regular gifts in three tabs, the
// non-upgraded ones with a "hidden" switch for the gifts that have left the
// profile, plus the mini app's filters and sorting. The cards are the
// client's own gift buttons and a click opens the client's own gift sheet,
// so the two modes look alike.
//
// One request when the tab opens, one per page while scrolling, never one
// per gift - that is the backend's rate budget and the user's rule.

class PeerData;

namespace Ui {
class RpWidget;
namespace Menu {
struct MenuCallback;
} // namespace Menu
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Fork::SeeTg {

// Hook, called where the client creates the gifts list of a profile.
[[nodiscard]] object_ptr<Ui::RpWidget> WrapGifts(
	QWidget *parent,
	object_ptr<Ui::RpWidget> native,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

// Hook, called where the client fills the gifts tab's top bar menu: the
// see.tg list has its own entries, the client's own list keeps its own.
// Takes the wrapper or the native list inside it.
void FillGiftsMenu(
	not_null<Ui::RpWidget*> widget,
	const Ui::Menu::MenuCallback &addAction,
	Fn<void()> nativeFill);

} // namespace Fork::SeeTg
