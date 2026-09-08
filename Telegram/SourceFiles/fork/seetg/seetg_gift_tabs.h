#pragma once
namespace Data { struct UniqueGift; }
namespace Ui { class GenericBox; class VerticalLayout; }
namespace ChatHelpers { class Show; }
namespace Fork::SeeTg::GiftTabs {
[[nodiscard]] not_null<Ui::VerticalLayout*> Add(
	not_null<Ui::GenericBox*> box,
	not_null<Ui::VerticalLayout*> content,
	std::shared_ptr<ChatHelpers::Show> show,
	const Data::UniqueGift &gift);
} // namespace Fork::SeeTg::GiftTabs
