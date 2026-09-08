#include "fork/disguise.h"

#include "core/application.h"
#include "fork/seetg/seetg_auth.h"
#include "fork/seetg/seetg_http.h"
#include "tray.h"
#include "window/main_window.h"
#include "window/window_controller.h"

#include <rpl/event_stream.h>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtCore/QFile>
#include <QtSvg/QSvgRenderer>
#include "ui/style/style_core.h"

namespace Fork::Disguise {
namespace {

bool CleanMode = true;
Settings Appearance;
uint64 Revision = 0;
uint64 ScopeRevision = 0;
rpl::event_stream<> Updated;

} // namespace

bool FeaturesEnabled() {
	return !CleanMode;
}

bool Clean() {
	return CleanMode;
}

uint64 Generation() {
	return Revision;
}

uint64 ScopeGeneration() {
	return ScopeRevision;
}

rpl::producer<> Changes() {
	return Updated.events();
}

rpl::producer<bool> FeaturesValue() {
	return rpl::single(rpl::empty) | rpl::then(Changes())
		| rpl::map([] { return FeaturesEnabled(); })
		| rpl::distinct_until_changed();
}

QString Name() {
	return Clean() ? u"Telegram"_q
		: Appearance.name.isEmpty() ? u"SeeGram"_q : Appearance.name;
}

QString FullName() {
	return Clean() ? u"Telegram Desktop"_q
		: Appearance.name.isEmpty() ? u"SeeGram Desktop"_q : Appearance.name;
}

rpl::producer<QString> NameValue() {
	return rpl::single(rpl::empty) | rpl::then(Changes())
		| rpl::map([] { return Name(); });
}

const QImage &Image(Icon icon) {
	static const auto telegram = QImage(
#ifdef Q_OS_MAC
		u":/seegram/telegram-mac.png"_q
#else
		u":/seegram/telegram.png"_q
#endif
	);
	static const auto seegram = QImage(u":/seegram/icon.png"_q);
	return (icon == Icon::Telegram) ? telegram : seegram;
}

const QImage &Image() {
	return Image(Clean() ? Icon::Telegram : Appearance.icon);
}

Icon TrayChoice() {
	return Clean() ? Icon::Telegram
		: (Appearance.trayIcon == TrayIcon::Application) ? Appearance.icon
		: (Appearance.trayIcon == TrayIcon::Telegram) ? Icon::Telegram
		: Icon::SeeGram;
}

const QImage &TrayImage() {
	return Image(TrayChoice());
}

QImage TrayMonochrome(QSize size, QColor color) {
	const auto path = TrayChoice() == Icon::Telegram
		? u":/gui/icons/tray/monochrome.svg"_q : u":/seegram/eye.svg"_q;
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) return {};
	auto result = QImage(size, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		auto painter = QPainter(&result);
		auto renderer = QSvgRenderer(file.readAll());
		renderer.setAspectRatioMode(Qt::KeepAspectRatio);
		renderer.render(&painter, QRectF(QPointF(), size));
	}
	return style::colorizeImage(result, color);
}

bool ValidName(const QString &name) {
	if (name.size() > 64) {
		return false;
	}
	for (const auto ch : name) {
		if (!ch.isPrint() || ch.category() == QChar::Other_Format) {
			return false;
		}
	}
	return true;
}

void Apply(bool clean, const Settings &settings) {
	if (CleanMode == clean && Appearance == settings) {
		return;
	}
	if (CleanMode != clean) ++ScopeRevision;
	CleanMode = clean;
	Appearance = settings;
	++Revision;
	if (clean) {
		SeeTg::Auth::CancelAll();
		SeeTg::Http::CancelAll();
	}
	QGuiApplication::setApplicationDisplayName(FullName());
	Updated.fire({});
}

void RefreshApplication() {
	QGuiApplication::setApplicationDisplayName(FullName());
	Core::App().refreshApplicationIcon();
	Core::App().tray().updateIconCounters();
	Core::App().enumerateWindows([](not_null<Window::Controller*> window) {
		window->widget()->updateTitle();
		window->widget()->updateWindowIcon();
	});
#ifdef Q_OS_MAC
	RefreshNativeMenu();
#endif // Q_OS_MAC
}

} // namespace Fork::Disguise
