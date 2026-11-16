# Use XPlasma's native window decorations

If an application's contents appear before its XP title bar or frame, check
whether KWin is using XPlasma's Aurorae SVG theme. XPlasma already includes a
native KDecoration3 renderer. On one Debian Plasma 6.3.6 Wayland desktop,
switching to that renderer resolved the user's reported decoration delay.
This is a manual optimization for an existing installation, not a new renderer
or a guarantee that every source of desktop lag has the same cause.

## Benefits and tradeoffs

| Native renderer benefit | Tradeoff or limit |
| --- | --- |
| Avoids Aurorae's SVG/QML decoration rendering path. | A compiled KWin plugin must match the installed KDecoration/Plasma series. Rebuild and check it after a Plasma series upgrade. |
| Shares rendered frame and button parts between windows, with an 8 MiB cache budget. | Rendering still consumes CPU and memory; this is not a measured speedup for every workload. |
| Draws the existing XP look directly from `xplasmarc`. | Compare title text, button geometry, maximized windows and fractional scaling with the previous theme; rendering equivalence is not certified. |
| Watches theme configuration changes without replacing the Aurorae theme on each change. | This configuration watcher is not an automatic ABI recovery service. |
| Keeps the XP appearance rather than requiring a switch to Breeze. | The native plugin runs inside KWin. A plugin defect can affect the desktop compositor. |

XPlasma's normal theme-apply code prefers a shipped native decoration when its
version stamp matches the running Plasma series, and otherwise uses Aurorae.
That check happens when applying the theme: manually selecting the native
plugin does **not** itself guarantee an automatic fallback after an upgrade.
Keep the previous decoration settings and a rollback path.

## Check the current installation

Run these commands in a terminal in the Plasma desktop session:

```sh
kreadconfig6 --file kwinrc --group org.kde.kdecoration2 --key library
kreadconfig6 --file kwinrc --group org.kde.kdecoration2 --key theme
plasmashell --version
```

`org.kde.kwin.aurorae` and a theme beginning with `__aurorae__svg__xplasma-`
identify the fallback. If the library is already
`fyi.hotsocket.xplasma.decoration`, this optimization is already selected.

## Build for the receiving desktop

Use the source from the installed release or a matching checkout. Do not copy
a decoration binary from a different distribution or Plasma series. The
following example uses the source extracted by the installer. Adjust the
source path if your installation stores it elsewhere.

On Debian with Plasma 6.3, the development packages needed for the decoration
include `libkdecorations3-dev`, `libkf6coreaddons-dev`, `extra-cmake-modules`,
`qt6-base-dev`, a C++ compiler, CMake and Ninja. The complete XPlasma source
configuration also needs its regular native build dependencies. Install those
through your distribution's package manager. Earlier Plasma releases with
KDecoration2 cannot use this KDecoration3 plugin; retain Aurorae there.

```sh
xp_source="$HOME/.local/share/kde-xp-suite/build/source/xplasma"
xp_build="$HOME/.cache/xplasma-native-decoration"
cmake -S "$xp_source" -B "$xp_build" -G Ninja \
  -DQT_MAJOR=6 -DCMAKE_BUILD_TYPE=Release
cmake --build "$xp_build" --target xplasma-decoration --parallel 2
cat "$xp_build/fyi.hotsocket.xplasma.decoration.plasma"
ldd -r "$xp_build/fyi.hotsocket.xplasma.decoration.so"
```

Stop if the target is unavailable, dependencies are missing, or `ldd` reports
unresolved symbols. The stamp's major/minor series must match the running
Plasma series; matching series alone is not a substitute for checking that the
plugin loads against the receiving system's actual libraries.

## Install with a backup

The example below backs up the previous KWin configuration and every file it
replaces. It installs into the user plugin directory and both installed runtime
locations so a later theme apply can find the shipped plugin and its version
stamp. It stages each replacement before renaming it, avoiding an in-place
write to a plugin that KWin might already have mapped.

```sh
python3 - <<'PY'
from datetime import datetime
from pathlib import Path
import json
import shutil

home = Path.home()
build = home / '.cache/xplasma-native-decoration'
name = 'fyi.hotsocket.xplasma.decoration'
sources = [build / (name + suffix) for suffix in ('.so', '.plasma')]
assert all(path.is_file() for path in sources), 'Build the decoration first'
backup = home / '.local/state/xplasma-native-decoration' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
backup.mkdir(parents=True)
config = home / '.config/kwinrc'
shutil.copy2(config, backup / 'kwinrc')
destinations = [
    home / '.local/lib/qt6/plugins/org.kde.kdecoration3',
    home / '.local/share/kde-xp-suite/runtime/lib/xplasma/decoration',
    home / '.local/share/kde-xp-suite/build/runtime/lib/xplasma/decoration',
]
records = []
for directory in destinations:
    directory.mkdir(parents=True, exist_ok=True)
    for source in sources:
        target = directory / source.name
        saved = backup / str(len(records)) if target.exists() else None
        if saved is not None:
            shutil.copy2(target, saved)
        records.append({'path': str(target), 'backup': str(saved) if saved else None})
        (backup / 'files.json').write_text(json.dumps(records, indent=2))
        staged = target.with_name(target.name + '.new')
        shutil.copy2(source, staged)
        staged.replace(target)
print('Backup:', backup)
PY
```

XPlasma's session environment normally adds
`$HOME/.local/lib/qt6/plugins` to `QT_PLUGIN_PATH`. Verify that this is present
in the desktop session before selecting a user-local plugin. If the environment
was just installed, log out and back in first. Do not disable or restart KWin
to force a plugin reload.

## Activate and compare

```sh
kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 \
  --key library fyi.hotsocket.xplasma.decoration
kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key theme ''
qdbus6 org.kde.KWin /KWin reconfigure
```

Use the commands in the actual desktop session; a restricted build session may
not have desktop D-Bus access. Logging out and back in is another way to load
the configured decoration.

Open the same applications and compare title-bar appearance time. Check
minimize, maximize, restore, close, dialog buttons, resizing, active/inactive
titles, theme changes and the scaling factors you use. Keep the native renderer
if it improves responsiveness and preserves the behavior you need.

## Roll back

Use the `library` and `theme` values from the backup's `kwinrc` to restore just
the decoration settings with `kwriteconfig6`, then run the D-Bus reconfigure
command again. This preserves other desktop settings changed since the backup.
For example, the library for the previous SVG decoration is
`org.kde.kwin.aurorae`; use the **exact previous theme ID**, not a placeholder.
If the desktop cannot start, restore these settings from another login or a
text console before starting a new Plasma session.

The backup's `files.json` lists installed paths and any previous copies. After
switching away from the native decoration, restore those copies through a
staged rename or remove paths that had no previous copy. Keep the saved
configuration until you have verified the restored desktop.

## Validation behind this guide

The native decoration built in Release mode against Debian KDecoration 6.3.4
for a KWin 6.3.6 Wayland desktop with Qt 6.8.2. `ldd -r` found no unresolved
symbols. Qt's `QPluginLoader` loaded the plugin and instantiated its factory;
installed copies matched. The user activated the change and confirmed it fixed
the reported lag.

No controlled before/after timing, complete visual regression, upgrade
recovery, or other-distribution live test was performed. The reported
half-second delay was the user's observation, not an instrumented benchmark.
