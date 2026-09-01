# The credentials the client identifies itself with to Telegram.
#
# Kept in the repository so that every build uses the same pair without a
# runner or a working copy having to carry a copy of it: local builds, the
# macOS runner and the Windows runner all read this one file.
#
# These are Telegram Desktop's own. Telegram issues them to its own client and
# expects other clients to register their own at my.telegram.org, so a fork
# using them is outside the API terms and accounts have been banned for it.
#
# Set as normal variables rather than cache entries on purpose: a value left
# behind in an old CMakeCache.txt would otherwise quietly win.

set(TDESKTOP_API_ID 2040)
set(TDESKTOP_API_HASH b18441a1ff607e10a989891a5462e627)
