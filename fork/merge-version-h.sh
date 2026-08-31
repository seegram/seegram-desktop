#!/usr/bin/env bash
#
# Custom merge driver for Telegram/SourceFiles/core/version.h.
#
# The fork changes AppId/AppNameOld/AppName/AppFile; upstream bumps
# AppVersion/AppVersionStr on the lines right below. git folds the two into
# one conflicting region on every upstream release, and rerere cannot cache
# the resolution because the version differs each time. The resolution is
# mechanical - keep the fork's names, take upstream's version - so do it here.
#
# The trick is to not merge the branding lines at all: normalise them to the
# merge base on all three sides, let git merge what is left (which is only
# ever upstream's version bump), then write the fork's names back. That needs
# no guessing about which side is which - during a rebase "ours" is upstream
# and "theirs" is the fork, during a merge it is the other way round, and
# getting it backwards silently produces a file with the wrong names.
#
# Registered per clone by fork_ensure_setup in fork/_common.sh.
# Arguments follow git's merge driver contract: %O %A %B.
set -euo pipefail

python3 - "$1" "$2" "$3" <<'PY'
import re, subprocess, sys

base, ours, theirs = sys.argv[1], sys.argv[2], sys.argv[3]

LINE = re.compile(
    r'^[ \t]*constexpr auto (AppId|AppNameOld|AppName|AppFile)[ \t]*=.*$', re.M)

def read(p):
    return open(p, encoding='utf-8').read()

def write(p, text):
    open(p, 'w', encoding='utf-8').write(text)

def branding(text):
    return {m.group(1): m.group(0) for m in LINE.finditer(text)}

texts = {p: read(p) for p in (base, ours, theirs)}
b_base, b_ours, b_theirs = (branding(texts[p]) for p in (base, ours, theirs))

if not b_base:
    sys.exit(1)          # file no longer looks the way we expect

# The fork side is whichever differs from the merge base. If both differ and
# disagree, this is a real conflict - leave it to a human rather than pick.
ours_changed = b_ours != b_base
theirs_changed = b_theirs != b_base
if ours_changed and theirs_changed and b_ours != b_theirs:
    sys.exit(1)
keep = b_ours if ours_changed else b_theirs if theirs_changed else b_base

# Normalise all three sides to the base's branding, so the only difference
# left for git to merge is upstream's version bump.
for p in (base, ours, theirs):
    write(p, LINE.sub(lambda m: b_base.get(m.group(1), m.group(0)), texts[p]))

rc = subprocess.call(['git', 'merge-file',
                      '-L', 'ours', '-L', 'base', '-L', 'theirs',
                      ours, base, theirs])

# Put the fork's branding back into the merged result.
merged = LINE.sub(lambda m: keep.get(m.group(1), m.group(0)), read(ours))
write(ours, merged)

sys.exit(1 if (rc != 0 or '<<<<<<<' in merged) else 0)
PY
