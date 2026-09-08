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
