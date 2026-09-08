#pragma once

#include <rpl/producer.h>

namespace Ui { class RpWidget; }
namespace Window { class SessionController; }
class PeerData;

namespace Fork::SeeTg::Reactions {
void Setup(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	rpl::producer<bool> shown,
	rpl::producer<bool> backShown);
} // namespace Fork::SeeTg::Reactions
