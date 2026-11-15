/*
    SPDX-FileCopyrightText: 2014 Aleix Pol Gonzalez <aleixpol@blue-systems.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2

import org.kde.plasma.components as PlasmaComponents
import org.kde.coreaddons as KCoreAddons
import org.kde.kirigami as Kirigami

import org.kde.breeze.components
import "timer.js" as AutoTriggerTimer

import org.kde.plasma.private.sessions

Item {
    id: root
    objectName: "root"
    Kirigami.Theme.inherit: false
    Kirigami.Theme.colorSet: Kirigami.Theme.Complementary
    height: screenGeometry.height
    width: screenGeometry.width

    signal logoutRequested()
    signal haltRequested()
    signal haltUpdateRequested()
    signal suspendRequested(int spdMethod)
    signal rebootRequested()
    signal rebootRequested2(int opt)
    signal rebootUpdateRequested()
    signal cancelRequested()
    signal lockScreenRequested()
    signal cancelSoftwareUpdateRequested()

    function sleepRequested() {
        root.suspendRequested(2);
    }

    function hibernateRequested() {
        root.suspendRequested(4);
    }

    readonly property string bootLoaderEntry: typeof rebootToBootLoaderEntry === "undefined" ? "" : rebootToBootLoaderEntry
    readonly property bool powerMode: sdtype !== ShutdownType.ShutdownTypeNone
    readonly property var navigationButtons: [suspendButton, hibernateButton, shutdownButton, shutdownWithoutUpdatesButton, rebootButton, rebootWithoutUpdatesButton, switchUserButton, logoutButton, cancelButton].filter(b => b.visible && b.enabled)
    function neighbor(button, direction) {
        const buttons = navigationButtons;
        return buttons[(buttons.indexOf(button) + direction + buttons.length) % buttons.length];
    }
    property real timeout: 30
    property real remainingTime: root.timeout

    property var currentAction: {
        switch (sdtype) {
        case ShutdownType.ShutdownTypeReboot:
            return () => softwareUpdatePending ? rebootUpdateRequested() : rebootRequested();
        case ShutdownType.ShutdownTypeHalt:
            return () => softwareUpdatePending ? haltUpdateRequested() : haltRequested();
        default:
            return () => logoutRequested();
        }
    }

    readonly property bool showAllOptions: sdtype === ShutdownType.ShutdownTypeDefault

    KCoreAddons.KUser {
        id: kuser
    }
    SessionManagement {
        id: sessionManagement
    }

    // For showing an "other users are logged in" hint
    SessionsModel {
        id: otherSessionsModel

        // In case the default values ever change
        includeUnusedSessions: false
    }

    // SessionsModel excludes this session, not other sessions of this account.
    // A TTY/SSH login by the owner is not another person who may lose work.
    property int sessionEntriesRevision: 0
    readonly property int otherUserCount: {
        const revision = sessionEntriesRevision;
        const accounts = {};
        for (let i = 0; i < sessionEntries.count; ++i) {
            const entry = sessionEntries.objectAt(i);
            if (entry && entry.name && entry.name !== kuser.loginName)
                accounts[entry.name] = true;
        }
        return Object.keys(accounts).length;
    }
    Instantiator {
        id: sessionEntries
        model: otherSessionsModel
        delegate: QtObject { required property string name }
        onObjectAdded: root.sessionEntriesRevision++
        onObjectRemoved: root.sessionEntriesRevision++
    }

    QQC2.Action {
        onTriggered: root.cancelRequested()
        shortcut: "Escape"
    }

    Rectangle {
        id: backgroundRect
        anchors.fill: parent
        // Intentionally hardcoded because any other color looks terrible here.
        // Any bug reports about illegible text or icons should be considered
        // a color scheme error and sent back to the user or their distro.
        color: "black"
        opacity: 0.55
    }
    MouseArea {
        anchors.fill: parent
        onClicked: root.cancelRequested()
    }
    // Geometry follows XP's msgina dialog 20100 (Turn Off) and shell32 dialog 1089 (Log Off):
    // 208x122 dialog units drawn on the 313x198 background bitmap.
    readonly property var actionButtons: [suspendButton, hibernateButton, shutdownButton, shutdownWithoutUpdatesButton, rebootButton, rebootWithoutUpdatesButton, switchUserButton, logoutButton].filter(b => b.visible)
    readonly property int actionRows: Math.max(1, Math.ceil(actionButtons.length / 3))
    readonly property real warningSpace: Math.max(0, column.implicitHeight - 28)
    readonly property real extraHeight: (actionRows - 1) * 96 + warningSpace
    function slotY(button) { return Math.floor(actionButtons.indexOf(button) / 3) * 96; }
    function slotX(button) {
        const absoluteIndex = actionButtons.indexOf(button);
        const index = absoluteIndex % 3, count = Math.min(3, actionButtons.length - Math.floor(absoluteIndex / 3) * 3);
        const centres = count === 2 ? [66, 143] : count === 3 ? [47, 104, 161] : [];
        const centre = centres.length ? centres[index] : 208 * (index + 1) / (count + 1);
        return Math.round(centre * 313 / 208 - button.width / 2);
    }
    BorderImage {
        id: frame
        objectName: "frame"
        anchors.centerIn: parent
        width: 313; height: 198 + root.extraHeight
        border.top: 44; border.bottom: 44
        source: "assets/20140.png"; smooth: false
        Text {
            x: 11; y: (42 - height) / 2
            text: root.powerMode ? "Turn off computer" : "Log Off Windows"
            color: "white"; font.family: "Tahoma OS"; font.pixelSize: 20; font.bold: true
        }
        Image { source: "assets/20141.png"; x: 265; y: 1; width: 48; height: 40 }
    }
    ColumnLayout {
        id: column

        anchors.top: frame.top
        anchors.topMargin: 46
        anchors.horizontalCenter: frame.horizontalCenter
        width: frame.width - 24
        spacing: 2

        PlasmaComponents.Label {
            font.family: "Tahoma OS"
            font.pixelSize: 11
            color: "white"
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.italic: true
            text: i18ndp("plasma_lookandfeel_org.kde.lookandfeel",
                         "One other user is currently logged in. If the computer is shut down or restarted, that user may lose work.",
                         "%1 other users are currently logged in. If the computer is shut down or restarted, those users may lose work.",
                         root.otherUserCount)
            textFormat: Text.PlainText
            visible: root.otherUserCount > 0 && (sdtype !== ShutdownType.ShutdownTypeNone || root.showAllOptions)
        }

        PlasmaComponents.Label {
            font.family: "Tahoma OS"
            font.pixelSize: 11
            color: "white"
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.italic: true
            text: {
                if (typeof isUefi !== "undefined" && isUefi) {
                    return i18nd("plasma_lookandfeel_org.kde.lookandfeel", "When restarted, the computer will enter UEFI firmware settings.");
                } else {
                    return i18nd("plasma_lookandfeel_org.kde.lookandfeel", "When restarted, the computer will enter the firmware setup screen.");
                }
            }
            textFormat: Text.PlainText
            visible: rebootToFirmwareSetup
        }

        PlasmaComponents.Label {
            font.family: "Tahoma OS"
            font.pixelSize: 11
            color: "white"
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.italic: true
            text: i18nd("plasma_lookandfeel_org.kde.lookandfeel", "When restarted, the computer will enter the boot loader menu.")
            textFormat: Text.PlainText
            visible: rebootToBootLoaderMenu
        }

        PlasmaComponents.Label {
            font.family: "Tahoma OS"
            font.pixelSize: 11
            color: "white"
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.italic: true
            text: i18ndc("plasma_lookandfeel_org.kde.lookandfeel", "%1 is a boot loader entry", "When restarted, the computer will switch to %1.", root.bootLoaderEntry)
            textFormat: Text.PlainText
            visible: root.bootLoaderEntry !== ""
        }

    }
    Item {
        id: logoutButtonsRow
        x: frame.x; y: frame.y + 80 + root.warningSpace
        width: frame.width; height: 52 + (root.actionRows - 1) * 96

            LogoutButton {
                id: suspendButton
                x: root.slotX(suspendButton)
                y: root.slotY(suspendButton)
        objectName: "suspendButton"
                KeyNavigation.left: root.neighbor(suspendButton, -1)
                KeyNavigation.right: root.neighbor(suspendButton, 1)
                icon.name: "system-suspend"
                text: root.showAllOptions ? i18ndc("plasma_lookandfeel_org.kde.lookandfeel", "Suspend to RAM", "Stand By")
                                          : i18ndc("plasma_lookandfeel_org.kde.lookandfeel", "Suspend to RAM", "Stand By")
                onClicked: root.sleepRequested()


                visible: spdMethods.SuspendState && root.powerMode
            }
            LogoutButton {
                id: hibernateButton
                x: root.slotX(hibernateButton)
                y: root.slotY(hibernateButton)
        objectName: "hibernateButton"
                KeyNavigation.left: root.neighbor(hibernateButton, -1)
                KeyNavigation.right: root.neighbor(hibernateButton, 1)
                icon.name: "system-suspend-hibernate"
                text: root.showAllOptions ? i18nd("plasma_lookandfeel_org.kde.lookandfeel", "&Hibernate")
                                          : i18nd("plasma_lookandfeel_org.kde.lookandfeel", "&Hibernate Now")
                onClicked: root.hibernateRequested()


                visible: spdMethods.HibernateState && root.showAllOptions
            }
            LogoutButton {
                id: shutdownButton
                x: root.slotX(shutdownButton)
                y: root.slotY(shutdownButton)
        objectName: "shutdownButton"
                KeyNavigation.left: root.neighbor(shutdownButton, -1)
                KeyNavigation.right: root.neighbor(shutdownButton, 1)
                icon.name: softwareUpdatePending ? "system-shutdown-update" : "system-shutdown"
                text: {
                    if (softwareUpdatePending) {
                        return i18ndc("plasma_lookandfeel_org.kde.lookandfeel", "@action:button Keep short", "Install Updates and Shut Down")
                    } else {
                        return root.showAllOptions ? i18nd("plasma_lookandfeel_org.kde.lookandfeel", "Turn Off")
                                                   : i18nd("plasma_lookandfeel_org.kde.lookandfeel", "Turn Off")
                    }
                }
                onClicked: {
                    if (softwareUpdatePending) {
                        root.haltUpdateRequested();
                    } else {
                        root.haltRequested();
                    }
                }


                focus: sdtype === ShutdownType.ShutdownTypeHalt || root.showAllOptions
                visible: maysd && root.powerMode
            }
            LogoutButton {
                id: shutdownWithoutUpdatesButton
                x: root.slotX(shutdownWithoutUpdatesButton)
                y: root.slotY(shutdownWithoutUpdatesButton)
        objectName: "shutdownWithoutUpdatesButton"
                KeyNavigation.left: root.neighbor(shutdownWithoutUpdatesButton, -1)
                KeyNavigation.right: root.neighbor(shutdownWithoutUpdatesButton, 1)
                icon.name: "system-shutdown"
                text: root.showAllOptions ? i18nd("plasma_lookandfeel_org.kde.lookandfeel", "Turn Off")
                                          : i18nd("plasma_lookandfeel_org.kde.lookandfeel", "Turn Off")
                onClicked: {
                    root.haltRequested();
                }


                focus: sdtype === ShutdownType.ShutdownTypeHalt || root.showAllOptions
                visible: maysd && softwareUpdatePending && root.powerMode
            }
            LogoutButton {
                id: rebootButton
                x: root.slotX(rebootButton)
                y: root.slotY(rebootButton)
        objectName: "rebootButton"
                KeyNavigation.left: root.neighbor(rebootButton, -1)
                KeyNavigation.right: root.neighbor(rebootButton, 1)
                icon.name: softwareUpdatePending ? "system-reboot-update" : "system-reboot"
                text: {
                    if (softwareUpdatePending) {
                        return i18ndc("plasma_lookandfeel_org.kde.lookandfeel", "@action:button Keep short", "Install Updates and Restart")
                    } else {
                        return root.showAllOptions ? i18nd("plasma_lookandfeel_org.kde.lookandfeel", "&Restart")
                                                   : i18nd("plasma_lookandfeel_org.kde.lookandfeel", "Restart")
                    }
                }
                onClicked: {
                    if (softwareUpdatePending) {
                        root.rebootUpdateRequested();
                    } else {
                        root.rebootRequested();
                    }
                }


                focus: sdtype === ShutdownType.ShutdownTypeReboot
                visible: maysd && root.powerMode
            }
            LogoutButton {
                id: rebootWithoutUpdatesButton
                x: root.slotX(rebootWithoutUpdatesButton)
                y: root.slotY(rebootWithoutUpdatesButton)
        objectName: "rebootWithoutUpdatesButton"
                KeyNavigation.left: root.neighbor(rebootWithoutUpdatesButton, -1)
                KeyNavigation.right: root.neighbor(rebootWithoutUpdatesButton, 1)
                icon.name: "system-reboot"
                text: root.showAllOptions ? i18nd("plasma_lookandfeel_org.kde.lookandfeel", "&Restart")
                                          : i18nd("plasma_lookandfeel_org.kde.lookandfeel", "Restart")
                onClicked: {
                    root.rebootRequested();
                }


                visible: maysd && softwareUpdatePending && root.powerMode
            }
            LogoutButton {
                id: switchUserButton
                x: root.slotX(switchUserButton)
                y: root.slotY(switchUserButton)
                objectName: "switchUserButton"
                KeyNavigation.left: root.neighbor(switchUserButton, -1)
                KeyNavigation.right: root.neighbor(switchUserButton, 1)
                text: "Switch User"
                sprite: "assets/14356.png"
                spriteRow: 0
                onClicked: {
                    root.cancelRequested();
                    sessionManagement.switchUser();
                }
                visible: sessionManagement.canSwitchUser && !root.powerMode
            }
            LogoutButton {
                id: logoutButton
                x: root.slotX(logoutButton)
                y: root.slotY(logoutButton)
        objectName: "logoutButton"
                KeyNavigation.left: root.neighbor(logoutButton, -1)
                KeyNavigation.right: root.neighbor(logoutButton, 1)
                icon.name: "system-log-out"
                sprite: "assets/14356.png"
                spriteRow: 3
                text: root.showAllOptions ? i18nd("plasma_lookandfeel_org.kde.lookandfeel", "Log Off")
                                          : i18nd("plasma_lookandfeel_org.kde.lookandfeel", "Log Off")
                onClicked: root.logoutRequested()
                focus: sdtype === ShutdownType.ShutdownTypeNone
                visible: canLogout && !root.powerMode
            }
    }
    QQC2.Button {
        id: cancelButton
        objectName: "cancelButton"
                KeyNavigation.left: root.neighbor(cancelButton, -1)
                KeyNavigation.right: root.neighbor(cancelButton, 1)
        x: frame.x + 242; y: frame.y + 167 + root.extraHeight
        width: 60; height: 20
        text: "Cancel"
        font.family: "Tahoma OS"; font.pixelSize: 11
        onClicked: root.cancelRequested()

        Keys.onPressed: AutoTriggerTimer.cancelAutoTrigger()
        background: Rectangle { color: cancelButton.down ? "#d8d8d8" : "#f0f0f0"; radius: 2; border.color: cancelButton.visualFocus ? "#ffc73c" : "#003c74"; border.width: 1 }
        contentItem: Text { text: cancelButton.text; color: "black"; font: cancelButton.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
    }
}
