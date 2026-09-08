/*
This file is part of SeeGram Desktop, a Telegram Desktop fork.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/scheduled_preview.h"
#include "fork/disguise.h"
#include "fork/ghost_notifications.h"

#include "api/api_common.h"
#include "base/unixtime.h"
#include "base/timer.h"
#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "data/components/scheduled_messages.h"
#include "history/history_item_helpers.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_scheduled_section.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

namespace Fork::ScheduledPreview {
namespace {

// Entries are erased from HistoryItem's destructor, including session teardown.
base::flat_map<const HistoryItem*, FullMsgId> Previews;
base::flat_map<const HistoryItem*, FullMsgId> Sources;
std::unique_ptr<base::Timer> CountdownTimer;

void StartCountdown() {
	if (!CountdownTimer) {
		CountdownTimer = std::make_unique<base::Timer>([] {
			// Notifications may rebuild views: collect ids before calling them.
			auto items = std::vector<std::pair<Main::Session*, FullMsgId>>();
			for (const auto &[preview, source] : Sources) {
				items.emplace_back(&preview->history()->session(), preview->fullId());
			}
			for (const auto &[session, id] : items) {
				if (const auto preview = session->data().message(id); Is(preview)) {
					session->data().notifyItemDataChange(preview);
				}
			}
		});
	}
	if (!CountdownTimer->isActive()) {
		CountdownTimer->callEach(1000);
	}
}

void Open(not_null<Window::SessionController*> controller, FullMsgId id) {
	const auto preview = controller->session().data().message(id);
	const auto i = Sources.find(preview);
	const auto source = (i != end(Sources))
		? controller->session().data().message(i->second)
		: nullptr;
	if (!source || !source->isScheduled()) {
		return;
	}
	controller->showSection(std::make_shared<HistoryView::ScheduledMemento>(
		source->history(), source->id));
}

} // namespace

bool Is(const HistoryItem *item) {
	return Disguise::FeaturesEnabled() && item && Sources.contains(item);
}

TimeId Deadline(not_null<const HistoryItem*> item) {
	const auto i = Sources.find(item.get());
	const auto source = (i != end(Sources))
		? item->history()->owner().message(i->second)
		: nullptr;
	if (!source || source->isSending()) {
		return 0;
	}
	return source->hasFailed() ? -1 : source->date();
}

QString CountdownText(TimeId deadline) {
	if (deadline < 0) {
		return tr::lng_sr_chat_failed(tr::now);
	}
	const auto seconds = std::max(TimeId(0), deadline - base::unixtime::now());
	return seconds ? tr::lng_seconds_tiny(tr::now, lt_count, seconds)
		: tr::lng_sr_chat_sending(tr::now);
}

void Track(HistoryItem *item, const Api::SendOptions &options) {
	if (!options.ghostScheduled || !item || !item->isScheduled()
		|| Previews.contains(item)) {
		return;
	}
	GhostNotifications::Remember(item);
	const auto history = item->history();
	const auto preview = history->makeMessage(HistoryItemCommonFields{
		.id = history->owner().nextLocalMessageId(),
		.flags = MessageFlag::Local | MessageFlag::HistoryEntry
			| MessageFlag::FakeHistoryItem | MessageFlag::Outgoing
			| MessageFlag::HasFromId,
		.from = item->from()->id,
		.replyTo = item->replyTo(),
		.date = base::unixtime::now(),
		.groupedId = item->groupId().raw(),
		.ignoreForwardFrom = !item->Get<HistoryMessageForwarded>(),
	}, not_null<HistoryItem*>(item));
	Sources.emplace(preview.get(), item->fullId());
	Previews.emplace(item, preview->fullId());
	StartCountdown();
	history->addNewLocalMessage(preview);
	history->owner().sendHistoryChangeNotifications();
}

void TrackResult(not_null<History*> history, const MTPUpdates &result,
		const Api::SendOptions &options) {
	if (!options.ghostScheduled) {
		return;
	}
	const auto append = [&](const MTPUpdate &update) {
		update.match([&](const MTPDupdateNewScheduledMessage &data) {
			const auto &message = data.vmessage();
			if (PeerFromMessage(message) != history->peer->id) {
				return;
			}
			message.match([&](const auto &fields) {
				const auto localId = history->session().scheduledMessages()
					.localMessageId(fields.vid().v);
				Track(history->owner().message({ history->peer->id, localId }), options);
			});
		}, [](const auto &) {});
	};
	result.match([&](const auto &data) {
		if constexpr (requires { data.vupdates(); }) {
			for (const auto &update : data.vupdates().v) {
				append(update);
			}
		} else if constexpr (requires { data.vupdate(); }) {
			append(data.vupdate());
		}
	});
}

void Refresh(not_null<HistoryItem*> source) {
	GhostNotifications::Refresh(source);
	const auto i = Previews.find(source.get());
	if (i == end(Previews)) {
		return;
	}
	if (const auto preview = source->history()->owner().message(i->second)) {
		Sources[preview] = source->fullId();
		if (const auto page = source->richPage()) {
			preview->applyLocalRichPage(page);
		} else {
			preview->setText(source->originalText());
		}
		preview->history()->owner().requestItemTextRefresh(preview);
		preview->history()->owner().notifyItemDataChange(preview);
	}
}

void Rebind(not_null<HistoryItem*> from, HistoryItem *to) {
	GhostNotifications::Rebind(from, to);
	const auto i = Previews.find(from.get());
	if (i == end(Previews) || !to) {
		return;
	}
	if (to != from && Previews.contains(to)) {
		Forget(from);
		return;
	}
	const auto id = i->second;
	Previews.erase(i);
	Previews.emplace(to, id);
	Refresh(to);
}

void Forget(not_null<const HistoryItem*> item) {
	GhostNotifications::Forget(item);
	if (Is(item)) {
		Sources.remove(item.get());
		if (Sources.empty() && CountdownTimer) {
			CountdownTimer->cancel();
		}
		// The chat can be cleared independently of its scheduled messages.
		for (auto i = begin(Previews); i != end(Previews); ++i) {
			if (i->second == item->fullId()) {
				Previews.erase(i);
				break;
			}
		}
	}
	const auto i = Previews.find(item.get());
	if (i == end(Previews)) {
		return;
	}
	const auto id = i->second;
	Previews.erase(i);
	const auto session = &item->history()->session();
	// Defer removal until the scheduled list finishes its own erase operation.
	crl::on_main(session, [=] {
		if (const auto preview = session->data().message(id); Is(preview)) {
			session->data().destroyMessageWithCacheCleanup(preview);
		}
	});
}

ClickHandlerPtr Link(not_null<const HistoryItem*> item) {
	if (!Is(item)) {
		return nullptr;
	}
	const auto id = item->fullId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			Open(controller, id);
		}
	});
}

bool FillMenu(not_null<Ui::PopupMenu*> menu, HistoryItem *item,
		not_null<Window::SessionController*> controller) {
	if (!Is(item)) {
		return false;
	}
	const auto id = item->fullId();
	menu->addAction(tr::lng_scheduled_messages(tr::now), crl::guard(controller, [=] {
		Open(controller, id);
	}), &st::menuIconSchedule);
	return true;
}

} // namespace Fork::ScheduledPreview
