# Temporary tlottie gradient fix

Upstream revision `4b940c7942fbde8ee56f10f39a5224a4153bd91e` rejects legitimate
gradient-rich animations with `RenderWork`. Algorithm Cup #20 (Gold) fails on
frames 0–178; lib_lottie consequently clears the partially rendered image.
The tlottie revision shipped with tdesktop v7.2.7, `758c7cb744`, renders it.

`gradient-work.patch` corrects the LUT cache-miss work estimate: interpolation
scans color/opacity stops, rather than their four/two scalar components. The
estimate still bounds a full scan for every LUT sample and accounts for the
linear input pass. It does not raise limits, bypass errors, or change gradient
interpolation (including duplicate and unsorted stop handling).

## Build integration

The existing `fork/build_options.cmake` hook includes `override.cmake`. This
covers the normal Dev build and macOS, Windows and Linux release builds.
`prepare.py` builds the **exact affected commit**, with the patch, into
`<CMake build directory>/fork/tlottie/`. Only the imported library path changes;
the shared prepared libraries, vendor checkout and tdesktop submodules stay
untouched. Archives are cached by source revision, patch, builder, Rust version
and target architectures. Preparation logs and the regression result live in
that directory as well.

macOS uses the requested architecture(s), including universal release builds.
Windows follows upstream's static CRT / Win7 build-std settings. Both reuse the
prepared Rust 1.96.1 toolchain. The upstream Linux container deletes Rust after
building its dependencies, so the overlay bootstraps a minimal Rust toolchain
under its own build directory. Its first build needs access to GitHub and
static.rust-lang.org; later builds reuse the archive. No Rust installation or
configuration is written to the runner user's home directory.

`fork/lottie_cache.h` gives rendered TGS frames a new cache namespace at the
sticker and custom-emoji cache entry points. lib_lottie previously persisted
the transparent frames it produced after a render error, so fixing the
renderer alone would leave already viewed gifts broken. Original media and
non-TGS emoji caches are unchanged; old rendered frames expire normally.

## Taking an upstream fix

The patch is only enabled for the affected pin in upstream's `prepare.py`.
When that pin changes, **the patch is automatically bypassed**. The normal
upstream library must then compile and pass `gradient-regression.cpp`, which
renders changing gradients, revisits frames and checks visible output through
the public C API. The result is cached against the **actual library bytes**,
so a stale dependency cache cannot silently reuse a success for another binary.

If the new upstream library still fails, configuration stops with a diagnostic;
it does not silently reapply the old patch or produce another broken release.
Update/rebuild the prepared dependencies first, then investigate the new
upstream revision if it still fails. Once upstream passes, delete this directory
and its single include from `fork/build_options.cmake` when convenient. Keep
the small cache-namespace helper and its two call sites: reverting them would
allow old transparent frames to be reused. The renderer repair itself has no
forked submodule or edited upstream file to reconcile.

## Validation

- The generated C API regression fails on unmodified `4b940c7942` and passes
  on the patched archive and unmodified `758c7cb744`.
- The public Gold TGS (SHA-256
  `d0fc42c916c13d05b8e42c9dbff9695b5788e33bb8e62c825715d04caa40da9e`)
  renders all 180 frames at 96, 192, 288, 384 and 512 px with the patch.
- All 240 upstream Rust library tests, including resource-exhaustion cases,
  pass with `cargo test --locked --release --lib -- --test-threads=1`.
  Use release mode for the suite's timing assertions.

The downloaded gift is diagnostic input only and is not included in the repo.
This fix addresses disappearing animations; it does not establish a cause for
the separately reported allocator crashes.
