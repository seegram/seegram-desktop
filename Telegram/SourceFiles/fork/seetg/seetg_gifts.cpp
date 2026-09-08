/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_gifts.h"

#include "fork/fork_lang.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_card.h"
#include "fork/seetg/seetg_peers.h"
#include "fork/seetg/seetg_picker.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_types.h"
#include "fork/seetg/seetg_visuals.h"
#include "api/api_premium.h"
#include "core/local_url_handlers.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "settings/settings_common.h"
#include "settings/settings_credits_graphics.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/controls/sub_tabs.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/text.h"
#include "ui/vertical_list.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h" // mediaPlayerMenuCheck
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QJsonArray>
#include <QtGui/QPainterPath>

namespace Fork::SeeTg {
namespace {

using Lang::Key;
using Info::PeerGifts::GiftButton;
using Info::PeerGifts::GiftButtonMode;
using Info::PeerGifts::GiftTypeStars;

constexpr auto kPerPage = 30;

// The mini app's three sub-tabs of «Подарки».
enum class Kind {
	Upgraded,
	Limited,
	Regular,
};

struct Sort {
	QString by = u"NUMBER"_q;
	QString dir = u"ASC"_q;
};

struct Filter {
	QStringList collectionIds;
	QStringList collectionNames;
	QStringList models;
	QStringList backdrops;
	QStringList patterns;
	int numberFrom = 0;
	int numberTo = 0;
	bool onSale = false;
};

// Remembered for the process: reopening a profile lands where the user was.
bool LastModeSeeTg = false;

[[nodiscard]] QJsonArray ToArray(const QStringList &list) {
	auto result = QJsonArray();
	for (const auto &item : list) {
		result.push_back(item);
	}
	return result;
}

[[nodiscard]] QString ErrorText(const Api::Error &error) {
	using Kind = Api::Error::Kind;
	switch (error.kind) {
	case Kind::Auth: return Lang::Text(Key::SeeTgErrorAuth);
	case Kind::RateLimited: return Lang::Text(Key::SeeTgErrorRate);
	case Kind::Premium: return Lang::Text(Key::SeeTgErrorPremium);
	case Kind::Network: return Lang::Text(Key::SeeTgErrorNetwork);
	case Kind::Other: break;
	}
	return Lang::Text(Key::SeeTgErrorOther);
}

class GiftsList final : public Ui::BoxContentDivider {
public:
	GiftsList(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer);

	void fillMenu(const Ui::Menu::MenuCallback &addAction);

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Item {
		std::variant<Nft, Saved> data;
		std::unique_ptr<Ui::AbstractButton> button;
	};

	void setupControls();
	void setKind(Kind kind);
	void applySavedHidden(bool hidden);
	void reload();
	void loadMore();
	void applyNfts(const Page<Nft> &page);
	void applySaved(const Page<Saved> &page);
	void appendItems(std::vector<Item> items);
	void createButton(int index);
	void refreshStatus();
	void openItem(int index, bool catalogReady = false);
	void showFilters();
	void ensureCatalog(Fn<void()> done);
	[[nodiscard]] const Data::StarGift *catalogGift(uint64 id) const;
	[[nodiscard]] QJsonObject variables() const;
	[[nodiscard]] int layoutButtons(int top);

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	Info::PeerGifts::Delegate _delegate;
	const std::unique_ptr<::Api::PremiumGiftCodeOptions> _catalog;

	std::unique_ptr<Ui::SubTabs> _kinds;
	Ui::FlatLabel *_status = nullptr;

	Kind _kind = Kind::Upgraded;
	bool _showHidden = false;
	bool _savedHidden = false;
	bool _savedHiddenKnown = false;
	Sort _sorting;
	Filter _filter;

	std::vector<Item> _items;
	QString _endCursor;
	bool _hasNext = false;
	bool _loading = false;
	int _serial = 0;
	std::optional<int> _total;
	QString _error;

	QSize _singleMin;
	QSize _single;
	int _perRow = 0;

};

GiftsList::GiftsList(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer)
: BoxContentDivider(parent)
, _controller(controller)
, _peer(peer)
, _delegate(&controller->session(), GiftButtonMode::Minimal)
, _catalog(std::make_unique<::Api::PremiumGiftCodeOptions>(
	controller->session().user()))
 {
	_singleMin = _delegate.buttonSize();
	setupControls();
	reload();
}

void GiftsList::setupControls() {
	auto tabs = std::vector<Ui::SubTabs::Tab>();
	tabs.push_back({
		.id = u"upgraded"_q,
		.text = { Lang::Text(Key::SeeTgKindUpgraded) },
	});
	tabs.push_back({
		.id = u"limited"_q,
		.text = { Lang::Text(Key::SeeTgKindLimited) },
	});
	tabs.push_back({
		.id = u"regular"_q,
		.text = { Lang::Text(Key::SeeTgKindRegular) },
	});
	_kinds = std::make_unique<Ui::SubTabs>(
		this,
		st::collectionSubTabs,
		Ui::SubTabs::Options{ .selected = u"upgraded"_q, .centered = true },
		std::move(tabs));
	// Hidden until the first answer says whether this profile shows its
	// non-upgraded gifts at all. Appearing late is fine; appearing and then
	// vanishing under the cursor is not.
	_kinds->hide();
	_kinds->activated(
	) | rpl::on_next([=](const QString &id) {
		_kinds->setActiveTab(id);
		setKind((id == u"limited"_q)
			? Kind::Limited
			: (id == u"regular"_q)
			? Kind::Regular
			: Kind::Upgraded);
	}, _kinds->lifetime());

	_status = Ui::CreateChild<Ui::FlatLabel>(
		this,
		QString(),
		st::boxDividerLabel);
}

void GiftsList::fillMenu(const Ui::Menu::MenuCallback &addAction) {
	if (_kind != Kind::Upgraded) {
		// The client's own way to show a switch in a menu: a check mark
		// on the active item, nothing on the others.
		addAction(
			Lang::Text(Key::SeeTgHiddenToggle),
			crl::guard(this, [=] {
				_showHidden = !_showHidden;
				reload();
			}),
			_showHidden ? &st::mediaPlayerMenuCheck : nullptr);
		return;
	}
	addAction(Ui::Menu::MenuCallback::Args{
		.text = Lang::Text(Key::SeeTgSort),
		.icon = &st::menuIconReorder,
		.fillSubmenu = [=](not_null<Ui::PopupMenu*> menu) {
			const auto add = [&](Key key, Sort sort) {
				const auto active = (_sorting.by == sort.by)
					&& (_sorting.dir == sort.dir);
				menu->addAction(Lang::Text(key), crl::guard(this, [=] {
					_sorting = sort;
					if (_sorting.by == u"PRICE"_q) {
						_filter.onSale = true;
					}
					reload();
				}), active ? &st::mediaPlayerMenuCheck : nullptr);
			};
			add(Key::SeeTgSortNumberAsc, { u"NUMBER"_q, u"ASC"_q });
			add(Key::SeeTgSortNumberDesc, { u"NUMBER"_q, u"DESC"_q });
			add(Key::SeeTgSortName, { u"NAME"_q, u"ASC"_q });
			add(Key::SeeTgSortEstimate, { u"ESTIMATE"_q, u"DESC"_q });
			add(Key::SeeTgSortPrice, { u"PRICE"_q, u"ASC"_q });
		},
	});
	addAction(Lang::Text(Key::SeeTgFilters), crl::guard(this, [=] {
		showFilters();
	}), &st::menuIconTagFilter);
}

void GiftsList::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(rect(), st::boxDividerBg->c);
	paintTop(p);
}

void GiftsList::applySavedHidden(bool hidden) {
	if (_savedHiddenKnown && _savedHidden == hidden) {
		return;
	}
	_savedHiddenKnown = true;
	_savedHidden = hidden;
	_kinds->setVisible(!hidden);
	if (hidden && _kind != Kind::Upgraded) {
		_kinds->setActiveTab(u"upgraded"_q);
		setKind(Kind::Upgraded);
	} else {
		resizeToWidth(width());
	}
}

void GiftsList::setKind(Kind kind) {
	if (_kind == kind) {
		return;
	}
	_kind = kind;
	reload();
}

void GiftsList::reload() {
	++_serial;
	_items.clear();
	_endCursor = QString();
	_hasNext = false;
	_loading = false;
	_total = std::nullopt;
	_error = QString();
	resizeToWidth(width());
	loadMore();
}

QJsonObject GiftsList::variables() const {
	auto filter = QJsonObject();
	filter.insert(u"ownerSeeId"_q, SeeId(_peer->id));
	auto result = QJsonObject();
	result.insert(u"n"_q, kPerPage);
	result.insert(
		u"a"_q,
		_endCursor.isEmpty() ? QJsonValue() : QJsonValue(_endCursor));
	if (_kind == Kind::Upgraded) {
		result.insert(u"o"_q, SeeId(_peer->id));
		if (!_filter.collectionIds.isEmpty()) {
			filter.insert(u"giftIds"_q, ToArray(_filter.collectionIds));
		}
		if (!_filter.models.isEmpty()) {
			filter.insert(u"models"_q, ToArray(_filter.models));
		}
		if (!_filter.backdrops.isEmpty()) {
			filter.insert(u"backdrops"_q, ToArray(_filter.backdrops));
		}
		if (!_filter.patterns.isEmpty()) {
			filter.insert(u"patterns"_q, ToArray(_filter.patterns));
		}
		if (_filter.numberFrom) {
			filter.insert(u"numberFrom"_q, _filter.numberFrom);
		}
		if (_filter.numberTo) {
			filter.insert(u"numberTo"_q, _filter.numberTo);
		}
		if (_filter.onSale) {
			filter.insert(u"onSale"_q, true);
		}
		result.insert(u"by"_q, _sorting.by);
		result.insert(u"dir"_q, _sorting.dir);
	} else {
		filter.insert(
			u"kind"_q,
			(_kind == Kind::Limited) ? u"LIMITED"_q : u"REGULAR"_q);
		filter.insert(u"onlyVisible"_q, !_showHidden);
		filter.insert(u"onlyHidden"_q, _showHidden);
		result.insert(u"by"_q, u"ISSUED"_q);
		result.insert(u"dir"_q, u"DESC"_q);
	}
	result.insert(u"f"_q, filter);
	return result;
}

void GiftsList::loadMore() {
	if (_loading || (!_items.empty() && !_hasNext)) {
		return;
	}
	_loading = true;
	refreshStatus();

	const auto serial = _serial;
	const auto kind = _kind;
	const auto document = QString::fromLatin1((kind == Kind::Upgraded)
		? kQueryProfileNfts
		: kQueryProfileSaved);
	const auto session = &_controller->session();
	Api::Query(session, document, variables(), crl::guard(this, [=](
			const QJsonObject &data) {
		if (serial != _serial) {
			return;
		}
		if (kind == Kind::Upgraded) {
			// The owner may have closed their non-upgraded gifts to
			// everyone but themselves (see.tg Premium): then the two tabs
			// for those go, as they do in the mini app.
			const auto owner = data.value(u"owner"_q).toObject();
			applySavedHidden(owner.value(u"savedGiftsHidden"_q).toBool());
			applyNfts(ParseNftPage(data.value(u"searchGifts"_q).toObject()));
		} else {
			applySaved(ParseSavedPage(
				data.value(u"searchSavedGifts"_q).toObject()));
		}
	}), crl::guard(this, [=](const Api::Error &error) {
		if (serial != _serial) {
			return;
		}
		_loading = false;
		_hasNext = false;
		_error = ErrorText(error);
		refreshStatus();
	}));
}

void GiftsList::applyNfts(const Page<Nft> &page) {
	_hasNext = page.hasNext && !page.items.empty()
		&& !page.endCursor.isEmpty() && page.endCursor != _endCursor;
	_endCursor = page.endCursor;
	if (page.total) _total = page.total;
	auto list = std::vector<Item>();
	for (const auto &nft : page.items) list.push_back(Item{ .data = nft });
	appendItems(std::move(list));
}

void GiftsList::applySaved(const Page<Saved> &page) {
	_hasNext = page.hasNext && !page.items.empty()
		&& !page.endCursor.isEmpty() && page.endCursor != _endCursor;
	_endCursor = page.endCursor;
	if (page.total) _total = page.total;
	auto list = std::vector<Item>();
	for (const auto &saved : page.items) list.push_back(Item{ .data = saved });
	appendItems(std::move(list));
}

void GiftsList::appendItems(std::vector<Item> items) {
	_loading = false;
	const auto from = int(_items.size());
	for (auto &item : items) {
		_items.push_back(std::move(item));
	}
	for (auto i = from, count = int(_items.size()); i != count; ++i) {
		createButton(i);
	}
	refreshStatus();
}

void GiftsList::ensureCatalog(Fn<void()> done) {
	if (!_catalog->starGifts().empty()) {
		done();
		return;
	}
	_catalog->requestStarGifts(
	) | rpl::on_next_error_done([] {
	}, [=](const QString &) {
		done();
	}, [=] {
		done();
	}, lifetime());
}

const Data::StarGift *GiftsList::catalogGift(uint64 id) const {
	const auto &gifts = _catalog->starGifts();
	const auto i = ranges::find(gifts, id, &Data::StarGift::id);
	if (i != end(gifts)) {
		return &*i;
	}
	return nullptr;
}

void GiftsList::createButton(int index) {
	auto &item = _items[index];
	const auto nft = std::get_if<Nft>(&item.data);
	const auto saved = std::get_if<Saved>(&item.data);
	if (nft) {
		item.button = std::make_unique<Card>(this, CardData{
			.giftId = nft->giftId,
			.title = nft->title,
			.model = nft->model,
			.backdrop = nft->backdrop,
			.pattern = nft->pattern,
			.num = nft->num,
			.saleAmount = nft->onSale ? nft->saleAmount : QString(),
			.saleCurrency = nft->saleCurrency,
			.saleMarket = nft->saleMarket,
		});
	} else {
		item.button = std::make_unique<Card>(this, CardData{
			.giftId = saved->giftId,
		});
	}
	item.button->setClickedCallback([=] { openItem(index); });
	item.button->show();
}

void GiftsList::refreshStatus() {
	auto text = QString();
	if (!_error.isEmpty()) {
		text = _error;
	} else if (_loading && _items.empty()) {
		text = Lang::Text(Key::SeeTgLoading);
	} else if (_items.empty()) {
		text = Lang::Text(Key::SeeTgEmpty);
	}
	_status->setText(text);
	_status->setVisible(!text.isEmpty());
	resizeToWidth(width());
}

void GiftsList::openItem(int index, bool catalogReady) {
	if (index < 0 || index >= int(_items.size())) {
		return;
	}
	const auto &item = _items[index];
	if (const auto nft = std::get_if<Nft>(&item.data)) {
		Core::ResolveAndShowUniqueGift(_controller->uiShow(), nft->nftSlug());
		return;
	}
	const auto &saved = v::get<Saved>(item.data);
	const auto gift = catalogGift(saved.giftId);
	if (!gift) {
		if (!catalogReady) {
			const auto serial = _serial;
			ensureCatalog(crl::guard(this, [=] {
				if (serial == _serial) openItem(index, true);
			}));
		} else {
			_controller->showToast(Lang::Text(Key::SeeTgErrorOther));
		}
		return;
	}
	// The client's sheet wants its own saved-gift record. see.tg has no
	// message id for it, so the record carries what see.tg knows and the
	// sheet shows the gift without the manage actions. The sender is
	// resolved first: a peer the client has never met would show as an
	// empty circle otherwise.
	const auto controller = _controller;
	const auto peer = _peer;
	const auto info = *gift;
	const auto show = [=](PeerData *from) {
		auto data = Data::SavedStarGift{ .info = info };
		data.message = { saved.message };
		data.starsConverted = saved.convertStars;
		data.fromId = from ? from->id : PeerId();
		data.date = saved.issuedAt;
		data.anonymous = saved.nameHidden || !from;
		data.hidden = !saved.visible;
		data.mine = peer->isSelf();
		::Settings::ShowSavedStarGiftBox(controller, peer, data);
	};
	if (saved.nameHidden || !saved.from.known()) {
		show(nullptr);
		return;
	} else if (!Current().resolveAutomatically) {
		// Manual mode: no lookup is spent on open. The sheet gets the raw
		// id and shows it with a «Узнать кто» next to it - see
		// fork/seetg/seetg_peers.h.
		auto data = Data::SavedStarGift{ .info = info };
		data.message = { saved.message };
		data.starsConverted = saved.convertStars;
		data.fromId = saved.from.peerId;
		data.date = saved.issuedAt;
		data.hidden = !saved.visible;
		data.mine = peer->isSelf();
		::Settings::ShowSavedStarGiftBox(controller, peer, data);
		return;
	}
	Peers::Resolve(
		&_controller->session(),
		saved.from,
		crl::guard(controller, show));
}

// The filters, each row a picker with chips and a search over the
// changes.tg catalogue - the mini app's form. Models and patterns list what
// the picked collections have; with none picked, patterns list everything
// and models ask for a collection first.
void GiftsList::showFilters() {
	const auto controller = _controller;
	const auto draft = std::make_shared<Filter>(_filter);
	_controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(Lang::Value(Key::SeeTgFilters));
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();

		struct Labels {
			rpl::variable<QString> collection;
			rpl::variable<QString> models;
			rpl::variable<QString> backdrops;
			rpl::variable<QString> patterns;
		};
		const auto labels = box->lifetime().make_state<Labels>();
		const auto summary = [](const QStringList &names) {
			if (names.isEmpty()) {
				return Lang::Text(Key::SeeTgFilterAny);
			} else if (names.size() <= 2) {
				return names.join(u", "_q);
			}
			return names.mid(0, 2).join(u", "_q)
				+ u" +"_q
				+ QString::number(names.size() - 2);
		};
		const auto refresh = [=] {
			labels->collection = summary(draft->collectionNames);
			labels->models = summary(draft->models);
			labels->backdrops = summary(draft->backdrops);
			labels->patterns = summary(draft->patterns);
		};
		refresh();

		const auto addRow = [&](Key key, rpl::producer<QString> label) {
			return ::Settings::AddButtonWithLabel(
				content,
				Lang::Value(key),
				std::move(label),
				st::settingsButtonNoIcon);
		};
		Ui::AddSkip(content);
		addRow(Key::SeeTgFilterCollection, labels->collection.value()
		)->addClickHandler([=] {
			Visuals::Collections(crl::guard(box, [=](
					const std::vector<Visuals::Collection> &list) {
				auto items = std::vector<Picker::Item>();
				for (const auto &collection : list) {
					items.push_back({
						.id = QString::number(collection.id),
						.name = collection.name,
						.imageUrl = Visuals::OriginalImageUrl(collection.id),
					});
				}
				Picker::Show(controller, {
					.title = Lang::Value(Key::SeeTgFilterCollection),
					.items = std::move(items),
					.selected = draft->collectionIds,
					.done = crl::guard(box, [=](QStringList ids) {
						draft->collectionIds = ids;
						draft->collectionNames.clear();
						for (const auto &id : ids) {
							for (const auto &collection : list) {
								if (QString::number(collection.id) == id) {
									draft->collectionNames.push_back(
										collection.name);
								}
							}
						}
						// A model or a pattern of a collection no longer
						// picked would filter for nothing.
						draft->models.clear();
						refresh();
					}),
				});
			}));
		});
		addRow(Key::SeeTgFilterModel, labels->models.value()
		)->addClickHandler([=] {
			if (draft->collectionIds.isEmpty()) {
				controller->showToast(
					Lang::Text(Key::SeeTgPickCollectionFirst));
				return;
			}
			// Every picked collection's models, one cached request each.
			const auto pending = std::make_shared<int>(
				draft->collectionIds.size());
			const auto names = std::make_shared<QStringList>();
			const auto firstId = draft->collectionIds.front().toULongLong();
			for (const auto &idText : draft->collectionIds) {
				Visuals::Models(idText.toULongLong(), crl::guard(box, [=](
						QStringList models) {
					for (const auto &name : models) {
						if (!names->contains(name)) {
							names->push_back(name);
						}
					}
					if (--*pending > 0) {
						return;
					}
					names->sort(Qt::CaseInsensitive);
					auto items = std::vector<Picker::Item>();
					for (const auto &name : *names) {
						items.push_back({
							.id = name,
							.name = name,
							.imageUrl = Visuals::ModelImageUrl(firstId, name),
						});
					}
					Picker::Show(controller, {
						.title = Lang::Value(Key::SeeTgFilterModel),
						.items = std::move(items),
						.selected = draft->models,
						.done = crl::guard(box, [=](QStringList names) {
							draft->models = names;
							refresh();
						}),
					});
				}));
			}
		});
		addRow(Key::SeeTgFilterBackdrop, labels->backdrops.value()
		)->addClickHandler([=] {
			Visuals::Backdrops(crl::guard(box, [=] {
				auto items = std::vector<Picker::Item>();
				for (const auto &[name, backdrop] : Visuals::AllBackdrops()) {
					items.push_back({
						.id = name,
						.name = name,
						.backdrop = backdrop,
					});
				}
				Picker::Show(controller, {
					.title = Lang::Value(Key::SeeTgFilterBackdrop),
					.items = std::move(items),
					.selected = draft->backdrops,
					.done = crl::guard(box, [=](QStringList names) {
						draft->backdrops = names;
						refresh();
					}),
				});
			}));
		});
		addRow(Key::SeeTgFilterPattern, labels->patterns.value()
		)->addClickHandler([=] {
			Visuals::Patterns(crl::guard(box, [=] {
				const auto collection = draft->collectionNames.isEmpty()
					? QString()
					: draft->collectionNames.front();
				auto items = std::vector<Picker::Item>();
				for (const auto &name : Visuals::PatternNames(
						draft->collectionNames)) {
					items.push_back({
						.id = name,
						.name = name,
						.imageUrl = collection.isEmpty()
							? QString()
							: Visuals::PatternUrl(collection, name),
						.tintImage = true,
					});
				}
				Picker::Show(controller, {
					.title = Lang::Value(Key::SeeTgFilterPattern),
					.items = std::move(items),
					.selected = draft->patterns,
					.done = crl::guard(box, [=](QStringList names) {
						draft->patterns = names;
						refresh();
					}),
				});
			}));
		});
		Ui::AddSkip(content);

		const auto number = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Lang::Value(Key::SeeTgFilterNumber),
			draft->numberFrom
				? (draft->numberTo && draft->numberTo != draft->numberFrom
					? QString::number(draft->numberFrom)
						+ '-'
						+ QString::number(draft->numberTo)
					: QString::number(draft->numberFrom))
				: QString()));
		const auto onSale = box->addRow(object_ptr<Ui::Checkbox>(
			box,
			Lang::Text(Key::SeeTgFilterOnSale),
			draft->onSale));

		const auto apply = crl::guard(this, [=] {
			auto filter = *draft;
			const auto range = number->getLastText().trimmed();
			const auto dash = range.indexOf('-');
			filter.numberFrom = filter.numberTo = 0;
			if (dash > 0) {
				filter.numberFrom = range.left(dash).trimmed().toInt();
				filter.numberTo = range.mid(dash + 1).trimmed().toInt();
			} else if (!range.isEmpty()) {
				filter.numberFrom = filter.numberTo = range.toInt();
			}
			filter.onSale = onSale->checked();
			_filter = filter;
			if (!_filter.onSale && _sorting.by == u"PRICE"_q) {
				_sorting = Sort();
			}
			reload();
		});
		box->addLeftButton(Lang::Value(Key::Reset), [=] {
			*draft = Filter();
			number->clear();
			onSale->setChecked(false);
			refresh();
		});
		box->addButton(Lang::Value(Key::SeeTgApply), [=] {
			apply();
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

int GiftsList::layoutButtons(int top) {
	const auto padding = st::giftBoxPadding;
	const auto skipw = st::giftBoxGiftSkip.x();
	const auto skiph = st::giftBoxGiftSkip.y();
	const auto extend = _delegate.buttonExtend();
	auto index = 0;
	for (auto &item : _items) {
		if (!item.button) {
			continue;
		}
		const auto row = index / _perRow;
		const auto column = index % _perRow;
		const auto inner = QRect(
			QPoint(
				padding.left() + column * (_single.width() + skipw),
				top + row * (_single.height() + skiph)),
			_single);
		if (const auto gift = dynamic_cast<GiftButton*>(item.button.get())) {
			gift->setGeometry(inner, extend);
		} else {
			item.button->setGeometry(inner);
		}
		++index;
	}
	const auto rows = (index + _perRow - 1) / _perRow;
	return rows ? (rows * (_single.height() + skiph) - skiph) : 0;
}

int GiftsList::resizeGetHeight(int newWidth) {
	const auto padding = st::giftBoxPadding;
	const auto available = newWidth - padding.left() - padding.right();
	const auto skipw = st::giftBoxGiftSkip.x();
	_perRow = std::max(1, (available + skipw) / (_singleMin.width() + skipw));
	const auto singlew = std::min(
		((available + skipw) / _perRow) - skipw,
		2 * _singleMin.width());
	_single = QSize(std::max(singlew, _singleMin.width()), _singleMin.height());

	// The same spacing the client's own list uses: a tab strip under the top
	// padding, and the grid a bottom padding below whatever stands above it.
	// Unlike that list, this one always has the Telegram/see.tg switch above
	// it, so there is nothing to pad for when the kind tabs are away.
	auto top = 0;
	if (!_kinds->isHidden()) {
		top += padding.top();
		_kinds->resizeToWidth(newWidth);
		_kinds->move(0, top);
		top += _kinds->height();
	}

	if (!_status->isHidden()) {
		top += padding.bottom();
		_status->resizeToWidth(available);
		_status->moveToLeft(padding.left(), top);
		top += _status->height();
	}

	top += padding.bottom();
	top += layoutButtons(top);
	return top + padding.bottom();
}

void GiftsList::visibleTopBottomUpdated(int visibleTop, int visibleBottom) {
	if (_hasNext && !_loading && visibleBottom >= height() - _single.height()) {
		loadMore();
	}
}

class Wrapper final : public Ui::RpWidget {
public:
	Wrapper(
		QWidget *parent,
		object_ptr<Ui::RpWidget> native,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer);

	void fillMenu(
		const Ui::Menu::MenuCallback &addAction,
		Fn<void()> nativeFill);

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

private:
	void refreshTabs();
	void setMode(bool seetg);
	[[nodiscard]] Ui::RpWidget *active() const;
	void relayout();

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	object_ptr<Ui::RpWidget> _native;
	std::unique_ptr<Ui::SettingsSlider> _tabs;
	std::unique_ptr<GiftsList> _list;
	bool _seetg = false;
	bool _resizing = false;
	int _visibleTop = 0;
	int _visibleBottom = 0;

};

Wrapper::Wrapper(
	QWidget *parent,
	object_ptr<Ui::RpWidget> native,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer)
: RpWidget(parent)
, _controller(controller)
, _peer(peer)
, _native(std::move(native)) {
	_native->setParent(this);
	_native->show();
	_native->heightValue(
	) | rpl::on_next([=] {
		relayout();
	}, _native->lifetime());

	EnabledValue(Feature::Gifts
	) | rpl::on_next([=](bool enabled) {
		if (!enabled && _seetg) {
			setMode(false);
		}
		refreshTabs();
	}, lifetime());
	if (LastModeSeeTg) {
		setMode(true);
	}
}

void Wrapper::refreshTabs() {
	if (!Enabled(Feature::Gifts)) {
		if (base::take(_tabs)) {
			relayout();
		}
		return;
	} else if (_tabs) {
		return;
	}
	// The same underlined tabs the profile draws for its own sections, so
	// the switch reads as part of the page rather than as a control on it.
	_tabs = std::make_unique<Ui::SettingsSlider>(this, st::defaultTabsSlider);
	_tabs->setSections({
		Lang::Text(Key::SeeTgTabTelegram),
		Lang::Text(Key::SeeTgTabSeeTg),
	});
	_tabs->setActiveSectionFast(_seetg ? 1 : 0);
	_tabs->show();
	_tabs->sectionActivated(
	) | rpl::on_next([=](int index) {
		setMode(index == 1);
	}, _tabs->lifetime());
	relayout();
}

void Wrapper::setMode(bool seetg) {
	seetg = seetg && Enabled(Feature::Gifts);
	if (_seetg == seetg) {
		return;
	}
	_seetg = seetg;
	LastModeSeeTg = seetg;
	if (_tabs) {
		_tabs->setActiveSectionFast(_seetg ? 1 : 0);
	}
	if (_seetg && !_list) {
		_list = std::make_unique<GiftsList>(this, _controller, _peer);
		_list->heightValue(
		) | rpl::on_next([=] {
			relayout();
		}, _list->lifetime());
	}
	_native->setVisible(!_seetg);
	if (_list) {
		_list->setVisible(_seetg);
	}
	relayout();
}

void Wrapper::fillMenu(
		const Ui::Menu::MenuCallback &addAction,
		Fn<void()> nativeFill) {
	if (_seetg && _list) {
		_list->fillMenu(addAction);
	} else {
		nativeFill();
	}
}

Ui::RpWidget *Wrapper::active() const {
	return _seetg ? static_cast<Ui::RpWidget*>(_list.get()) : _native.data();
}

void Wrapper::relayout() {
	if (_resizing || !width()) {
		return;
	}
	resizeToWidth(width());
	visibleTopBottomUpdated(_visibleTop, _visibleBottom);
}

int Wrapper::resizeGetHeight(int newWidth) {
	_resizing = true;
	const auto guard = gsl::finally([&] { _resizing = false; });

	auto top = 0;
	if (_tabs) {
		_tabs->resizeToWidth(newWidth);
		_tabs->move(0, top);
		top += _tabs->height();
	}
	const auto widget = active();
	widget->resizeToWidth(newWidth);
	widget->moveToLeft(0, top);
	return top + widget->height();
}

void Wrapper::visibleTopBottomUpdated(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	const auto widget = active();
	widget->setVisibleTopBottom(
		visibleTop - widget->y(),
		visibleBottom - widget->y());
}

} // namespace

object_ptr<Ui::RpWidget> WrapGifts(
		QWidget *parent,
		object_ptr<Ui::RpWidget> native,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	return object_ptr<Wrapper>(parent, std::move(native), controller, peer);
}

void FillGiftsMenu(
		not_null<Ui::RpWidget*> widget,
		const Ui::Menu::MenuCallback &addAction,
		Fn<void()> nativeFill) {
	// Called with either the wrapper or the native list it holds.
	auto wrapper = dynamic_cast<Wrapper*>(widget.get());
	if (!wrapper) {
		wrapper = dynamic_cast<Wrapper*>(widget->parentWidget());
	}
	if (wrapper) {
		wrapper->fillMenu(addAction, std::move(nativeFill));
	} else {
		nativeFill();
	}
}

} // namespace Fork::SeeTg
