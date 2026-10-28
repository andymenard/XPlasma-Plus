# Pixel text in XP Plasma widgets

`XpText.qml` uses the installed XP GDI Tahoma capture and optical families when
`~/.local/share/xplasma/appearance.json` enables `crispText`. It picks the physical
pixel size using the window's current screen scale, rounds the nominal XP font
size before scaling (the same rule as the native style), and uses fractional
point sizes to land on that physical size. Native rendering and disabled
antialiasing preserve solid pixel edges. Pixel mode is selected only at quarter-step
scales from 100% through 300%, including 275%, when the matching face exists.
Intermediate scales and missing faces use scalable Tahoma at the requested size.
Other font families remain unchanged.

One `XpTextStyle` per widget watches appearance.json, including atomic saves,
with FolderListModel. A debounced, safely quoted read uses Plasma's existing
executable data engine; there is no continuous polling. Program submenus share
the Start menu's reader. Switching crisp text off restores the original font.

The Start menu, program flyouts, clock, and six optional XP tray panels use
these components. This does not replace arbitrary third-party Plasma widgets.
Run `python3 tools/widget-text/sync.py` after editing either QML file to update
all packaged copies. The native build embeds the Start menu/clock copies and
the installer ships the tray copies.

Tests load the repository's pixel faces from `non-free/payload/assets/fonts`
and require normal Plasma service access:

```
QT_QPA_PLATFORM=offscreen QT_STYLE_OVERRIDE=Fusion python3 -m unittest discover -s tools/widget-text/tests -v
```

Font source/provenance: [XP capture](../crisp-text/xp-capture/README.md).
