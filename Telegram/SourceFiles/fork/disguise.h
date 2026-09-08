#pragma once

#include <rpl/producer.h>

class QImage;
class QColor;

namespace Window {
class MainWindow;
} // namespace Window

namespace Fork::Disguise {

enum class Icon { SeeGram, Telegram };
enum class TrayIcon { Application, SeeGram, Telegram };

struct Settings {
	QString name;
	Icon icon = Icon::SeeGram;
	TrayIcon trayIcon = TrayIcon::Application;

	friend bool operator==(const Settings &, const Settings &) = default;
};

[[nodiscard]] bool FeaturesEnabled();
[[nodiscard]] bool Clean();
[[nodiscard]] uint64 Generation();
[[nodiscard]] uint64 ScopeGeneration();
[[nodiscard]] rpl::producer<> Changes();
[[nodiscard]] rpl::producer<bool> FeaturesValue();
[[nodiscard]] QString Name();
[[nodiscard]] QString FullName();
[[nodiscard]] rpl::producer<QString> NameValue();
[[nodiscard]] const QImage &Image();
[[nodiscard]] const QImage &Image(Icon icon);
[[nodiscard]] Icon TrayChoice();
[[nodiscard]] const QImage &TrayImage();
[[nodiscard]] QImage TrayMonochrome(QSize size, QColor color);
[[nodiscard]] bool ValidName(const QString &name);
void Apply(bool clean, const Settings &settings);
void BindWindow(not_null<Window::MainWindow*> window);
void RefreshApplication();
#ifdef Q_OS_WIN
void RefreshNativeIcon(not_null<Window::MainWindow*> window);
#endif // Q_OS_WIN
#ifdef Q_OS_MAC
void RefreshNativeMenu();
void RefreshNativeIcon();
#endif // Q_OS_MAC

} // namespace Fork::Disguise
