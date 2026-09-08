#pragma once
#include <rpl/producer.h>
namespace Ui { class VerticalLayout; class GenericBox; }
namespace Window { class SessionController; }
class PeerData;
namespace Fork::GiftGrid {
void AddSetting(not_null<Ui::VerticalLayout*> content);
rpl::producer<bool> EnabledValue();
void Show(not_null<Ui::GenericBox*> original,
	not_null<Window::SessionController*> window, not_null<PeerData*> peer);
}
