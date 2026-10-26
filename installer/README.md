# Alpha installer

The public edition includes Desktop by default and optional IE6 Firefox.
OTA updates are excluded from prereleases. XP applications, custom tray menus, Codex colors and
Clippy are excluded. See the root README and CREDITS for scope and attribution.

This source checkout alone is not the complete asset-bearing installer. Local
test bundles include user-supplied resources; see ASSET-DISTRIBUTION.md before
redistributing them. In a complete local bundle, launch Start-Installer.desktop
or install.sh as the desktop user. The GUI permits individual feature selection,
icon/wallpaper resolution choices and dark application colors. It never logs out
or reboots automatically. System components and dependencies request authentication.

Failed operations show a copyable report with system details, captured output,
recovery messages and a GitHub issues link. A private copy is retained at
`~/.local/state/xplasma-plus/last-failure.log`.

The XP taskbar is required when selected. Packaged-QML ports and compiled plugin
recipes retain the XP widgets; missing or incompatible components fail and roll
back rather than substituting stock widgets. See `docs/wiki/Compatibility.md`
in the source repository for the branch matrix and testing status.

Native components build against the receiving machine's KDE/Qt. Desktop features
target Plasma 6 on supported Fedora, Debian/Ubuntu, Arch and openSUSE families.
Unknown distributions can use resource-only features when tools already exist.
Allow 5 GB for native builds or 2 GB for resource-only installation. Firefox uses a
separate ESR profile; existing browsing data and toolbar preferences are preserved.

Login uses SDDM on systemd. Boot requires existing splash-enabled Plymouth with
dracut or update-initramfs; mkinitcpio-only boot configurations are not supported.
The Welcome splash is separate from login and boot screens and appears as Plasma
starts. Remove an older overlapping installer before switching installation managers.

New versions are distributed as complete installers on GitHub. Properties links
to that releases page; no OTA updater or updater feed is installed. To upgrade,
run the new installer and choose Upgrade installation: it installs over the
existing installation and keeps your current theme, dark mode and Quick Launch.
Installed features stay selected (new ones can be added); to drop one, remove the
installation first. Remove still restores the settings from before the first
installation, and a failed upgrade returns to the release it was upgrading.

Remove also deletes the XP looks and caches made since installation (by
Properties too), keeping saved and imported themes, custom colour styles and the
IE6 browser profile. Settings backups stay in ~/.local/share/kde-xp-suite unless
"Also delete the settings backups" is checked. IE6 Firefox can be made the
default web browser; Remove restores the previous one.

Firefox removal also cleans hidden Firefox-generated launchers that point into
the dedicated IE6 runtime, obsolete panel/favourite references and browser
associations. It restores a valid previous browser association when available,
otherwise using an installed regular browser. Unrelated browser launchers are
preserved. Plasma saves its live panel before restoration, and its application
cache is refreshed with the graphical session's Snap/Flatpak search paths.

Properties asks whether to cache generated theme artwork the first time a theme
is applied. Caching is optional and can be changed under Appearance. With it
off, unused generated looks are discarded while active resources and recovery
data remain protected. With it on, the existing disk/count limits still apply.
The Custom Colors tab notes that only Luna has been extensively tested; other
base themes are experimental.
