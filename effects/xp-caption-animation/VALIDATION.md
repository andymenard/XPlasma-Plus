Built for the installed ARM64 KWin 6.7.5 / Qt6.11 environment.

Geometry test passed: linear start/midpoint/end, fixed caption height, opaque
rendering at 1x, 1.5x, 2.5x and 3x. Virtual KWin sessions passed maximize,
unmaximize, minimize, taskbar-equivalent restore, interrupted transitions and
close during animation. Runtime logs show ordinary transitions completing at
256ms (250ms nominal, quantized to the 16ms timer/frame schedule).

Installed in ~/.local/lib/qt6/plugins/kwin/effects/plugins/, enabled in kwinrc
and confirmed in the real compositor's loadedEffects list. Live user visual
comparison remains to be done; the automated tests used virtual sessions.

Backup: ~/.local/share/xplasma/maintenance/caption-animation-20261011-221520
Native source is kept here and in the existing Git checkout's effects folder.
This effect has not been published in OTA or added to the cross-distro GUI
installer yet; other KWin versions need a matching build/API port.

## Kubuntu source-build release 0.2.2

- Ubuntu 26.04 container, ARM64, KWin 6.6.6, Qt 6.10.2: full native build,
  caption tests and ldd -r symbol/library checks passed.
- Local builder ran as an unprivileged desktop user, installed the tested
  module, then reused it on the second run without recompiling.
- KWin 6.6 uses the exported renderer factory for the overlay image item and
  the four-argument prePaintWindow API. The 6.7 implementation is retained.
- 28 updater/build/outline tests passed. Signed 0.2.1 -> 0.2.2 optional-feature
  install and rollback passed for AMD64 Qt 6.10 and ARM64 Qt 6.11 fixtures.
- Physical Kubuntu AMD64 desktop activation has not been tested here.

Debian 13 regression: built and linked against KWin 6.3.6 / Qt 6.8.2,
passed the caption motion/raster test, and exercised maximize, restore,
minimize, KWin-requested unminimize, interrupted animation and window close
in an isolated virtual KWin session (`test-kwin.py`). The virtual test uses a
synthetic taskbar destination; real panel geometry requires a live check.
