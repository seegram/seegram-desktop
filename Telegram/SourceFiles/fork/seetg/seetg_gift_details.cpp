#include "fork/seetg/seetg_gift_details.h"
#include "fork/seetg/seetg_api.h"
#include "fork/seetg/seetg_settings.h"
#include "fork/seetg/seetg_visuals.h"
#include "fork/fork_lang.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/click_handler_types.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/controls/table_rows.h"
#include "ui/wrap/table_layout.h"
#include "ui/vertical_list.h"
#include "styles/style_giveaway.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include <QtGui/QPainterPath>
#include "lang/lang_keys.h"
#include "styles/style_boxes.h"
#include "styles/style_widgets.h"
#include <QtCore/QJsonArray>
#include <QtCore/QLocale>
#include <QtCore/QUrl>
#include <cmath>
#include <QtSvg/QSvgRenderer>

namespace Fork::SeeTg::GiftDetails {
namespace {
using Lang::Key;
QString Amount(double value) {
	auto result = QString::number(value, 'f', value >= 1000 ? 0 : value >= 100 ? 1 : 2);
	if (result.contains('.')) {
		while (result.endsWith('0')) result.chop(1);
		if (result.endsWith('.')) result.chop(1);
	}
	return result;
}
void Load(std::shared_ptr<ChatHelpers::Show> show, const QString &slug, Api::Done done) {
	const auto split = slug.lastIndexOf('-');
	const auto num = slug.mid(split + 1).toInt();
	if (split <= 0 || num <= 0) return;
	Api::Query(&show->session(), u"query GiftDetails($slug:String!,$num:Int!){ searchGifts(filter:{slug:$slug,numberFrom:$num,numberTo:$num},first:1){items{estimate{ton low high confidence collectionFloor modelFloor candidates{kind ton} number{premiumTon} updatedAt}}} marketListings(slug:$slug,num:$num){market amount currency link}}"_q,
		{{u"slug"_q, slug.left(split)}, {u"num"_q, num}}, std::move(done), [](const Api::Error&) {});
}
Ui::RpWidget *Icon(QWidget *parent, const QString &url) {
	const auto icon = Ui::CreateChild<Ui::RpWidget>(parent);
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto data = icon->lifetime().make_state<QImage>();
	icon->paintRequest() | rpl::on_next([=] {
		QPainter p(icon);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.setRenderHint(QPainter::Antialiasing);
		if (!url.startsWith(':')) {
			auto clip = QPainterPath();
			clip.addEllipse(QRectF(icon->rect()));
			p.setClipPath(clip);
		}
		if (!data->isNull()) {
			auto image = *data;
			if (url.startsWith(':')) {
				QPainter tint(&image);
				tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
				tint.fillRect(image.rect(), st::windowFg->c);
			}
			p.drawImage(icon->rect(), image);
		}
	}, icon->lifetime());
	if (url.startsWith(':')) {
		const auto size = st::normalFont->height * style::DevicePixelRatio();
		*data = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
		data->fill(Qt::transparent);
		QPainter p(data);
		QSvgRenderer renderer(url);
		renderer.render(&p);
	}
	else Visuals::Image(url, crl::guard(icon, [=](QImage image) { *data = std::move(image); icon->update(); }));
	icon->show();
	return icon;
}
object_ptr<Ui::RpWidget> Branded(QWidget *parent, object_ptr<Ui::RpWidget> body, const QString &user) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	const auto content = body.data();
	body->setParent(raw);
	body.release();
	content->show();
	const auto icon = Icon(raw, user.startsWith(':') ? user : Visuals::UserpicUrl(user));
	const auto size = st::normalFont->height;
	const auto gap = st::boxRowPadding.left() / 3;
	icon->resize(size, size);
	icon->move(0, 0);
	raw->widthValue() | rpl::on_next([=](int width) {
		content->resizeToWidth(std::max(width - size - gap, 1));
		content->move(size + gap, 0);
		raw->resize(width, std::max(size, content->height()));
	}, raw->lifetime());
	content->heightValue() | rpl::on_next([=](int height) { raw->resize(raw->width(), std::max(size, height)); }, raw->lifetime());
	return result;
}
class TonAmount final : public Ui::RpWidget {
public:
	TonAmount(QWidget *parent, QString text, const style::FlatLabel &st, bool pill = false)
	: RpWidget(parent), _text(std::move(text)), _st(st), _pill(pill) {
		const auto margin = _pill ? _st.margin : style::margins();
		const auto width = _st.style.font->width(_text) + _st.style.font->spacew + iconSize()
			+ margin.left() + margin.right();
		setNaturalWidth(width);
		resize(width, _st.style.font->height + margin.top() + margin.bottom());
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	QString accessibilityName() override { return _text + u" TON"_q; }
protected:
	void paintEvent(QPaintEvent*) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto margin = _pill ? _st.margin : style::margins();
		if (_pill) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			p.drawRoundedRect(rect(), height() / 2., height() / 2.);
		}
		p.setFont(_st.style.font);
		p.setPen(_st.textFg);
		const auto size = iconSize();
		const auto available = std::max(width() - margin.left() - margin.right()
			- size - _st.style.font->spacew, 0);
		const auto text = _st.style.font->elided(_text, available);
		p.drawText(margin.left(), margin.top() + _st.style.font->ascent, text);
		const auto pixels = QSize(size, size) * style::DevicePixelRatio();
		if (_glyph.size() != pixels) {
			_glyph = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
			_glyph.fill(Qt::transparent);
			QPainter painter(&_glyph);
			QSvgRenderer renderer(u":/fork/gifts/ton.svg"_q);
			renderer.render(&painter);
		}
		auto image = _glyph;
		{
			QPainter painter(&image);
			painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
			painter.fillRect(image.rect(), _st.textFg->c);
		}
		p.drawImage(QRect(margin.left() + _st.style.font->width(text) + _st.style.font->spacew,
			margin.top() + (_st.style.font->height - size) / 2, size, size), image);
	}
private:
	int iconSize() const { return _st.style.font->height * 3 / 4; }
	QString _text;
	const style::FlatLabel &_st;
	bool _pill = false;
	QImage _glyph;
};

void Explain(std::shared_ptr<ChatHelpers::Show> show, QJsonObject estimate) {
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setStyle(st::giveawayGiftCodeBox);
		box->setWidth(st::boxWideWidth);
		box->setTitle(tr::lng_gift_unique_value() | rpl::map([](const QString &text) { return text + u" · see.tg"_q; }));
		const auto content = box->verticalLayout();
		Ui::AddSkip(content);
		box->addRow(object_ptr<TonAmount>(box, u"~"_q + Amount(estimate[u"ton"_q].toDouble()),
			st::uniqueGiftValuePrice, true), style::al_top);
		Ui::AddSkip(content);
		box->addRow(object_ptr<Ui::FlatLabel>(box, Lang::Text(Key::SeeTgEstimateAbout),
			st::uniqueGiftValueAbout))->setTryMakeSimilarLines(true);
		Ui::AddSkip(content);
		auto table = content->add(object_ptr<Ui::TableLayout>(content,
			st::giveawayGiftCodeTable), st::giveawayGiftCodeTableMargin);
		const auto row = [&](Key key, const QString &value) {
			Ui::AddTableRow(table, Lang::Value(key), rpl::single(TextWithEntities{ value }));
		};
		row(Key::SeeTgEstimateRange, Amount(estimate[u"low"_q].toDouble()) + u" – "_q + Amount(estimate[u"high"_q].toDouble()) + u" TON"_q);

		const auto confidence = estimate[u"confidence"_q].toString();
		row(Key::SeeTgEstimateConfidence, Lang::Text(confidence == u"high" ? Key::SeeTgEstimateHigh
			: confidence == u"medium" ? Key::SeeTgEstimateMedium : Key::SeeTgEstimateLow));
		Ui::AddSkip(content);
		Ui::AddSubsectionTitle(content, Lang::Value(Key::SeeTgEstimateHow));
		table = content->add(object_ptr<Ui::TableLayout>(content,
			st::giveawayGiftCodeTable), st::giveawayGiftCodeTableMargin);
		for (const auto &[key, field] : { std::pair{Key::SeeTgEstimateModel, u"modelFloor"_q}, std::pair{Key::SeeTgEstimateCollection, u"collectionFloor"_q} }) {
			if (const auto price = estimate[field].toDouble(); price > 0) row(key, Amount(price) + u" TON"_q);
		}
		for (const auto &entry : estimate[u"candidates"_q].toArray()) {
			const auto signal = entry.toObject();
			const auto kind = signal[u"kind"_q].toString();
			if (kind != u"own_sale" && kind != u"old_sale"
				&& kind != u"combo_sales" && kind != u"model_sales") continue;
			const auto key = kind == u"own_sale" ? Key::SeeTgEstimateOwn
				: kind == u"old_sale" ? Key::SeeTgEstimateOld
				: kind == u"combo_sales" ? Key::SeeTgEstimateCombo
				: kind == u"model_sales" ? Key::SeeTgEstimateModels
				: kind == u"model_floor" ? Key::SeeTgEstimateModel
				: Key::SeeTgEstimateCollection;
			const auto price = signal[u"ton"_q].toDouble();
			if (std::isfinite(price) && price > 0) row(key, Amount(price) + u" TON"_q);
		}
		if (const auto premium = estimate[u"number"_q].toObject()[u"premiumTon"_q].toDouble(); premium > 0) {
			row(Key::SeeTgEstimateNumber, Amount(premium) + u" TON"_q);
		}
		Ui::AddSkip(content);
		const auto updated = box->addRow(object_ptr<Ui::FlatLabel>(box,
			Lang::Text(Key::SeeTgEstimateUpdated) + u": "_q + estimate[u"updatedAt"_q].toString().left(10),
			st::uniqueGiftValueAbout), style::al_top);
		updated->setTextColorOverride(st::windowSubTextFg->c);
		Ui::AddSkip(content);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}
} // namespace

object_ptr<Ui::RpWidget> Value(not_null<Ui::TableLayout*> parent, std::shared_ptr<ChatHelpers::Show> show,
		const QString &slug, object_ptr<Ui::RpWidget> telegram) {
	if (!Enabled(Feature::GiftDetails)) return telegram;
	auto result = object_ptr<Ui::VerticalLayout>(parent);
	const auto root = result.data();
	const auto wrap = root->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(root, object_ptr<Ui::VerticalLayout>(root)));
	wrap->toggle(false, anim::type::instant);
	if (telegram) root->add(Branded(root, std::move(telegram), u"telegram"_q));
	Load(show, slug, crl::guard(root, [=](const QJsonObject &data) {
		if (!Enabled(Feature::GiftDetails)) return;
		const auto items = data[u"searchGifts"_q].toObject()[u"items"_q].toArray();
		if (items.isEmpty()) return;
		const auto estimate = items.first().toObject()[u"estimate"_q].toObject();
		const auto amount = estimate[u"ton"_q].toDouble();
		if (!std::isfinite(amount) || amount <= 0) return;
		const auto body = wrap->entity();
		const auto price = Ui::CreateChild<TonAmount>(parent, u"~"_q + Amount(amount), parent->st().defaultValue);
		auto value = Ui::MakeValueWithSmallButton(parent, price,
			Lang::Value(Key::SeeTgEstimateHow) | rpl::map([](const QString &text) { return text.toLower(); }),
			[=](not_null<Ui::RpWidget*>) { Explain(show, estimate); });
		body->add(Branded(body, std::move(value.widget), u"seetgbot"_q));
		wrap->toggle(true, anim::type::instant);
	}));
	return result;
}

void BuyButton(not_null<Ui::RoundButton*> button, std::shared_ptr<ChatHelpers::Show> show, const QString &slug, Fn<void()> ready) {
	if (!Enabled(Feature::GiftDetails)) return;
	Load(show, slug, crl::guard(button, [=](const QJsonObject &data) {
		if (!Enabled(Feature::GiftDetails)) return;
		for (const auto &item : data[u"marketListings"_q].toArray()) {
			const auto sale = item.toObject();
			const auto market = sale[u"market"_q].toString().toLower();
			const auto user = (market == u"portals") ? u"portals"_q : (market == u"tonnel") ? u"tonnel_relayer_bot"_q : (market == u"getgems") ? u"getgems"_q : (market == u"mrkt") ? u"mrkt"_q : QString();
			const auto url = QUrl(sale[u"link"_q].toString());
			const auto currency = sale[u"currency"_q].toString().toLower();
			const auto amount = sale[u"amount"_q].toString().toDouble() / (currency == u"usdt" ? 1e6 : 1e9);
			if ((currency != u"ton" && currency != u"gram" && currency != u"usdt"
				&& currency != u"major" && currency != u"not" && currency != u"dogs")
				|| user.isEmpty() || !std::isfinite(amount) || amount <= 0 || url.scheme() != u"https" || !(url.host() == u"t.me" || url.host() == u"getgems.io")) continue;
			const auto label = tr::lng_gift_buy_resale_button(tr::now, lt_cost, QLocale().toString(amount, 'f', amount >= 1000 ? 0 : amount >= 100 ? 1 : 2) + ' ' + ((currency == u"gram" || currency == u"ton") ? u"TON"_q : currency.toUpper()));
			const auto size = button->st().style.font->height;
			const auto gap = st::boxRowPadding.left() / 3;
			button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
			button->setText(rpl::single(label));
			button->setTextFgOverride(Qt::transparent);
			button->setClickedCallback([=] { if (Enabled(Feature::GiftDetails)) UrlClickHandler::Open(url.toString()); });
			const auto caption = Ui::CreateChild<Ui::RpWidget>(button);
			caption->setAttribute(Qt::WA_TransparentForMouseEvents);
			const auto icon = Icon(caption, Visuals::UserpicUrl(user));
			icon->resize(size, size);
			caption->paintRequest() | rpl::on_next([=] {
				const auto &st = button->st();
				const auto padding = st::boxRowPadding.left();
				const auto text = st.style.font->elided(label, std::max(caption->width() - 2 * padding - size - gap, 0));
				const auto width = st.style.font->width(text) + size + gap;
				const auto left = (caption->width() - width) / 2;
				icon->move(left, (caption->height() - size) / 2);
				auto p = Painter(caption);
				p.setFont(st.style.font);
				p.setPen(button->isOver() ? st.textFgOver : st.textFg);
				p.drawText(left + size + gap, (caption->height() - st.style.font->height) / 2 + st.style.font->ascent, text);
			}, caption->lifetime());
			button->sizeValue() | rpl::on_next([=](QSize bounds) {
				caption->setGeometry(QRect(QPoint(), bounds));
			}, caption->lifetime());
			caption->show();
			if (ready) ready();
			break;
		}
	}));
}
} // namespace Fork::SeeTg::GiftDetails
