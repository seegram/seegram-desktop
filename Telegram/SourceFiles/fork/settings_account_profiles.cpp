#include "fork/settings_account_profiles.h"

#include "fork/fork_lang.h"
#include "fork/settings_rows.h"
#include "base/event_filter.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/settings_common_session.h"
#include "settings/sections/settings_local_passcode.h"
#include "storage/storage_domain.h"
#include "ui/layers/generic_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/rp_widget.h"
#include "ui/painter.h"
#include "ui/controls/userpic_button.h"
#include "styles/style_account_profiles.h"
#include "styles/style_userpic_button.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QMimeData>
#include <QtGui/QDrag>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>

namespace Fork::AccountProfiles {
namespace {

using Lang::Key;
constexpr auto kAccountMime = "application/x-seegram-account-index";

Storage::Domain &Store() {
	return Core::App().domain().local();
}

QString AccountName(not_null<Main::Account*> account) {
	const auto session = account->maybeSession();
	return session ? session->user()->name() : tr::lng_menu_add_account(tr::now);
}

class ProfileRow final : public Ui::RippleButton {
public:
	ProfileRow(QWidget *parent, QString title, QString subtitle,
			PeerData *peer = nullptr, QString count = {}, std::optional<bool> checked = {})
	: RippleButton(parent, st::settingsButton.ripple)
	, _title(std::move(title))
	, _subtitle(std::move(subtitle)) {
		_name = Ui::CreateChild<Ui::FlatLabel>(this, _title, st::accountProfileTitle);
		_description = Ui::CreateChild<Ui::FlatLabel>(this, _subtitle, st::accountProfileSubtitle);
		_name->setAttribute(Qt::WA_TransparentForMouseEvents);
		_description->setAttribute(Qt::WA_TransparentForMouseEvents);
		if (peer) {
			_photo = Ui::CreateChild<Ui::UserpicButton>(this, peer, st::accountProfileUserpic);
			_photo->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
		if (checked) {
			_check = std::make_unique<Ui::CheckView>(st::defaultCheck, *checked);
			_check->setUpdateCallback([=] { update(); });
			addClickHandler([=] {
				_check->setChecked(!_check->checked(), anim::type::normal);
			});
		} else if (!count.isEmpty()) {
			_count = Ui::CreateChild<Ui::FlatLabel>(this, count, st::defaultSettingsRightLabel);
			_count->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
	}
	QString accessibilityName() override { return _title; }
	Ui::AccessibilityState accessibilityState() const override {
		return { .checkable = _check != nullptr, .checked = checked() };
	}
	bool checked() const { return _check && _check->checked(); }
protected:
	int resizeGetHeight(int width) override {
		const auto textWidth = std::max(1, width - st::accountProfileTextLeft - st::accountProfileRightSkip);
		_name->setText(st::accountProfileTitle.style.font->elided(_title, textWidth));
		_description->setText(st::accountProfileSubtitle.style.font->elided(_subtitle, textWidth));
		_name->resizeToWidth(textWidth);
		_description->resizeToWidth(textWidth);
		const auto height = st::accountProfileRowHeight;
		const auto textHeight = _name->height() + (_subtitle.isEmpty() ? 0 : st::accountProfileTextGap + _description->height());
		const auto top = (height - textHeight) / 2;
		_name->moveToLeft(st::accountProfileTextLeft, top, width);
		_description->moveToLeft(st::accountProfileTextLeft, top + _name->height() + st::accountProfileTextGap, width);
		_description->setVisible(!_subtitle.isEmpty());
		if (_photo) _photo->moveToLeft(st::accountProfileRowPadding.left(), (height - _photo->height()) / 2, width);
		if (_count) _count->moveToRight(st::accountProfileRowPadding.right(), (height - _count->height()) / 2, width);
		return height;
	}
	void paintEvent(QPaintEvent *event) override {
		auto p = Painter(this);
		p.fillRect(event->rect(), isOver() ? st::windowBgOver : st::windowBg);
		paintRipple(p, 0, 0);
		if (_check) {
			const auto size = _check->getSize();
			_check->paint(p, width() - st::accountProfileRowPadding.right() - size.width(),
				(height() - size.height()) / 2, width());
		}
		if (!_photo) {
			st::menuIconShowInFolder.paint(p, st::accountProfileRowPadding.left()
				+ (st::accountProfileUserpic.photoSize - st::menuIconShowInFolder.width()) / 2,
				(height() - st::menuIconShowInFolder.height()) / 2, width());
		}
	}
private:
	QString _title, _subtitle;
	Ui::FlatLabel *_name = nullptr, *_description = nullptr, *_count = nullptr;
	Ui::UserpicButton *_photo = nullptr;
	std::unique_ptr<Ui::CheckView> _check;
};

QString AccountSubtitle(not_null<Main::Account*> account) {
	const auto user = account->session().user();
	return user->username().isEmpty() ? QString() : '@' + user->username();
}

Ui::PasswordInput *AddPassword(
		not_null<Ui::GenericBox*> box,
		Key placeholder) {
	const auto wrap = box->addRow(object_ptr<Ui::RpWidget>(box));
	const auto field = Ui::CreateChild<Ui::PasswordInput>(wrap,
		st::defaultInputField, Lang::Value(placeholder));
	wrap->resize(wrap->width(), st::defaultInputField.heightMin);
	wrap->widthValue() | rpl::on_next([=](int width) {
		field->resize(width, field->height());
	}, wrap->lifetime());
	return field;
}

void EditProfile(
		not_null<Window::SessionController*> controller,
		Storage::Domain::AccountProfile profile) {
	if (Store().restrictedProfile()) {
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::accountProfileBoxWidth);
		box->setTitle(profile.id.isEmpty()
			? Lang::Value(Key::ProfileCreate)
			: rpl::single(profile.name));
		const auto name = box->addRow(object_ptr<Ui::InputField>(box,
			st::defaultInputField, Lang::Value(Key::ProfileName), profile.name));
		Ui::AddSkip(box->verticalLayout(), st::accountProfileFieldGap);
		const auto password = AddPassword(box, profile.id.isEmpty()
			? Key::ProfilePassword : Key::ProfilePasswordOptional);
		if (!profile.id.isEmpty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(box,
				Lang::Value(Key::ProfilePasswordHint), st::accountProfileSubtitle));
		}
		Ui::AddSkip(box->verticalLayout(), st::accountProfileFieldGap);
		const auto repeat = AddPassword(box, Key::ProfileRepeat);
		const auto content = box->verticalLayout();
		Ui::AddSkip(content, st::accountProfileSectionGap);
		Ui::AddDivider(content);
		Ui::AddSkip(content);
		const auto clean = SettingsRows::AddToggle(content,
			Lang::Value(Key::ProfileClean), Lang::Value(Key::ProfileCleanAbout),
			rpl::single(profile.clean));
		Ui::AddSkip(content);
		Ui::AddDivider(content);
		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(content, Lang::Value(Key::ProfileAccounts));
		auto choices = std::vector<std::pair<int, ProfileRow*>>();
		for (const auto &entry : Core::App().domain().accounts()) {
			if (!entry.account->sessionExists()) continue;
			const auto row = content->add(object_ptr<ProfileRow>(content,
				AccountName(entry.account.get()), AccountSubtitle(entry.account.get()),
				entry.account->session().user(), QString(), profile.accounts.contains(entry.index)));
			choices.emplace_back(entry.index, row);
		}
		Ui::AddSkip(content);

		const auto error = box->addRow(object_ptr<Ui::FlatLabel>(box,
			QString(), st::boxDividerLabel));
		const auto save = [=] {
			if (password->getLastText() != repeat->getLastText()) {
				error->setText(Lang::Text(Key::ProfileMismatch));
				repeat->showError();
				return;
			}
			auto selected = Indices();
			for (const auto &[index, checkbox] : choices) {
				if (checkbox->checked()) {
					selected.emplace(index);
				}
			}
			if (!Store().saveAccountProfile(profile.id,
					name->getLastText(), selected,
					password->getLastText().toUtf8(), clean->toggled())) {
				error->setText(Lang::Text(Key::ProfileError));
				return;
			}
			Core::App().settings().setSystemUnlockEnabled(false);
			Core::App().saveSettingsDelayed();
			box->closeBox();
		};
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		if (!profile.id.isEmpty()) {
			Ui::AddDivider(content);
			Ui::AddSkip(content);
			::Settings::AddButtonWithIcon(content, Lang::Value(Key::ProfileDelete),
				st::settingsAttentionButtonWithIcon, { &st::menuIconDelete })->addClickHandler([=] {
				controller->show(Ui::MakeConfirmBox({
					.text = Lang::Text(Key::ProfileDeleteAbout),
					.confirmed = [=](Fn<void()> close) {
						if (Store().removeAccountProfile(profile.id)) {
							close();
							box->closeBox();
						}
					},
				}));
			});
		}
		box->setFocusCallback([=] { name->setFocusFast(); });
	}));
}

class ProfilesSection final : public ::Settings::Section<ProfilesSection> {
public:
	ProfilesSection(QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller)
	, _controller(controller)
	, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
		rebuild();
		Ui::ResizeFitChild(this, _content);
		Store().accountProfilesChanged() | rpl::on_next([=] {
			crl::on_main(this, [=] { rebuild(); });
		}, lifetime());
		Store().localPasscodeChanged() | rpl::on_next([=] {
			crl::on_main(this, [=] { rebuild(); });
		}, lifetime());
	}

	rpl::producer<QString> title() override {
		return Lang::Value(Key::DoubleBottom);
	}

private:
	void rebuild();
	const not_null<Window::SessionController*> _controller;
	const not_null<Ui::VerticalLayout*> _content;

};

void ProfilesSection::rebuild() {
	_content->clear();
	if (Store().restrictedProfile()) {
		return;
	}
	Ui::AddSkip(_content);
	SettingsRows::AddDescription(_content, Lang::Value(Key::DoubleBottomAbout));
	::Settings::AddButtonWithIcon(_content,
		Lang::Value(Key::ProfileMainPassword), st::settingsButton,
		{ &st::menuIconLock })->addClickHandler([=] {
		showOther(Store().hasLocalPasscode()
			? ::Settings::LocalPasscodeCheckId()
			: ::Settings::LocalPasscodeCreateId());
	});
	SettingsRows::AddDescription(_content, Lang::Value(Key::ProfileMainAbout));
	Ui::AddSkip(_content);
	Ui::AddDivider(_content);
	Ui::AddSkip(_content);
	if (!Store().hasLocalPasscode()) {
		SettingsRows::AddDescription(_content,
			Lang::Value(Key::ProfilePasswordNeeded));
		return;
	}
	for (const auto &profile : Store().accountProfiles()) {
		auto names = QStringList();
		for (const auto &entry : Core::App().domain().accounts()) {
			if (profile.accounts.contains(entry.index)) {
				names.push_back(AccountName(entry.account.get()));
			}
		}
		auto subtitle = names.join(u", "_q);
		if (profile.clean) subtitle = Lang::Text(Key::ProfileClean)
			+ (subtitle.isEmpty() ? QString() : u" · "_q + subtitle);
		const auto button = _content->add(object_ptr<ProfileRow>(_content,
			profile.name, subtitle, nullptr, QString::number(names.size())));
		button->addClickHandler([=] { EditProfile(_controller, profile); });
		button->setAcceptDrops(true);

		base::install_event_filter(button, [=](not_null<QEvent*> event) {
			if (event->type() == QEvent::DragEnter) {
				const auto drag = static_cast<QDragEnterEvent*>(event.get());
				if (drag->mimeData()->hasFormat(kAccountMime)) {
					drag->acceptProposedAction();
					return base::EventFilterResult::Cancel;
				}
			} else if (event->type() == QEvent::Drop) {
				const auto drop = static_cast<QDropEvent*>(event.get());
				auto ok = false;
				const auto index = drop->mimeData()->data(kAccountMime).toInt(&ok);
				if (ok) {
					auto assigned = profile.accounts;
					assigned.emplace(index);
					if (Store().saveAccountProfile(
							profile.id, profile.name, assigned, {})) {
						drop->acceptProposedAction();
					}
				}
				return base::EventFilterResult::Cancel;
			}
			return base::EventFilterResult::Continue;
		});
	}
	::Settings::AddButtonWithIcon(_content,
		Lang::Value(Key::ProfileCreate), st::settingsButton,
		{ &st::menuIconAddToFolder })->addClickHandler([=] {
		EditProfile(_controller, {});
	});
	Ui::AddSkip(_content);
	Ui::AddDivider(_content);
	Ui::AddSkip(_content);
	Ui::AddSubsectionTitle(_content, Lang::Value(Key::ProfileAllAccounts));
	SettingsRows::AddDescription(_content, Lang::Value(Key::ProfileDrag));
	for (const auto &entry : Core::App().domain().accounts()) {
		if (!entry.account->sessionExists()) {
			continue;
		}
		const auto index = entry.index;
		const auto button = _content->add(object_ptr<ProfileRow>(_content,
			AccountName(entry.account.get()), AccountSubtitle(entry.account.get()),
			entry.account->session().user()));
		const auto origin = button->lifetime().make_state<QPoint>();
		base::install_event_filter(button, [=](not_null<QEvent*> event) {
			if (event->type() == QEvent::MouseButtonPress) {
				*origin = static_cast<QMouseEvent*>(event.get())->pos();
			} else if (event->type() == QEvent::MouseMove) {
				const auto mouse = static_cast<QMouseEvent*>(event.get());
				if ((mouse->buttons() & Qt::LeftButton)
					&& (mouse->pos() - *origin).manhattanLength()
						>= QApplication::startDragDistance()) {
					const auto drag = new QDrag(this);
					const auto mime = new QMimeData();
					mime->setData(kAccountMime, QByteArray::number(index));
					drag->setMimeData(mime);
					drag->exec(Qt::CopyAction);
					drag->deleteLater();
					return base::EventFilterResult::Cancel;
				}
			}
			return base::EventFilterResult::Continue;
		});
	}
	Ui::AddSkip(_content);
	::Settings::AddButtonWithIcon(_content,
		Lang::Value(Key::ProfileLock), st::settingsButton,
		{ &st::menuIconLock })->addClickHandler([] {
		Core::App().maybeLockByPasscode();
	});
}

} // namespace

::Settings::Type SectionId() {
	return ProfilesSection::Id();
}

} // namespace Fork::AccountProfiles
