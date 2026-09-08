#include "fork/settings_disguise.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "fork/disguise.h"
#include "fork/fork_lang.h"
#include "fork/settings_rows.h"
#include "lang/lang_keys.h"
#include "main/main_domain.h"
#include "settings/settings_common_session.h"
#include "storage/storage_domain.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"

#include "styles/style_about_seegram.h"
#include "styles/style_disguise.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Fork::Disguise {
namespace {

using Lang::Key;

class IconChoice final : public Ui::RippleButton {
public:
	IconChoice(QWidget *parent, bool tray, int index);
	QString accessibilityName() override;
	QAccessible::Role accessibilityRole() override;
	Ui::AccessibilityState accessibilityState() const override;

protected:
	int resizeGetHeight(int width) override;
	void paintEvent(QPaintEvent *event) override;
	QImage prepareRippleMask() const override;

private:
	void refresh();
	void choose();
	void paintTray(Painter &p);
	[[nodiscard]] Icon icon() const;
	[[nodiscard]] bool selected() const;

	const bool _tray;
	const int _index;
	Ui::FlatLabel *_label = nullptr;
	Ui::RadioView _radio;
	QImage _light, _dark;
	qreal _previewRatio = 0.;
};

class IconChoices final : public Ui::RpWidget {
public:
	IconChoices(QWidget *parent, bool tray);

protected:
	int resizeGetHeight(int width) override;

private:
	std::vector<IconChoice*> _choices;
};

class DisguiseSection final : public ::Settings::Section<DisguiseSection> {
public:
	DisguiseSection(QWidget *parent,
		not_null<Window::SessionController*> controller);
	rpl::producer<QString> title() override;

};

Storage::Domain &Store() {
	return Core::App().domain().local();
}

IconChoice::IconChoice(QWidget *parent, bool tray, int index)
: RippleButton(parent, st::settingsButton.ripple)
, _tray(tray)
, _index(index)
, _radio(st::disguiseChoiceRadio, selected(), [=] { update(); }) {
	setFocusPolicy(Qt::StrongFocus);
	_label = Ui::CreateChild<Ui::FlatLabel>(this,
		(_tray && !_index) ? Lang::Value(Key::DisguiseTrayFollow)
			: rpl::single(icon() == Icon::Telegram
				? u"Telegram"_q : u"SeeGram"_q),
		st::disguiseChoiceLabel);
	_label->setAttribute(Qt::WA_TransparentForMouseEvents);
	addClickHandler([=] { choose(); });
	Changes() | rpl::on_next([=] { refresh(); }, lifetime());
	style::PaletteChanged() | rpl::on_next([=] { refresh(); }, lifetime());
	Core::App().settings().trayIconMonochromeChanges()
		| rpl::on_next([=] { refresh(); }, lifetime());
}

QString IconChoice::accessibilityName() {
	return _label->accessibilityName();
}

QAccessible::Role IconChoice::accessibilityRole() {
	return QAccessible::RadioButton;
}

Ui::AccessibilityState IconChoice::accessibilityState() const {
	auto result = RippleButton::accessibilityState();
	result.checkable = true;
	result.checked = selected();
	return result;
}

Icon IconChoice::icon() const {
	return !_tray ? Icon(_index)
		: !_index ? Store().disguiseSettings().icon
		: TrayIcon(_index) == TrayIcon::Telegram ? Icon::Telegram
		: Icon::SeeGram;
}

bool IconChoice::selected() const {
	const auto settings = Store().disguiseSettings();
	return _tray ? int(settings.trayIcon) == _index
		: int(settings.icon) == _index;
}

void IconChoice::choose() {
	if (selected()) {
		return;
	}
	auto settings = Store().disguiseSettings();
	if (_tray) {
		settings.trayIcon = TrayIcon(_index);
	} else {
		settings.icon = Icon(_index);
	}
	if (!Store().setDisguiseSettings(settings)) {
		return;
	}
}

void IconChoice::refresh() {
	_radio.setChecked(selected(), anim::type::normal);
	_light = _dark = QImage();
	update();
}

int IconChoice::resizeGetHeight(int width) {
	const auto padding = st::disguiseChoicePadding;
	_label->resizeToWidth(std::max(1, width - 2 * padding));
	const auto top = st::disguiseChoiceTop
		+ st::disguiseChoiceIcon + padding;
	_label->moveToLeft(padding, top, width);
	return top + _label->height() + padding;
}

QImage IconChoice::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(size(),
		st::disguiseChoiceRadius);
}

void IconChoice::paintTray(Painter &p) {
	const auto ratio = devicePixelRatioF();
	if (_light.isNull() || _previewRatio != ratio) {
		_previewRatio = ratio;
		const auto side = st::disguiseTrayPreviewIcon;
		const auto size = QSize(side, side) * ratio;
#ifdef Q_OS_MAC
		const auto mono = true;
#else
		const auto mono = icon() == Icon::SeeGram
			|| Core::App().settings().trayIconMonochrome();
#endif
		_light = mono ? TrayMonochrome(icon(), size, st::disguiseTrayPreviewDark->c)
			: Image(icon()).scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		_dark = mono ? TrayMonochrome(icon(), size, st::disguiseTrayPreviewLight->c)
			: _light;
		_light.setDevicePixelRatio(ratio);
		_dark.setDevicePixelRatio(ratio);
	}
	const auto size = st::disguiseTrayPreviewSize;
	const auto gap = st::disguiseTrayPreviewGap;
	const auto left = (width() - 2 * size.width() - gap) / 2;
	const auto top = st::disguiseChoiceTop
		+ (st::disguiseChoiceIcon - size.height()) / 2;
	const auto radius = st::disguiseTrayPreviewRadius;
	for (const auto dark : { false, true }) {
		const auto rect = QRect(QPoint(left
			+ (dark ? size.width() + gap : 0), top), size);
		p.setPen(Qt::NoPen);
		p.setBrush(dark ? st::disguiseTrayPreviewDark : st::disguiseTrayPreviewLight);
		p.drawRoundedRect(rect, radius, radius);
		const auto side = st::disguiseTrayPreviewIcon;
		p.drawImage(QRect(rect.x() + (size.width() - side) / 2,
			rect.y() + (size.height() - side) / 2, side, side),
			dark ? _dark : _light);
	}
}

void IconChoice::paintEvent(QPaintEvent *event) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto inset = st::disguiseChoiceStroke / 2.;
	const auto rect = QRectF(this->rect()).adjusted(inset, inset, -inset, -inset);
	const auto radius = st::disguiseChoiceRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(isOver() ? st::windowBgOver : st::windowBg);
	p.drawRoundedRect(rect, radius, radius);
	if (selected()) {
		auto tint = st::windowActiveTextFg->c;
		tint.setAlphaF(st::disguiseChoiceTint);
		p.setBrush(tint);
		p.drawRoundedRect(rect, radius, radius);
	}
	paintRipple(p, 0, 0);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen((selected() || hasFocus())
		? st::windowActiveTextFg : st::boxDividerBg, st::disguiseChoiceStroke));
	p.drawRoundedRect(rect, radius, radius);
	const auto padding = st::disguiseChoicePadding;
	_radio.paint(p, width() - padding - _radio.getSize().width(),
		padding, width());
	if (_tray) {
		paintTray(p);
	} else {
		const auto side = st::disguiseChoiceIcon;
		p.drawImage(QRect((width() - side) / 2,
			st::disguiseChoiceTop, side, side), Image(icon()));
	}
}

IconChoices::IconChoices(QWidget *parent, bool tray) : RpWidget(parent) {
	for (auto i = 0; i != (tray ? 3 : 2); ++i) {
		_choices.push_back(Ui::CreateChild<IconChoice>(this, tray, i));
	}
}

int IconChoices::resizeGetHeight(int width) {
	const auto count = int(_choices.size());
	const auto gap = st::disguiseChoiceGap;
	const auto columns = std::clamp((width + gap)
		/ (st::disguiseChoiceMinWidth + gap), 1, count);
	const auto side = (width - (columns - 1) * gap) / columns;
	auto rowHeight = 0;
	for (const auto choice : _choices) {
		choice->resizeToWidth(side);
		rowHeight = std::max(rowHeight, choice->height());
	}
	for (auto i = 0; i != count; ++i) {
		_choices[i]->resize(side, rowHeight);
		_choices[i]->moveToLeft((i % columns) * (side + gap),
			(i / columns) * (rowHeight + gap), width);
	}
	return ((count + columns - 1) / columns) * (rowHeight + gap) - gap;
}

void EditName(not_null<Ui::GenericBox*> box) {
	box->setTitle(Lang::Value(Key::DisguiseName));
	const auto field = box->addRow(object_ptr<Ui::InputField>(box,
		st::defaultInputField,
		rpl::single(u"SeeGram"_q),
		Store().disguiseSettings().name));
	box->addRow(object_ptr<Ui::FlatLabel>(box,
		Lang::Value(Key::DisguiseNameAbout), st::boxDividerLabel));
	const auto save = [=] {
		auto settings = Store().disguiseSettings();
		settings.name = field->getLastText().trimmed();
		if (!Store().setDisguiseSettings(settings)) {
			field->showError();
			return;
		}
		box->closeBox();
	};
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] { field->setFocusFast(); });
}

DisguiseSection::DisguiseSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	if (Store().restrictedProfile()) {
		return;
	}
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	Ui::AddSkip(content);
	const auto preview = content->add(object_ptr<Ui::RpWidget>(content),
		st::defaultBoxDividerLabelPadding);
	preview->resize(preview->width(), st::seegramAboutLogo);
	const auto name = Ui::CreateChild<Ui::FlatLabel>(preview,
		NameValue(), st::boxTitle);
	const auto placeName = [=] {
		const auto left = st::seegramAboutLogo + st::seegramAboutGap;
		name->resizeToWidth(std::max(1, preview->width() - left));
		preview->resize(preview->width(), std::max(st::seegramAboutLogo, name->height()));
		name->moveToLeft(left, (preview->height() - name->height()) / 2);
	};
	preview->widthValue() | rpl::on_next(placeName, preview->lifetime());
	Changes() | rpl::on_next([=] {
		placeName();
		preview->update();
	}, preview->lifetime());
	preview->paintRequest() | rpl::on_next([=] {
		auto p = Painter(preview);
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(QRect(QPoint(), QSize(st::seegramAboutLogo,
			st::seegramAboutLogo)), Image());
	}, preview->lifetime());
	Ui::AddSkip(content);
	SettingsRows::AddDescription(content, Lang::Value(Key::DisguiseAbout));
	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);
	::Settings::AddButtonWithLabel(content,
		Lang::Value(Key::DisguiseName), NameValue(), st::settingsButton,
		{ &st::menuIconEdit })->addClickHandler([=] {
		controller->show(Box(EditName));
	});
	Ui::AddSkip(content);
	for (const auto tray : { false, true }) {
		Ui::AddSubsectionTitle(content,
			Lang::Value(tray ? Key::DisguiseTray : Key::DisguiseIcon));
		Ui::AddSkip(content);
		content->add(object_ptr<IconChoices>(content, tray),
			QMargins(st::settingsButtonNoIcon.padding.left(), 0,
				st::settingsButtonNoIcon.padding.right(), 0));
		Ui::AddSkip(content, st::disguiseChoiceGap * 2);
	}
	SettingsRows::AddDescription(content, Lang::Value(Key::DisguiseCleanAbout));
	Ui::AddSkip(content);
	Ui::ResizeFitChild(this, content);
}

rpl::producer<QString> DisguiseSection::title() {
	return Lang::Value(Key::DisguiseTitle);
}

} // namespace

::Settings::Type SectionId() {
	return DisguiseSection::Id();
}

} // namespace Fork::Disguise
