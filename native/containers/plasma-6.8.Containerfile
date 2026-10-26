# SPDX-License-Identifier: GPL-3.0-or-later
# Build image for XPlasma's forks of Plasma's compiled widgets for the
# Plasma 6.8 series, whose libPlasma is .so.8 (see plasma-6.6.Containerfile
# for the why of it all).
#
# Plasma 6.8 is built from the stable Arch repositories, against Qt 6.11+
# and the corresponding Plasma 6.8 development libraries.
#
#   podman build -t xplasma-plasma:6.8 -f containers/plasma-6.8.Containerfile containers
FROM docker.io/library/archlinux:latest
RUN pacman -Syu --noconfirm --needed base-devel cmake ninja ccache git xz python extra-cmake-modules kdoctools vulkan-headers \
      plasma-workspace plasma-desktop plasma-wayland-protocols wayland-protocols qt6-tools qt6-declarative qt6-wayland \
      qt6-5compat qt6-svg kunitconversion krunner kholidays ktexteditor kuserfeedback appstream-qt phonon-qt6 \
      libqalculate networkmanager-qt prison kpipewire kscreenlocker libkexiv2 baloo kaccounts-integration \
      packagekit-qt6 libxkbfile xorg-server-devel libxcursor libxtst sdl2 xf86-input-libinput xf86-input-evdev \
      xdotool kactivitymanagerd plasma-activities-stats intltool \
 && pacman -Scc --noconfirm \
 && pacman -Q plasma-workspace qt6-base
ENV CCACHE_DIR=/ccache
