# XPlasma Plus 1.0.2 Public Beta

This update fixes installation and visual issues and expands the actual XP desktop widgets across Plasma 6.0–6.8.

- Keep the XP Start menu, task buttons, clock, tray and notifications mandatory. An unsupported layout or failed widget build stops installation and reports the failure.
- Add a copyable installer failure report with complete output, system details and recovery messages. A private copy is saved at `~/.local/state/xplasma-plus/last-failure.log`.
- Fix desktop icon shadows, title shadows on dark captions and the empty IE6 Firefox bookmark bar after Firefox initializes Places.
- Refresh APT package indexes before resolving dependencies, report missing candidates and use system Python for privileged installation.
- Install Quick Launch and notification-volume dependencies even when using a prebuilt runtime.
- Fix the Start menu's Run Command action on Plasma 6.0, the old tray graphics import and recursive notification cleanup. Fix notification popup geometry loops on Plasma 6.7 and 6.8.
- Add experimental Void/XBPS dependency support and a non-systemd theme/cursor watcher. Automatic display-manager management still requires systemd.

## Downloads

Choose the generic installer or the matching Kubuntu/Ubuntu 26.04 or Fedora 44 installer for x86-64 (`amd64`) or ARM64. A verified matching runtime speeds installation. If the distribution, Qt, Plasma, KWin or source identity differs, supported components build locally. The Fedora ARM64 archive also includes the Fedora-built runtime validated against the matching Asahi Remix 44 libraries, native CLI and exact KWin caption ABI. No GitHub account is needed for installation.

## Upgrade

Back up your configuration and save your work. Extract the complete new archive and run `Start-Installer.desktop` or `install.sh` as your desktop user. Installing over an existing installation uses the in-place upgrade path and retains the original pre-XP settings backup. Follow any reported recovery instructions if a step fails.

## Validation and limits

Live desktop acceptance passed on Plasma 6.0.4, 6.1.2, 6.2.1, Debian 6.3.6, 6.4.3, 6.5.5, 6.6.4, 6.7.5 and 6.8.1. It exercises apply/reapply, a visible task button restoring a minimized window, Start and All Programs, Run Command, calendar, real notifications, Classic switching and reset. The pinned source matrix checks 108 files and parses 99 QML files. Qt 6.6.3 builds and the native regressions also passed.

These are disposable desktop and build tests, not certification of every distribution, full login/hardware configuration or system-wide installer lifecycle. Void remains experimental. The previously unidentified failure in issue #30 still needs a retry with the new diagnostic report.

See `RELEASE-VALIDATION.json` alongside the archives for the actual package preparation and test results. Original artwork, third-party notices and credits retain their existing licenses. No OTA update feed is included.

## Verify your download

Download `SHA256SUMS`, `SHA256SUMS.sig` and `xplasma-release-key.pem` beside your chosen installer:

```sh
openssl pkeyutl -verify -pubin -inkey xplasma-release-key.pem -rawin -in SHA256SUMS -sigfile SHA256SUMS.sig
sha256sum --ignore-missing -c SHA256SUMS
```
