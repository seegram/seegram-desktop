/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// The profile's history as see.tg keeps it: every collectible that came or
// went, every regular gift that was given, left or came back, and every
// change to the profile's own fields. A full Info section like «Подарки»,
// with three tabs like the mini app: NFT, regular gifts, profile info. One
// request when the section opens and one per further page; nothing per row.

#include "info/info_content_widget.h"

class PeerData;

namespace Ui {
class RpWidget;
class SettingsButton;
class VerticalLayout;
class MultiSlideTracker;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Fork::SeeTg::History {

class Memento final : public Info::ContentMemento {
public:
	explicit Memento(not_null<PeerData*> peer);

	object_ptr<Info::ContentWidget> createWidget(
		QWidget *parent,
		not_null<Info::Controller*> controller,
		const QRect &geometry) override;

	Info::Section section() const override;

};

class Widget final : public Info::ContentWidget {
public:
	Widget(QWidget *parent, not_null<Info::Controller*> controller);

	[[nodiscard]] not_null<PeerData*> peer() const;

	bool showInternal(not_null<Info::ContentMemento*> memento) override;
	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	rpl::producer<QString> title() override;
	void fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) override;

private:
	std::shared_ptr<Info::ContentMemento> doCreateMemento() override;

	const not_null<PeerData*> _peer;
	Ui::RpWidget *_inner = nullptr;
	Fn<void(const Ui::Menu::MenuCallback&)> _fillMenu;

};

[[nodiscard]] std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer);

// Hook: the «История» row under «Подарки» in the profile. The row follows the
// integration switch, so it is always created and shown only while enabled.
not_null<Ui::SettingsButton*> AddButton(
	not_null<Ui::VerticalLayout*> parent,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	Ui::MultiSlideTracker &tracker);

} // namespace Fork::SeeTg::History
