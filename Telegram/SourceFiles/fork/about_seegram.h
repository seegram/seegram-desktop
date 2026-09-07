/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class FlatLabel;
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Fork::About {

[[nodiscard]] QString VersionText();
void Fill(not_null<Ui::GenericBox*> box);
void SetupFooter(not_null<Ui::FlatLabel*> seegram,
	not_null<Ui::FlatLabel*> telegram,
	not_null<Window::SessionController*> controller);

} // namespace Fork::About
