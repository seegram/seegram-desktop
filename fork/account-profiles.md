# Account profiles (Double bottom)

The existing local passcode is the master passcode. It opens every account
and enables profile management in SeeGram settings. Each additional passcode
selects an explicit, nonempty set of account indices. Groups can overlap.
The master passcode and group passcodes must differ from each other.

Only selected accounts start after a cold unlock. Switching groups while the
app is running closes additional windows and calls, clears notifications,
rebuilds the main window, and destroys accounts outside the selected group.
Group management is also rejected at the storage layer in restricted mode.
System and biometric unlock are disabled while groups exist.

The account key file retains its original three fields. The encrypted account
list appends versioned profile metadata; a fourth outer field contains salted
passcode wrappers. The wrappers use Telegram's local key derivation and
encryption routines. Profile names and assignments are encrypted. Saving from
a restricted group retains all unloaded account indices, and new accounts
cannot reuse indices still stored or assigned to another group.

This provides isolation in the running client, not forensic deniability.
All profiles use Telegram's existing shared local encryption key. A party
with a group passcode and access to the files or process memory can recover
that key and inspect other local data. Profile metadata, backups and older
clients must not be treated as secure deletion or evidence of absence.

Validation for the initial implementation includes the account profile policy
tests, 31 native storage checks on disposable data, and manual client testing.
The storage checks cover cold unlock, restricted writes, master/group password
collisions, changing the master passcode, deleting groups and corrupt files.

## Clean groups and application appearance

The group editor can mark a password group as clean. Before its accounts start,
SeeGram disables extension settings and see.tg HTTP/GraphQL requests. Entering
or leaving clean mode recreates each retained account's Main::Session while
preserving the MTProto account, local storage and authorization. This clears
session-owned retained messages, self-destruct previews and extension widgets.
Other groups and the master view restore the user's stored extension settings.
Updates from the SeeGram feed are suspended in clean mode.

The master-only Appearance (Маскировка) section controls the in-app name, the
application icon and an independent tray icon (follow application, SeeGram or
Telegram). Clean mode always selects Telegram for all three. The encrypted
account-list tail stores the appearance settings and clean group IDs after the
existing profile data; files without the new tail keep their old behavior.
The group editor and storage mutators both reject changes from restricted groups.

macOS uses NSWorkspace custom Finder icons to retain the selected app icon after
quitting. Signed Contents remain byte-for-byte unchanged and normal deep code
signature verification passes. Strict packaging verification rejects Finder
metadata; packaging must start from clean build output, not from a running app
with a custom icon. The dev build script validates its clean stage strictly,
then preserves an existing custom Finder icon and verifies the signature again.
A read-only app bundle can only change the running icon. Package identifiers,
executable names, signatures and local data paths are not renamed.

Windows follows AyuGram Desktop's icon application flow: each choice has a
bundled, persistent ICO, taskbar relaunch properties are set before `WM_SETICON`,
matching shortcuts are saved again on selection, and Explorer's icon cache is
refreshed even when no matching shortcut was found. Existing desktop, Start
menu and pinned taskbar shortcuts are selected by the executable's file identity;
shortcut arguments, names and targets are preserved. ICO files are copied
atomically into `tdata/SeeGram-seegram.ico` and `tdata/SeeGram-telegram.ico`.
The signed EXE itself is unchanged; its file icon remains the packaged artwork.
Window titles and icons subscribe to appearance changes, including the first
successful load of encrypted profile settings.

AyuGram reference: Radolyn, 2026, GPL-3.0-or-later, revision
`db3b9891cb0b04ebb7d8c0e71ada3bcc669b910a`:
[icon application](https://github.com/AyuGram/AyuGramDesktop/blob/db3b9891cb0b04ebb7d8c0e71ada3bcc669b910a/Telegram/SourceFiles/ayu/ui/components/icon_picker.cpp),
[ICO assets](https://github.com/AyuGram/AyuGramDesktop/blob/db3b9891cb0b04ebb7d8c0e71ada3bcc669b910a/Telegram/SourceFiles/ayu/ui/ayu_logo.cpp),
[shortcut refresh](https://github.com/AyuGram/AyuGramDesktop/blob/db3b9891cb0b04ebb7d8c0e71ada3bcc669b910a/Telegram/SourceFiles/ayu/utils/windows_utils.cpp),
[taskbar relaunch properties](https://github.com/AyuGram/AyuGramDesktop/blob/db3b9891cb0b04ebb7d8c0e71ada3bcc669b910a/Telegram/SourceFiles/platform/win/main_window_win.cpp).
The Windows Build Check workflow also compiles the actual Shell helper into a
standalone native test: Windows decodes both ICOs at every supported size;
renamed shortcuts switch in both directions while preserving launch parameters;
foreign targets are rejected; taskbar properties are updated and cleared.

`python3 fork/build-dev-mac.py --build-only` compiles without replacing the
user's dev app or touching its profile. Use this for intermediate native tests
in disposable app copies with a distinct bundle identifier. The ordinary command
installs the final dev app at the existing development path.

Validation for clean groups and appearance: the 10 standalone fork tests pass,
and a disposable native macOS scenario passes 58 checks, including cold storage
reload, overlapping account session replacement, restricted writes, synchronous
network rejection and independent tray selection. Native settings and the group
editor were captured at 100% and 200% scale. Both application icons persist as
Finder icons after quitting; signed Contents are unchanged. The temporary test
scenario is removed from the production source before the final dev build.
