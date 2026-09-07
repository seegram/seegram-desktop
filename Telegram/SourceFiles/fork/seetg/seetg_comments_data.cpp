/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_comments_data.h"

#include <QtCore/QJsonArray>
#include <algorithm>

namespace Fork::SeeTg::Comments {
namespace {

const auto Fields = u"id body pinned createdAt canManage mine parentId replyCount "
	"author { seeId telegramId telegramType username usernames name title "
	"verifications { type slot description descriptionTranslations warning } } "
	"replyTo { seeId telegramId telegramType username name title }"_q;

} // namespace

Comment ParseComment(const QJsonObject &object) {
	return {
		.id = object.value(u"id"_q).toString(),
		.body = object.value(u"body"_q).toString(),
		.createdAt = object.value(u"createdAt"_q).toString(),
		.parentId = object.value(u"parentId"_q).toString(),
		.author = object.value(u"author"_q).toObject(),
		.replyTo = object.value(u"replyTo"_q).toObject(),
		.replyCount = std::max(0, object.value(u"replyCount"_q).toInt()),
		.pinned = object.value(u"pinned"_q).toBool(),
		.canManage = object.value(u"canManage"_q).toBool(),
		.mine = object.value(u"mine"_q).toBool(),
	};
}

Page ParsePage(const QJsonObject &object) {
	auto result = Page();
	for (const auto &value : object.value(u"items"_q).toArray()) {
		auto item = ParseComment(value.toObject());
		if (!item.id.isEmpty()) {
			result.items.push_back(std::move(item));
		}
	}
	const auto info = object.value(u"pageInfo"_q).toObject();
	result.cursor = info.value(u"endCursor"_q).toString();
	result.hasMore = info.value(u"hasNextPage"_q).toBool()
		&& !result.cursor.isEmpty();
	return result;
}

void Merge(std::vector<Comment> &items, const std::vector<Comment> &page) {
	for (const auto &item : page) {
		const auto i = std::find_if(items.begin(), items.end(), [&](const Comment &other) {
			return item.id == other.id;
		});
		if (i == items.end()) {
			items.push_back(item);
		}
	}
}

void SortRoots(std::vector<Comment> &items) {
	std::stable_sort(items.begin(), items.end(), [](const Comment &a, const Comment &b) {
		return (a.pinned != b.pinned) ? a.pinned : a.id.toULongLong() > b.id.toULongLong();
	});
}

void SortReplies(std::vector<Comment> &items) {
	std::stable_sort(items.begin(), items.end(), [](const Comment &a, const Comment &b) {
		return a.id.toULongLong() < b.id.toULongLong();
	});
}

QString AuthorUsername(const QJsonObject &author) {
	auto result = author.value(u"username"_q).toString();
	if (result.isEmpty()) {
		const auto usernames = author.value(u"usernames"_q).toArray();
		if (!usernames.isEmpty()) {
			result = usernames.first().toString();
		}
	}
	while (result.startsWith('@')) {
		result.remove(0, 1);
	}
	return result;
}

QString AuthorName(const QJsonObject &author) {
	for (const auto &key : { u"name"_q, u"title"_q }) {
		const auto value = author.value(key).toString();
		if (!value.isEmpty()) {
			return value;
		}
	}
	const auto username = AuthorUsername(author);
	return !username.isEmpty() ? '@' + username
		: author.value(u"telegramId"_q).toString();
}

QString ListQuery() {
	return u"query Comments($targetId: String!, $after: String) { "
		"comments(target: OWNER, targetId: $targetId, after: $after) { items { "_q
		+ Fields + u" } pageInfo { endCursor hasNextPage } } }"_q;
}

QString RepliesQuery() {
	return u"query CommentReplies($id: String!, $after: String) { "
		"commentReplies(id: $id, after: $after) { items { "_q
		+ Fields + u" } pageInfo { endCursor hasNextPage } } }"_q;
}

QString PostMutation() {
	return u"mutation PostComment($targetId: String!, $body: String!, $replyToId: String) { "
		"postComment(target: OWNER, targetId: $targetId, body: $body, replyToId: $replyToId) { "_q
		+ Fields + u" } }"_q;
}

QString PinMutation() {
	return u"mutation PinComment($id: String!, $pinned: Boolean!) { "
		"pinComment(id: $id, pinned: $pinned) }"_q;
}

QString DeleteMutation() {
	return u"mutation DeleteComment($id: String!) { deleteComment(id: $id) }"_q;
}

} // namespace Fork::SeeTg::Comments
