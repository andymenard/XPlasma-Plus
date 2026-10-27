# XPlasma Plus 1.0.3 Public Beta

This update fixes Firefox dependency installation on Ubuntu 26.04 and shows why a feature can't be selected.

- Install `libxt6t64` for IE6 Firefox on Ubuntu and Kubuntu releases after the 64-bit time transition, such as 26.04. Earlier releases still get `libxt6`. (#35)
- Unavailable features are greyed out and labelled "(unavailable)". Select one to see why under the feature list. They are skipped when you tick their group or use **Select all**. (#36)
- The XP boot splash now says which requirement is missing: neither `dracut` nor `update-initramfs` was found, or the kernel command line has no `splash` or `rhgb` option. If you reported #34, please select XP boot splash and tell us what it says.

The native components and prebuilt runtimes are unchanged from 1.0.2. Only the installer's Python files and version metadata differ.

## Downloads

Choose the generic installer or the matching Kubuntu/Ubuntu 26.04 or Fedora 44 installer for x86-64 (`amd64`) or ARM64. A verified matching runtime speeds installation. If the distribution, Qt, Plasma, KWin or source identity differs, supported components build locally. The Fedora ARM64 archive also includes the Fedora-built runtime validated against the matching Asahi Remix 44 libraries. No GitHub account is needed for installation.

## Upgrade

Back up your configuration and save your work. Extract the complete new archive and run `Start-Installer.desktop` or `install.sh` as your desktop user. Installing over an existing installation uses the in-place upgrade path and retains the original pre-XP settings backup.

## Verify your download

Download `SHA256SUMS`, `SHA256SUMS.sig` and `xplasma-release-key.pem` beside your chosen installer:

```sh
openssl pkeyutl -verify -pubin -inkey xplasma-release-key.pem -rawin -in SHA256SUMS -sigfile SHA256SUMS.sig
sha256sum --ignore-missing -c SHA256SUMS
```

The signing key is the same one used for 1.0.2. Original artwork, third-party notices and credits retain their existing licenses. No OTA update feed is included.
