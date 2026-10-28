// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtCore
import Qt.labs.folderlistmodel
import org.kde.plasma.plasma5support as P5Support
Item {
    id: style
    visible: false
    width: 0; height: 0
    property bool crispEnabled: false
    property string settingsDirectory: decodeURIComponent(String(StandardPaths.writableLocation(StandardPaths.GenericDataLocation)).replace(/^file:\/\//, "")) + "/xplasma"
    readonly property var families: Qt.fontFamilies()
    function supports(family) {
        return families.indexOf("XPlasma Tahoma Pixel") >= 0
            && (family === "Tahoma" || family === "Tahoma OS" || family === "MS Sans Serif" || family.indexOf("XPlasma Tahoma Pixel") === 0);
    }
    function standardScale(dpr) {
        return isFinite(dpr) && dpr >= 1 && dpr <= 3
            && Math.abs(dpr * 4 - Math.round(dpr * 4)) < 0.0001;
    }
    function physical(pixels, dpr) { return Math.max(1, Math.round(Math.max(1, Math.round(pixels)) * dpr)); }
    function pixelFamily(pixels, dpr) {
        const size = physical(pixels, dpr);
        return size === 11 ? "XPlasma Tahoma Pixel" : "XPlasma Tahoma Pixel " + size;
    }
    function pixelActive(original, pixels, dpr) {
        return crispEnabled && supports(original) && standardScale(dpr)
            && families.indexOf(pixelFamily(pixels, dpr)) >= 0;
    }
    function family(original, pixels, dpr) {
        if (pixelActive(original, pixels, dpr)) return pixelFamily(pixels, dpr);
        if (crispEnabled && supports(original)) return "Tahoma";
        return original === "Tahoma" && families.indexOf("Tahoma OS") >= 0 ? "Tahoma OS" : original;
    }
    // Share selection with labels, input fields and text areas.
    function font(original, points, dpr) {
        const pixels = points * 96 / 72;
        const active = pixelActive(original, pixels, dpr);
        return Qt.font({family: family(original, pixels, dpr),
                        pointSize: active ? physical(pixels, dpr) / dpr * 72 / 96 : points,
                        kerning: !active,
                        hintingPreference: crispEnabled && supports(original) ? Font.PreferFullHinting : Font.PreferDefaultHinting});
    }
    function read() { debounce.restart(); }
    Component.onCompleted: read()
    onSettingsDirectoryChanged: read()
    // Atomic replacement of appearance.json emits model changes. No polling.
    FolderListModel {
        id: files
        folder: "file://" + style.settingsDirectory
        nameFilters: ["appearance.json"]
        showDirs: false
        onCountChanged: style.read()
        onStatusChanged: if (status === FolderListModel.Ready) style.read()
    }
    Connections {
        target: files
        function onDataChanged() { style.read(); }
        function onModelReset() { style.read(); }
    }
    Timer {
        id: debounce
        interval: 30
        onTriggered: {
            const path = style.settingsDirectory + "/appearance.json";
            // Quote the user data path as one shell argument; it is never code.
            const command = "cat -- '" + path.replace(/'/g, "'\\''") + "'";
            if (reader.connectedSources.length) { restart(); return; }
            reader.connectSource(command);
        }
    }
    P5Support.DataSource {
        id: reader
        engine: "executable"
        onNewData: (source, data) => {
            disconnectSource(source);
            try { style.crispEnabled = JSON.parse(String(data.stdout || "{}")).crispText === true; }
            catch (error) { style.crispEnabled = false; }
        }
    }
}
