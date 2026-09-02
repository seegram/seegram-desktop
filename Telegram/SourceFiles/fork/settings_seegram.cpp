/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/settings_seegram.h"

#include "fork/build_counter.h"
#include "fork/fork_lang.h"
#include "fork/settings_ghost.h"
#include "fork/settings_marks.h"
#include "fork/settings_spy.h"
#include "core/click_handler_types.h"
#include "core/version.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Fork::SeeGram {
namespace {

// The strings are in fork/fork_lang.cpp. These few are names, not words,
// and stay the same in every language.
constexpr auto kTitle = "SeeGram";
constexpr auto kSourceLabel = "GitHub";
constexpr auto kSourceUrl = "https://github.com/seegram/seegram-desktop";
constexpr auto kNewsUsername = "seeclient";

using Lang::Key;
using Lang::Language;

[[nodiscard]] rpl::producer<QString> Text(const char *value) {
	return rpl::single(QString::fromUtf8(value));
}

[[nodiscard]] QString VersionText() {
	return u"Desktop v"_q
		+ QString::fromLatin1(AppVersionStr)
		+ u" (build "_q
		+ QString::number(BuildCounter)
		+ u")"_q;
}

void AddCategory(
		not_null<Ui::VerticalLayout*> container,
		Key title,
		const style::icon &icon,
		::Settings::Type section,
		Fn<void(::Settings::Type)> showOther) {
	::Settings::AddButtonWithIcon(
		container,
		Lang::Value(title),
		st::settingsButton,
		{ &icon }
	)->addClickHandler([=] {
		showOther(section);
	});
}

void AddLink(
		not_null<Ui::VerticalLayout*> container,
		Key title,
		const char *label,
		const style::icon &icon,
		const char *url) {
	::Settings::AddButtonWithLabel(
		container,
		Lang::Value(title),
		Text(label),
		st::settingsButton,
		{ &icon }
	)->addClickHandler([=] {
		UrlClickHandler::Open(QString::fromLatin1(url));
	});
}

void AddChannel(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		Key title,
		const char *username) {
	::Settings::AddButtonWithLabel(
		container,
		Lang::Value(title),
		rpl::single(u"@"_q + QString::fromLatin1(username)),
		st::settingsButton,
		{ &st::menuIconChannel }
	)->addClickHandler([=] {
		controller->showPeerByLink(Window::PeerByLinkInfo{
			.usernameOrId = QString::fromLatin1(username),
		});
	});
}

// The picker lists the languages the fork's strings exist in, the first
// entry following whatever language the application itself is in.
void AddLanguage(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	auto label = rpl::combine(
		Lang::ChosenValue(),
		Lang::Value(Key::LanguageSameAsApp)
	) | rpl::map([](Language chosen, const QString &sameAsApp) {
		return (chosen == Language::SameAsApp)
			? sameAsApp
			: Lang::Name(chosen);
	});
	::Settings::AddButtonWithLabel(
		container,
		Lang::Value(Key::LanguageTitle),
		std::move(label),
		st::settingsButton,
		{ &st::menuIconTranslate }
	)->addClickHandler([=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			auto options = std::vector<QString>();
			for (auto i = 0; i != int(Language::Count); ++i) {
				options.push_back(Lang::Name(Language(i)));
			}
			SingleChoiceBox(box, {
				.title = Lang::Value(Key::LanguageTitle),
				.options = options,
				.initialSelection = int(Lang::Chosen()),
				.callback = [](int index) {
					Lang::Choose(Language(index));
				},
			});
		}));
	});
}

void BuildContent(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		Fn<void(::Settings::Type)> showOther) {
	Ui::AddSkip(container);
	Ui::AddSkip(container);
	container->add(
		object_ptr<Ui::FlatLabel>(container, Text(kTitle), st::boxTitle),
		style::al_top);
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			VersionText(),
			st::boxDividerLabel),
		style::al_top);
	Ui::AddSkip(container);
	Ui::AddSkip(container);
	Ui::AddDividerText(container, Lang::Value(Key::SeeGramAbout));

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, Lang::Value(Key::Categories));
	AddCategory(
		container,
		Key::GhostMode,
		st::menuIconStealth,
		Ghost::SectionId(),
		showOther);
	AddCategory(
		container,
		Key::SpyMode,
		st::menuIconGroupLog,
		Spy::SectionId(),
		showOther);
	AddCategory(
		container,
		Key::Messages,
		st::menuIconChatBubble,
		Marks::SectionId(),
		showOther);
	AddLanguage(container, controller);
	Ui::AddSkip(container);
	Ui::AddDivider(container);

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, Lang::Value(Key::Links));
	AddChannel(container, controller, Key::NewsChannel, kNewsUsername);
	AddLink(
		container,
		Key::SourceCode,
		kSourceLabel,
		st::menuIconLinks,
		kSourceUrl);
	Ui::AddSkip(container);
}

class MainSection final : public ::Settings::Section<MainSection> {
public:
	MainSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

MainSection::MainSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	BuildContent(content, controller, showOtherMethod());
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> MainSection::title() {
	return Text(kTitle);
}

} // namespace

::Settings::Type SectionId() {
	return MainSection::Id();
}

void AddSettingsButton(::Settings::Builder::SectionBuilder &builder) {
	builder.addSectionButton({
		.title = Text(kTitle),
		.targetSection = SectionId(),
		.icon = { &st::menuIconStealth },
	});
}

} // namespace Fork::SeeGram
