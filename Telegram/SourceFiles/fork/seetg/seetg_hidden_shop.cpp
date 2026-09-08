#include "fork/seetg/seetg_hidden_shop.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_visuals.h"
#include "fork/fork_lang.h"
#include "api/api_global_privacy.h"
#include "api/api_text_entities.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "boxes/star_gift_box.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_credits.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "payments/payments_checkout_process.h"
#include "window/window_session_controller.h"
#include <QtCore/QJsonArray>

namespace Fork::SeeTg::HiddenShop {
void Load(not_null<PeerData*> peer,
	Fn<void(std::vector<Info::PeerGifts::GiftTypeStars>)> done,
	Fn<void(QString)> fail) {
	const auto session = &peer->session();
	Api::FreshQuery(session, u"query HiddenGiftShop { hiddenGiftShop { giftId stars convertStars limited availabilityTotal upgradeStars } }"_q, {},
	crl::guard(session, [=](const QJsonObject &data) {
		auto list = std::vector<Info::PeerGifts::GiftTypeStars>();
		const auto user = peer->asUser();
		using Type = ::Api::DisallowedGiftType;
		const auto disallowed = user ? user->disallowedGiftTypes() : Type();
		for (const auto &entry : data.value(u"hiddenGiftShop"_q).toArray()) {
			const auto object = entry.toObject();
			const auto id = object.value(u"giftId"_q).toString().toULongLong();
			const auto stars = object.value(u"stars"_q).toString().toLongLong();
			const auto limited = object.value(u"limited"_q).toBool();
			if (!id || stars <= 0 || stars > 1000000000) continue;
			if (!peer->isSelf() && (disallowed & (limited ? Type::Limited : Type::Unlimited))) continue;
			// A local preview document, never a Telegram document/access hash. Only the
			// original gift ID goes into the payment invoice.
			const auto document = session->data().document(base::RandomValue<DocumentId>());
			document->date = base::unixtime::now();
			document->setMimeString(u"application/x-tgsticker"_q);
			document->setattributes({});
			document->setContentUrl(Visuals::OriginalAnimationUrl(id));
			auto gift = Info::PeerGifts::GiftTypeStars{ .info = Data::StarGift{ .document = document } };
			gift.info.id = id;
			gift.info.stars = stars;
			gift.info.starsConverted = object.value(u"convertStars"_q).toString().toLongLong();
			// Holdings do not tell us remaining stock. Do not invent a stock counter.
			gift.hiddenPurchase = true;
			list.push_back(std::move(gift));
		}
		done(std::move(list));
	}), [=](const Api::Error &error) { fail(error.message); });
}

void Send(not_null<Window::SessionController*> window,
	not_null<PeerData*> peer,
	const Info::PeerGifts::GiftSendDetails &details,
	Fn<void(Payments::CheckoutResult)> done) {
	const auto gift = std::get<Info::PeerGifts::GiftTypeStars>(details.descriptor);
	const auto show = window->uiShow();
	using Flag = MTPDinputInvoiceStarGift::Flag;
	const auto invoice = MTP_inputInvoiceStarGift(
		MTP_flags((details.anonymous ? Flag::f_hide_name : Flag(0))
			| (details.text.empty() ? Flag(0) : Flag::f_message)),
		peer->input(), MTP_long(gift.info.id),
		MTP_textWithEntities(MTP_string(details.text.text),
			::Api::EntitiesToMTP(&peer->session(), details.text.entities, ::Api::ConvertOption::SkipLocal)));
	Ui::RequestOurForm(show, invoice, [=](uint64 formId, CreditsAmount price,
			std::optional<Payments::CheckoutResult> failure) {
		if (failure) { done(*failure); return; }
		if (!show->valid() || !price.stars() || price.whole() != gift.info.stars) {
			if (show->valid()) show->showToast(Lang::Text(Lang::Key::GiftBatchPriceChanged));
			done(Payments::CheckoutResult::Failed); return;
		}
		Ui::SubmitStarsForm(show, invoice, formId, price.whole(),
			[=](Payments::CheckoutResult result, const MTPUpdates *) { done(result); });
	});
}
}
