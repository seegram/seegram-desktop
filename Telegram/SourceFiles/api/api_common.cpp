/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_common.h"

#include "base/qt/qt_key_modifiers.h"
#include "data/data_histories.h"
#include "data/data_thread.h"
#include "fork/ghost_mode.h"
#include "history/history.h"

namespace Api {

MTPSuggestedPost SuggestToMTP(SuggestOptions suggest) {
	using Flag = MTPDsuggestedPost::Flag;
	return suggest.exists
		? MTP_suggestedPost(
			MTP_flags((suggest.date ? Flag::f_schedule_date : Flag())
				| (suggest.price().empty() ? Flag() : Flag::f_price)),
			StarsAmountToTL(suggest.price()),
			MTP_int(suggest.date))
		: MTPSuggestedPost();
}

SendAction::SendAction(
	not_null<Data::Thread*> thread,
	SendOptions options)
: history(thread->owningHistory())
, options(options)
, replyTo({ .messageId = { history->peer->id, thread->topicRootId() } }) {
	replyTo.topicRootId = replyTo.messageId.msg;
	// Every send path builds one of these, so ghost mode's scheduled sending
	// needs exactly this one hook to cover text, media, albums and forwards.
	Fork::Ghost::ApplyScheduledSend(this->options.scheduled);
}

SendOptions DefaultSendWhenOnlineOptions() {
	return {
		.scheduled = kScheduledUntilOnlineTimestamp,
		.silent = base::IsCtrlPressed(),
	};
}

MTPInputReplyTo SendAction::mtpReplyTo() const {
	return Data::ReplyToForMTP(history, replyTo);
}

} // namespace Api
