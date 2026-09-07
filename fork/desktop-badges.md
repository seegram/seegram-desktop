# Profile badge icons

SeeGram reads icons from `https://see.tg/desktop/badges/<type>.svg`.
The server catalog lives at `/home/crgr/see-tg/desktop-badges/` on `pizdes`,
independently of frontend builds. A new verification type needs its matching SVG
there; no desktop release is required after installing the client that supports
this catalog. Built-in icons are only offline fallbacks.

Use the existing 56×56 frame. `$SEAL` and `$GLYPH` produce a theme-colored mask
with a transparent glyph; literal colors preserve multicolor artwork.
Description rows use `<type>.svg`, `zv-mono.svg` for `zv`, and `warning.svg` for
warnings. Telegram's own verification icons remain Telegram-managed.

Publish by writing a temporary file then renaming it over `<type>.svg`.
Keep files below 128 KiB and self-contained: paths, groups, shapes, gradients,
clip paths, optionally embedded PNG. Scripts, animation, external references,
DTDs and arbitrary CSS are rejected. `fork/tests/badge_svg_tests.cpp` checks the
format and bundled fallback assets.

The client loads its last valid disk cache immediately, merges concurrent
requests, and revalidates seen icons every five minutes with ETags. A five-minute
URL bucket also avoids the site's longer CDN cache. Invalid responses and
network failures preserve the last good icon. New data repaints profile headers,
descriptions and comment authors without reopening the application.
