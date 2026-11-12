
    // XPlasma: XP's balloons in place of Plasma's popups, when the look
    // brings their art (widgets/xp-notify). One balloon at a time, oldest
    // first; the rest wait as icons in the tray (xpQueue), and a balloon that
    // timed out unread leaves its icon behind, blinking, to show it again.
    property int xpArtVersion: 0
    property KSvg.Svg xpArt: KSvg.Svg {
        imagePath: "widgets/xp-notify"
        onRepaintNeeded: globals.xpArtVersion++
    }
    readonly property bool xpBalloons: xpArtVersion >= 0 && xpArt.hasElement("close-normal")
    onXpBalloonsChanged: {
        xpCurrentId = -1;
        Qt.callLater(xpAdvance);
    }

    property var xpQueue: []            // [{id, icon, unread, applicationName, desktopEntry, snapshot}]
    property var xpIcons: ({})          // notification id -> the tray item its balloon points at
    property int xpCurrentId: -1        // whose balloon is on screen
    property QtObject xpRecalled: null  // a balloon shown again from its icon

    function xpEntry(id) {
        return xpQueue.find(e => e.id === id);
    }
    function xpEnqueue(popup) {
        if (xpEntry(popup.notificationId))
            return;
        xpQueue = xpQueue.concat([{
            id: popup.notificationId,
            icon: popup.trayIcon,
            unread: false,
            applicationName: popup.applicationName,
            desktopEntry: popup.desktopEntry,
        }]);
    }
    function xpForget(id) {
        if (xpEntry(id))
            xpQueue = xpQueue.filter(e => e.id !== id);
    }
    // Timed out unread: the icon stays, blinking, with what to show again.
    function xpExpired(popup) {
        const snapshot = {
            summary: popup.summary,
            body: popup.body,
            image: popup.image,
            iconName: popup.iconName,
            applicationIconName: popup.applicationIconName,
            applicationName: popup.applicationName,
            desktopEntry: popup.desktopEntry,
            urgency: popup.urgency,
            urls: popup.urls,
            jobError: popup.jobError,
        };
        xpQueue = xpQueue.map(e => e.id === popup.notificationId ? Object.assign({}, e, {unread: true, snapshot: snapshot}) : e);
    }
    // A notification leaving the history -- dismissed from it, or the history
    // cleared -- takes its icon with it, blinking or not: an icon for a
    // notification the list no longer has would be one only a click could
    // remove. (A transient one was never kept in the history; its icon stays
    // until clicked, as it did.)
    property QtObject xpHistory: NotificationManager.Notifications {
        showExpired: true
        showDismissed: true
        showJobs: false
        groupMode: NotificationManager.Notifications.GroupDisabled
    }
    property QtObject xpHistoryWatch: Connections {
        target: globals.xpHistory
        function onRowsAboutToBeRemoved(parent, first, last) {
            for (let row = first; row <= last; ++row) {
                const index = globals.xpHistory.index(row, 0);
                if (globals.xpHistory.data(index, NotificationManager.Notifications.TransientRole))
                    continue;
                globals.xpForget(globals.xpHistory.data(index, NotificationManager.Notifications.IdRole));
            }
        }
    }
    // A popup gone: its icon too, unless it's left blinking.
    function xpDropped(id) {
        const e = xpEntry(id);
        if (e && !e.unread)
            xpForget(id);
        Qt.callLater(xpAdvance);
    }
    function xpRegisterIcon(id, item) {
        const icons = Object.assign({}, xpIcons);
        icons[id] = item;
        xpIcons = icons;
    }
    function xpUnregisterIcon(id, item) {
        if (xpIcons[id] !== item)
            return;
        const icons = Object.assign({}, xpIcons);
        delete icons[id];
        xpIcons = icons;
    }
    // The next balloon: the one on screen stays; otherwise the oldest waiting.
    function xpAdvance() {
        if (!xpBalloons) {
            xpCurrentId = -1;
            return;
        }
        if (xpRecalled) {
            xpCurrentId = xpRecalled.notificationId;
            return;
        }
        let next = -1;
        for (let i = 0; i < popupInstantiator.count; ++i) {
            const popup = popupInstantiator.objectAt(i);
            if (!popup || !popup.wantsBalloon || popup.notificationId === undefined)
                continue;
            if (popup.notificationId === xpCurrentId)
                return;
            if (next < 0 || popup.notificationId < next)
                next = popup.notificationId;
        }
        xpCurrentId = next;
    }
    function xpRecall(id) {
        const e = xpEntry(id);
        if (!e || !e.snapshot)
            return;
        if (xpRecalled)
            xpEndRecall(false);
        xpRecalled = xpRecallComponent.createObject(globals, Object.assign({notificationId: id, recalled: true}, e.snapshot));
        xpAdvance();
    }
    function xpEndRecall(read) {
        if (!xpRecalled)
            return;
        if (read)
            xpForget(xpRecalled.notificationId);
        xpRecalled.destroy();
        xpRecalled = null;
        xpAdvance();
    }
    property Component xpRecallComponent: Component {
        XpPopup {
            onReadRequested: globals.xpEndRecall(true)
            onCloseRequested: globals.xpEndRecall(true)
            onExpireRequested: globals.xpEndRecall(false)
        }
    }
