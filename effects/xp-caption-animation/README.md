# Windows XP caption animation for KWin

Independent GPL-3.0-or-later implementation using the behavior documented by
[aubymori's Windhawk reference](https://windhawk.net/mods/classic-min-max-animations):
250ms linear movement of a constant-height caption strip, with the app icon and
caption text, without caption buttons. Colors and font come from kdeglobals WM.
Minimize targets KWin's actual taskbar icon geometry; restore reverses the trip.
Maximize/restore retain the old window snapshot until the caption arrives.

This native module supports the KWin 6.6, 6.7 and 6.8 APIs and must be rebuilt for
another KWin ABI/version. It is not a Windows binary or a full-window scale
animation. Borderless/CSD windows and windows without taskbar icon geometry
are intentionally not given a fabricated destination. Fullscreen effects pause
it; closing or interrupting the animated window cancels and releases snapshots.

Build with Qt6, KF6 Config/CoreAddons and the matching KWin development package:

    cmake -S . -B build -DBUILD_SHARED_LIBS=ON
    cmake --build build
    QT_QPA_PLATFORM=offscreen build/test-caption

Install xp-caption-animation.so under the active Qt plugin path at
kwin/effects/plugins/, set Plugins/xp-caption-animationEnabled=true in kwinrc,
and reconfigure/load it through org.kde.KWin's Effects interface. Disable stock
maximize, squash and magiclamp effects to avoid simultaneous animations.

Validation: geometry and HiDPI raster checks; virtual KWin 6.7.5 sessions for
maximize/unmaximize, minimize/taskbar-equivalent restore, rapid interruption,
and window close during animation. No live-session test is claimed by these
virtual checks. Debug traces/test endpoints only activate when the isolated test
session sets XPLASMA_ANIMATION_TEST=1.

The user confirmed the installed Fedora ARM64 effect works on the live desktop.

## OTA 0.2.1

The release supplies source to desktop installations. The optional Caption
Animation binary is tested only on Fedora ARM64 / Qt 6.11 / KWin 6.7.5.
After selecting that feature in XPlasma Update, activate it with:

    python3 ~/.local/share/kwin/xplasma/caption-source/activate.py

The helper checks the architecture, exact KWin version and binary hash before
loading or disabling stock effects. Kubuntu AMD64 needs a matching native build;
the optional ARM binary should remain unselected there. Its other OTA fixes
work independently. Native KWin plugins must be rebuilt after KWin ABI updates.
See CREDITS.md for aubymori's behavioral reference.

## Local Kubuntu build (OTA 0.2.2)

Select Caption Animation in XPlasma Update, then open **XP Caption Animation
Setup** from Start. It installs Ubuntu build dependencies through sudo when
needed, compiles and tests as your normal user, and activates the matching
effect. No XP ISO or Fedora binary is needed. KWin 6.6, 6.7 and 6.8 are supported.

Alternatively:

    python3 ~/.local/share/kwin/xplasma/caption-source/build-local.py --install-deps --activate

Rerun after an OTA source update or KDE upgrade. The helper compares KWin,
Qt, framework package versions, source hashes and the installed binary before
deciding to rebuild. A failed compile/test leaves the previous plugin intact.
An upgraded or already-loaded compositor may require logout/login before
activation; the helper does not restart KWin. It checks the development SDK
against the installed runtime and keeps a prior plugin and configuration backup.

## Rendering optimization

The effect now advances on compositor frames, reuses a pre-rendered caption
label during each animation, and watches WM configuration instead of reopening
it for each animation. KWin 6.6 factory/signature support and native-build ABI
checks are retained. This source merge does not install or activate the effect.
