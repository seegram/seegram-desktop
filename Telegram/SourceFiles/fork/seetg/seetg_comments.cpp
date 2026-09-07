/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_comments.h"

#include "fork/fork_lang.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_comments_data.h"
#include "fork/seetg/seetg_peers.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_types.h"
#include "fork/seetg/seetg_verifications.h"
#include "fork/seetg/seetg_visuals.h"
#include "base/weak_ptr.h"
#include "data/data_peer.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_icon.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/text/text.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_seetg_comments.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QDateTime>
#include <QtCore/QLocale>
#include <QtCore/QTimer>
#include <QtCore/QSet>
#include <QtGui/QPainterPath>

namespace Fork::SeeTg::Comments {
namespace {

using Lang::Key;

[[nodiscard]] QString Name(const QJsonObject &author) {
	const auto name = AuthorName(author);
	return name.isEmpty() ? Lang::Text(Key::SeeTgHistoryUnknownOwner) : name;
}

[[nodiscard]] QString Time(const Comment &comment) {
	const auto date = QDateTime::fromString(comment.createdAt, Qt::ISODateWithMs).toLocalTime();
	if (!date.isValid()) {
		return {};
	}
	if (date.secsTo(QDateTime::currentDateTime()) < 60) {
		return Lang::Text(Key::SeeTgCommentJustNow);
	}
	return QLocale(Lang::ResolvedId()).toString(date, u"dd MMM HH:mm"_q);
}

[[nodiscard]] QString ActionError(const Api::Error &error, crl::time retryAt) {
	if (retryAt > crl::now()) {
		const auto seconds = int((retryAt - crl::now() + 999) / 1000);
		const auto duration = Lang::Text(seconds >= 60
			? Key::SeeTgCommentUnitMin : Key::SeeTgCommentUnitSec)
			.replace(u"{n}"_q, QString::number(seconds >= 60 ? (seconds + 59) / 60 : seconds));
		return Lang::Text(Key::SeeTgCommentFloodWait).replace(u"{t}"_q, duration);
	}
	return Lang::Text(error.message == u"blocked"_q
		? Key::SeeTgCommentBlocked
		: error.kind == Api::Error::Kind::Auth
		? Key::SeeTgErrorAuth
		: Key::SeeTgCommentsActionFailed);
}

class PaintButton final : public Ui::AbstractButton {
public:
	PaintButton(QWidget *parent, Fn<void(QPainter&, QRect)> paint)
	: AbstractButton(parent), _paint(std::move(paint)) {
		setCursor(Qt::PointingHandCursor);
		setFocusPolicy(Qt::StrongFocus);
	}

protected:
	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		_paint(p, rect());
	}

private:
	Fn<void(QPainter&, QRect)> _paint;

};

} // namespace

class State final : public std::enable_shared_from_this<State> {
public:
	explicit State(not_null<PeerData*> peer)
	: session(base::make_weak(&peer->session())), seeId(SeeId(peer->id)) {
	}

	void load(bool more = false) {
		if (!session || loading || !busy.isEmpty() || (more && !hasMore)) {
			return;
		}
		loading = true;
		loadingMore = more;
		loadError = false;
		const auto self = shared_from_this();
		changes.fire({});
		Api::FreshQuery(session.get(), ListQuery(), {
			{ u"targetId"_q, seeId },
			{ u"after"_q, more ? QJsonValue(cursor) : QJsonValue(QJsonValue::Null) },
		}, [self, more](const QJsonObject &data) {
			auto page = ParsePage(data.value(u"comments"_q).toObject());
			if (!more) {
				++self->generation;
				self->items.clear();
				self->threads.clear();
			}
			self->merge(self->items, page.items);
			SortRoots(self->items);
			self->cursor = page.cursor;
			self->hasMore = page.hasMore;
			self->loading = false;
			self->loaded = true;
			self->changes.fire({});
		}, [self](const Api::Error &) {
			self->loading = false;
			self->loadError = true;
			self->changes.fire({});
		});
	}

	void loadThread(QString id, bool more = false) {
		auto &thread = threads[id];
		if (!session || thread.loading || (more && !thread.hasMore)) {
			return;
		}
		thread.loading = true;
		thread.error = false;
		const auto epoch = generation;
		const auto after = more ? QJsonValue(thread.cursor) : QJsonValue(QJsonValue::Null);
		const auto self = shared_from_this();
		changes.fire({});
		Api::FreshQuery(session.get(), RepliesQuery(), {
			{ u"id"_q, id }, { u"after"_q, after },
		}, [self, id, epoch](const QJsonObject &data) {
			if (self->generation != epoch || !self->threads.contains(id)) {
				return;
			}
			auto page = ParsePage(data.value(u"commentReplies"_q).toObject());
			auto &thread = self->threads[id];
			self->merge(thread.items, page.items);
			SortReplies(thread.items);
			thread.cursor = page.cursor;
			thread.hasMore = page.hasMore;
			thread.loading = false;
			thread.loaded = true;
			self->changes.fire({});
		}, [self, id, epoch](const Api::Error &) {
			if (self->generation == epoch && self->threads.contains(id)) {
				self->threads[id].loading = false;
				self->threads[id].error = true;
				self->changes.fire({});
			}
		});
	}

	void toggle(Comment root) {
		auto &thread = threads[root.id];
		thread.open = !thread.open;
		if (thread.open && !thread.loaded) {
			loadThread(root.id);
		} else {
			changes.fire({});
		}
	}

	void fail(const Api::Error &error) {
		busy.clear();
		actionError = error;
		if (error.message.startsWith(u"floodwait:"_q)) {
			retryAt = crl::now() + std::clamp(error.message.mid(10).toLongLong(), 0LL, 86400LL) * 1000;
		}
		changes.fire({});
	}

	void post() {
		const auto body = draft.trimmed();
		if (!session || !busy.isEmpty() || loading || body.isEmpty() || body.size() > kMaxLength || retryAt > crl::now()) {
			return;
		}
		busy = u"post"_q;
		actionError.reset();
		const auto self = shared_from_this();
		const auto target = replying ? QJsonValue(replying->id) : QJsonValue(QJsonValue::Null);
		changes.fire({});
		Api::Mutation(session.get(), PostMutation(), {
			{ u"targetId"_q, seeId }, { u"body"_q, body }, { u"replyToId"_q, target },
		}, [self](const QJsonObject &data) {
			const auto item = ParseComment(data.value(u"postComment"_q).toObject());
			if (item.id.isEmpty()) {
				self->fail({ Api::Error::Kind::Other, {} });
				return;
			}
			self->busy.clear();
			self->draft.clear();
			self->replying.reset();
			self->loaded = true;
			auto fetchThread = false;
			if (!item.parentId.isEmpty()) {
				for (auto &root : self->items) {
					if (root.id == item.parentId) {
						fetchThread = root.replyCount > 0;
						++root.replyCount;
					}
				}
				auto &thread = self->threads[item.parentId];
				thread.open = true;
				Merge(thread.items, { item });
				SortReplies(thread.items);
				fetchThread = fetchThread && !thread.loaded && !thread.loading;
				if (!fetchThread && !thread.loading) {
					thread.loaded = true;
				}
			} else {
				Merge(self->items, { item });
				SortRoots(self->items);
			}
			self->changes.fire({});
			if (fetchThread) {
				self->loadThread(item.parentId);
			}
		}, [self](const Api::Error &error) { self->fail(error); });
	}

	void pin(Comment item) {
		if (!session || !busy.isEmpty() || loading || !item.canManage || !item.parentId.isEmpty()) {
			return;
		}
		busy = item.id;
		actionError.reset();
		const auto self = shared_from_this();
		changes.fire({});
		Api::Mutation(session.get(), PinMutation(), {
			{ u"id"_q, item.id }, { u"pinned"_q, !item.pinned },
		}, [self, item](const QJsonObject &data) {
			if (!data.value(u"pinComment"_q).toBool()) {
				self->fail({ Api::Error::Kind::Other, {} });
				return;
			}
			for (auto &root : self->items) {
				if (root.id == item.id) {
					root.pinned = !item.pinned;
				}
			}
			SortRoots(self->items);
			self->busy.clear();
			self->changes.fire({});
		}, [self](const Api::Error &error) { self->fail(error); });
	}

	void remove(Comment item) {
		if (!session || !busy.isEmpty() || loading || (!item.canManage && !item.mine)) {
			return;
		}
		busy = item.id;
		actionError.reset();
		const auto self = shared_from_this();
		changes.fire({});
		Api::Mutation(session.get(), DeleteMutation(), { { u"id"_q, item.id } },
		[self, item](const QJsonObject &data) {
			if (!data.value(u"deleteComment"_q).toBool()) {
				self->fail({ Api::Error::Kind::Other, {} });
				return;
			}
			self->deleted.insert(item.id);
			const auto erase = [&](std::vector<Comment> &list) {
				list.erase(std::remove_if(list.begin(), list.end(), [&](const Comment &other) {
					return other.id == item.id;
				}), list.end());
			};
			if (item.parentId.isEmpty()) {
				erase(self->items);
				self->threads.remove(item.id);
			} else {
				erase(self->threads[item.parentId].items);
				for (auto &root : self->items) {
					if (root.id == item.parentId) {
						root.replyCount = std::max(0, root.replyCount - 1);
					}
				}
			}
			if (self->replying && (self->replying->id == item.id || self->replying->parentId == item.id)) {
				self->replying.reset();
			}
			self->busy.clear();
			self->changes.fire({});
		}, [self](const Api::Error &error) { self->fail(error); });
	}

	void merge(std::vector<Comment> &list, const std::vector<Comment> &page) {
		auto filtered = page;
		filtered.erase(std::remove_if(filtered.begin(), filtered.end(), [&](const Comment &item) {
			return deleted.contains(item.id) || deleted.contains(item.parentId);
		}), filtered.end());
		Merge(list, filtered);
	}

	base::weak_ptr<Main::Session> session;
	QString seeId;
	QSet<QString> deleted;
	int generation = 0;
	std::vector<Comment> items;
	QMap<QString, Thread> threads;
	QString cursor;
	QString draft;
	QString busy;
	std::optional<Comment> replying;
	std::optional<Api::Error> actionError;
	crl::time retryAt = 0;
	bool loaded = false;
	bool loading = false;
	bool loadingMore = false;
	bool loadError = false;
	bool hasMore = false;
	rpl::event_stream<> changes;

};

namespace {

class Row final : public Ui::RpWidget {
public:
	Row(QWidget *parent, Comment item,
		Fn<void(QJsonObject)> openAuthor, Fn<void()> reply, Fn<void(QPoint)> actions)
	: RpWidget(parent), _item(std::move(item)) {
		const auto name = Name(_item.author);
		const auto entries = Verification::Parse(_item.author);
		_avatar = Ui::CreateChild<PaintButton>(this, [=](QPainter &p, QRect rect) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive->c);
			p.drawEllipse(rect);
			p.setFont(st::semiboldFont->f);
			p.setPen(st::windowFgActive->c);
			p.drawText(rect, Qt::AlignCenter, name.left(name.front().isHighSurrogate() ? 2 : 1).toUpper());
			if (!_image.isNull()) {
				auto clip = QPainterPath();
				clip.addEllipse(rect);
				p.setClipPath(clip);
				const auto side = std::min(_image.width(), _image.height());
				p.drawImage(rect, _image, QRect((_image.width() - side) / 2, (_image.height() - side) / 2, side, side));
			}
		});
		_avatar->setAccessibleName(name);
		_avatar->addClickHandler([=] { openAuthor(_item.author); });
		const auto username = AuthorUsername(_item.author);
		if (!username.isEmpty()) {
			Visuals::Image(Visuals::UserpicUrl(username), crl::guard(this, [=](QImage image) {
				if (image.width() > 1 && image.height() > 1) {
					_image = std::move(image);
					_avatar->update();
				}
			}));
		}
		_name = Ui::CreateChild<PaintButton>(this, [=](QPainter &p, QRect rect) {
			const auto size = st::seetgCommentBadge;
			const auto step = size + st::lineWidth * 3;
			const auto count = std::min(int(entries.size()), std::max(0, (rect.width() - size * 3) / step));
			auto x = 0;
			auto painted = 0;
			const auto badges = [&](const QString &slot) {
				for (const auto &entry : entries) {
					if (entry.slot == slot && painted < count) {
						Verification::PaintBadge(p, entry,
							style::rtlrect(x, (rect.height() - size) / 2, size, size, rect.width()),
							st::profileVerifiedCheckBg->c);
						x += step;
						++painted;
					}
				}
			};
			badges(u"left"_q);
			p.setFont(st::semiboldFont->f);
			p.setPen(st::windowFg->c);
			const auto available = std::max(0, rect.width() - x - (count - painted) * step);
			const auto shown = st::semiboldFont->elided(name, available);
			const auto textWidth = st::semiboldFont->width(shown);
			p.drawText(style::rtlrect(x, 0, available, rect.height(), rect.width()),
				(rtl() ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, shown);
			x += textWidth + st::lineWidth * 3;
			badges(u"right"_q);
		});
		_name->setAccessibleName(name);
		_name->setToolTip(name);
		_name->addClickHandler([=] { openAuthor(_item.author); });
		auto body = TextWithEntities{ _item.body };
		if (!_item.replyTo.isEmpty()) {
			const auto tag = '@' + Name(_item.replyTo);
			body.text = tag + ' ' + body.text;
			body.entities.push_back({ EntityType::CustomUrl, 0, int(tag.size()), u"seetg:comment-author"_q });
		}
		_body = Ui::CreateChild<Ui::FlatLabel>(this, rpl::single(body), st::seetgCommentText);
		_body->setSelectable(true);
		if (!_item.replyTo.isEmpty()) {
			_body->setLink(1, std::make_shared<LambdaClickHandler>([=] { openAuthor(_item.replyTo); }));
		}
		_reply = Ui::CreateChild<Ui::LinkButton>(this, Lang::Text(Key::SeeTgCommentReply), st::seetgCommentReply);
		_reply->addClickHandler(std::move(reply));
		_time = Ui::CreateChild<Ui::FlatLabel>(this, rpl::single(Time(_item)
			+ (_item.pinned ? u" · "_q + Lang::Text(Key::SeeTgCommentPinnedBadge) : QString())), st::seetgCommentMeta);
		if (_item.canManage || _item.mine) {
			_more = Ui::CreateChild<Ui::LinkButton>(this, u"⋯"_q);
			_more->setAccessibleName(Lang::Text(Key::SeeTgCommentActions));
			_more->addClickHandler([=] { actions(_more->mapToGlobal(QPoint(0, _more->height()))); });
		}
		style::PaletteChanged() | rpl::on_next([=] {
			_avatar->update();
			_name->update();
		}, lifetime());
	}

protected:
	int resizeGetHeight(int width) override {
		const auto avatar = st::seetgCommentAvatar;
		const auto left = avatar + st::seetgCommentAvatarSkip;
		const auto available = std::max(1, width - left);
		const auto nameHeight = std::max(st::semiboldFont->height, st::seetgCommentBadge);
		_avatar->resize(avatar, avatar);
		_avatar->moveToLeft(0, 0, width);
		_name->resize(available, nameHeight);
		_name->moveToLeft(left, 0, width);
		_body->resizeToWidth(available);
		_body->moveToLeft(left, nameHeight + st::lineWidth * 3, width);
		const auto foot = nameHeight + st::lineWidth * 3 + _body->height() + st::lineWidth * 5;
		_reply->moveToLeft(left, foot, width);
		const auto timeLeft = left + _reply->width() + st::lineWidth * 8;
		_time->resizeToWidth(std::max(1, width - timeLeft - (_more ? _more->width() + st::lineWidth * 4 : 0)));
		_time->moveToLeft(timeLeft, foot, width);
		if (_more) {
			_more->moveToRight(0, foot, width);
		}
		return std::max(avatar, foot + std::max(_reply->height(), _time->height())) + st::seetgCommentGap;
	}

private:
	Comment _item;
	QImage _image;
	PaintButton *_avatar = nullptr;
	PaintButton *_name = nullptr;
	Ui::FlatLabel *_body = nullptr;
	Ui::FlatLabel *_time = nullptr;
	Ui::LinkButton *_reply = nullptr;
	Ui::LinkButton *_more = nullptr;

};

} // namespace

class Inner final : public Ui::RpWidget {
public:
	Inner(QWidget *parent, not_null<Window::SessionController*> controller,
		std::shared_ptr<State> state, Fn<void()> scrollToComposer)
	: RpWidget(parent), _controller(controller), _state(std::move(state)), _scrollToComposer(std::move(scrollToComposer)) {
		_replyChip = Ui::CreateChild<Ui::FlatLabel>(this, rpl::single(QString()), st::seetgCommentMeta);
		_cancelReply = Ui::CreateChild<Ui::LinkButton>(this, u"×"_q);

		_cancelReply->setAccessibleName(Lang::Text(Key::SeeTgCommentsCancel));
		_cancelReply->addClickHandler([=] { _state->replying.reset(); scheduleRefresh(); });
		_input = Ui::CreateChild<Ui::InputField>(this, st::seetgCommentInput,
			Ui::InputField::Mode::MultiLine, Lang::Value(Key::SeeTgCommentPlaceholder));
		_input->setMaxLength(kMaxLength);
		_input->setSubmitSettings(Ui::InputField::SubmitSettings::Enter);
		_input->setTextWithTags({ _state->draft });
		_input->changes() | rpl::on_next([=] {
			_state->draft = _input->getLastText();
			updateComposer();
		}, lifetime());
		_input->heightChanges() | rpl::on_next([=] { resizeToWidth(width()); }, lifetime());
		_input->submits() | rpl::on_next([=] { _state->post(); }, lifetime());
		_send = Ui::CreateChild<PaintButton>(this, [=](QPainter &p, QRect rect) {
			p.setPen(Qt::NoPen);
			p.setBrush(_send->isEnabled() ? st::windowBgActive->c : st::windowBgOver->c);
			p.drawEllipse(rect.adjusted(st::lineWidth * 2, st::lineWidth * 2, -st::lineWidth * 2, -st::lineWidth * 2));
			p.setPen(QPen(_send->isEnabled() ? st::windowFgActive->c : st::windowSubTextFg->c,
				st::lineWidth * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			const auto c = rect.center();
			const auto d = rect.width() / 6;
			p.drawLine(c.x(), c.y() + d, c.x(), c.y() - d);
			p.drawLine(c.x() - d, c.y(), c.x(), c.y() - d);
			p.drawLine(c.x() + d, c.y(), c.x(), c.y() - d);
		});
		_send->addClickHandler([=] { _state->post(); });
		_counter = Ui::CreateChild<Ui::FlatLabel>(this, rpl::single(QString()), st::seetgCommentMeta);
		_error = Ui::CreateChild<Ui::FlatLabel>(this, rpl::single(QString()), st::seetgCommentMeta);
		_rows = Ui::CreateChild<Ui::VerticalLayout>(this);
		_rows->heightValue() | rpl::on_next([=] { resizeToWidth(width()); }, lifetime());
		_state->changes.events() | rpl::on_next([=] { scheduleRefresh(); }, lifetime());
		Lang::Changes() | rpl::on_next([=] { scheduleRefresh(); }, lifetime());
		const auto timer = new QTimer(this);
		QObject::connect(timer, &QTimer::timeout, this, [=] {
			if (_state->retryAt) {
				if (_state->retryAt <= crl::now()) {
					_state->retryAt = 0;
					_state->actionError.reset();
				}
				updateComposer();
			}
		});
		timer->start(1000);
		refresh();
		if (!_state->loaded) {
			_state->load();
		}
	}

	void scheduleRefresh() {
		if (_refreshScheduled) {
			return;
		}
		_refreshScheduled = true;
		crl::on_main(this, [=] {
			_refreshScheduled = false;
			refresh();
		});
	}

	void refresh() {
		_input->setPlaceholder(Lang::Value(_state->replying
			? Key::SeeTgCommentReplyPlaceholder : Key::SeeTgCommentPlaceholder));
		_send->setAccessibleName(Lang::Text(Key::SeeTgCommentSend));
		if (_input->getLastText() != _state->draft) {
			_input->setTextWithTags({ _state->draft });
		}
		_replyChip->setText(_state->replying
			? Name(_state->replying->author) + '\n'
				+ st::normalFont->elided(_state->replying->body.simplified(), std::max(1, width() - 2 * st::seetgCommentPadding - st::seetgCommentAvatar))
			: QString());
		_replyChip->setVisible(_state->replying.has_value());
		_cancelReply->setVisible(_state->replying.has_value());
		_rows->clear();
		if (_state->items.empty()) {
			message(Lang::Text(_state->loading ? Key::SeeTgCommentsLoadingMore
				: _state->loadError ? Key::SeeTgCommentsError : Key::SeeTgCommentsEmpty));
		}
		for (const auto &item : _state->items) {
			addRow(item, 0);
			const auto thread = _state->threads.value(item.id);
			const auto count = std::max(item.replyCount, int(thread.items.size()));
			if (!count) {
				continue;
			}
			const auto indent = st::seetgCommentAvatar + st::seetgCommentAvatarSkip;
			button(thread.open ? Lang::Text(Key::SeeTgCommentHideReplies)
				: Lang::Text(Key::SeeTgCommentViewReplies).replace(u"{n}"_q, QString::number(count)),
				[=] { _state->toggle(item); }, indent);
			if (!thread.open) {
				continue;
			}
			for (const auto &reply : thread.items) {
				addRow(reply, indent);
			}
			if (thread.loading) {
				message(Lang::Text(Key::SeeTgCommentsLoadingMore), indent);
			} else if (thread.error) {
				button(Lang::Text(Key::SeeTgCommentRepliesError) + u" · "_q + Lang::Text(Key::SeeTgCommentsRetry),
					[=] { _state->loadThread(item.id, thread.loaded); }, indent);
			} else if (thread.hasMore) {
				button(Lang::Text(Key::SeeTgCommentsMore), [=] { _state->loadThread(item.id, true); }, indent);
			}
		}
		if (_state->loadError) {
			button(Lang::Text(Key::SeeTgCommentsRetry), [=] { _state->load(_state->loadingMore); });
		} else if (_state->hasMore) {
			button(Lang::Text(_state->loading ? Key::SeeTgCommentsLoadingMore : Key::SeeTgCommentsMore),
				[=] { _state->load(true); });
		}
		updateComposer();
	}

protected:
	int resizeGetHeight(int width) override {
		if (!_rows || _resizing) {
			return height();
		}
		_resizing = true;
		const auto padding = st::seetgCommentPadding;
		const auto available = std::max(1, width - 2 * padding);
		auto top = padding;
		if (_state->replying) {
			_replyChip->resizeToWidth(std::max(1, available - _cancelReply->width() - st::seetgCommentGap));
			_replyChip->moveToLeft(padding, top, width);
			_cancelReply->moveToRight(padding, top, width);
			top += _replyChip->height() + st::seetgCommentGap;
		}
		const auto sendSize = st::seetgCommentAvatar;
		_input->resizeToWidth(std::max(1, available - sendSize - st::seetgCommentGap));
		_input->moveToLeft(padding, top, width);
		_send->resize(sendSize, sendSize);
		_send->moveToRight(padding, top + (_input->height() - sendSize) / 2, width);
		top += std::max(_input->height(), sendSize) + st::seetgCommentGap;
		for (const auto label : { _counter, _error }) {
			if (!label->isHidden()) {
				label->resizeToWidth(available);
				label->moveToLeft(padding, top, width);
				top += label->height() + st::seetgCommentGap;
			}
		}
		_rows->resizeToWidth(available);
		_rows->moveToLeft(padding, top + st::seetgCommentGap, width);
		_resizing = false;
		return top + _rows->height() + padding;
	}

private:
	void updateComposer() {
		if (!_send || !_counter || !_error) {
			return;
		}
		_input->setDisabled(_state->busy == u"post"_q);
		_send->setEnabled(_state->busy.isEmpty() && !_state->loading
			&& !_state->draft.trimmed().isEmpty() && _state->retryAt <= crl::now());
		_send->update();
		_counter->setText(QString::number(_state->draft.size()) + '/' + QString::number(kMaxLength));
		_counter->setVisible(_state->draft.size() >= kMaxLength - 100);
		_error->setText(_state->actionError ? ActionError(*_state->actionError, _state->retryAt) : QString());
		_error->setVisible(_state->actionError.has_value());
		resizeToWidth(width());
	}

	void message(const QString &text, int indent = 0) {
		_rows->add(object_ptr<Ui::FlatLabel>(_rows, rpl::single(text), st::seetgCommentMeta),
			style::margins(indent, st::seetgCommentGap, 0, st::seetgCommentGap), style::al_justify);
	}

	void button(const QString &text, Fn<void()> action, int indent = 0) {
		const auto button = _rows->add(object_ptr<Ui::LinkButton>(_rows, text),
			style::margins(indent, 0, 0, st::seetgCommentGap), style::al_left);
		button->addClickHandler(std::move(action));
	}

	void addRow(const Comment &item, int indent) {
		_rows->add(object_ptr<Row>(_rows, item,
			crl::guard(this, [=](QJsonObject author) { openAuthor(author); }),
			crl::guard(this, [=] {
				if (!_state->busy.isEmpty()) {
					return;
				}
				_state->replying = item;
				scheduleRefresh();
				_scrollToComposer();
				_input->setFocus();
			}), crl::guard(this, [=](QPoint point) { actions(item, point); })),
			style::margins(indent, st::seetgCommentGap, 0, 0), style::al_justify);
	}

	void openAuthor(const QJsonObject &author) {
		if (_controller->showFrozenError()) {
			return;
		}
		const auto controller = _controller;
		auto owner = ParseOwner(author);
		owner.username = AuthorUsername(author);
		owner.name = Name(author);
		controller->show(Ui::MakeConfirmBox({
			.text = Lang::Text(Key::SeeTgCommentOpenProfile) + u"\n\n"_q + owner.name,
			.confirmed = crl::guard(controller, [=](Fn<void()> &&close) {
				close();
				Peers::Resolve(&controller->session(), owner, crl::guard(controller, [=](PeerData *peer) {
					if (peer) {
						controller->showPeerInfo(peer);
					} else {
						controller->showToast(Lang::Text(Key::SeeTgWhoIsFailed));
					}
				}));
			}),
			.confirmText = Lang::Value(Key::SeeTgCommentOpenProfile),
			.cancelText = Lang::Value(Key::SeeTgCommentsCancel),
		}));
	}

	void actions(const Comment &item, QPoint point) {
		if (!_state->busy.isEmpty() || _state->loading) {
			return;
		}
		_menu = std::make_unique<Ui::PopupMenu>(this);
		if (item.canManage && item.parentId.isEmpty()) {
			_menu->addAction(Lang::Text(item.pinned ? Key::SeeTgCommentUnpin : Key::SeeTgCommentPin),
				[model = _state, item] { model->pin(item); }, item.pinned ? &st::menuIconUnpin : &st::menuIconPin);
		}
		if (item.canManage || item.mine) {
			_menu->addAction(Lang::Text(Key::SeeTgCommentDelete), crl::guard(this, [=] {
				_controller->show(Ui::MakeConfirmBox({
					.text = Lang::Value(Key::SeeTgCommentDeleteConfirm),
					.confirmed = [model = _state, item](Fn<void()> &&close) {
						close();
						model->remove(item);
					},
					.confirmText = Lang::Value(Key::SeeTgCommentDelete),
					.cancelText = Lang::Value(Key::SeeTgCommentsCancel),
					.confirmStyle = &st::attentionBoxButton,
				}));
			}), &st::menuIconDelete);
		}
		_menu->popup(point);
	}

	const not_null<Window::SessionController*> _controller;
	const std::shared_ptr<State> _state;
	Fn<void()> _scrollToComposer;
	Ui::FlatLabel *_replyChip = nullptr;
	Ui::LinkButton *_cancelReply = nullptr;
	Ui::InputField *_input = nullptr;
	PaintButton *_send = nullptr;
	Ui::FlatLabel *_counter = nullptr;
	Ui::FlatLabel *_error = nullptr;
	Ui::VerticalLayout *_rows = nullptr;
	std::unique_ptr<Ui::PopupMenu> _menu;
	bool _resizing = false;
	bool _refreshScheduled = false;

};

Memento::Memento(not_null<PeerData*> peer, std::shared_ptr<State> state)
: ContentMemento(peer, nullptr, nullptr, PeerId()), _state(std::move(state)) {
}

object_ptr<Info::ContentWidget> Memento::createWidget(
		QWidget *parent, not_null<Info::Controller*> controller, const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, _state);
	result->setInternalState(geometry, this);
	return result;
}

Info::Section Memento::section() const {
	return Info::Section(Info::Section::Type::SeeTgComments);
}

Widget::Widget(QWidget *parent, not_null<Info::Controller*> controller, std::shared_ptr<State> state)
: ContentWidget(parent, controller)
, _peer(controller->key().peer())
, _state(state ? std::move(state) : std::make_shared<State>(_peer)) {
	_inner = setInnerWidget(object_ptr<Inner>(this, controller->parentController(), _state,
		[=] { scrollTopRestore(0); }));
}

bool Widget::showInternal(not_null<Info::ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (const auto own = dynamic_cast<Memento*>(memento.get())) {
		if (own->peer() == _peer.get()) {
			scrollTopRestore(own->scrollTop());
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(const QRect &geometry, not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	scrollTopRestore(memento->scrollTop());
}

rpl::producer<QString> Widget::title() {
	return Lang::Value(Key::SeeTgCommentsTab);
}

void Widget::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	addAction(Lang::Text(Key::SeeTgCommentsRefresh), [model = _state] { model->load(); }, &st::menuIconRestore);
}

std::shared_ptr<Info::ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(_peer, _state);
	result->setScrollTop(scrollTopSave());
	return result;
}

std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<Info::ContentMemento>>(1, std::make_shared<Memento>(peer)));
}

not_null<Ui::SettingsButton*> AddButton(
		not_null<Ui::VerticalLayout*> parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Ui::MultiSlideTracker &tracker) {
	const auto wrap = parent->add(object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(parent,
		object_ptr<Ui::SettingsButton>(parent, Lang::Value(Key::SeeTgCommentsTab), st::infoSharedMediaButton)));
	wrap->toggleOn(EnabledValue());
	tracker.track(wrap);
	const auto button = wrap->entity();
	button->addClickHandler([=] {
		if (!navigation->showFrozenError()) {
			navigation->showSection(Make(peer));
		}
	});
	object_ptr<Info::Profile::FloatingIcon>(button, st::seetgCommentsIcon, st::infoSharedMediaButtonIconPosition)->show();
	return button;
}

} // namespace Fork::SeeTg::Comments
