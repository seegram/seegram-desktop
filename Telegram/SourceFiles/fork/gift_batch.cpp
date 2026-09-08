#include "fork/gift_batch.h"
#include "fork/gift_grid.h"
#include "fork/gift_batch_policy.h"
#include "fork/fork_lang.h"
#include "fork/settings_rows.h"
#include "api/api_text_entities.h"
#include "api/api_credits.h"
#include "apiwrap.h"
#include "data/data_credits.h"
#include "data/components/credits.h"
#include "boxes/star_gift_box.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "payments/payments_checkout_process.h"
#include "settings/settings_common_session.h"
#include "settings/settings_credits_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "styles/style_gift_grid.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <rpl/variable.h>

namespace Fork::GiftBatch {
namespace {
using Lang::Key;
using Info::PeerGifts::GiftSendDetails;
using Info::PeerGifts::GiftTypeStars;

QString Path() { return cWorkingDir() + u"tdata/fork_gifts.json"_q; }
rpl::variable<bool> &Enabled() {
	static auto value = rpl::variable<bool>([] {
		auto file = QFile(Path());
		return !file.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(file.readAll())
			.object().value(u"multiplePurchases"_q).toBool(true);
	}());
	return value;
}
rpl::variable<bool> &HiddenEnabledState() {
	static auto value = rpl::variable<bool>([] {
		auto file = QFile(Path());
		return !file.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(file.readAll())
			.object().value(u"hiddenPurchases"_q).toBool(true);
	}());
	return value;
}
void SaveSetting(const QString &key, bool enabled) {
	auto source = QFile(Path());
	auto object = source.open(QIODevice::ReadOnly)
		? QJsonDocument::fromJson(source.readAll()).object() : QJsonObject();
	object.insert(key, enabled);
	auto file = QSaveFile(Path());
	if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(object).toJson()); file.commit(); }
}
void SetEnabled(bool enabled) {
	if (Enabled().current() == enabled) return;
	Enabled() = enabled;
	SaveSetting(u"multiplePurchases"_q, enabled);
}

class GiftActionButton final : public Ui::RippleButton {
public:
	GiftActionButton(QWidget *parent) : RippleButton(parent, st::settingsButton.ripple) {}
	void setLabel(QString text) { _label = std::move(text); setToolTip(_label); update(); accessibilityNameChanged(); }
	QString accessibilityName() override { return _label; }
	int preferredHeight(int width) const {
		const auto textWidth = std::max(1, width - 2 * st::giftActionPadding);
		const auto textHeight = QFontMetrics(st::giftActionFont->f).boundingRect(
			QRect(0, 0, textWidth, QWIDGETSIZE_MAX), Qt::AlignCenter | Qt::TextWordWrap, _label).height();
		return std::max(st::giftActionHeight, textHeight + 2 * st::giftActionPadding);
	}
protected:
	void paintEvent(QPaintEvent*) override {
		auto p = Painter(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush((isOver() ? st::windowBgOver : st::boxDividerBg)->c);
		p.drawRoundedRect(rect(), st::giftActionRadius, st::giftActionRadius);
		paintRipple(p, 0, 0);
		p.setFont(st::giftActionFont);
		p.setPen(st::windowActiveTextFg);
		p.drawText(rect().marginsRemoved(QMargins(st::giftActionPadding, st::giftActionPadding,
			st::giftActionPadding, st::giftActionPadding)), Qt::AlignCenter | Qt::TextWordWrap, _label);
	}
private:
	QString _label;
};

struct Run : Progress, std::enable_shared_from_this<Run> {
	std::shared_ptr<Main::SessionShow> show;
	std::vector<MTPInputInvoice> invoices;
	uint64 unitPrice = 0;
	std::vector<uint64> prices;
	Fn<void(QString)> progress;
	Fn<void()> close;
	Fn<void(int)> completed;
	QString status() const {
		return Lang::Text(Key::GiftBatchResult).replace(u"{sent}"_q, QString::number(sent))
			.replace(u"{total}"_q, QString::number(invoices.size()));
	}
	void finish() {
		if (finished) return;
		finished = true;
		if (show->valid()) show->showToast(status());
		if (close) close();
		if (completed) completed(sent);
	}
	void next() {
		if (finished) return;
		if (!canSend() || !show->valid()) {
			finish();
			return;
		}
		if (progress) progress(status());
		const auto self = shared_from_this();
		const auto invoice = invoices[sent];
		Ui::RequestOurForm(show, invoice, [self, invoice](uint64 formId,
				CreditsAmount price, std::optional<Payments::CheckoutResult> failure) {
			if (!self->canSend() || !self->show->valid() || failure) {
				self->finish();
			} else if (!price.stars() || price.whole() != (self->prices.empty() ? self->unitPrice : self->prices[self->sent])) {
				self->show->showToast(Lang::Text(Key::GiftBatchPriceChanged));
				self->finish();
			} else {
				self->show->session().api().request(MTPpayments_SendStarsForm(MTP_long(formId), invoice))
				.done([self](const MTPpayments_PaymentResult &result) {
					result.match([&](const MTPDpayments_paymentResult &data) {
						self->show->session().api().applyUpdates(data.vupdates());
						self->show->session().credits().load(true);
						++self->sent;
						crl::on_main(&self->show->session(), [self] { self->next(); });
					}, [&](const MTPDpayments_paymentVerificationNeeded&) { self->finish(); });
				}).fail([self](const MTP::Error &error) {
					if (self->show->valid() && !Ui::ShowGiftErrorToast(self->show, error)) self->show->showToast(error.type());
					self->finish();
				}).send();
			}
		});
	}
};

void BatchBox(not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer, GiftSendDetails details,
		bool allowMessage, int maximum, Fn<void(int)> completed) {
	const auto gift = std::get<GiftTypeStars>(details.descriptor);
	const auto price = uint64(gift.info.stars + (details.upgraded ? gift.info.starsToUpgrade : 0));
	box->setTitle(Lang::Value(Key::GiftBatchOpen));
	box->setWidth(st::boxWideWidth);
	const auto content = box->verticalLayout();
	Ui::AddSubsectionTitle(content, rpl::single(peer->name()));
	auto preview = details;
	preview.text = {};
	content->add(Ui::MakeCatalogGiftPreview(content, peer, preview));
	Ui::AddSubsectionTitle(content, Lang::Value(Key::GiftBatchQuantity));
	struct Draft {
		rpl::variable<int> count = 2;
		rpl::variable<bool> separate = false;
		std::vector<Ui::InputField*> fields;
		Ui::InputField *common = nullptr;
	QWidget *pay = nullptr;
		std::shared_ptr<Run> run;
	};
	const auto draft = box->lifetime().make_state<Draft>();
	draft->count = std::min(2, maximum);
	const auto slider = content->add(object_ptr<Ui::SettingsSlider>(content, st::defaultTabsSlider));
	auto labels = std::vector<QString>();
	for (auto i = 1; i <= maximum; ++i) labels.push_back(QString::number(i));
	slider->setSections(labels);
	slider->setActiveSectionFast(draft->count.current() - 1);
	slider->sectionActivated() | rpl::on_next([=](int index) { draft->count = index + 1; }, box->lifetime());
	Ui::AddSkip(content);
	if (allowMessage) {
		const auto separate = SettingsRows::AddToggle(content,
			Lang::Value(Key::GiftBatchSeparate), Lang::Value(Key::GiftBatchCommentsAbout), draft->separate.value());
		separate->toggledChanges() | rpl::on_next([=](bool on) { draft->separate = on; }, box->lifetime());
		const auto common = content->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(content, object_ptr<Ui::VerticalLayout>(content)));
		draft->common = Ui::AddStarGiftMessageField(window->uiShow(), common->entity(), box->getDelegate()->outerContainer(),
			tr::lng_gift_send_message(), details.text.text);
		draft->common->setTextWithTags({ details.text.text, TextUtilities::ConvertEntitiesToTextTags(details.text.entities) });
		common->toggleOn(draft->separate.value() | rpl::map([](bool separate) { return !separate; }));
		for (auto i = 0; i < maximum; ++i) {
			const auto row = content->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(content, object_ptr<Ui::VerticalLayout>(content)));
			Ui::AddSubsectionTitle(row->entity(), rpl::single(QString::number(i + 1)));
			draft->fields.push_back(Ui::AddStarGiftMessageField(window->uiShow(), row->entity(), box->getDelegate()->outerContainer(), tr::lng_gift_send_message(), QString()));
			row->toggleOn(rpl::combine(draft->count.value(), draft->separate.value())
				| rpl::map([=](int count, bool separate) { return separate && i < count; }));
		}
	}
	Ui::AddSkip(content);
	const auto status = content->add(object_ptr<Ui::FlatLabel>(content,
		Lang::Value(Key::GiftBatchStopAbout), st::boxLabel), st::defaultBoxDividerLabelPadding);
	const auto pay = box->addButton(draft->count.value() | rpl::map([=](int count) {
		return Lang::Text(Key::GiftBatchPay).replace(u"{count}"_q, QString::number(count))
			.replace(u"{total}"_q, QString::number(price * count));
	}), [=] {
		if (draft->run) return;
		const auto run = std::make_shared<Run>();
		draft->run = run;
		draft->pay->setDisabled(true);
		run->show = window->uiShow();
		run->unitPrice = price;
		run->completed = completed;
		for (auto i = 0; i != draft->count.current(); ++i) {
			auto message = TextWithEntities();
			if (allowMessage) {
				const auto field = draft->separate.current() ? draft->fields[i] : draft->common;
				const auto text = field->getTextWithAppliedMarkdown();
				message = { text.text, TextUtilities::ConvertTextTagsToEntities(text.tags) };
			}
			using Flag = MTPDinputInvoiceStarGift::Flag;
			run->invoices.push_back(MTP_inputInvoiceStarGift(
				MTP_flags((details.anonymous ? Flag::f_hide_name : Flag(0))
					| (details.upgraded ? Flag::f_include_upgrade : Flag(0))
					| (message.empty() ? Flag(0) : Flag::f_message)),
				peer->input(), MTP_long(gift.info.id), MTP_textWithEntities(MTP_string(message.text),
					Api::EntitiesToMTP(&peer->session(), message.entities, Api::ConvertOption::SkipLocal))));
		}
		run->total = int(run->invoices.size());
		run->progress = crl::guard(box, [=](QString text) { status->setText(text); });
		run->close = crl::guard(box, [=] { box->closeBox(); });
		content->setDisabled(true);
		Settings::MaybeRequestBalanceIncrease(run->show, price * run->total,
			Settings::SmallBalanceDeepLink{}, [run](Settings::SmallBalanceResult result) {
				if (result == Settings::SmallBalanceResult::Success
					|| result == Settings::SmallBalanceResult::Already) {
					run->next();
				} else {
					run->finish();
				}
			});
	});
	draft->pay = pay.data();
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->boxClosing() | rpl::on_next([=] {
		if (draft->run) draft->run->cancelled = true;
	}, box->lifetime());
}

class GiftSection final : public ::Settings::Section<GiftSection> {
public:
	GiftSection(QWidget *parent, not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		Ui::AddSkip(content);
		const auto toggle = SettingsRows::AddToggle(content, Lang::Value(Key::GiftBatchEnabled), Lang::Value(Key::GiftBatchAbout), Enabled().value());
		toggle->toggledChanges() | rpl::on_next([](bool on) { SetEnabled(on); }, toggle->lifetime());
		Ui::AddSkip(content);
		const auto hidden = SettingsRows::AddToggle(content, Lang::Value(Key::GiftHiddenTab), Lang::Value(Key::GiftHiddenAbout), HiddenEnabledState().value());
		hidden->toggledChanges() | rpl::on_next([](bool on) {
			HiddenEnabledState() = on; SaveSetting(u"hiddenPurchases"_q, on);
		}, hidden->lifetime());
		Ui::AddSkip(content);
		GiftGrid::AddSetting(content);
		Ui::ResizeFitChild(this, content);
	}
	rpl::producer<QString> title() override { return Lang::Value(Key::GiftSettings); }
};
} // namespace

bool HiddenEnabled() { return HiddenEnabledState().current(); }
rpl::producer<bool> HiddenEnabledValue() { return HiddenEnabledState().value(); }

Fn<void()> SendSequence(not_null<PeerData*> peer,
	std::shared_ptr<Main::SessionShow> show, std::vector<GiftSendDetails> gifts,
	Fn<void(QString)> progress, Fn<void(int)> completed) {
	const auto run = std::make_shared<Run>();
	run->show = show; run->progress = std::move(progress); run->completed = std::move(completed);
	uint64 totalPrice = 0;
	for (const auto &details : gifts) {
		const auto &gift = std::get<GiftTypeStars>(details.descriptor);
		const auto price = uint64(gift.info.stars + (details.upgraded ? gift.info.starsToUpgrade : 0));
		if (!price || price > uint64(INT64_MAX) || totalPrice > uint64(INT64_MAX) - price) { run->finish(); return [] {}; }
		totalPrice += price; run->prices.push_back(price);
		using Flag = MTPDinputInvoiceStarGift::Flag;
		run->invoices.push_back(MTP_inputInvoiceStarGift(
			MTP_flags((details.anonymous ? Flag::f_hide_name : Flag(0))
				| (details.upgraded ? Flag::f_include_upgrade : Flag(0))
				| (details.text.empty() ? Flag(0) : Flag::f_message)),
			peer->input(), MTP_long(gift.info.id), MTP_textWithEntities(MTP_string(details.text.text),
				Api::EntitiesToMTP(&peer->session(), details.text.entities, Api::ConvertOption::SkipLocal))));
	}
	run->total = int(gifts.size()); run->maximum = run->total;
	if (!run->total) { run->finish(); return [] {}; }
	Settings::MaybeRequestBalanceIncrease(show, totalPrice, Settings::SmallBalanceDeepLink{},
		[run](Settings::SmallBalanceResult result) {
			if (result == Settings::SmallBalanceResult::Success || result == Settings::SmallBalanceResult::Already) run->next();
			else run->finish();
		});
	return [run] { run->cancelled = true; };
}

::Settings::Type SectionId() { return GiftSection::Id(); }

void AddButton(not_null<Ui::VerticalLayout*> container,
		not_null<Ui::GenericBox*> original,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		Fn<GiftSendDetails()> details,
		Fn<bool()> messageAllowed) {
	const auto initial = details();
	const auto gift = std::get_if<GiftTypeStars>(&initial.descriptor);
	if (!gift || gift->resale || gift->info.unique || gift->info.auction() || gift->info.soldOut || gift->info.stars <= 0) return;
	const auto maximum = Maximum(gift->info.limitedCount, gift->info.limitedLeft,
		gift->info.perUserTotal, gift->info.perUserRemains);
	const auto wrap = container->add(object_ptr<Ui::SlideWrap<Ui::RpWidget>>(container,
		object_ptr<Ui::RpWidget>(container)), st::boxRowPadding);
	const auto row = wrap->entity();
	const auto batch = Ui::CreateChild<GiftActionButton>(row);
	const auto grid = Ui::CreateChild<GiftActionButton>(row);
	rpl::combine(row->widthValue(), Enabled().value(), GiftGrid::EnabledValue(),
		Lang::Value(Key::GiftBatchOpen), Lang::Value(Key::GiftGridOpen))
	| rpl::on_next([=](int width, bool batchOn, bool gridOn, const QString &batchText, const QString &gridText) {
		batchOn = batchOn && maximum >= 2;
		batch->setLabel(batchText); grid->setLabel(gridText);
		batch->setVisible(batchOn); grid->setVisible(gridOn);
		const auto both = batchOn && gridOn;
		const auto cellWidth = both ? std::max(1, (width - st::giftActionGap) / 2) : width;
		const auto height = std::max(batchOn ? batch->preferredHeight(cellWidth) : 0,
			gridOn ? grid->preferredHeight(cellWidth) : 0);
		batch->setGeometry(0, 0, cellWidth, height);
		grid->setGeometry(both ? cellWidth + st::giftActionGap : 0, 0,
			both ? width - cellWidth - st::giftActionGap : width, height);
		row->resize(width, height + ((batchOn || gridOn) ? st::giftActionGap : 0));
		wrap->toggle(batchOn || gridOn, anim::type::instant);
	}, row->lifetime());
	grid->setClickedCallback([=] { GiftGrid::Show(original, window, peer); });
	batch->setClickedCallback([=] {
		if (!Enabled().current()) return;
		const auto weakOriginal = base::make_weak(original);
		const auto weakWindow = base::make_weak(window);
		window->show(Box(BatchBox, window, peer, details(), messageAllowed(), maximum,
			Fn<void(int)>([=](int sent) {
				if (!sent) return;
				if (const auto strong = weakOriginal.get()) strong->closeBox();
				if (const auto strong = weakWindow.get()) strong->showPeerHistory(peer);
			})));
	});
}
}
