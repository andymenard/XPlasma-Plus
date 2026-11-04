// SPDX-License-Identifier: GPL-3.0-or-later
// XPlasma: where notifications' balloons come from, XP-style. An app with
// its own icon in the tray gets its balloon from that; the rest get an icon
// here while their balloon waits its turn or shows, left blinking if it
// timed out unread (steady while it shows again), and a click shows it
// again. (The queue is the notifications applet's; this only draws it.)
import QtQuick

import org.kde.kirigami as Kirigami
import org.kde.plasma.plasmoid

Grid {
    id: icons
    required property var trayModel    // the tray's shown items...
    required property Item trayView     // ...and their view
    property bool vertical: false
    property int thickness: 0           // the panel's

    property Item notifications: null
    readonly property var queue: notifications && notifications.xpQueue ? notifications.xpQueue : []
    readonly property int shownId: notifications ? notifications.xpCurrentId : -1
    columns: vertical ? 1 : Math.max(1, drawn.count)
    spacing: 2
    visible: drawn.count > 0

    // Finds the notifications applet among the tray's.
    Instantiator {
        model: Plasmoid.systemTrayModel
        delegate: QtObject {
            required property QtObject applet
            required property string itemId
        }
        onObjectAdded: (index, object) => {
            if (object.itemId === "org.kde.plasma.notifications" && object.applet)
                icons.notifications = object.applet;
        }
        onObjectRemoved: (index, object) => {
            if (object.applet === icons.notifications)
                icons.notifications = null;
        }
    }

    // An app's own icon in the tray, if it has one: by its name or its
    // desktop file against the item's id or title.
    property int trayRevision: 0
    Connections {
        target: icons.trayModel
        function onRowsInserted() { icons.trayRevision++; }
        function onRowsRemoved() { icons.trayRevision++; }
        function onModelReset() { icons.trayRevision++; }
        function onLayoutChanged() { icons.trayRevision++; }
    }
    function simple(s) {
        return (s || "").toString().toLowerCase().replace(/[^a-z0-9]/g, "");
    }
    function ownIcon(entry) {
        if (trayRevision < 0 || !trayModel)
            return null;
        const name = simple(entry.applicationName);
        const desktop = simple(entry.desktopEntry);
        for (let row = 0; row < trayModel.rowCount(); ++row) {
            const index = trayModel.index(row, 0);
            // (an Electron app's item is "<name>_status_icon_<n>": the app's name)
            const id = simple(trayModel.data(index, Qt.UserRole + 2)).replace(/statusicon\d*$/, "");
            const title = simple(trayModel.data(index, Qt.DisplayRole));
            if (id === "orgkdeplasmanotifications")
                continue;
            if ((name.length > 0 && (name === id || name === title))
                    || (desktop.length > 0 && (desktop === id || (id.length >= 4 && desktop.endsWith(id)))))
                return trayView.itemAtIndex(row);
        }
        return null;
    }
    readonly property var owners: queue.map(entry => ownIcon(entry))
    onOwnersChanged: {
        for (let i = 0; i < queue.length; ++i)
            if (owners[i])
                notifications.xpRegisterIcon(queue[i].id, owners[i]);
    }

    Repeater {
        id: drawn
        // (newest first: XP adds tray icons on the left, so the ones
        // already there -- and a balloon pointing at one -- stay put)
        model: icons.queue.filter((entry, i) => !icons.owners[i]).reverse()
        Item {
            id: cell
            required property var modelData
            width: icons.vertical ? icons.thickness : 16
            height: icons.vertical ? 16 : icons.thickness
            // Unread, it blinks -- each icon to its own beat, like cars'
            // indicators at a light: a rate of its own, a little uneven
            // flash to flash, drifting in and out of step with the others.
            readonly property bool blinking: modelData.unread && modelData.id !== icons.shownId
            readonly property int beat: 420 + Math.floor(Math.random() * 180)
            property bool lit: true
            Timer {
                running: cell.blinking
                repeat: true
                interval: cell.beat
                onTriggered: {
                    cell.lit = !cell.lit;
                    interval = cell.beat + Math.floor(Math.random() * 80) - 40;
                }
                onRunningChanged: {
                    cell.lit = true;
                    interval = Math.floor(Math.random() * cell.beat) + 1;   // (from anywhere in its beat)
                }
            }
            Kirigami.Icon {
                id: icon
                anchors.centerIn: parent
                width: 16
                height: 16
                source: cell.modelData.icon
                opacity: cell.blinking && !cell.lit ? 0 : 1
            }
            MouseArea {
                anchors.fill: parent
                onClicked: icons.notifications.xpIconClicked(cell.modelData.id)
            }
            Component.onCompleted: icons.notifications.xpRegisterIcon(cell.modelData.id, icon)
            Component.onDestruction: if (icons.notifications) icons.notifications.xpUnregisterIcon(cell.modelData.id, icon)
        }
    }
}
