#pragma once
#include "info/peer_gifts/info_peer_gifts_common.h"
namespace Payments { enum class CheckoutResult; }
namespace Fork::SeeTg::HiddenShop {
void Load(not_null<PeerData*> peer,
	Fn<void(std::vector<Info::PeerGifts::GiftTypeStars>)> done,
	Fn<void(QString)> fail);
void Send(not_null<Window::SessionController*> window,
	not_null<PeerData*> peer,
	const Info::PeerGifts::GiftSendDetails &details,
	Fn<void(Payments::CheckoutResult)> done);
}
