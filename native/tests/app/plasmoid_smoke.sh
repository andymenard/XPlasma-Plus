#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The Start plasmoid, loaded by Plasma for real (plasmawindowed, offscreen,
# in a scratch home) with a look's theme.json, both as the Start button and
# as the menu: requires each representation to instantiate and stay alive;
# any QML error, type error or binding loop fails it.
#
#   tests/app/plasmoid_smoke.sh THEME.json [SECONDS]
#
# Needs the host's Plasma (the KDE QML modules), so it runs outside the
# build containers. (plasmawindowed starts applets with fresh settings, so
# the theme goes in through a data file in the scratch copy.)
set -eu
theme="$1"; seconds="${2:-8}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
status=0
for representation in compactRepresentation fullRepresentation; do
    pkg="$work/$representation/.local/share/plasma/plasmoids/fyi.hotsocket.xplasma.start"
    mkdir -p "$(dirname "$pkg")"
    cp -r "$root/src/app/start-menu" "$pkg"
    { echo ".pragma library"; printf "var data = "; cat "$theme"; echo ";"; } > "$pkg/contents/ui/SmokeTheme.js"
    sed -i -e 's|^import org.kde.kirigami as Kirigami$|&\nimport "SmokeTheme.js" as SmokeTheme|' \
           -e 's|JSON.parse(Plasmoid.configuration.theme \|\| "{}")|SmokeTheme.data|' \
           -e "s|preferredRepresentation: compactRepresentation|preferredRepresentation: $representation|" \
           -e 's|StartButton { launcher: start }|StartButton { launcher: start; Component.onCompleted: console.warn("XPLASMA_SMOKE_READY compactRepresentation") }|' \
           -e 's|XpFullRepresentation { launcher: start; focus: true }|XpFullRepresentation { launcher: start; focus: true; Component.onCompleted: console.warn("XPLASMA_SMOKE_READY fullRepresentation") }|' \
           "$pkg/contents/ui/main.qml"
    home="$work/$representation"
    result=0
    HOME="$home" XDG_CONFIG_HOME="$home/.config" XDG_DATA_HOME="$home/.local/share" QT_QPA_PLATFORM=offscreen \
        QT_FORCE_STDERR_LOGGING=1 timeout "$seconds" plasmawindowed fyi.hotsocket.xplasma.start > "$work/log" 2>&1 || result=$?
    if [ "$result" -ne 124 ] || ! grep -Fq "XPLASMA_SMOKE_READY $representation" "$work/log" ||
       grep -Eq "\.qml:[0-9]+|TypeError|ReferenceError|Binding loop|is not installed|module .* not" "$work/log"; then
        cat "$work/log"
        echo "FAIL $representation (exit $result; expected a loaded widget alive until timeout)"; status=1
    else
        echo "ok   $representation"
    fi
done
exit $status
