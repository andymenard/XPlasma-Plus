// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import "popupguard-v8" as PopupGuard
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.private.kicker as Kicker

Item {
    id: root
    required property var launcher
    // Plasma 6.4+ AppletPopup adds its own frame padding. Let our original
    // artwork cover that frame instead of growing the measured 380x478 menu.
    readonly property real frameLeft: root.Window.window?.leftPadding ?? 0
    readonly property real frameRight: root.Window.window?.rightPadding ?? 0
    readonly property real frameTop: root.Window.window?.topPadding ?? 0
    readonly property real frameBottom: root.Window.window?.bottomPadding ?? 0
    implicitWidth: root.launcher.themeData.layout.width - frameLeft - frameRight
    implicitHeight: menu.height - frameTop - frameBottom
    Layout.minimumWidth: implicitWidth
    Layout.maximumWidth: implicitWidth
    Layout.minimumHeight: implicitHeight
    Layout.maximumHeight: implicitHeight
    focus: true
    // The room above the taskbar (Plasma's own panels excluded), measured as
    // the menu opens: the menu grows to fit its programs, up to that.
    property real availableHeight: 100000
    function measureScreen() {
        // (Kicker's C++ needs the item in a window: without one it crashes
        // Plasma's shell.)
        if (!root.Window.window || !root.visible) return;
        const rect = submenu.availableScreenRectForItem(root);
        if (rect && rect.height > 0)
            availableHeight = rect.height;
    }

    Binding {
        target: root.Window.window
        property: "backgroundHints"
        value: PlasmaCore.Dialog.NoBackground
        when: root.Window.window !== null && root.Window.window.hasOwnProperty("backgroundHints")
            && root.Window.window.topPadding === undefined
        restoreMode: Binding.RestoreBindingOrValue
    }
    // The XP artwork includes its own edges. AppletPopup otherwise asks
    // KWin for the Plasma theme's border/shadow tiles around the menu.
    Binding {
        target: popupGuard.bordersWritable ? root.Window.window : null
        property: "borders"
        value: 0
        when: popupGuard.bordersWritable
        restoreMode: Binding.RestoreBindingOrValue
    }
    // PopupPlasmaWindow recalculates its borders after positioning on
    // every opening. A constant Binding alone does not override that write.
    Connections {
        target: root.Window.window
        ignoreUnknownSignals: true
        function onBordersChanged() {
            const window = root.Window.window;
            if (window && popupGuard.bordersWritable && window.borders !== 0)
                window.borders = 0;
        }
    }
    // CompactApplet draws a panel/popup separator above the XP footer
    // and reserves an extra row for it. The XP menu joins the Start button
    // directly, so hide that separator and remove its reserved geometry.
    readonly property Item panelSeparator: {
        if (!root.parent) return null;
        for (const item of root.parent.children) {
            if (item.imagePath === "widgets/line"
                    && (item.elementId === "horizontal-line" || item.elementId === "vertical-line"))
                return item;
        }
        return null;
    }
    Binding {
        target: root.panelSeparator
        property: "visible"
        value: false
        when: root.panelSeparator !== null
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: root.parent
        property: "extraHeight"
        value: 0
        when: root.parent !== null && root.parent.hasOwnProperty("extraHeight")
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: root.parent
        property: "extraWidth"
        value: 0
        when: root.parent !== null && root.parent.hasOwnProperty("extraWidth")
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: root.anchors
        property: "leftMargin"
        value: 0
        when: root.panelSeparator !== null
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: root.anchors
        property: "rightMargin"
        value: 0
        when: root.panelSeparator !== null
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: root.anchors
        property: "topMargin"
        value: 0
        when: root.panelSeparator !== null
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: root.anchors
        property: "bottomMargin"
        value: 0
        when: root.panelSeparator !== null
        restoreMode: Binding.RestoreBindingOrValue
    }
    PopupGuard.ResizeGuard { id: popupGuard; window: root.Window.window }
    Component {
        id: nativeIcon
        Kirigami.Icon { source: parent.iconSource; smooth: false }
    }
    XpBackend {
        id: backend
        launcher: root.launcher
        onCloseRequested: root.launcher.expanded = false
        onBrowseRequested: (sourceModel, title, categories) => {
            submenu.sourceModel = sourceModel;
            submenu.title = title;
            submenu.categoriesOnly = categories;
            submenu.pointerMode = submenu.anchorHovered;
            submenu.rebuild();
            submenu.visible = true;
            submenu.mainItem.forceActiveFocus();
        }
    }
    StartMenu {
        id: menu
        x: -root.frameLeft
        y: -root.frameTop
        themeData: root.launcher.themeData
        maximumHeight: root.availableHeight
        userName: backend.userName
        userPicture: backend.userPicture
        pinnedItems: backend.pinned
        recentItems: backend.recent
        placeGroups: backend.places
        iconDelegate: nativeIcon
        canLogout: backend.canLogout
        canShutdown: backend.canShutdown
        allProgramsOpen: submenu.visible && submenu.categoriesOnly
        recentFilesOpen: submenu.visible && !submenu.categoriesOnly
        onCloseProgramsRequested: submenu.back(false)
        onRequested: action => backend.activate(action)
        onContextRequested: (action, item, x, y) => root.showActions(action, item, x, y)
        onPinIdRequested: (id, at) => backend.pinIdAt(id, at)
    }
    ActionMenu {
        id: actionMenu
        property var target: null
        onTriggered: entry => backend.runAction(target, entry)
    }
    function showActions(action, item, x, y) {
        const list = backend.actionsFor(action);
        if (list.length === 0) return;
        actionMenu.target = action;
        actionMenu.show(list, item, x, y);
    }
    XpProgramsPopup {
        id: submenu
        xpTextStyle: menu.xpTextStyle
        themeData: root.launcher.themeData
        visualParent: categoriesOnly ? menu.allProgramsItem : menu.recentFilesItem
        anchorHovered: categoriesOnly ? menu.allProgramsHovered : menu.recentFilesHovered
        contextMenu: actionMenu
        onActivated: action => backend.activate(action)
        onDismissRequested: root.launcher.expanded = false
        onContextRequested: (action, item, x, y) => root.showActions(action, item, x, y)
    }
    Binding {
        target: root.launcher
        property: "hideOnWindowDeactivate"
        value: false
        when: submenu.visible || actionMenu.opened || menu.dragging
        restoreMode: Binding.RestoreBindingOrValue
    }
    Connections {
        target: root.launcher
        function onExpandedChanged() {
            if (root.launcher.expanded) {
                root.measureScreen();
                backend.refresh();
                menu.resetScroll();
                menu.forceActiveFocus();
            } else { submenu.visible = false; }
        }
    }
    Component.onCompleted: root.launcher.rootModel.refresh()
}

