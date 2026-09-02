/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/spy_ui.h"

#include "fork/deleted_messages.h"
#include "fork/edit_history.h"
#include "fork/fork_lang.h"
#include "fork/message_store.h"
#include "fork/spy_mode.h"
#include "base/unixtime.h"
#include "core/ui_integration.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Fork::SpyUi {
namespace {

// A box is laid out in one go, so a chat with thousands of deleted messages
// would take a while to open. The newest ones are what is being looked for.
constexpr auto kMaxShown = 200;

// The strings are in fork/fork_lang.cpp; the submenu carries the name.
constexpr auto kSubmenu = "SeeGram";

using Lang::Key;

[[nodiscard]] QString FormatDate(TimeId date) {
	return langDateTime(base::unixtime::parse(date));
}

[[nodiscard]] TextWithEntities Display(const Store::Record &record) {
	const auto &text = record.mediaSummary.empty()
		? record.text
		: record.mediaSummary;
	return text.empty()
		? TextWithEntities{ Lang::Text(Key::NoText) }
		: text;
}

void AddEntry(
		not_null<Ui::GenericBox*> box,
		const QString &caption,
		const TextWithEntities &text,
		const Ui::Text::MarkedContext &context) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(box, caption, st::boxDividerLabel),
		style::margins(
			st::boxRowPadding.left(),
			st::boxRowPadding.top(),
			st::boxRowPadding.right(),
			0));
	const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(text),
		st::boxLabel,
		st::defaultPopupMenu,
		context));
	label->setSelectable(true);
}

void ConfirmClear(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		MsgId topicRootId,
		Fn<void()> done) {
	controller->show(Ui::MakeConfirmBox({
		.text = Lang::Value(Key::ClearConfirm),
		.confirmed = [=](Fn<void()> &&close) {
			Deleted::Clear(peer, topicRootId);
			close();
			if (done) {
				done();
			}
		},
		.confirmText = Lang::Value(Key::Clear),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

} // namespace

void ShowEditHistory(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto revisions = Store::For(session).edits(item->fullId());
	if (!revisions) {
		return;
	}
	const auto list = *revisions;
	const auto current = item->originalText();
	const auto edited = item->Get<HistoryMessageEdited>();
	const auto currentCaption = Lang::Text(Key::Current)
		+ u", "_q
		+ FormatDate(edited ? edited->date : item->date());
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(Lang::Value(Key::EditHistory));
		box->setWidth(st::boxWideWidth);
		const auto context = Core::TextContext({ .session = session });
		for (const auto &revision : list) {
			const auto caption = revision.savedAt
				? (Lang::Text(Key::Edited)
					+ u", "_q
					+ FormatDate(revision.savedAt))
				: (Lang::Text(Key::Original)
					+ u", "_q
					+ FormatDate(revision.date));
			AddEntry(box, caption, revision.text, context);
		}
		AddEntry(box, currentCaption, current, context);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void ShowDeletedMessages(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		MsgId topicRootId) {
	const auto session = &peer->session();
	auto records = Store::For(session).deleted(peer->id, topicRootId);
	const auto total = int(records.size());
	if (total > kMaxShown) {
		records.erase(begin(records), begin(records) + (total - kMaxShown));
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(Lang::Value(Key::DeletedMessages));
		box->setWidth(st::boxWideWidth);
		if (records.empty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				Lang::Value(Key::NothingSaved),
				st::boxLabel));
		} else {
			if (total > kMaxShown) {
				box->addRow(object_ptr<Ui::FlatLabel>(
					box,
					Lang::Text(Key::ShowingLast).arg(kMaxShown).arg(total),
					st::boxDividerLabel));
			}
			const auto context = Core::TextContext({ .session = session });
			for (const auto &record : records) {
				const auto from = session->data().peerLoaded(record.from);
				const auto name = from ? from->name() : record.fromName;
				const auto caption = (name.isEmpty() ? QString() : name + u", "_q)
					+ FormatDate(record.date)
					+ u" ("_q
					+ Lang::Text(Key::DeletedAt)
					+ u" "_q
					+ FormatDate(record.savedAt)
					+ u")"_q;
				AddEntry(box, caption, Display(record), context);
			}
			const auto weak = QPointer<Ui::GenericBox>(box);
			box->addLeftButton(Lang::Value(Key::Clear), [=] {
				ConfirmClear(controller, peer, topicRootId, [=] {
					if (const auto strong = weak.data()) {
						strong->closeBox();
					}
				});
			}, st::attentionBoxButton);
		}
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void AddHistoryAction(
		not_null<Ui::PopupMenu*> menu,
		HistoryItem *item,
		not_null<Window::SessionController*> controller) {
	if (!item
		|| item->hideEditedBadge()
		|| !item->Get<HistoryMessageEdited>()
		|| !Edits::HasRevisions(item)) {
		return;
	}
	const auto owner = &item->history()->owner();
	const auto itemId = item->fullId();
	menu->addAction(Lang::Text(Key::EditHistory), [=] {
		if (const auto item = owner->message(itemId)) {
			ShowEditHistory(controller, item);
		}
	}, &st::menuIconSchedule);
}

void AddChatActions(
		PeerData *peer,
		Data::Thread *thread,
		not_null<Window::SessionController*> controller,
		const Ui::Menu::MenuCallback &addAction) {
	if (!peer || !Spy::Current().saveDeletedMessages) {
		return;
	}
	const auto strong = not_null<PeerData*>(peer);
	const auto topic = (peer->isForum() && thread) ? thread->asTopic() : nullptr;
	const auto topicRootId = topic ? topic->rootId() : MsgId(0);
	addAction(Ui::Menu::MenuCallback::Args{
		.text = QString::fromUtf8(kSubmenu),
		.icon = &st::menuIconGroupLog,
		.fillSubmenu = [=](not_null<Ui::PopupMenu*> menu) {
			const auto add = Ui::Menu::CreateAddActionCallback(menu);
			add(Lang::Text(Key::ViewDeleted), [=] {
				ShowDeletedMessages(controller, strong, topicRootId);
			}, &st::menuIconArchive);
			add({
				.text = Lang::Text(Key::ClearDeleted),
				.handler = [=] {
					ConfirmClear(controller, strong, topicRootId, nullptr);
				},
				.icon = &st::menuIconDeleteAttention,
				.isAttention = true,
			});
		},
	});
}

} // namespace Fork::SpyUi
