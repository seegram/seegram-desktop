/*
This file is part of SeeGram Desktop,
a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtGlobal>

// The fork's build counter within one upstream version.
//
// AppVersion belongs to upstream and only changes when upstream releases,
// so on its own it leaves no room to ship a fork build in between. The v2
// update format already carries a 64 bit version of (base << 32 | counter)
// and upstream fills the counter from CanaryBuildCounter - but a
// static_assert in core/update_channel.h forbids a non-zero canary counter
// on a non-canary build, which a stable fork release is. Hence a separate
// counter that lives here.
//
// Bump the number below to publish a fork build between two upstream
// releases, and reset it to 1 when the fork rebases onto a newer upstream
// version. Keeping it a literal rather than a -D flag is deliberate: only
// core/update_channel.h includes this header, so a bump recompiles six
// translation units instead of the whole client, and the number a build
// carries is recorded in git rather than in whoever ran cmake.
//
// -DSEEGRAM_BUILD_COUNTER=N still overrides it for one-off builds.

#ifndef SEEGRAM_BUILD_COUNTER
#define SEEGRAM_BUILD_COUNTER 10
#endif // SEEGRAM_BUILD_COUNTER

namespace Fork {

inline constexpr auto BuildCounter = quint32(SEEGRAM_BUILD_COUNTER);

} // namespace Fork
