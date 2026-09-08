#pragma once
namespace Main { class Session; }
namespace Ui { class RpWidget; }
namespace Info::PeerGifts { class GiftButton; }
namespace Fork::SeeTg::MarketPreviews {
using Bind = Fn<void(not_null<Info::PeerGifts::GiftButton*>, const QString&)>;
[[nodiscard]] Bind Create(not_null<Ui::RpWidget*> owner, not_null<Main::Session*> session);
} // namespace Fork::SeeTg::MarketPreviews
