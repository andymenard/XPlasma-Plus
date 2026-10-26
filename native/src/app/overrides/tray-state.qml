
    property bool xpTrayExpanded: false
    readonly property var xpHiddenItems: Plasmoid.configuration.hiddenItems
    function xpExplicitlyHidden(itemId) {
        return root.xpTrayAvailable && !root.vertical && xpHiddenItems.indexOf(itemId) !== -1;
    }
    function xpRevealHidden(itemId) {
        return root.xpTrayExpanded && xpExplicitlyHidden(itemId);
    }
    function xpRefreshFilters() {
        if (activeModel) activeModel.invalidateFilter();
        // Before Plasma 6.2 the hidden model lives in ExpandedRepresentation.
        const hidden = typeof hiddenModel !== "undefined" ? hiddenModel : (root.hiddenLayout ? root.hiddenLayout.model : null);
        if (hidden && hidden.invalidateFilter) hidden.invalidateFilter();
        if (xpExpandableModel) xpExpandableModel.invalidateFilter();
        xpCountExpandable();
    }
    onXpTrayExpandedChanged: xpRefreshFilters()
    onXpHiddenItemsChanged: xpRefreshFilters()
    // (and once the tray's art is there: items that came before it were
    // filtered without it, and aren't looked at again till they change)
    readonly property bool xpHiding: root.xpTrayAvailable && !root.vertical
    onXpHidingChanged: xpRefreshFilters()

    // How many hidden items the arrow would show -- counted here, as the
    // model's own count isn't told of rows a new filter lets in (Plasma 6.6).
    property int xpExpandableCount: 0
    function xpCountExpandable() {
        xpExpandableCount = xpExpandableModel ? xpExpandableModel.rowCount() : 0;
    }
    Connections {
        target: xpExpandableModel
        function onRowsInserted() { root.xpCountExpandable(); }
        function onRowsRemoved() { root.xpCountExpandable(); }
        function onModelReset() { root.xpCountExpandable(); }
        function onLayoutChanged() { root.xpCountExpandable(); }
    }

    KItemModels.KSortFilterProxyModel {
        id: xpExpandableModel
        filterRoleName: "effectiveStatus"
        filterRowCallback: (sourceRow, sourceParent) => {
            const index = sourceModel.index(sourceRow, 0, sourceParent);
            // ItemId is the same base role used by KDE's ConfigEntries.qml.
            return sourceModel.data(index, filterRole) === PlasmaCore.Types.PassiveStatus
                && root.xpExplicitlyHidden(sourceModel.data(index, Qt.UserRole + 2));
        }
        Component.onCompleted: {
            sourceModel = Plasmoid.systemTrayModel;
            root.xpCountExpandable();
        }
    }
    // XPlasma: the taskbar's colours, the theme's Complementary set (its
    // Window set is XP's dialog colours, for Plasma's popups).
    Kirigami.Theme.colorSet: root.xpTrayAvailable ? Kirigami.Theme.Complementary : Kirigami.Theme.Window
    Kirigami.Theme.inherit: !root.xpTrayAvailable
