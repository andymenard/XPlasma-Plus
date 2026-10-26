// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtCore
import org.kde.coreaddons as KCoreAddons
import org.kde.kitemmodels as KItemModels
import org.kde.kcmutils as KCM
import org.kde.plasma.private.kicker as Kicker
import org.kde.plasma.private.sessions as Sessions
import "popupguard-v8" as PopupGuard

Item {
    id: backend
    required property var launcher
    // Dry-run mode is used only by the test harness: never invoke host actions.
    property bool dryRun: false
    signal actionDispatched(var action)
    signal browseRequested(var sourceModel, string title, bool categories)
    signal closeRequested()
    property var pinned: []
    property var recent: []
    // XP's "Number of programs on Start menu" (6 by default): recent
    // programs have their own rows, and the menu grows to hold them.
    readonly property int recentLimit: Math.max(0, launcher.recentCount ?? 6)
    onRecentLimitChanged: Qt.callLater(refresh)
    property var allApps: null
    readonly property string userName: user.fullName || user.loginName
    // The user's own picture, or else the XP one XPlasma gave them (as XP
    // gave every account one of its own).
    readonly property url userPicture: user.faceIconUrl.toString() !== "" ? user.faceIconUrl
                                                                           : (launcher.themeData?.userPicture ?? "")
    readonly property bool canLogout: session.canLogout
    readonly property bool canShutdown: session.canShutdown

    PopupGuard.SessionActions { id: sessionActions }
    KCoreAddons.KUser { id: user }
    Sessions.SessionManagement { id: session }
    Kicker.RecentUsageModel {
        id: usage
        shownItems: Kicker.RecentUsageModel.OnlyApps
        ordering: Kicker.RecentUsageModel.Recent
        favoritesModel: backend.launcher.rootModel.favoritesModel
        onCountChanged: Qt.callLater(backend.refresh)
    }
    Kicker.RecentUsageModel { id: documents; shownItems: Kicker.RecentUsageModel.OnlyDocs }

    function role(model, row, name) {
        const id = model.KItemModels.KRoleNames.role(name);
        return id >= 0 ? model.data(model.index(row, 0), id) : null;
    }
    function entries(model, limit, excluded) {
        let result = [];
        if (!model) return result;
        for (let row = 0; row < model.count && result.length < limit; ++row) {
            const label = String(model.data(model.index(row, 0), Qt.DisplayRole) || "");
            if (!label || role(model, row, "isSeparator")) continue;
            const identity = String(role(model, row, "favoriteId") || role(model, row, "url") || label);
            const url = String(role(model, row, "url") || "");
            // (Recent use can list one application more than once; and two
            // favourites can be one application -- preferred://browser and
            // firefox.desktop, as openSUSE's defaults have them.)
            const same = entry => entry.identity === identity || (url !== "" && entry.url === url);
            if (excluded.indexOf(identity) !== -1 || (url !== "" && excluded.indexOf(url) !== -1) || result.some(same)) continue;
            result.push({label: label, icon: model.data(model.index(row, 0), Qt.DecorationRole),
                         description: String(role(model, row, "description") || ""), identity: identity,
                         favoriteId: String(role(model, row, "favoriteId") || ""), url: url,
                         action: {model: model, row: row, identity: identity}});
        }
        return result;
    }
    function refresh() {
        const favorites = launcher.rootModel.favoritesModel;
        // Use the launcher's real Plasma favorites, including order and an
        // intentionally empty list. No private pins or synthetic defaults.
        pinned = entries(favorites, favorites ? favorites.count : 0, []);
        const excluded = pinned.map(entry => entry.identity).concat(pinned.map(entry => entry.url).filter(url => url !== ""));
        let result = entries(usage, recentLimit, excluded);
        for (const entry of result) {
            entry.description = "";
        }
        recent = result;
        // Keep KDE's category tree instead of selecting KICKER_ALL_MODEL.
        allApps = launcher.rootModel;
    }
    function pathUrl(location) {
        // QtCore's QML StandardPaths wrapper already returns an encoded URL.
        return String(StandardPaths.writableLocation(location));
    }
    readonly property var places: [
        [{label: launcher.xpExplorerEnabled ? "My Documents" : "Documents", icon: "folder-documents", action: {url: pathUrl(StandardPaths.DocumentsLocation)}},
         {label: "Recent Files", icon: "folder-recent-files", action: "recent-documents"},
         {label: "Pictures", icon: "folder-pictures", action: {url: pathUrl(StandardPaths.PicturesLocation)}},
         {label: "Music", icon: "folder-music", action: {url: pathUrl(StandardPaths.MusicLocation)}},
         {label: launcher.xpExplorerEnabled ? "My Computer" : "File System", icon: "computer", action: {url: launcher.xpExplorerEnabled ? "xp-shell:/computer" : "file:///"}}],
        [{label: launcher.xpControlPanelEnabled ? "Control Panel" : "System Settings", icon: "preferences-system", action: launcher.xpControlPanelEnabled ? {url: "xp-shell:/control-panel"} : {kcm: ""}},
         {label: "Default Applications", icon: "preferences-other", action: {kcm: "kcm_componentchooser"}},
         {label: "Printers", icon: "printer", action: {kcm: "kcm_printer_manager"}}],
        [{label: "Help Center", icon: "help-contents", action: {url: "help:/"}},
         {label: "Search", icon: "edit-find", action: "search"},
         {label: "Run Command...", icon: "system-run", action: "run"}]
    ]
    // Resolve the stable ID again: applications can be installed/removed or
    // usage reordered while a menu is open. Never act on a stale row.
    function resolve(action) {
        return entries(action.model, action.model.count, []).find(entry => entry.identity === action.identity);
    }

    // The right-click menu for an entry, as XP's: Open first, pinning, then
    // what Plasma's model offers for it, in XP's words where XP had the
    // same command. (Places and categories have none.)
    readonly property var xpWords: ({
        "forget": "Remove from This List",
        "forgetAll": "Clear This List",
    })
    function actionsFor(action) {
        if (typeof action !== "object" || !action || !action.model) return [];
        const current = resolve(action);
        if (!current) return [];
        const model = action.model, row = current.action.row;
        let list = [{text: "Open", actionId: "_xplasma_open"}];
        const favorites = launcher.rootModel.favoritesModel;
        const favoriteId = role(model, row, "favoriteId");
        if (favoriteId && favorites && favorites.enabled !== false) {
            const pinned = favorites.isFavorite(favoriteId);
            list.push({text: pinned ? "Unpin from Start menu" : "Pin to Start menu",
                       actionId: pinned ? "_kicker_favorite_remove" : "_kicker_favorite_add",
                       actionArgument: {favoriteId: String(favoriteId)}});
        }
        const offered = role(model, row, "actionList");
        const extra = offered ? Array.from(offered).map(entry => {
            const copy = Object.assign({}, entry);
            if (xpWords[copy.actionId]) copy.text = xpWords[copy.actionId];
            return copy;
        }) : [];
        if (extra.length > 0) {
            list.push({type: "separator"});
            list = list.concat(extra);
        }
        return list;
    }
    function runAction(action, entry) {
        if (dryRun) { actionDispatched({action: action, actionId: entry.actionId}); return; }
        const current = resolve(action);
        if (!current) { refresh(); return; }
        const id = entry.actionId || "";
        if (id === "_xplasma_open") {
            activate(action);
        } else if (id === "_kicker_favorite_add" || id === "_kicker_favorite_remove") {
            const favorites = launcher.rootModel.favoritesModel;
            if (id === "_kicker_favorite_add") favorites.addFavorite(entry.actionArgument.favoriteId);
            else favorites.removeFavorite(entry.actionArgument.favoriteId);
            refresh();
        } else {
            if (action.model.trigger(current.action.row, id, entry.actionArgument ?? null)) closeRequested();
            Qt.callLater(refresh);
        }
    }

    // pinned[from] to pinned[to]'s place.
    function movePinned(from, to) {
        if (dryRun) { actionDispatched({move: [from, to]}); return; }
        const a = pinned[from], b = pinned[to];
        if (!a || !b) return;
        launcher.rootModel.favoritesModel.moveRow(a.action.row, b.action.row);
        Qt.callLater(refresh);
    }
    // A program dropped on the pinned list by its favourite id: pinned
    // there, or -- pinned already -- moved there.
    function pinIdAt(id, at) {
        if (dryRun) { actionDispatched({pinId: id, at: at}); return; }
        const favorites = launcher.rootModel.favoritesModel;
        if (favorites.isFavorite(id)) {
            const from = pinned.findIndex(entry => entry.identity === id);
            const to = at > from ? at - 1 : at;
            if (from >= 0 && to !== from && pinned[to]) movePinned(from, to);
            return;
        }
        favorites.addFavorite(id, at < pinned.length ? pinned[at].action.row : favorites.count);
        Qt.callLater(refresh);
    }

    function activate(action) {
        if (dryRun) { actionDispatched(action); return; }
        if (action === "all-programs") {
            refresh();
            if (allApps) browseRequested(allApps, "All Programs", true);
            return;
        }
        if (action === "recent-documents") { browseRequested(documents, "Recent Files", false); return; }
        if (typeof action === "object" && action.model) {
            const current = resolve(action);
            if (!current) { refresh(); return; }
            action.model.trigger(current.action.row, "", null);
        } else if (typeof action === "object" && action.url) {
            Qt.openUrlExternally(action.url);
        } else if (typeof action === "object" && action.kcm !== undefined) {
            KCM.KCMLauncher.openSystemSettings(action.kcm);
        } else if (action === "logoff" && canLogout) {
            session.requestLogout(Sessions.SessionManagement.ConfirmationMode.ForcePrompt);
        } else if (action === "shutdown" && canShutdown) {
            session.requestShutdown(Sessions.SessionManagement.ConfirmationMode.ForcePrompt);
        } else if (action === "run" || action === "search") {
            sessionActions.showRunner();
        } else if (action !== "close") { return; }
        closeRequested();
    }
    Connections {
        target: backend.launcher.rootModel
        function onRefreshed() { backend.refresh(); }
        function onCountChanged() { Qt.callLater(backend.refresh); }
    }
    Connections {
        target: backend.launcher.rootModel.favoritesModel
        function onCountChanged() { backend.refresh(); }
        function onDataChanged() { backend.refresh(); }
        function onModelReset() { backend.refresh(); }
        function onRowsMoved() { Qt.callLater(backend.refresh); }
        function onLayoutChanged() { Qt.callLater(backend.refresh); }
        function onFavoritesChanged() { Qt.callLater(backend.refresh); }
    }
    Component.onCompleted: refresh()
}
