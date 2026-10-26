# Compatibility and downloads

## Check before installing

1. Back up your personal files and desktop/system configuration. Save your work.
2. Open **System Settings → About this System** (the location/name can vary), or run `plasmashell --version` in a terminal.
3. Check your distribution release and architecture. `uname -m` reports `x86_64` for Intel/AMD 64-bit or `aarch64` for ARM64.
4. Choose an installer from the [1.0.1 release](https://github.com/andymenard/XPlasma-Plus/releases/tag/v1.0.1).

## Available installers

| Filename ending | Intended matching prebuilt target |
| --- | --- |
| `kubuntu-26.04-amd64.tar.gz` | Kubuntu/Ubuntu 26.04, Intel/AMD 64-bit |
| `kubuntu-26.04-arm64.tar.gz` | Kubuntu/Ubuntu 26.04, ARM64 |
| `fedora-44-amd64.tar.gz` | Fedora 44, Intel/AMD 64-bit |
| `fedora-44-arm64.tar.gz` | Fedora 44 ARM64; also includes a matching Fedora Asahi Remix build |
| `generic.tar.gz` | Builds locally on supported KDE systems |

**Build availability is not hardware testing.** Only Asahi Linux ARM64 and Kubuntu Intel/AMD have received hands-on testing. Four native CI targets passed automated build/regression checks.

Prebuilt components are accepted only when distribution, release, architecture, Qt, Plasma and source identity match. The caption effect also checks the exact KWin build. Otherwise supported components build locally, which takes longer. Internet access is needed for Linux dependencies, including when using a prebuilt installer. Original XP resources are bundled; an XP ISO is not needed.

## Plasma and Qt versions

The generic source installer keeps the XP Start menu, task buttons, clock, tray and notification UI. It does not substitute stock widgets when an XP port or plugin cannot be installed.

Plasma **6.0–6.4** ports patch the distribution's packaged QML to implement the XP behavior. Plasma **6.5** needs compiled XP notification and panel replacements as well as packaged task-manager and tray patches. Plasma **6.6–6.8** use the matching compiled widget recipes. The installer checks Qt and the running Plasma series, builds the needed XP components, and verifies that the XP Start button, task buttons and clock load. Missing components fail installation and trigger recovery.

The port matrix pins upstream source revisions for all existing Plasma 6 branches (6.0–6.8) and checks the production patcher against each branch's tray, task manager, panel, notifications and desktop QML. Source-patch and syntax checks are distinct from compilation and live desktop testing. Support for a future Plasma 6 branch must be added and checked before the XP taskbar is enabled there. Optional logout, lock and caption-effect integrations retain their separate compatibility checks.

The current work is in progress: runtime testing across every distribution/branch is not complete. Published 1.0.1 installers do not include these source changes until rebuilt.

APT, DNF, pacman, Zypper and Void Linux XBPS dependency adapters are included in the source installer. Void support is experimental and has not received a live installation test. Non-systemd sessions use an XDG autostart watcher for theme and cursor changes. Automatic login-screen/display-manager management still requires systemd; configure your non-systemd display manager separately. Boot splash needs an existing splash-enabled Plymouth setup using dracut or update-initramfs. Unsupported features are disabled with an explanation in Setup.

If APT dependency installation fails, copy the output before the final `CalledProcessError`. The current installer refreshes package indexes before selecting library names and reports packages without installation candidates explicitly. A final pkexec exit code alone cannot distinguish authentication, repository, network or dependency errors.

Continue to [Installation](Installation.md).
