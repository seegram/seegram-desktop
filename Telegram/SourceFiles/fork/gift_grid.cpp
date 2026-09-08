#include "fork/gift_grid.h"
#include "fork/disguise.h"
#include "fork/gift_batch.h"
#include "fork/gift_batch_policy.h"
#include "fork/fork_lang.h"
#include "fork/settings_rows.h"
#include "fork/seetg/seetg_hidden_shop.h"
#include "boxes/star_gift_box.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "styles/style_gift_grid.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <rpl/variable.h>

namespace Fork::GiftGrid {
namespace {
using Lang::Key;
using namespace Info::PeerGifts;
constexpr auto kColumns = 3;
QString Path() { return cWorkingDir() + u"tdata/fork_gifts.json"_q; }
rpl::variable<bool> &Enabled() {
	static auto value = rpl::variable<bool>([] {
		auto file = QFile(Path());
		return !file.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(file.readAll()).object().value(u"gridPurchases"_q).toBool(true);
	}());
	return value;
}

class EmptySlot final : public Ui::RippleButton {
public:
	EmptySlot(QWidget *parent) : RippleButton(parent, st::settingsButton.ripple) {
		setToolTip(Lang::Text(Key::GiftGridChoose));
	}
	QString accessibilityName() override { return Lang::Text(Key::GiftGridChoose); }
protected:
	void paintEvent(QPaintEvent*) override {
		auto p = Painter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen); p.setBrush((isOver() ? st::windowBgOver : st::boxDividerBg)->c);
		p.drawRoundedRect(rect(), st::giftGridRadius, st::giftGridRadius);
		paintRipple(p, 0, 0);
		p.setFont(st::giftGridPlusFont); p.setPen(st::windowActiveTextFg);
		p.drawText(rect(), Qt::AlignCenter, u"+"_q);
	}
};

void PickerBox(not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> window, not_null<PeerData*> peer,
	Fn<void(GiftDescriptor)> chosen) {
	box->setWidth(st::boxWideWidth); box->setTitle(Lang::Value(Key::GiftGridChoose));
	struct Catalog {
		rpl::variable<std::vector<GiftTypeStars>> regular;
		rpl::variable<std::vector<GiftTypeStars>> hidden;
		rpl::variable<bool> hiddenTab = false;
		rpl::variable<QString> status;
		bool requested = false;
	};
	const auto state = box->lifetime().make_state<Catalog>();
	const auto content = box->verticalLayout();
	state->regular = GiftsStars(&window->session(), peer) | rpl::map([](std::vector<GiftTypeStars> list) {
		list.erase(std::remove_if(list.begin(), list.end(), [](const GiftTypeStars &g) {
			return g.resale || g.info.unique || g.info.limitedCount || g.info.auction() || g.info.soldOut || g.info.stars <= 0;
		}), list.end());
		return list;
	});
	const auto tabs = content->add(object_ptr<Ui::SlideWrap<Ui::SettingsSlider>>(content,
		object_ptr<Ui::SettingsSlider>(content, st::defaultTabsSlider)));
	tabs->entity()->setSections(std::vector<QString>{ u"Telegram"_q, Lang::Text(Key::GiftHiddenTab) });
	tabs->toggleOn(GiftBatch::HiddenEnabledValue());
	GiftBatch::HiddenEnabledValue() | rpl::on_next([=](bool enabled) {
		if (!enabled) { state->hiddenTab = false; tabs->entity()->setActiveSectionFast(0); }
	}, box->lifetime());
	tabs->entity()->sectionActivated() | rpl::on_next([=](int index) {
		if (index == 1) box->setMinHeight(box->height());
		state->hiddenTab = index == 1 && GiftBatch::HiddenEnabled();
		if (!state->hiddenTab.current() || state->requested) return;
		state->requested = true; state->status = Lang::Text(Key::SeeTgLoading);
		SeeTg::HiddenShop::Load(peer, crl::guard(box, [=](std::vector<GiftTypeStars> list) {
			state->hidden = std::move(list);
			state->status = state->hidden.current().empty() ? Lang::Text(Key::GiftHiddenEmpty) : QString();
		}), crl::guard(box, [=](QString error) { state->status = error; state->requested = false; }));
	}, box->lifetime());

	content->add(Ui::MakeGiftsList({
		.window = window, .peer = peer,
		.gifts = rpl::combine(state->regular.value(), state->hidden.value(), state->hiddenTab.value())
			| rpl::map([](const auto &regular, const auto &hidden, bool useHidden) {
				Ui::GiftsDescriptor result;
				for (const auto &gift : useHidden ? hidden : regular) result.list.push_back(gift);
				return result;
			}),
		.placeholder = rpl::combine(state->hiddenTab.value(), state->status.value())
			| rpl::map([](bool hidden, const QString &text) { return hidden ? text : QString(); }),
		.handler = [=](GiftDescriptor descriptor) { const auto callback = chosen; box->closeBox(); callback(std::move(descriptor)); },
	}));
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

struct GridDraft {
	explicit GridDraft(not_null<Main::Session*> session) : delegate(session, GiftButtonMode::Full) {}
	Delegate delegate;
	std::vector<std::optional<GiftSendDetails>> slots = std::vector<std::optional<GiftSendDetails>>(9);
	std::vector<Ui::RpWidget*> tiles;
	rpl::variable<QString> payText;
	rpl::event_stream<> changed;
	Fn<void()> cancel;
	bool sending = false;
};

void GridBox(not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> window, not_null<PeerData*> peer,
	Fn<void()> allSent) {
	box->setWidth(st::boxWideWidth); box->setTitle(Lang::Value(Key::GiftGridOpen));
	const auto content = box->verticalLayout();
	Ui::AddSubsectionTitle(content, rpl::single(peer->name()));
	SettingsRows::AddDescription(content, Lang::Value(Key::GiftGridAbout));
	const auto state = box->lifetime().make_state<GridDraft>(&peer->session());
	const auto grid = content->add(object_ptr<Ui::RpWidget>(content), st::boxRowPadding);
	const auto layout = [=] {
		const auto gap = st::giftGridGap;
		const auto w = std::max(1, (grid->width() - (kColumns - 1) * gap) / kColumns);
		const auto h = state->delegate.buttonSize().height() + st::giftGridNumberHeight;
		for (auto i = 0; i < int(state->tiles.size()); ++i) state->tiles[i]->setGeometry((i % kColumns) * (w + gap), (i / kColumns) * (h + gap), w, h);
		grid->resize(grid->width(), int(state->slots.size() / kColumns) * (h + gap) - gap);
	};
	const auto edit = box->lifetime().make_state<Fn<void(int, GiftSendDetails)>>();
	*edit = crl::guard(box, [=](int index, GiftSendDetails details) {
		const auto editor = window->show(Box(Ui::EditCatalogGiftBox, window, peer, std::move(details),
			Fn<void(GiftSendDetails)>(crl::guard(box, [=](GiftSendDetails saved) {
				state->slots[index] = std::move(saved); state->changed.fire({});
			}))));
		const auto top = editor->addTopButton(st::boxTitleMenu);
		const auto menu = top->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();
		top->setClickedCallback([=] {
			*menu = base::make_unique_q<Ui::PopupMenu>(top, st::popupMenuWithIcons);
			(*menu)->addAction(Lang::Text(Key::GiftGridChange), [=] {
				crl::on_main(box, [=] {
					if (!editor) return;
					editor->closeBox();
					window->show(Box(PickerBox, window, peer, Fn<void(GiftDescriptor)>(crl::guard(box, [=](GiftDescriptor descriptor) {
						auto next = state->slots[index].value_or(GiftSendDetails{});
						next.descriptor = std::move(descriptor); next.upgraded = false;
						(*edit)(index, std::move(next));
					}))));
				});
			}, &st::menuIconEdit);
			(*menu)->addAction(tr::lng_settings_quick_dialog_action_delete(tr::now), [=] {
				crl::on_main(box, [=] {
					if (!editor) return;
					state->slots[index].reset(); state->changed.fire({}); editor->closeBox();
				});
			}, &st::menuIconDelete);
			(*menu)->popup(QCursor::pos());
		});
	});
	const auto rebuild = [=] {
		for (const auto tile : state->tiles) delete tile;
		state->tiles.clear();
		auto count = 0; uint64 total = 0;
		for (auto i = 0; i < int(state->slots.size()); ++i) {
			const auto tile = Ui::CreateChild<Ui::RpWidget>(grid);
			state->tiles.push_back(tile);
			const auto number = Ui::CreateChild<Ui::FlatLabel>(tile, rpl::single(QString::number(i + 1)), st::defaultFlatLabel);
			const auto details = state->slots[i];
			Ui::RpWidget *button = nullptr;
			if (details) {
				++count;
				const auto &gift = std::get<GiftTypeStars>(details->descriptor);
				total += gift.info.stars + (details->upgraded ? gift.info.starsToUpgrade : 0);
				const auto filled = Ui::CreateChild<GiftButton>(tile, &state->delegate);
				filled->setDescriptor(details->descriptor, GiftButtonMode::Full);
				filled->setClickedCallback([=] { if (!state->sending) (*edit)(i, *details); });
				button = filled;
			} else {
				const auto empty = Ui::CreateChild<EmptySlot>(tile);
				empty->setClickedCallback([=] {
					if (state->sending) return;
					window->show(Box(PickerBox, window, peer, Fn<void(GiftDescriptor)>(crl::guard(box, [=](GiftDescriptor descriptor) {
						(*edit)(i, GiftSendDetails{ .descriptor = std::move(descriptor) });
					}))));
				});
				button = empty;
			}
			tile->sizeValue() | rpl::on_next([=](QSize size) {
				number->moveToLeft(0, 0);
				const auto rect = QRect(0, st::giftGridNumberHeight, size.width(), size.height() - st::giftGridNumberHeight);
				if (details) static_cast<GiftButton*>(button)->setGeometry(rect, {});
				else button->setGeometry(rect);
			}, tile->lifetime());
			tile->show();
		}
		state->payText = Lang::Text(Key::GiftBatchPay).replace(u"{count}"_q, QString::number(count)).replace(u"{total}"_q, QString::number(total));
		layout();
	};
	grid->widthValue() | rpl::on_next([=] { layout(); }, grid->lifetime());
	state->changed.events() | rpl::on_next([=] { rebuild(); }, grid->lifetime());
	const auto add = content->add(object_ptr<Ui::RoundButton>(content, Lang::Value(Key::GiftGridRow), st::defaultLightButton), st::boxRowPadding);
	content->widthValue() | rpl::on_next([=](int width) {
		add->setFullWidth(std::max(1, width - st::boxRowPadding.left() - st::boxRowPadding.right()));
	}, add->lifetime());
	add->setClickedCallback([=] { if (!state->sending) { state->slots.resize(state->slots.size() + kColumns); state->changed.fire({}); } });
	Ui::AddSkip(content);
	const auto status = content->add(object_ptr<Ui::FlatLabel>(content, Lang::Value(Key::GiftBatchStopAbout), st::boxLabel), st::boxRowPadding);
	struct PaymentButton { QPointer<Ui::RoundButton> value; };
	const auto pay = box->lifetime().make_state<PaymentButton>();
	pay->value = box->addButton(state->payText.value(), [=] {
		if (state->sending) return;
		const auto order = GiftBatch::ReverseFilledSlots(state->slots);
		if (order.empty()) return;
		std::vector<GiftSendDetails> gifts;
		for (const auto i : order) gifts.push_back(*state->slots[i]);
		state->sending = true; content->setDisabled(true); pay->value->setDisabled(true);
		state->cancel = GiftBatch::SendSequence(peer, window->uiShow(), std::move(gifts),
			crl::guard(box, [=](QString text) { status->setText(text); }),
			crl::guard(box, [=](int sent) {
				state->sending = false; state->cancel = nullptr;
				for (auto i = 0; i < sent; ++i) state->slots[order[i]].reset();
				content->setDisabled(false); state->changed.fire({});
				if (sent == int(order.size())) { const auto done = allSent; box->closeBox(); done(); }
			}));
	});
	state->changed.events() | rpl::on_next([=] {
		pay->value->setDisabled(state->sending || GiftBatch::ReverseFilledSlots(state->slots).empty());
	}, box->lifetime());
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->boxClosing() | rpl::on_next([=] { if (state->cancel) state->cancel(); }, box->lifetime());
	state->changed.fire({});
}
} // namespace

void AddSetting(not_null<Ui::VerticalLayout*> content) {
	const auto toggle = SettingsRows::AddToggle(content, Lang::Value(Key::GiftGridOpen), Lang::Value(Key::GiftGridSetting), Enabled().value());
	toggle->toggledChanges() | rpl::on_next([](bool on) {
		Enabled() = on;
		auto source = QFile(Path());
		auto object = source.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(source.readAll()).object() : QJsonObject();
		object.insert(u"gridPurchases"_q, on);
		auto file = QSaveFile(Path());
		if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(object).toJson()); file.commit(); }
	}, toggle->lifetime());
	Ui::AddSkip(content);
}

rpl::producer<bool> EnabledValue() { return rpl::combine(Enabled().value(), Disguise::FeaturesValue())
	| rpl::map([](bool enabled, bool allowed) { return enabled && allowed; }); }

void Show(not_null<Ui::GenericBox*> original,
	not_null<Window::SessionController*> window, not_null<PeerData*> peer) {
	if (Disguise::Clean() || !Enabled().current()) return;
	const auto weak = base::make_weak(original);
	window->show(Box(GridBox, window, peer, Fn<void()>([=] { if (const auto strong = weak.get()) strong->closeBox(); })));
}
}
