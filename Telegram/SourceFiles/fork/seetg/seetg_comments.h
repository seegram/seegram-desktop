/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"

namespace Ui {
class SettingsButton;
class VerticalLayout;
class MultiSlideTracker;
} // namespace Ui

namespace Window {
class SessionNavigation;
class SessionController;
} // namespace Window

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Fork::SeeTg::Comments {

class State;
class Inner;

class Memento final : public Info::ContentMemento {
public:
	explicit Memento(not_null<PeerData*> peer, std::shared_ptr<State> state = {});

	object_ptr<Info::ContentWidget> createWidget(
		QWidget *parent,
		not_null<Info::Controller*> controller,
		const QRect &geometry) override;
	Info::Section section() const override;

private:
	std::shared_ptr<State> _state;

};

class Widget final : public Info::ContentWidget {
public:
	Widget(QWidget *parent, not_null<Info::Controller*> controller,
		std::shared_ptr<State> state);

	bool showInternal(not_null<Info::ContentMemento*> memento) override;
	void setInternalState(const QRect &geometry, not_null<Memento*> memento);
	rpl::producer<QString> title() override;
	void fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) override;

private:
	std::shared_ptr<Info::ContentMemento> doCreateMemento() override;

	const not_null<PeerData*> _peer;
	const std::shared_ptr<State> _state;
	Inner *_inner = nullptr;

};

[[nodiscard]] object_ptr<Ui::RpWidget> ForGift(QWidget *parent,
	not_null<Window::SessionController*> controller,
	const QString &giftId, Fn<void()> scrollToComposer);

[[nodiscard]] std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer);
not_null<Ui::SettingsButton*> AddButton(
	not_null<Ui::VerticalLayout*> parent,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	Ui::MultiSlideTracker &tracker);

} // namespace Fork::SeeTg::Comments
