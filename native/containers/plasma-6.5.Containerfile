# SPDX-License-Identifier: GPL-3.0-or-later
# Frozen Plasma 6.5 SDK; current mirrors have moved to newer Plasma ABIs.
FROM docker.io/library/archlinux:base-20260125.0.484595
RUN printf 'Server = https://archive.archlinux.org/repos/2026/02/01/$repo/os/$arch\n' > /etc/pacman.d/mirrorlist \
 && pacman -Syyuu --noconfirm --needed base-devel cmake ninja ccache git xz python extra-cmake-modules kdoctools vulkan-headers \
      plasma-workspace plasma-desktop plasma-wayland-protocols wayland-protocols qt6-tools qt6-declarative qt6-wayland \
      qt6-5compat qt6-svg kunitconversion krunner kholidays ktexteditor kuserfeedback appstream-qt phonon-qt6 \
      libqalculate networkmanager-qt prison kpipewire kscreenlocker libkexiv2 baloo kaccounts-integration \
      packagekit-qt6 libxkbfile xorg-server-devel libxcursor libxtst sdl2 xf86-input-libinput xf86-input-evdev \
      xdotool kactivitymanagerd plasma-activities-stats intltool \
 && pacman -Scc --noconfirm \
 && pacman -Q plasma-workspace qt6-base
ENV CCACHE_DIR=/ccache
