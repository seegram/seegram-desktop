/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/settings_ghost.h"

#include "fork/ghost_mode.h"
#include "settings/settings_builder.h"
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

// Not in lang.strings: adding keys there means editing a file upstream
// rewrites on every release, for four switches only this fork has.
constexpr auto kSectionTitle = "SeeGram";
constexpr auto kGhostMode = "Ghost mode";

// Phrased as what is withheld rather than as what is sent, because that is
// what turning a switch on does. The stored settings are named the same way.
constexpr auto kReadReceipts = "Don't send read receipts";
constexpr auto kTyping = "Don't send typing status";
constexpr auto kOnlineStatus = "Don't send online status";
constexpr auto kUploadProgress = "Don't send upload progress";

constexpr auto kAbout =
	"Each switch withholds one thing the client would otherwise tell the "
	"server about you. Read receipts and typing status are invisible to the "
	"other side; going without an online status is not - people who could "
	"see when you were last online will see you stop appearing.\n\n"
	"The switch at the bottom of the side menu turns all four on or off at "
	"once, and reads as on only while all four are.";

[[nodiscard]] rpl::producer<QString> Text(const char *value) {
	return rpl::single(QString::fromUtf8(value));
}

void AddSwitch(
		not_null<Ui::VerticalLayout*> container,
		const char *text,
		bool Settings::*field) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		Text(text),
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
	Ui::AddSubsectionTitle(container, Text(kGhostMode));

	AddSwitch(container, kReadReceipts, &Settings::blockReadReceipts);
	AddSwitch(container, kTyping, &Settings::blockTyping);
	AddSwitch(container, kOnlineStatus, &Settings::blockOnlineStatus);
	AddSwitch(container, kUploadProgress, &Settings::blockUploadProgress);

	Ui::AddSkip(container);
	Ui::AddDividerText(container, Text(kAbout));
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
	return Text(kSectionTitle);
}

} // namespace

::Settings::Type SectionId() {
	return GhostSection::Id();
}

void AddSettingsButton(::Settings::Builder::SectionBuilder &builder) {
	builder.addSectionButton({
		.title = Text(kSectionTitle),
		.targetSection = SectionId(),
		.icon = { &st::menuIconStealth },
	});
}

void SetupMainMenuToggle(not_null<Ui::VerticalLayout*> container) {
	const auto button = ::Settings::AddButtonWithIcon(
		container,
		Text(kGhostMode),
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
