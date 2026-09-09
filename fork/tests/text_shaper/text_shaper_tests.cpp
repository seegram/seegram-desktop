#include "base/integration.h"
#include "ui/integration.h"
#include "ui/text/text_block.h"
#include "ui/text/text_shaper.h"
#include "styles/style_basic.h"

#include <QtCore/QResource>
#include <QtCore/QTemporaryDir>
#include <QtGui/QGuiApplication>
#include <QtGui/QFontDatabase>
#include <QtGui/QPainter>
#include <QtPlugin>

#include <iostream>

Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)

namespace {

class BaseIntegration final : public base::Integration {
public:
	using Integration::Integration;
	void enterFromEventLoop(FnMut<void()> &&call) override { call(); }
	bool logSkipDebug() override { return true; }
	void logMessageDebug(const QString &) override {}
	void logMessage(const QString &) override {}
	void logAssertionViolation(const QString &info) override {
		std::cerr << info.toStdString() << '\n';
	}

};

class UiIntegration final : public Ui::Integration {
public:
	void postponeCall(FnMut<void()> &&call) override { call(); }
	void registerLeaveSubscription(not_null<QWidget*>) override {}
	void unregisterLeaveSubscription(not_null<QWidget*>) override {}
	QString emojiCacheFolder() override { return _directory.path(); }
	QString fontsCacheFolder() override { return _directory.path(); }
	QString openglCheckFilePath() override { return {}; }
	QString angleBackendFilePath() override { return {}; }
	void touchCounterIncrement() override {}
	int touchCounterNow() override { return 0; }

private:
	QTemporaryDir _directory;

};

struct Formatting {
	EntityType entity;
	Ui::Text::TextBlockFlags flags;
};

struct Measurement {
	Ui::Fixed bearing;
	QImage image;
};

Measurement Measure(
		const style::TextStyle &style,
		const QString &sample,
		Formatting first,
		Formatting last,
		bool underline) {
	auto marked = TextWithEntities{ sample + u" X"_q };
	if (first.entity != EntityType::Invalid) {
		marked.entities.emplace_back(first.entity, 0, sample.size());
	}
	if (last.entity != EntityType::Invalid) {
		marked.entities.emplace_back(last.entity, sample.size() + 1, 1);
	}
	auto text = Ui::Text::String(style, marked);
	const auto plain = text.toString();
	auto paragraph = Ui::Text::Paragraph();
	paragraph.resolve(&text, 0, plain.size(), false, 0, -1);
	auto shaper = Ui::Text::LineShaper(&text, paragraph, 0, plain);
	shaper.shapeRange(0, shaper.findItem(plain.size() - 1));
	const auto index = shaper.findItem(sample.size() - 1);
	const auto &item = shaper.items()[index];
	const auto shaped = shaper.shape(index);
	const auto length = int(sample.size()) - item.position;
	const auto bearing = shaped.rightBearingBefore(length);

	// Paint another item first. Its font/cache must not affect the saved item.
	const auto tailIndex = int(shaper.items().size()) - 1;
	const auto tail = shaper.shape(tailIndex);
	auto scratch = QImage(256, 96, QImage::Format_ARGB32_Premultiplied);
	scratch.fill(Qt::white);
	auto scratchPainter = QPainter(&scratch);
	tail.draw(scratchPainter, QPointF(16, 64), 0, tail.length(),
		Ui::Text::WithFlags(style.font, last.flags)->f);
	scratchPainter.end();

	auto image = QImage(256, 96, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::white);
	auto painter = QPainter(&image);
	painter.setPen(Qt::black);
	auto font = Ui::Text::WithFlags(style.font, first.flags)->f;
	font.setUnderline(underline);
	shaped.draw(painter, QPointF(16, 64), 0, length, font);
	painter.end();
	if (shaped.rightBearingBefore(length) != bearing) {
		std::cerr << "A saved glyph's bearing changed after drawing another font.\n";
		std::exit(1);
	}
	return { bearing, image };
}

} // namespace

namespace crl {
rpl::producer<> on_main_update_requests() {
	return rpl::never<>();
}
} // namespace crl

int main(int argc, char *argv[]) {
	auto app = QGuiApplication(argc, argv);
	if (argc != 2 || !QResource::registerResource(QString::fromLocal8Bit(argv[1]))) {
		std::cerr << "Pass this build's lib_ui.rcc as the only argument.\n";
		return 2;
	}
	auto base = BaseIntegration(argc, argv);
	base::Integration::Set(&base);
	auto ui = UiIntegration();
	Ui::Integration::Set(&ui);
	style::SetDevicePixelRatio(1);
	style::internal::StartFonts();
	style::StartManager(100);

	using Flag = Ui::Text::TextBlockFlag;
	const auto formats = std::array{
		Formatting{ EntityType::Invalid, {} },
		Formatting{ EntityType::Bold, Flag::Bold },
		Formatting{ EntityType::Italic, Flag::Italic },
		Formatting{ EntityType::Code, Flag::Code },
	};
	const auto samples = QStringList{
		u"f"_q, u"j"_q, u"ffi"_q, u"Привет"_q, u"中文"_q,
		u"עברית"_q, u"العربية"_q, u"𝒜"_q, u"𝗮"_q, u"ᚠ"_q,
		u"𐰀"_q, u"ꙮ"_q, u"ᗩ"_q, u"ʕ"_q,
	};
	const auto families = QStringList{
		u"Open Sans"_q,
		QFontDatabase::systemFont(QFontDatabase::GeneralFont).family(),
		QFontDatabase::systemFont(QFontDatabase::FixedFont).family(),
	};
	auto checked = 0;
	for (const auto &family : families) {
		auto textStyle = st::defaultTextStyle;
		textStyle.font = style::font(18, {}, family);
		for (const auto &sample : samples) {
			for (const auto first : formats) {
				for (const auto underline : { false, true }) {
					const auto expected = Measure(textStyle, sample, first, first, underline);
					for (const auto last : formats) {
						const auto actual = Measure(textStyle, sample, first, last, underline);
						if (actual.bearing != expected.bearing || actual.image != expected.image) {
							std::cerr << "An unrelated trailing font changed an earlier glyph: "
								<< family.toStdString() << " / " << sample.toStdString()
								<< " / " << int(first.entity) << " -> " << int(last.entity)
								<< " / bearing " << actual.bearing.raw()
								<< " expected " << expected.bearing.raw() << '\n';
							return 1;
						}
						++checked;
					}
				}
			}
		}
	}
	std::cout << "PASS: " << checked << " mixed-font bearing and rendering checks.\n";
	return 0;
}
