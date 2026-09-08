/*
This file is part of SeeGram Desktop, a Telegram Desktop fork.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/expired_media.h"

#include "fork/spy_mode.h"
#include "core/click_handler_types.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace Fork::ExpiredMedia {
namespace {
struct Retained {
	std::shared_ptr<Data::PhotoMedia> photo;
	std::shared_ptr<Data::DocumentMedia> document;
	std::optional<PreparedServiceText> baseText;
};
base::flat_map<const HistoryItem*, Retained> Media;
base::flat_set<Main::Session*> Observed;

void Observe(not_null<Main::Session*> session) {
	if (!Observed.emplace(session.get()).second) {
		return;
	}
	session->lifetime().add([=] { Observed.remove(session.get()); });
	Spy::Changes() | rpl::map([](const Spy::Settings &settings) {
		return settings.previewSelfDestructMedia;
	}) | rpl::distinct_until_changed() | rpl::on_next([=] {
		auto changes = std::vector<std::pair<FullMsgId, PreparedServiceText>>();
		for (const auto &[item, saved] : Media) {
			if (&item->history()->session() == session && saved.baseText) {
				changes.emplace_back(item->fullId(), *saved.baseText);
			}
		}
		for (auto &[id, text] : changes) {
			if (const auto item = session->data().message(id)) {
				item->updateServiceText(std::move(text));
			}
		}
	}, session->lifetime());
}

} // namespace

bool Capture(not_null<HistoryItem*> item, Data::Media *media) {
	if (!Spy::Current().previewSelfDestructMedia || !media || !media->ttlSeconds()) {
		return false;
	}
	auto retained = Retained();
	if (const auto photo = media->photo()) {
		retained.photo = photo->activeMediaView();
		if (!retained.photo || !retained.photo->loaded()) {
			return false;
		}
	} else if (const auto document = media->document()) {
		retained.document = document->activeMediaView();
		if (!retained.document || !retained.document->loaded()) {
			return false;
		}
	} else {
		return false;
	}
	Media[item.get()] = std::move(retained);
	Observe(&item->history()->session());
	return true;
}

void Decorate(not_null<HistoryItem*> item, PreparedServiceText &text) {
	const auto saved = Media.find(item.get());
	if (saved == end(Media)) {
		return;
	}
	saved->second.baseText = text;
	if (!Spy::Current().previewSelfDestructMedia) {
		return;
	}
	const auto id = item->fullId();
	const auto session = &item->history()->session();
	text.text.append(u"  ·  "_q).append(tr::link(
		tr::lng_open_link(tr::now), int(text.links.size()) + 1));
	text.links.push_back(std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		if (!controller || &controller->session() != session
			|| !Spy::Current().previewSelfDestructMedia) {
			return;
		}
		const auto current = session->data().message(id);
		const auto i = Media.find(current);
		if (i == end(Media)) {
			return;
		}
		// Keep the bytes alive while the viewer acquires its own reference.
		const auto retained = i->second;
		if (retained.photo) {
			controller->openPhoto(retained.photo->owner(), { .id = id });
		} else if (retained.document) {
			controller->openDocument(retained.document->owner(), true, { .id = id });
		}
	}));
}

void Forget(not_null<const HistoryItem*> item) {
	Media.remove(item.get());
}
} // namespace Fork::ExpiredMedia
