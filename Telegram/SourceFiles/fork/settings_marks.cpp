/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/settings_marks.h"

#include "fork/fork_lang.h"
#include "fork/message_marks.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Fork::Marks {
namespace {

// The strings are in fork/fork_lang.cpp.
using Lang::Key;

void ShowEditMarkBox(
		not_null<Window::SessionController*> controller,
		Key title,
		const QString &current,
		const QString &fallback,
		Fn<void(QString)> save) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(Lang::Value(title));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Lang::Value(title),
			current));
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		const auto submit = [=] {
			save(field->getLastText().trimmed());
			box->closeBox();
		};
		field->submits(
		) | rpl::on_next([=](Qt::KeyboardModifiers) {
			submit();
		}, field->lifetime());
		box->addLeftButton(Lang::Value(Key::Reset), [=] {
			field->setText(fallback);
		});
		box->addButton(tr::lng_settings_save(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void AddMarkButton(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		Key title,
		QString Settings::*field,
		Fn<QString()> fallback) {
	auto label = Value() | rpl::map([=](const Settings &settings) {
		const auto &value = settings.*field;
		return value.isEmpty() ? fallback() : value;
	});
	::Settings::AddButtonWithLabel(
		container,
		Lang::Value(title),
		std::move(label),
		st::settingsButtonNoIcon
	)->addClickHandler([=] {
		ShowEditMarkBox(
			controller,
			title,
			Current().*field,
			fallback(),
			[=](QString value) {
				auto settings = Current();
				settings.*field = value;
				Set(settings);
			});
	});
}

void AddSwitch(
		not_null<Ui::VerticalLayout*> container,
		Key text,
		bool Settings::*field) {
	const auto button = container->add(object_ptr<Ui::SettingsButton>(
		container,
		Lang::Value(text),
		st::settingsButtonNoIcon));
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

void BuildContent(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller) {
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, Lang::Value(Key::Messages));
	AddMarkButton(
		container,
		controller,
		Key::DeletedMark,
		&Settings::deletedMark,
		DefaultDeletedMark);
	AddMarkButton(
		container,
		controller,
		Key::EditedMark,
		&Settings::editedMark,
		DefaultEditedMark);
	AddSwitch(
		container,
		Key::TranslucentDeleted,
		&Settings::translucentDeleted);
	Ui::AddSkip(container);
	Ui::AddDividerText(container, Lang::Value(Key::MarksAbout));
}

class MarksSection final : public ::Settings::Section<MarksSection> {
public:
	MarksSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

};

MarksSection::MarksSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	BuildContent(content, controller);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> MarksSection::title() {
	return Lang::Value(Key::Messages);
}

} // namespace

::Settings::Type SectionId() {
	return MarksSection::Id();
}

} // namespace Fork::Marks
