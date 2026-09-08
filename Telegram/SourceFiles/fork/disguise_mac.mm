#include "fork/disguise.h"

#include <QtCore/QBuffer>
#include <QtGui/QImage>

#import <Cocoa/Cocoa.h>

namespace Fork::Disguise {

void RefreshNativeMenu() {
	const auto menu = NSApplication.sharedApplication.mainMenu;
	if (!menu.numberOfItems) {
		return;
	}
	static auto previous = QString();
	const auto name = Name();
	const auto item = [menu itemAtIndex:0];
	const auto applicationMenu = item.submenu;
	if (!applicationMenu) {
		return;
	}
	if (previous.isEmpty()) {
		previous = QString::fromNSString(applicationMenu.title);
	}
	for (NSMenuItem *entry in applicationMenu.itemArray) {
		if (entry.action == @selector(hide:)
			|| entry.action == @selector(terminate:)) {
			auto title = QString::fromNSString(entry.title);
			for (const auto &original : { previous, u"SeeGram Dev"_q, u"SeeGram"_q,
				u"Telegram Desktop"_q, u"Telegram"_q }) {
				if (!original.isEmpty() && title.endsWith(original)) {
					title.chop(original.size());
					title += name;
					break;
				}
			}
			entry.title = title.toNSString();
		}
	}
	item.title = name.toNSString();
	applicationMenu.title = name.toNSString();
	previous = name;
}

void RefreshNativeIcon() {
	// Finder custom icons are filesystem metadata. Keep signed Contents intact.
	static auto applied = std::optional<qint64>();
	const auto &image = Image();
	if (image.isNull() || applied == image.cacheKey()) return;
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return;
	const auto native = [[NSImage alloc] initWithData:
		[NSData dataWithBytes:bytes.constData() length:bytes.size()]];
	if (native) {
		const auto path = NSBundle.mainBundle.bundlePath;
		if ([NSWorkspace.sharedWorkspace setIcon:native forFile:path options:0]) {
			applied = image.cacheKey();
		}
		[native release];
	}
}

} // namespace Fork::Disguise
