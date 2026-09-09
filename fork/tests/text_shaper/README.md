# Qt text-shaping regression

This links the client's actual `lib_ui`. It measures and paints multilingual
fragments with ordinary, bold, italic, and monospace formatting, then changes an
unrelated trailing fragment's font. The earlier fragment's bearing and pixels
must remain unchanged, including after drawing the trailing fragment and when
underlining is enabled. It opens no windows and uses no Telegram accounts.

For an already configured macOS development tree:

```sh
env -u QT cmake -S . -B out/dev-build -DSEEGRAM_BUILD_TEXT_SHAPER_TESTS=ON
cmake --build out/dev-build --config Debug --target seegram_text_shaper_tests -j 8
QT_QPA_PLATFORM=offscreen out/dev-build/seegram-tests/Debug/seegram_text_shaper_tests.app/Contents/MacOS/seegram_text_shaper_tests out/dev-build/lib_ui.rcc
```

The macOS test bundle contains only the command-line regression test and the
library's resources; it is not a client build. Build the normal development
client with `python3 fork/build-dev-mac.py`.

The target is opt-in and excluded from ordinary builds. The fix and this target
apply to the Qt text backend; builds using Pango do not use them.
