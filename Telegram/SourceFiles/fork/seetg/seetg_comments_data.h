/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <vector>

namespace Fork::SeeTg::Comments {

constexpr auto kMaxLength = 500;

struct Comment {
	QString id;
	QString body;
	QString createdAt;
	QString parentId;
	QJsonObject author;
	QJsonObject replyTo;
	int replyCount = 0;
	bool pinned = false;
	bool canManage = false;
	bool mine = false;
};

struct Page {
	std::vector<Comment> items;
	QString cursor;
	bool hasMore = false;
};

struct Thread {
	std::vector<Comment> items;
	QString cursor;
	bool open = false;
	bool loaded = false;
	bool loading = false;
	bool hasMore = false;
	bool error = false;
};

[[nodiscard]] Comment ParseComment(const QJsonObject &object);
[[nodiscard]] Page ParsePage(const QJsonObject &object);
void Merge(std::vector<Comment> &items, const std::vector<Comment> &page);
void SortRoots(std::vector<Comment> &items);
void SortReplies(std::vector<Comment> &items);
[[nodiscard]] QString AuthorName(const QJsonObject &author);
[[nodiscard]] QString AuthorUsername(const QJsonObject &author);

[[nodiscard]] QString ListQuery();
[[nodiscard]] QString RepliesQuery();
[[nodiscard]] QString PostMutation();
[[nodiscard]] QString PinMutation();
[[nodiscard]] QString DeleteMutation();

} // namespace Fork::SeeTg::Comments
