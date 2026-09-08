#pragma once
#include "settings/settings_type.h"
#include <rpl/producer.h>
namespace Main { class SessionShow; }
namespace Ui { class GenericBox; class VerticalLayout; }
namespace Window { class SessionController; }
namespace Info::PeerGifts { struct GiftSendDetails; }
class PeerData;
namespace Fork::GiftBatch {
[[nodiscard]] ::Settings::Type SectionId();
bool HiddenEnabled();
rpl::producer<bool> HiddenEnabledValue();
Fn<void()> SendSequence(not_null<PeerData*> peer,
	std::shared_ptr<Main::SessionShow> show,
	std::vector<Info::PeerGifts::GiftSendDetails> gifts,
	Fn<void(QString)> progress, Fn<void(int)> completed);
void AddButton(not_null<Ui::VerticalLayout*> container,
	not_null<Ui::GenericBox*> original,
	not_null<Window::SessionController*> window,
	not_null<PeerData*> peer,
	Fn<Info::PeerGifts::GiftSendDetails()> details,
	Fn<bool()> messageAllowed);
}
