> [!WARNING]
> **This project is vibecoded.** Hands-on testing has been limited to **Asahi Linux (ARM64)** and **Kubuntu on Intel/AMD x86-64**. Issues are expected; builds for other platforms do not mean they have been tested on real hardware. **I do not recommend installing this on a computer without backing up your data and system configuration first. While I personally run this on my main computers and have tried my best to audit the code, I am not a software engineer.**

# XPlasma Plus — 1.0.1 Public Beta

This is a project I've been working on for personal use that I wanted to share with anyone else who might enjoy it. This is a KDE Plasma transformation pack and **modified fork of [hotsocket's XPlasma](https://tangled.org/hotsocket.fyi/xplasma/)**. The native visual-style engine, Qt style, Properties editor and desktop generators are forked from that. This repository contains our (me and AI's) ports, integration and changes; it does not claim authorship of the upstream foundation. It also includes a KDE lock screen theme, SDDM theme, Plymouth boot splash theme, and icon theme with a special thanks to [marchmountain for the hi-res icons](https://github.com/marchmountain/-Windows-XP-High-Resolution-Icon-Pack).

Also check out **[Rozniak's Windows XP Total Conversion for XFCE](https://github.com/rozniak/xfce-winxp-tc)**, the main inspiration for this project. If you enjoy bringing the XP experience to Linux and are partial to XFCE, give Rozniak's project a look.

The alpha includes the XP desktop by default and the IE6 Firefox theme as an option. There are currently no XP remake applications included, but I may include them in the future. 

## Tutorials

New to XPlasma Plus? Follow the **[tutorial wiki](https://github.com/andymenard/XPlasma-Plus/wiki)** for installation, desktop customization, IE6 Firefox, lock/login/boot screens, upgrades, removal and troubleshooting. A [copy of the tutorials](docs/wiki/Home.md) is also kept in this repository.

## Screenshots

### Default theme

The default Windows XP desktop theme on Kubuntu.

![Default theme: The default Windows XP desktop theme on Kubuntu.](docs/screenshots/01-default-theme.png)

### Custom colors

Customize the theme colors in XPlasma Plus Properties.

![Custom colors: Customize the theme colors in XPlasma Plus Properties.](docs/screenshots/02-custom-colors.png)

### Customizable Start button

Choose your own Start button logo and appearance.

![Customizable Start button: Choose your own Start button logo and appearance.](docs/screenshots/03-custom-start-button.png)

### Internet Explorer appearance

The optional IE6 replica theme for Firefox ESR.

![Internet Explorer appearance: The optional IE6 replica theme for Firefox ESR.](docs/screenshots/04-internet-explorer-theme.png)

### Dark mode

Dark application colors with a matching Firefox theme.

![Dark mode: Dark application colors with a matching Firefox theme.](docs/screenshots/05-dark-mode.png)

### HiDPI support

The XP desktop on a HiDPI display at 250% scaling.

![HiDPI support: The XP desktop on a HiDPI display at 250% scaling.](docs/screenshots/06-hidpi-support.png)

### Lock screen

![XP lock screen at 1024×768](docs/screenshots/07-lockscreen-1024x768.png)

### Boot screen

![XP boot-screen preview at 1024×768](docs/screenshots/08-bootscreen-1024x768.png)

## Thank you to the projects that made this possible

XPlasma Plus exists because other people shared their work. Thank you to the creators, maintainers, artists, translators, testers and contributors behind these projects. Please visit their projects, support their work and give them credit when sharing this pack.

### Desktop, browser, artwork and inspiration

| People / project | What we are thanking them for |
| --- | --- |
| **hotsocket (Sock) — [XPlasma](https://tangled.org/hotsocket.fyi/xplasma/)** | The upstream project we forked: the native XP visual-style engine, Qt application style, Properties editor and desktop generators. This is the foundation of XPlasma Plus. |
| **xp2plasma — [preserved project documentation](https://tangled.org/hotsocket.fyi/xplasma/tree/trunk/docs/xp2plasma)** | XPlasma’s predecessor. Upstream imported its earlier desktop integration work and later rewrote the application around the native C++/Qt engine; this is part of the lineage inherited by XPlasma Plus. See the [upstream import record](https://tangled.org/hotsocket.fyi/xplasma/commit/2c6fa557666accfc31a21c88df06bb7417ed464b). |
| **Rozniak and contributors — [Windows XP Total Conversion for XFCE](https://github.com/rozniak/xfce-winxp-tc)** | The main inspiration for this project. Please check out their work, especially if you use XFCE. This is an inspiration credit, not a claim that their code or assets are bundled here. |
| **matthewmx86 — [RetroThemesFirefox](https://github.com/matthewmx86/RetroThemesFirefox)** | The foundation and legacy chrome assets for our optional IE6-style Firefox theme. |
| **marchmountain — [Windows XP High Resolution Icon Pack](https://github.com/marchmountain/-Windows-XP-High-Resolution-Icon-Pack)** | The high-resolution XP icon recreations used by our HiDPI option. |
| **WindowsAesthetics — [Windows XP HD Wallpaper Pack](https://www.deviantart.com/windowsaesthetics/art/Windows-XP-HD-Wallpaper-Pack-776806652)** | Finding and collecting the high-resolution XP wallpapers used by this pack. This collection credit is separate from the original photographers’ authorship and asset rights. |
| **KDE contributors, including Aleix Pol Gonzalez and David Edmundson — [Plasma Workspace](https://invent.kde.org/plasma/plasma-workspace), [Plasma Desktop](https://invent.kde.org/plasma/plasma-desktop), [KWin](https://invent.kde.org/plasma/kwin) and [KDE Frameworks](https://develop.kde.org/products/frameworks/)** | The desktop, compositor, widgets, session/logout and lock-screen foundations, libraries and integration tools that make the pack possible. |
| **Uri Herrera and KDE Breeze contributors — [Breeze Icons](https://invent.kde.org/frameworks/breeze-icons)** | The original geometry behind specialized XP-styled replacement glyphs. Their original artwork notices and licenses are retained. |
| **lou — [MS Sans Serif](https://fontstruct.com/fontstructions/show/1384746); Adam Hammad and Jordan Scales — [XP.css](https://github.com/botoxparty/XP.css) and [98.css](https://github.com/jdan/98.css)** | The optional pixel fonts and the projects through which those font files were distributed. |
| **Larry Ewing and The GIMP; Simon Budig; Garrett LeSage — [original Tux](https://isc.tamu.edu/~lewing/linux/), [GIMP](https://www.gimp.org/), [Simon’s vector artwork](https://www.home.unix-ag.org/simon/penguin/README) and [Garrett’s Tux](https://github.com/garrett/Tux)** | Tux and its vector adaptations, used for the optional Start logo. |
| **aubymori — [Classic Min/Max Animations](https://windhawk.net/mods/classic-min-max-animations)** | The behavioral reference for our independently implemented KWin caption animation. No Windhawk implementation code was copied. |
| **Mozilla and contributors — [Firefox](https://www.mozilla.org/firefox/)** | The modern Firefox ESR browser beneath the optional IE6 appearance. |
| **[Plymouth](https://gitlab.freedesktop.org/plymouth/plymouth) and [SDDM](https://github.com/sddm/sddm) contributors** | The boot-splash and login-manager software on which our themes run. |

### Libraries, tools and the Linux foundation

- **The Qt Company and Qt contributors; Riverbank Computing** — [Qt](https://www.qt.io/), [Qt for Python / PySide](https://doc.qt.io/qtforpython/) and [PyQt](https://www.riverbankcomputing.com/software/pyqt/) provide the native interface, QML and Python GUI foundations.
- **The Python Software Foundation and Python contributors** — [Python](https://www.python.org/) powers the installer and integration tools.
- **Tim Kientzle and libarchive contributors; Julian R. Seward; Sébastien Eustace and TOMLKit contributors** — [libarchive](https://www.libarchive.org/), [bzip2](https://sourceware.org/bzip2/) and [TOMLKit](https://github.com/python-poetry/tomlkit) provide archive reading, compression and configuration support.
- **Pillow, fontTools, freetype-py and FreeType contributors** — [Pillow](https://python-pillow.github.io/), [fontTools](https://github.com/fonttools/fonttools), [freetype-py](https://github.com/rougier/freetype-py) and [FreeType](https://freetype.org/) support image processing, font generation and font verification.
- **zlib, zlib-ng and Tukaani contributors** — [zlib](https://zlib.net/), [zlib-ng](https://github.com/zlib-ng/zlib-ng) and [XZ Utils / liblzma](https://tukaani.org/xz/) provide compression libraries used by the build and archive stack.
- **GNOME, GTK and Fontconfig contributors** — [GTK](https://www.gtk.org/), [GLib / GSettings](https://gitlab.gnome.org/GNOME/glib) and [Fontconfig](https://www.freedesktop.org/wiki/Software/fontconfig/) support application styling, settings and font selection.
- **The Linux kernel, Asahi Linux and distribution communities** — [Linux](https://www.kernel.org/), [Asahi Linux](https://asahilinux.org/), [Kubuntu](https://kubuntu.org/), [Ubuntu](https://ubuntu.com/), [Fedora](https://fedoraproject.org/), [Arch Linux](https://archlinux.org/) and [openSUSE](https://www.opensuse.org/) provide the platforms, packages and build environments used or targeted by the pack. Inclusion here does not mean every platform has been tested.
- **The wider Linux desktop and graphics communities** — [Wayland](https://wayland.freedesktop.org/), [X.Org](https://www.x.org/), [Mesa](https://www.mesa3d.org/), [D-Bus](https://www.freedesktop.org/wiki/Software/dbus/), [systemd](https://systemd.io/) and [polkit](https://gitlab.freedesktop.org/polkit/polkit) provide essential display, session and authorization infrastructure.
- **Build-tool maintainers and contributors** — [CMake](https://cmake.org/), [Ninja](https://ninja-build.org/), [Extra CMake Modules](https://invent.kde.org/frameworks/extra-cmake-modules), [pkgconf](https://github.com/pkgconf/pkgconf), [GCC](https://gcc.gnu.org/), [LLVM / Clang](https://llvm.org/) and [Git](https://git-scm.com/) make building, testing and maintaining this work possible.

These thanks accompany the **[full credits and licenses](CREDITS.md)**, [native notices](native/THIRD-PARTY-NOTICES) and [installer notices](installer/THIRD-PARTY-NOTICES); they do not replace them or imply endorsement by the original authors. The dependency stack contains additional contributors acknowledged in each project's own notices.

Original Microsoft XP assets and the separately credited wallpaper photography are not open-source project code. Their rights and attributions remain separate; see [asset distribution and provenance](ASSET-DISTRIBUTION.md). Thank you also to **[Gough Lui](https://www.youtube.com/watch?v=vTheKjAtnKs&t=134s)** for the visual reference used for the installer.

## Licence

The source code in this repository — essentially any *text* files, such as C++, QML, Python, JavaScript and shell scripts — is licensed under the GPL; see [LICENSE](LICENSE). Reused components keep their own notices and licenses: read [CREDITS.md](CREDITS.md), [native/THIRD-PARTY-NOTICES](native/THIRD-PARTY-NOTICES) and [installer/THIRD-PARTY-NOTICES](installer/THIRD-PARTY-NOTICES). This modified project is not endorsed by upstream developers or Microsoft.

This licence does **not** cover the assets from Windows XP (images, icons, cursors, sounds, fonts and visual styles): those are still Microsoft's copyright. They are in this repository for convenience, all in one folder, [`non-free/`](non-free/NOTICE.md), and the installer marks the components that use them as non-free. Windows program files and installers, third-party stock photographs and the high-resolution photo rescans are not in the repository. See [ASSET-DISTRIBUTION.md](ASSET-DISTRIBUTION.md).

## Builds

The complete installer is provided separately as an alpha prerelease asset, with its resources' copyright notices; this source checkout alone is not that full installer.

Build Desktop with `cmake -S native -B build/desktop -G Ninja -DQT_MAJOR=6` and `cmake --build build/desktop`. Native components must match the receiving machine's KDE/Qt. The optional browser sources are under `browser/ie6`. Source includes the caption effect and original third-party notices.

## New releases

Prereleases use complete installer downloads. OTA updates are not included and
there is no background update client. **Download New Releases** in Properties
opens the GitHub releases page in your browser. Signed checksums are provided
for verifying each installer download.

Read the new release's installation and migration notes before changing an
existing installation. Choose **Upgrade installation** to keep the original
rollback backup, selected features, theme and toolbar preferences.

## Download the installer

See [1.0.1 Public Beta](https://github.com/andymenard/XPlasma-Plus/releases/tag/v1.0.1).
Extract the installer tar.gz, then launch **Start-Installer.desktop** from the
extracted folder as your desktop user. Linux dependencies require internet
access; an XP ISO is not needed. Desktop is selected by default; Firefox is optional.
This is a public beta: please report problems on the repository's issue tracker.
Compatibility notes are in the release notes. Signed checksums authenticate the
installer download; this release does not activate an OTA payload.

## Installer variants

| Download | Prebuilt target |
| --- | --- |
| [Kubuntu x86-64](https://github.com/andymenard/XPlasma-Plus/releases/download/v1.0.1/xplasma-plus-1.0.1-installer-kubuntu-26.04-amd64.tar.gz) | Kubuntu/Ubuntu 26.04, Intel/AMD 64-bit |
| [Kubuntu ARM64](https://github.com/andymenard/XPlasma-Plus/releases/download/v1.0.1/xplasma-plus-1.0.1-installer-kubuntu-26.04-arm64.tar.gz) | Kubuntu/Ubuntu 26.04, ARM64 |
| [Fedora x86-64](https://github.com/andymenard/XPlasma-Plus/releases/download/v1.0.1/xplasma-plus-1.0.1-installer-fedora-44-amd64.tar.gz) | Fedora 44, Intel/AMD 64-bit |
| [Fedora ARM64](https://github.com/andymenard/XPlasma-Plus/releases/download/v1.0.1/xplasma-plus-1.0.1-installer-fedora-44-arm64.tar.gz) | Fedora 44 and matching Fedora Asahi Remix 44, ARM64 |
| [Generic](https://github.com/andymenard/XPlasma-Plus/releases/download/v1.0.1/xplasma-plus-1.0.1-installer-generic.tar.gz) | Builds locally on supported KDE systems |

Prebuilt components are used only when the distribution, architecture, Qt,
Plasma and source identity match; otherwise the installer builds locally.
The caption effect additionally checks the exact KWin build. All variants
include the same desktop features, optional Firefox theme and source fallback.
The generic installer supports APT, DNF, pacman and Zypper families; the bundled
native Plasma integrations currently target Plasma 6.6/Qt 6.10 and
Plasma 6.7/Qt 6.11 or newer Qt within the supported Plasma series. Other versions
may have unavailable components. Dependency installation needs internet access.

The source branch is extending the actual XP widget ports to Plasma 6.0–6.8.
It keeps the XP Start menu, task buttons, clock, tray and notifications required;
there is no stock-widget compatibility fallback. See [compatibility status](docs/wiki/Compatibility.md)
for the port matrix and remaining compilation/live-testing limits. These changes
are not yet included in the published 1.0.1 archives.

## Boot splash troubleshooting

If the boot splash stays black or flickers, see [Boot splash troubleshooting](BOOT-SPLASH-TROUBLESHOOTING.md) for the optional Intel early-driver workaround confirmed on a docked laptop. This is a manual troubleshooting step; the installer does not apply it automatically.

## Interface fonts

The installer includes **Tahoma OS** regular and bold, renamed from Microsoft
Tahoma to separate XP interface rendering from smooth Tahoma app content. The
font assets retain their separate Microsoft rights; see
[font provenance and notices](installer/assets/fonts/tahoma-os/NOTICE.txt).
