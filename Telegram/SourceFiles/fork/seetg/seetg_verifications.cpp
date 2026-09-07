/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_verifications.h"

#include "fork/fork_lang.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_types.h"
#include "core/click_handler_types.h"
#include "data/data_peer.h"
#include "info/profile/info_profile_section_stack.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "ui/text/text_html_tags.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_info.h"
#include "styles/style_seetg_verifications.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>
#include <QtSvg/QSvgRenderer>

namespace Fork::SeeTg::Verification {
namespace {

constexpr auto kQuery =
	"query ProfileVerifications($seeId: String!) { owner(seeId: $seeId) { "
	"seeId telegramId username title name address "
	"verifications { type slot description warning } } }";

[[nodiscard]] QString Substitute(QString text, const QJsonObject &owner) {
	const auto username = owner.value(u"username"_q).toString();
	auto name = owner.value(u"name"_q).toString();
	if (name.isEmpty()) {
		name = owner.value(u"title"_q).toString();
	}
	if (name.isEmpty() && !username.isEmpty()) {
		name = '@' + username;
	}
	const auto values = QMap<QString, QString>{
		{ u"name"_q, name },
		{ u"username"_q, username.isEmpty() ? QString() : '@' + username },
		{ u"tgId"_q, owner.value(u"telegramId"_q).toVariant().toString() },
		{ u"seeId"_q, owner.value(u"seeId"_q).toString() },
		{ u"address"_q, owner.value(u"address"_q).toString() },
	};
	const auto pattern = QRegularExpression(u"\\{(\\w+)\\}"_q);
	auto matches = pattern.globalMatch(text);
	auto result = QString();
	auto offset = qsizetype(0);
	while (matches.hasNext()) {
		const auto match = matches.next();
		result += text.mid(offset, match.capturedStart() - offset);
		const auto i = values.constFind(match.captured(1));
		result += (i == values.cend())
			? match.captured()
			: i.value().toHtmlEscaped();
		offset = match.capturedEnd();
	}
	return result + text.mid(offset);
}

[[nodiscard]] QByteArray IconSvg(const Entry &entry, bool mono) {
	auto type = (mono && entry.warning) ? u"warning"_q : entry.type;
	if (mono && type == u"zv"_q) {
		type = u"zv-mono"_q;
	}
	const auto allowed = QStringList{
		u"main"_q, u"premium"_q, u"deleted"_q, u"market"_q,
		u"casino"_q, u"poop"_q, u"zv"_q, u"zv-mono"_q,
		u"giftchanges"_q, u"warning"_q,
	};
	if (!allowed.contains(type)) {
		type = u"main"_q;
	}
	auto file = QFile(u":/seegram/verifications/"_q + type + u".svg"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto warning = type == u"warning"_q;
	const auto seal = warning ? st::attentionButtonFg->c
		: mono ? st::windowSubTextFg->c
		: type == u"premium"_q ? st::windowActiveTextFg->c
		: type == u"poop"_q ? QColor::fromRgb(0xa56b3f)
		: QColor::fromRgb(0x0088ff);
	const auto glyph = (mono || type == u"premium"_q) && !warning
		? st::windowBg->c
		: QColor(Qt::white);
	return file.readAll()
		.replace("$SEAL", seal.name().toUtf8())
		.replace("$GLYPH", glyph.name().toUtf8());
}

void PaintIcon(QPainter &p, const Entry &entry, QRect rect, bool mono) {
	auto renderer = QSvgRenderer(IconSvg(entry, mono));
	renderer.render(&p, QRectF(rect));
}

class DescriptionRow final : public Ui::RpWidget {
public:
	DescriptionRow(QWidget *parent, Entry entry)
	: RpWidget(parent)
	, _entry(std::move(entry))
	, _label(this, rpl::single(_entry.description), st::seetgVerificationDescription) {
		_label->setSelectable(true);
		setToolTip(u"see.tg"_q);
		style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
	}

protected:
	int resizeGetHeight(int width) override {
		const auto left = st::seetgVerificationDescriptionIcon
			+ st::seetgVerificationDescriptionSkip;
		_label->resizeToWidth(std::max(1, width - left));
		_label->moveToLeft(left, 0);
		return std::max(_label->height(), st::seetgVerificationDescriptionIcon);
	}

	void paintEvent(QPaintEvent *event) override {
		auto p = Painter(this);
		const auto size = st::seetgVerificationDescriptionIcon;
		PaintIcon(p, _entry, style::rtlrect(0, 0, size, size, width()), true);
	}

private:
	Entry _entry;
	object_ptr<Ui::FlatLabel> _label;

};

} // namespace

Entries Parse(const QJsonObject &owner) {
	auto result = Entries();
	for (const auto &value : owner.value(u"verifications"_q).toArray()) {
		if (!value.isObject()) {
			continue;
		}
		const auto object = value.toObject();
		const auto type = object.value(u"type"_q).toString();
		if (type.isEmpty()) {
			continue;
		}
		auto html = object.value(u"description"_q).toString();
		if (html.isEmpty() && type == u"premium"_q) {
			html = Lang::Text(Lang::Key::SeeTgPremiumVerificationDescription);
		}
		const auto parsed = TextUtilities::TextWithTagsFromHtmlFragment(
			Substitute(html, owner));
		auto entities = TextUtilities::ConvertTextTagsToEntities(parsed.tags);
		entities.erase(std::remove_if(entities.begin(), entities.end(), [](const EntityInText &entity) {
			if (entity.type() != EntityType::CustomUrl) {
				return false;
			}
			const auto scheme = QUrl(entity.data()).scheme().toLower();
			return scheme != u"http"_q && scheme != u"https"_q
				&& scheme != u"tg"_q && scheme != u"mailto"_q;
		}), entities.end());
		result.push_back({
			.type = type,
			.slot = object.value(u"slot"_q).toString(),
			.description = { parsed.text, std::move(entities) },
			.warning = object.value(u"warning"_q).toBool(),
		});
	}
	return result;
}

rpl::producer<Entries> Value(not_null<PeerData*> peer) {
	if (!peer->isUser() && !peer->isChannel()) {
		return rpl::single(Entries());
	}
	return rpl::make_producer<Entries>([=](auto &&consumer) {
		auto lifetime = rpl::lifetime();
		const auto generation = std::make_shared<int>(0);
		rpl::merge(
			EnabledValue() | rpl::to_empty,
			Lang::Changes()
		) | rpl::on_next([=] {
			const auto current = ++*generation;
			consumer.put_next(Entries());
			if (!Enabled()) {
				return;
			}
			Api::Query(
				&peer->session(),
				QString::fromLatin1(kQuery),
				{ { u"seeId"_q, SeeId(peer->id) } },
				[=](const QJsonObject &data) {
					if (current == *generation && Enabled()) {
						consumer.put_next(Parse(data.value(u"owner"_q).toObject()));
					}
				},
				[=](const Api::Error &) {
					if (current == *generation) {
						consumer.put_next(Entries());
					}
				});
		}, lifetime);
		return lifetime;
	});
}

Badges::Badges(
		QWidget *parent,
		not_null<PeerData*> peer,
		QString slot,
		bool enabled)
: AbstractButton(parent) {
	setAccessibleName(u"see.tg"_q);
	resize(0, st::seetgVerificationBadgeSize);
	if (!enabled) {
		return;
	}
	Value(peer) | rpl::on_next([=](const Entries &entries) {
		_entries.clear();
		auto tooltip = QStringList{ u"see.tg"_q };
		for (const auto &entry : entries) {
			if (entry.slot == slot) {
				_entries.push_back(entry);
				tooltip.push_back(entry.description.empty()
					? entry.type
					: entry.description.text);
			}
		}
		setToolTip(u"<qt>"_q + tooltip.join('\n').toHtmlEscaped()
			.replace(u"\n"_q, u"<br>"_q) + u"</qt>"_q);
		updateSize();
		_updated.fire({});
	}, lifetime());
	addClickHandler([=] {
		UrlClickHandler::Open(u"https://t.me/seetgbot/app?startapp="_q + SeeId(peer->id));
	});
	style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
}

void Badges::fitToWidth(int available) {
	_available = std::max(0, available);
	updateSize();
}

void Badges::updateSize() {
	const auto size = st::seetgVerificationBadgeSize;
	const auto skip = st::seetgVerificationBadgeSkip;
	_visible = std::min(int(_entries.size()), _available / (size + skip));
	resize(_visible ? _visible * (size + skip) - skip : 0, size);
	setVisible(_visible > 0);
	update();
}

int Badges::extent() const {
	return width() ? width() + st::seetgVerificationBadgeSkip : 0;
}

rpl::producer<> Badges::updated() const {
	return _updated.events();
}

void Badges::paintEvent(QPaintEvent *event) {
	auto p = Painter(this);
	const auto size = st::seetgVerificationBadgeSize;
	for (auto i = 0; i != _visible; ++i) {
		const auto rect = style::rtlrect(
			i * (size + st::seetgVerificationBadgeSkip), 0, size, size, width());
		if (i + 1 == _visible && _visible < int(_entries.size())) {
			p.setFont(st::normalFont);
			p.setPen(st::windowSubTextFg);
			p.drawText(rect, Qt::AlignCenter,
				'+' + QString::number(int(_entries.size()) - i));
		} else {
			PaintIcon(p, _entries[i], rect, false);
		}
	}
}

void AddDescriptions(
		not_null<Info::Profile::SectionStack*> stack,
		not_null<PeerData*> peer) {
	auto content = object_ptr<Ui::VerticalLayout>(stack->layout());
	const auto raw = content.data();
	const auto shown = raw->lifetime().make_state<rpl::variable<bool>>(false);
	Value(peer) | rpl::on_next([=](const Entries &entries) {
		raw->clear();
		const auto hasDescriptions = ranges::any_of(entries, [](const Entry &entry) {
			return !entry.description.empty();
		});
		if (hasDescriptions) {
			for (const auto &entry : entries) {
				if (!entry.description.empty()) {
					raw->add(object_ptr<DescriptionRow>(raw, entry),
						st::seetgVerificationPadding);
				}
			}
		}
		*shown = hasDescriptions;
	}, raw->lifetime());
	stack->add({ .widget = std::move(content), .shown = shown->value() });
}

} // namespace Fork::SeeTg::Verification
