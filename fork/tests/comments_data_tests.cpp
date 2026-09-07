#include "fork/seetg/seetg_comments_data.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <iostream>
#include <stdexcept>

using namespace Fork::SeeTg::Comments;

void Check(bool value, const char *message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

int main(int argc, char **argv) {
	const auto app = QCoreApplication(argc, argv);
	auto page = ParsePage(QJsonDocument::fromJson(R"({
		"items": [
			{"id":"9007199254740993", "body":"newer"},
			{"id":"9007199254740992", "body":"older"},
			{"id":"5", "body":"pinned", "pinned":true},
			{"body":"missing id"}
		],
		"pageInfo":{"endCursor":"opaque", "hasNextPage":true}
	})").object());
	Check(page.items.size() == 3, "Malformed rows must be skipped");
	Check(page.hasMore && page.cursor == u"opaque"_q, "Keep the server cursor");
	SortRoots(page.items);
	Check(page.items[0].id == u"5"_q, "Pinned roots go first");
	Check(page.items[1].body == u"newer"_q, "IDs must not lose 64-bit precision");

	const auto stale = ParseComment({ { u"id"_q, u"5"_q }, { u"pinned"_q, false } });
	Merge(page.items, { stale, ParseComment({ { u"id"_q, u"4"_q } }) });
	Check(page.items.size() == 4, "Overlapping pages must not duplicate comments");
	Check(page.items[0].pinned, "A late page must not undo local pin state");
	Check(!ParsePage(QJsonDocument::fromJson(R"({"pageInfo":{"hasNextPage":true}})").object()).hasMore,
		"A missing cursor must not trigger repeated first-page requests");

	auto replies = std::vector<Comment>{ ParseComment({ { u"id"_q, u"30"_q } }) };
	Merge(replies, { ParseComment({ { u"id"_q, u"20"_q } }), replies.front() });
	SortReplies(replies);
	Check(replies.size() == 2 && replies.front().id == u"20"_q,
		"Older reply pages must merge ahead of a newly posted reply");
	const auto reply = ParseComment(QJsonDocument::fromJson(R"({
		"id":"31", "parentId":"5", "body":"<b>plain</b>\n🙂",
		"replyTo":{"telegramId":"8", "name":"Answered author"},
		"author":{"telegramId":"9", "name":"Writer"},
		"mine":true, "canManage":false, "replyCount":-1
	})").object());
	Check(reply.parentId == u"5"_q && AuthorName(reply.replyTo) == u"Answered author"_q,
		"Reply-to-reply must retain both root and answered author");
	Check(reply.body == u"<b>plain</b>\n🙂"_q, "Comments must retain plain Unicode text");
	Check(reply.mine && !reply.canManage && !reply.replyCount, "Keep author and owner permissions distinct");
	const auto author = QJsonDocument::fromJson(R"({"usernames":["@fallback"], "title":"Channel"})").object();
	Check(AuthorUsername(author) == u"fallback"_q, "Avatar URLs need the username fallback without @");
	Check(AuthorName(author) == u"Channel"_q, "Channel authors use their title");

	if (argc == 2) {
		auto output = QFile(QString::fromLocal8Bit(argv[1]));
		Check(output.open(QIODevice::WriteOnly), "Cannot write GraphQL operations");
		output.write(QJsonDocument(QJsonArray{
			ListQuery(), RepliesQuery(), PostMutation(), PinMutation(), DeleteMutation(),
		}).toJson());
	}
	std::cout << "Comment pagination, replies, permissions and Unicode: passed\n";
}
