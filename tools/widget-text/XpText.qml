// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
Text {
    id: label
    property string xpFamily: "Tahoma"
    property real xpPixels: 11
    property real xpPoints: -1
    property bool xpBold: false
    property int xpWeight: Font.Normal
    property bool xpItalic: false
    // One settings watcher belongs to the containing widget, not each label.
    property var xpStyle: {
        let item = parent;
        while (item) {
            if (item.xpTextStyle !== undefined) return item.xpTextStyle;
            item = item.parent;
        }
        return null;
    }
    property real xpDpr: Math.max(1, Screen.devicePixelRatio)
    readonly property real xpBasePixels: xpPoints > 0 ? xpPoints * 96 / 72 : xpPixels
    readonly property bool xpActive: !!xpStyle && xpStyle.pixelActive(xpFamily, xpBasePixels, xpDpr) && !xpItalic
    font.family: xpItalic && xpStyle && xpStyle.crispEnabled && xpStyle.supports(xpFamily) ? "Tahoma"
                 : xpStyle ? xpStyle.family(xpFamily, xpBasePixels, xpDpr) : xpFamily
    font.pointSize: xpActive ? xpStyle.physical(xpBasePixels, xpDpr) / xpDpr * 72 / 96
                            : (xpPoints > 0 ? xpPoints : xpPixels * 72 / 96)
    font.weight: xpBold ? Font.Bold : xpWeight
    font.italic: xpItalic
    font.kerning: !xpActive
    font.hintingPreference: xpActive ? Font.PreferFullHinting : Font.PreferDefaultHinting
    antialiasing: !xpActive
    renderType: Text.NativeRendering
}
