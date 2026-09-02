/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/settings_ghost.h"

#include "fork/fork_lang.h"
#include "fork/ghost_mode.h"
#include "settings/settings_common_session.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

namespace Fork::Ghost {
namespace {

// The strings are in fork/fork_lang.cpp, phrased as what is withheld rather
// than as what is sent, because that is what turning a switch on does. The
// stored settings are named the same way.
using Lang::Key;

void AddSwitch(
		not_null<Ui::VerticalLayout*> container,
		Key text,
		bool Settings::*field) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		Lang::Value(text),
		st::settingsButtonNoIcon));

	// Driven by the stored value rather than by the click, so that this
	// switch and the one in the side menu cannot disagree while both exist.
	button->toggleOn(Value() | rpl::map([=](const Settings &settings) {
		return settings.*field;
	}));
	button->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return (toggled != Current().*field);
	}) | rpl::on_next([=](bool toggled) {
		auto settings = Current();
		settings.*field = toggled;
		Set(settings);
	}, button->lifetime());
}

void BuildContent(not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, Lang::Value(Key::GhostMode));

	AddSwitch(
		container,
		Key::DontSendReadReceipts,
		&Settings::blockReadReceipts);
	AddSwitch(container, Key::DontSendTyping, &Settings::blockTyping);
	AddSwitch(
		container,
		Key::DontSendOnlineStatus,
		&Settings::blockOnlineStatus);
	AddSwitch(
		container,
		Key::DontSendUploadProgress,
		&Settings::blockUploadProgress);

	Ui::AddSkip(container);
	Ui::AddDividerText(container, Lang::Value(Key::GhostAbout));
}

class GhostSection final : public ::Settings::Section<GhostSection> {
public:
	GhostSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

GhostSection::GhostSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	BuildContent(content);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> GhostSection::title() {
	return Lang::Value(Key::GhostMode);
}

} // namespace

::Settings::Type SectionId() {
	return GhostSection::Id();
}

void SetupMainMenuToggle(not_null<Ui::VerticalLayout*> container) {
	const auto button = ::Settings::AddButtonWithIcon(
		container,
		Lang::Value(Key::GhostMode),
		st::mainMenuButton,
		{ &st::menuIconStealth });

	button->toggleOn(Value() | rpl::map([](const Settings &settings) {
		return settings.allEnabled();
	}));
	button->toggledChanges(
	) | rpl::filter([](bool toggled) {
		return (toggled != Enabled());
	}) | rpl::on_next([](bool toggled) {
		SetEnabled(toggled);
	}, button->lifetime());
}

} // namespace Fork::Ghost
