# XPlasma Plus installer

The public edition includes Desktop by default and optional IE6 Firefox.
OTA updates are excluded. XP applications, custom tray menus, Codex colors and
Clippy are excluded. See the root README and CREDITS for scope and attribution.

This source checkout alone is not the complete asset-bearing installer. Local
test bundles include user-supplied resources; see ASSET-DISTRIBUTION.md before
redistributing them. In a complete local bundle, launch Start-Installer.desktop
or install.sh as the desktop user. The GUI permits individual feature selection,
icon/wallpaper resolution choices and dark application colors. It never logs out
or reboots automatically. System components and dependencies request authentication.

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

## XP widget compatibility and failure reports

Source builds retain the XP Start menu, task buttons, clock, tray and notification
UI. They patch packaged QML on older Plasma branches and build the matching XP
plugins on newer branches. Unknown layouts or missing XP components stop the
operation and trigger recovery; stock widgets are not substituted.

After a failed operation, use **Copy report** in the failure dialog and paste the
report into a new or existing GitHub issue. It includes system details, installer
output and recovery messages. A private copy is saved at
`~/.local/state/xplasma-plus/last-failure.log`.
