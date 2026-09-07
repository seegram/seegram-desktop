/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "fork/seetg/seetg_picker.h"

#include "fork/fork_lang.h"
#include "lang/lang_keys.h"
#include "ui/abstract_button.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/multi_select.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "styles/style_widgets.h"

namespace Fork::SeeTg::Picker {
namespace {

using Lang::Key;

constexpr auto kRowHeight = 52;
constexpr auto kPreviewSize = 36;
constexpr auto kMaxRows = 300;

// The little round preview a row and a chip share: the backdrop's gradient,
// the image (tinted for a pattern mask), or the first letter.
class Preview final {
public:
	Preview(not_null<QWidget*> owner, const Item &item);

	void paint(Painter &p, int x, int y, int size) const;

private:
	const Item &_item;
	QImage _image;

};

Preview::Preview(not_null<QWidget*> owner, const Item &item)
: _item(item) {
	if (!_item.imageUrl.isEmpty()) {
		Visuals::Image(
			_item.imageUrl,
			crl::guard(owner, [=, owner = owner.get()](QImage image) {
				_image = std::move(image);
				owner->update();
			}));
	}
}

void Preview::paint(Painter &p, int x, int y, int size) const {
	auto hq = PainterHighQualityEnabler(p);
	const auto rect = QRect(x, y, size, size);
	p.setPen(Qt::NoPen);
	if (_item.backdrop) {
		auto gradient = QRadialGradient(rect.center(), size / 2.);
		gradient.setStops({
			{ 0., _item.backdrop->center },
			{ 1., _item.backdrop->edge },
		});
		p.setBrush(gradient);
		p.drawEllipse(rect);
		return;
	}
	p.setBrush(st::windowBgOver);
	p.drawEllipse(rect);
	if (_image.isNull()) {
		p.setPen(st::windowSubTextFg);
		p.setFont(st::semiboldFont);
		p.drawText(rect, _item.name.left(1).toUpper(), style::al_center);
		return;
	}
	const auto inner = rect.marginsRemoved({ 4, 4, 4, 4 });
	if (_item.tintImage) {
		auto tinted = QImage(
			inner.size() * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		tinted.setDevicePixelRatio(style::DevicePixelRatio());
		tinted.fill(st::windowSubTextFg->c);
		auto q = QPainter(&tinted);
		q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		q.drawImage(QRect(QPoint(), inner.size()), _image);
		q.end();
		p.drawImage(inner, tinted);
	} else {
		p.drawImage(inner, _image);
	}
}

class Row final : public Ui::AbstractButton {
public:
	Row(QWidget *parent, const Item &item, bool selected);

	void setSelected(bool selected);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const Item &_item;
	Preview _preview;
	bool _selected = false;

};

Row::Row(QWidget *parent, const Item &item, bool selected)
: AbstractButton(parent)
, _item(item)
, _preview(this, item)
, _selected(selected) {
	resize(width(), kRowHeight);
	setPointerCursor(true);
}

void Row::setSelected(bool selected) {
	if (_selected != selected) {
		_selected = selected;
		update();
	}
}

void Row::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	if (isOver()) {
		p.fillRect(rect(), st::windowBgOver);
	}
	const auto left = st::boxRowPadding.left();
	_preview.paint(p, left, (height() - kPreviewSize) / 2, kPreviewSize);
	const auto textLeft = left + kPreviewSize + st::boxRowPadding.left() / 2;
	const auto right = st::boxRowPadding.right()
		+ st::mediaPlayerMenuCheck.width()
		+ st::boxRowPadding.right() / 2;
	p.setFont(st::normalFont);
	p.setPen(st::windowFg);
	p.drawTextLeft(
		textLeft,
		(height() - st::normalFont->height) / 2,
		width(),
		st::normalFont->elided(_item.name, width() - textLeft - right));
	if (_selected) {
		const auto &icon = st::mediaPlayerMenuCheck;
		icon.paint(
			p,
			width() - st::boxRowPadding.right() - icon.width(),
			(height() - icon.height()) / 2,
			width());
	}
}

void PickerBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Args> args) {
	box->setTitle(std::move(args->title));
	box->setWidth(st::boxWideWidth);

	struct State {
		base::flat_set<QString> selected;
		base::flat_map<QString, Row*> rows;
		std::vector<std::unique_ptr<Preview>> chipPreviews;
		QString query;
	};
	const auto state = box->lifetime().make_state<State>();
	for (const auto &id : args->selected) {
		state->selected.emplace(id);
	}
	const auto indexOf = [=](const QString &id) -> int {
		for (auto i = 0; i != int(args->items.size()); ++i) {
			if (args->items[i].id == id) {
				return i;
			}
		}
		return -1;
	};

	const auto select = box->setPinnedToTopContent(
		object_ptr<Ui::MultiSelect>(
			box,
			st::defaultMultiSelect,
			tr::lng_dlg_filter()));
	const auto content = box->verticalLayout();

	const auto addChip = [=](int index, bool animated) {
		const auto &item = args->items[index];
		state->chipPreviews.push_back(std::make_unique<Preview>(select, item));
		const auto preview = state->chipPreviews.back().get();
		const auto paint = [=](Painter &p, int x, int y, int outerWidth, int size) {
			preview->paint(p, x, y, size);
		};
		if (animated) {
			select->addItem(
				index + 1,
				item.name,
				st::defaultMultiSelect.item.textActiveBg,
				paint);
		} else {
			select->addItemInBunch(
				index + 1,
				item.name,
				st::defaultMultiSelect.item.textActiveBg,
				paint);
		}
	};
	for (const auto &id : args->selected) {
		if (const auto index = indexOf(id); index >= 0) {
			addChip(index, false);
		}
	}
	select->finishItemsBunch();

	const auto rebuild = [=] {
		state->rows.clear();
		while (content->count() > 0) {
			delete content->widgetAt(0);
		}
		const auto query = state->query.trimmed();
		auto shown = 0;
		for (auto i = 0; i != int(args->items.size()); ++i) {
			const auto &item = args->items[i];
			if (!query.isEmpty()
				&& !item.name.contains(query, Qt::CaseInsensitive)) {
				continue;
			}
			if (++shown > kMaxRows) {
				break;
			}
			const auto row = content->add(object_ptr<Row>(
				content,
				item,
				state->selected.contains(item.id)));
			state->rows.emplace(item.id, row);
			row->setClickedCallback([=] {
				const auto id = item.id;
				if (state->selected.contains(id)) {
					state->selected.remove(id);
					select->removeItem(i + 1);
					row->setSelected(false);
				} else {
					state->selected.emplace(id);
					addChip(i, true);
					row->setSelected(true);
				}
			});
		}
	};
	rebuild();

	select->setQueryChangedCallback([=](const QString &query) {
		state->query = query;
		rebuild();
	});
	select->setItemRemovedCallback([=](uint64 itemId) {
		const auto index = int(itemId) - 1;
		if (index < 0 || index >= int(args->items.size())) {
			return;
		}
		const auto &id = args->items[index].id;
		state->selected.remove(id);
		const auto i = state->rows.find(id);
		if (i != end(state->rows)) {
			i->second->setSelected(false);
		}
	});
	select->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		// Enter picks the single visible match, the way one expects.
		if (state->rows.size() == 1) {
			state->rows.begin()->second->clicked({}, Qt::LeftButton);
		}
	});
	box->setFocusCallback([=] { select->setInnerFocus(); });

	box->addLeftButton(Lang::Value(Key::Reset), [=] {
		for (const auto &id : base::take(state->selected)) {
			if (const auto index = indexOf(id); index >= 0) {
				select->removeItem(index + 1);
			}
		}
		for (const auto &[id, row] : state->rows) {
			row->setSelected(false);
		}
	});
	box->addButton(Lang::Value(Key::SeeTgApply), [=] {
		// In the catalogue's order, not the order they were picked in.
		auto result = QStringList();
		for (const auto &item : args->items) {
			if (state->selected.contains(item.id)) {
				result.push_back(item.id);
			}
		}
		const auto done = args->done;
		box->closeBox();
		done(result);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

void Show(not_null<Window::SessionController*> controller, Args &&args) {
	controller->show(Box(PickerBox, std::make_shared<Args>(std::move(args))));
}

} // namespace Fork::SeeTg::Picker
