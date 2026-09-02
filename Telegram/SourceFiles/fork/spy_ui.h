/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

// Where spy mode shows what it has kept: an "Edit history" entry in a
// message's context menu, and a "SeeGram" submenu in the chat's own menu
// with the deleted messages of that chat. Both lists open as boxes.
//
// AyuGram renders both as full chat-like sections with real message bubbles,
// which takes a copy of the admin log widget. A box with the texts is what
// this fork can afford without carrying three thousand lines through every
// rebase; the content shown is the same.

class HistoryItem;
class PeerData;

namespace Data {
class Thread;
} // namespace Data

namespace Ui {
class PopupMenu;
namespace Menu {
struct MenuCallback;
} // namespace Menu
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Fork::SpyUi {

void ShowEditHistory(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

void ShowDeletedMessages(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	MsgId topicRootId);

// Hook, called from the message context menus.
void AddHistoryAction(
	not_null<Ui::PopupMenu*> menu,
	HistoryItem *item,
	not_null<Window::SessionController*> controller);

// Hook, called from the chat's own menu.
void AddChatActions(
	PeerData *peer,
	Data::Thread *thread,
	not_null<Window::SessionController*> controller,
	const Ui::Menu::MenuCallback &addAction);

} // namespace Fork::SpyUi
