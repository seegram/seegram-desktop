/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_verifications.h"
#include "fork/seetg/seetg_verification_icon.h"

#include "fork/fork_lang.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_types.h"
#include "core/click_handler_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "ui/unread_badge.h"
#include "info/profile/info_profile_section_stack.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "ui/text/text_html_tags.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/vertical_list.h"
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
	"verifications { type slot description descriptionTranslations warning } } }";

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

[[nodiscard]] int BadgeSize() {
	return Data::FrameSizeFromTag(Data::CustomEmojiSizeTag::Normal)
		/ style::DevicePixelRatio();
}

[[nodiscard]] QByteArray IconSvg(const Entry &entry, bool description) {
	auto type = (description && entry.warning) ? u"warning"_q : entry.type;
	if (description && type == u"zv"_q) {
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
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void PaintIcon(
		QPainter &p,
		const Entry &entry,
		QRectF rect,
		bool description,
		QColor color) {
	const auto svg = IconSvg(entry, description);
	if (svg.contains("$SEAL")) {
		const auto ratio = p.device()->devicePixelRatioF();
		const auto image = TintIcon(svg, (rect.size() * ratio).toSize(),
			(description && entry.warning) ? st::attentionButtonFg->c : color);
		p.drawImage(rect, image);
	} else {
		auto renderer = QSvgRenderer(svg);
		renderer.render(&p, rect);
	}
}

class DescriptionRow final : public Ui::VerticalLayout {
public:
	DescriptionRow(QWidget *parent, Entry entry, not_null<PeerData*> peer)
	: VerticalLayout(parent)
	, _entry(std::move(entry)) {
		auto label = object_ptr<Ui::FlatLabel>(this,
			rpl::single(_entry.description), st::seetgVerificationDescription);
		label->setSelectable(true);
		label->setMinimumHeight(st::seetgVerificationDescriptionIcon);
		add(std::move(label), style::margins(
			st::seetgVerificationDescriptionIcon + st::seetgVerificationDescriptionSkip,
			0, 0, 0), style::al_justify);
		if (_entry.type == u"telegram"_q) {
			if (const auto details = peer->botVerifyDetails()) {
				_emoji = MakeWrappedEmoji<Ui::Text::FirstFrameEmoji>(
					peer->owner().customEmojiManager().create(
						details->iconId, [=] { update(); },
						Data::CustomEmojiSizeTag::Normal));
			}
		}
		style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
	}

protected:
	void paintEvent(QPaintEvent *event) override {
		auto p = Painter(this);
		const auto size = st::seetgVerificationDescriptionIcon;
		const auto rect = style::rtlrect(0, 0, size, size, width());
		if (_emoji) {
			const auto scale = size / float64(BadgeSize());
			auto hq = PainterHighQualityEnabler(p);
			p.translate(rect.topLeft());
			p.scale(scale, scale);
			_emoji->paint(p, {
				.textColor = st::windowSubTextFg->c,
				.now = crl::now(),
			});
		} else if (_entry.type != u"telegram"_q) {
			PaintIcon(p, _entry, rect, true, st::windowSubTextFg->c);
		}
	}

private:
	Entry _entry;
	std::unique_ptr<Ui::Text::CustomEmoji> _emoji;

};

} // namespace

void PaintBadge(QPainter &p, const Entry &entry, QRectF rect, QColor color) {
	PaintIcon(p, entry, rect, false, color);
}

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
		const auto translations = object.value(u"descriptionTranslations"_q).toObject();
		auto html = translations.value(Lang::ResolvedId()).toString();
		if (html.isEmpty()) {
			html = translations.value(u"en"_q).toString();
		}
		if (html.isEmpty()) {
			html = object.value(u"description"_q).toString();
		}
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
	resize(0, BadgeSize());
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

void Badges::setColor(QColor color) {
	_color = color;
	update();
}

void Badges::fitToWidth(int available) {
	_available = std::max(0, available);
	updateSize();
}

void Badges::updateSize() {
	const auto size = BadgeSize();
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
	const auto size = BadgeSize();
	for (auto i = 0; i != _visible; ++i) {
		const auto rect = style::rtlrect(
			i * (size + st::seetgVerificationBadgeSkip), 0, size, size, width());
		if (i + 1 == _visible && _visible < int(_entries.size())) {
			p.setFont(st::normalFont);
			p.setPen(st::windowSubTextFg);
			p.drawText(rect, Qt::AlignCenter,
				'+' + QString::number(int(_entries.size()) - i));
		} else {
			const auto inset = size * (1. - st::seetgVerificationBadgeScale) / 2.;
			PaintIcon(p, _entries[i],
				QRectF(rect).adjusted(inset, inset, -inset, -inset), false,
				_color.isValid() ? _color : st::profileVerifiedCheckBg->c);
		}
	}
}

void AddDescriptions(
		not_null<Info::Profile::SectionStack*> stack,
		not_null<PeerData*> peer,
		rpl::producer<TextWithEntities> telegramDescription,
		rpl::producer<bool> telegramShown) {
	auto content = object_ptr<Ui::VerticalLayout>(stack->layout());
	const auto raw = content.data();
	const auto shown = raw->lifetime().make_state<rpl::variable<bool>>(false);
	Ui::AddSkip(raw, st::infoProfileSkip);
	auto rows = raw->add(object_ptr<Ui::VerticalLayout>(raw), style::al_justify);
	Ui::AddSkip(raw, st::infoProfileSkip);
	rows->paintRequest() | rpl::on_next([=] {
		auto p = Painter(rows);
		p.fillRect(rows->rect(), st::boxDividerBg);
	}, rows->lifetime());
	style::PaletteChanged() | rpl::on_next([=] { rows->update(); }, rows->lifetime());
	rpl::combine(
		Value(peer),
		std::move(telegramDescription),
		std::move(telegramShown)
	) | rpl::on_next([=](
			const Entries &entries,
			const TextWithEntities &telegram,
			bool showTelegram) {
		rows->clear();
		auto descriptions = Entries();
		if (showTelegram && !telegram.empty()) {
			descriptions.push_back({
				.type = u"telegram"_q,
				.description = telegram,
			});
		}
		for (const auto &entry : entries) {
			if (!entry.description.empty()) {
				descriptions.push_back(entry);
			}
		}
		for (const auto &entry : descriptions) {
			rows->add(object_ptr<DescriptionRow>(rows, entry, peer),
				st::seetgVerificationPadding, style::al_justify);
		}
		*shown = !descriptions.empty();
	}, raw->lifetime());
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		stack->layout(), std::move(content));
	wrap->toggleOn(shown->value())->finishAnimating();
	stack->add({ .widget = std::move(wrap), .shown = shown->value() });
}

} // namespace Fork::SeeTg::Verification
