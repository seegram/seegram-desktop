#pragma once

namespace ChatHelpers { class Show; }
namespace Ui { class RpWidget; class RoundButton; class TableLayout; }
namespace Fork::SeeTg::GiftDetails {
object_ptr<Ui::RpWidget> Value(
	not_null<Ui::TableLayout*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	const QString &slug,
	object_ptr<Ui::RpWidget> telegram);
void BuyButton(not_null<Ui::RoundButton*> button,
	std::shared_ptr<ChatHelpers::Show> show, const QString &slug, Fn<void()> ready = nullptr);
} // namespace Fork::SeeTg::GiftDetails
