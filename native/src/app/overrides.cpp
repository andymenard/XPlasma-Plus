// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/overrides.h"

#include "app/package.h"
#include "app/paths.h"

#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QUrl>

namespace xpl::overrides {

namespace {

constexpr const char *kMarker = "X-XPlasma-Panel-Override";
// Spellings that came before: xp2plasma's (and the Python app's), and
// XPlasma's own while its name had a capital L.
constexpr const char *kOldMarkers[] = {"X-Xp2plasma-Panel-Override", "X-XPLasma-Panel-Override"};
constexpr const char *kShadowMarker = "// xplasma: caption text shadow override";
constexpr const char *kLegacyShadowMarker = "// xp2plasma: caption text shadow override";

QString resource(const QString &name)
{
    QFile f(QStringLiteral(":/overrides/") + name);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString systemDir(const QString &relative)
{
    return paths::findSystemData(relative);
}

bool ownedWidget(const QString &dir)
{
    QFile f(dir + QLatin1String("/metadata.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        // Older removal paths could leave only our shared text helper behind.
        // Recognize that exact fragment, never an unmarked third-party package.
        if (QFileInfo::exists(f.fileName()) || QFileInfo(dir).isSymLink())
            return false;
        const QString helper = QStringLiteral("contents/ui/XpTextStyle.qml");
        QDirIterator it(dir, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QFileInfo info(path);
            const QString relative = QDir(dir).relativeFilePath(path);
            if (info.isSymLink() || (info.isDir()
                    ? relative != QLatin1String("contents") && relative != QLatin1String("contents/ui")
                    : !info.isFile() || relative != helper))
                return false;
        }
        const QString expected = resource(QStringLiteral("XpTextStyle.qml"));
        return !expected.isEmpty() && readText(dir + QLatin1Char('/') + helper) == expected;
    }
    const QJsonObject meta = QJsonDocument::fromJson(f.readAll()).object();
    if (meta.value(QLatin1String(kMarker)).toBool())
        return true;
    return std::any_of(std::begin(kOldMarkers), std::end(kOldMarkers),
                       [&](const char *m) { return meta.value(QLatin1String(m)).toBool(); });
}

bool copyTree(const QString &from, const QString &to)
{
    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = to + file.mid(from.size());
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(file, target))
            return false;
    }
    return true;
}

bool writeText(const QString &path, const QString &text)
{
    if (readText(path) == text) return true;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(text.toUtf8()) >= 0 && f.commit();
}

} // namespace

QString resourceText(const QString &name)
{
    return resource(name);
}

std::optional<QString> patched(const QString &file, const QString &source, QString *error, const QJsonArray *rules)
{
    static const QJsonObject base = QJsonDocument::fromJson(resource(QStringLiteral("patches.json")).toUtf8()).object();
    if (!rules && !base.contains(file)) {
        fail(error, QStringLiteral("no patches for %1").arg(file));
        return std::nullopt;
    }
    const QJsonArray list = rules ? *rules : base.value(file).toArray();
    QString text = source;
    for (const QJsonValue &v : list) {
        const QJsonObject rule = v.toObject();
        const QString unless = rule.value(QStringLiteral("unlessContains")).toString();
        if (!unless.isEmpty() && text.contains(unless))
            continue;
        QString find = rule.value(QStringLiteral("find")).toString();
        QString replace = rule.value(QStringLiteral("replace")).toString();
        // Explicit source variants keep the same XP behavior on older
        // Plasma branches. Unknown layouts still fail instead of silently
        // omitting a required visual or substituting the stock widget.
        if (!text.contains(find)) {
            int matches = 0;
            for (const QJsonValue &alternative : rule.value(QStringLiteral("alternatives")).toArray()) {
                const QJsonObject candidate = alternative.toObject();
                const QString anchor = candidate.value(QStringLiteral("find")).toString();
                if (!anchor.isEmpty() && text.contains(anchor)) {
                    find = anchor;
                    replace = candidate.value(QStringLiteral("replace")).toString();
                    ++matches;
                }
            }
            if (matches > 1) {
                fail(error, QStringLiteral("ambiguous Plasma source variants for %1; not patching it").arg(file));
                return std::nullopt;
            }
        }
        // (optional: for what only some Plasmas have -- skipped where it's not)
        if (rule.value(QStringLiteral("optional")).toBool() && !text.contains(find))
            continue;
        if (text.count(find) != 1) {
            fail(error, QStringLiteral("this Plasma's %1 isn't laid out as XPlasma expects (\"%2\"); not patching it")
                            .arg(file, find.left(60).trimmed()));
            return std::nullopt;
        }
        // {{name}}: a snippet from overrides/name.qml.
        for (int at = int(replace.indexOf(QLatin1String("{{"))); at >= 0; at = int(replace.indexOf(QLatin1String("{{")))) {
            const int end = int(replace.indexOf(QLatin1String("}}"), at));
            if (end < 0)
                break;
            replace.replace(at, end + 2 - at, resource(replace.mid(at + 2, end - at - 2) + QLatin1String(".qml")));
        }
        text.replace(find, replace);
    }
    return text;
}

namespace {

struct Widget
{
    QString id, key;                 // plugin id, patches.json prefix
    QStringList files;               // what's patched, under contents/ui
    QHash<QString, QString> values;  // @KEY@ in the patched text -> value
    QStringList extras;              // whole files from overrides/, added to contents/ui
};

const QList<Widget> kPanelWidgets{
    {QStringLiteral("org.kde.plasma.systemtray"), QStringLiteral("systemtray"),
     {QStringLiteral("main.qml"), QStringLiteral("items/AbstractItem.qml"), QStringLiteral("HiddenItemsView.qml")}, {},
     {QStringLiteral("XpNotificationIcons.qml")}},
    {QStringLiteral("org.kde.plasma.taskmanager"), QStringLiteral("taskmanager"),
     {QStringLiteral("main.qml"), QStringLiteral("Task.qml"), QStringLiteral("code/layoutmetrics.js")}, {},
     {QStringLiteral("XpTextStyle.qml")}},
    // The panel itself (a containment): the taskbar's colours for every
    // widget on it, not only those XPlasma patches.
    {QStringLiteral("org.kde.panel"), QStringLiteral("panel"), {QStringLiteral("main.qml")}, {}, {}},
    // XP's balloons (when the look has their art; Plasma's popups otherwise).
    {QStringLiteral("org.kde.plasma.notifications"), QStringLiteral("notifications"),
     {QStringLiteral("main.qml"), QStringLiteral("global/Globals.qml")}, {},
     {QStringLiteral("XpPopup.qml"), QStringLiteral("XpBalloon.qml"), QStringLiteral("XpCopyDialog.qml"), QStringLiteral("XpButton.qml")}},
};
// Widgets XPlasma once patched and doesn't any more: a copy of ours goes.
// (The clock is XPlasma's own now: fyi.hotsocket.xplasma.clock.)
const QStringList kRetiredWidgets{QStringLiteral("org.kde.plasma.digitalclock")};
// (org.kde.plasma.folder, the desktop, is this package under another name.)
const QString kDesktopId = QStringLiteral("org.kde.desktopcontainment");


void removeWidgets(const QString &plasmoids, const QStringList &ids)
{
    for (const QString &id : ids)
        for (const QString &suffix : {QString(), QStringLiteral(".xplasma-new"), QStringLiteral(".xp2plasma-new")}) {
            const QString dir = plasmoids + QLatin1Char('/') + id + suffix;
            if (ownedWidget(dir))
                QDir(dir).removeRecursively();
        }
}

// One widget, patched from this Plasma's own copy, ready to put in place:
// its files' texts, or why it can't be.
std::optional<QList<std::pair<QString, QString>>> prepareWidget(const QString &plasmoids, const Widget &w, QString *error)
{
    const QString dest = plasmoids + QLatin1Char('/') + w.id;
    if (QFileInfo::exists(dest) && !ownedWidget(dest)) {
        fail(error, QStringLiteral("%1 is someone else's override; not replacing it").arg(dest));
        return std::nullopt;
    }
    const QString system = systemDir(QLatin1String("plasma/plasmoids/") + w.id);
    if (system.isEmpty()) {
        fail(error, QStringLiteral("this Plasma has no %1 package to patch (from Plasma 6.5 on, some widgets are built into "
                                   "their plugins)").arg(w.id));
        return std::nullopt;
    }
    QList<std::pair<QString, QString>> files;
    for (const QString &f : w.files) {
        // Plasma 6.0 calls the task sizing script layout.js; later versions
        // split it into layoutmetrics.js. Patch the host's own file in place.
        QString actual = f;
        if (w.key == QLatin1String("taskmanager") && f == QLatin1String("code/layoutmetrics.js")
            && !QFileInfo::exists(system + QLatin1String("/contents/ui/") + f))
            actual = QStringLiteral("code/layout.js");
        auto text = patched(w.key + QLatin1Char('/') + f, readText(system + QLatin1String("/contents/ui/") + actual), error);
        if (!text)
            return std::nullopt;
        for (auto it = w.values.cbegin(); it != w.values.cend(); ++it)
            text->replace(QLatin1Char('@') + it.key() + QLatin1Char('@'), it.value());
        files.append({actual, *text});
    }
    return files;
}

// ...put in place of any earlier copy of ours.
bool placeWidget(const QString &plasmoids, const Widget &w, const QList<std::pair<QString, QString>> &files, QString *error)
{
    const QString system = systemDir(QLatin1String("plasma/plasmoids/") + w.id);
    const QString dest = plasmoids + QLatin1Char('/') + w.id;
    const QString staging = dest + QLatin1String(".xplasma-new");
    QDir(staging).removeRecursively();
    if (!copyTree(system + QLatin1String("/contents"), staging + QLatin1String("/contents")))
        return fail(error, QStringLiteral("can't copy %1").arg(system));
    QFile metaFile(system + QLatin1String("/metadata.json"));
    metaFile.open(QIODevice::ReadOnly);
    QJsonObject meta = QJsonDocument::fromJson(metaFile.readAll()).object();
    meta.insert(QLatin1String(kMarker), true);
    writeText(staging + QLatin1String("/metadata.json"), QString::fromUtf8(QJsonDocument(meta).toJson()));
    const QString ui = staging + QLatin1String("/contents/ui/");
    for (const auto &[f, text] : files)
        writeText(ui + f, text);
    for (const QString &f : w.extras)
        writeText(ui + f, resource(f));
    if (w.key == QLatin1String("systemtray")) {
        // XP's show-hidden-icons button in place of KDE's arrow, which
        // stays as the fallback.
        QFile::rename(ui + QLatin1String("ExpanderArrow.qml"), ui + QLatin1String("PlasmaExpanderArrow.qml"));
        writeText(ui + QLatin1String("ExpanderArrow.qml"), resource(QStringLiteral("ExpanderArrow.qml")));
    }
    QDir(dest).removeRecursively();   // (ours: checked before)
    if (!QDir().rename(staging, dest))
        return fail(error, QStringLiteral("can't install %1").arg(dest));
    return true;
}

// All or nothing: every widget is checked before any is replaced (*error
// lists each that can't be, a line each), and one that fails to go in takes
// the others installed with it back out. With `dryRun`, only the checking.
bool installWidgets(const QString &plasmoids, const QList<Widget> &widgets, QString *error, bool dryRun = false)
{
    QStringList problems;
    QList<QList<std::pair<QString, QString>>> prepared;
    for (const Widget &w : widgets) {
        QString why;
        if (const auto files = prepareWidget(plasmoids, w, &why))
            prepared.append(*files);
        else
            problems << QStringLiteral("%1: %2").arg(w.id, why);
    }
    if (!problems.isEmpty())
        return fail(error, problems.join(QLatin1Char('\n')));
    if (dryRun)
        return true;
    QStringList placed;
    for (int i = 0; i < widgets.size(); ++i) {
        QString why;
        if (!placeWidget(plasmoids, widgets.at(i), prepared.at(i), &why)) {
            removeWidgets(plasmoids, placed << widgets.at(i).id);
            return fail(error, QStringLiteral("%1: %2").arg(widgets.at(i).id, why));
        }
        placed << widgets.at(i).id;
    }
    return true;
}

// A shell command as a QML string literal (see launchCommand).
QString launchLiteral(const QString &program, const QStringList &arguments = {})
{
    const QString command = launchCommand(program, arguments);
    return QString::fromUtf8(QJsonDocument(QJsonArray{command}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

} // namespace

QString launchCommand(const QString &program, const QStringList &arguments)
{
    // (QString, not auto: with QStringBuilder the sum is a builder holding a
    // reference to `word`, gone when the lambda returns.)
    auto quote = [](QString word) -> QString {
        word.replace(QLatin1Char('\''), QLatin1String("'\\''"));
        return QLatin1Char('\'') + word + QLatin1Char('\'');
    };
    QStringList words{quote(program)};
    for (const QString &a : arguments)
        words << quote(a);
    const QString run = words.join(QLatin1Char(' '));
    return QStringLiteral("systemd-run --user --collect --quiet -- %1 || %1").arg(run);
}

// Up to Plasma 6.3 the tray was two packages: org.kde.plasma.systemtray a
// thin wrapper, the tray itself org.kde.plasma.private.systemtray (6.4 made
// them one, near enough line for line) -- so there, that's the one patched.
const QString kOldTrayId = QStringLiteral("org.kde.plasma.private.systemtray");
static QList<Widget> panelWidgets()
{
    QList<Widget> list = kPanelWidgets;
    if (!systemDir(QLatin1String("plasma/plasmoids/") + kOldTrayId).isEmpty())
        for (Widget &w : list)
            if (w.key == QLatin1String("systemtray"))
                w.id = kOldTrayId;
    return list;
}

QStringList builtInPanelWidgets()
{
    QStringList ids;
    for (const Widget &w : panelWidgets())
        if (systemDir(QLatin1String("plasma/plasmoids/") + w.id).isEmpty())
            ids << w.id;
    return ids;
}

// The panel widgets this Plasma has as packages, which are patched.
static QList<Widget> packagedPanelWidgets()
{
    const QStringList builtIn = builtInPanelWidgets();
    QList<Widget> out;
    for (const Widget &w : panelWidgets())
        if (!builtIn.contains(w.id))
            out << w;
    return out;
}

bool installPanelWidgets(const QString &plasmoids, QString *error)
{
    // (Copies of ours from an earlier Plasma, of widgets it has since built
    // in or XPlasma no longer patches, go.)
    removeWidgets(plasmoids, kRetiredWidgets + builtInPanelWidgets());
    return installWidgets(plasmoids, packagedPanelWidgets(), error);
}

bool checkPanelWidgets(const QString &plasmoids, QString *error)
{
    return installWidgets(plasmoids, packagedPanelWidgets(), error, true);
}

void removePanelWidgets(const QString &plasmoids)
{
    QStringList ids = kRetiredWidgets;
    for (const Widget &w : kPanelWidgets)
        ids << w.id;
    ids << kOldTrayId;
    removeWidgets(plasmoids, ids);
}

bool installDesktopMenu(const QString &plasmoids, const QString &editor, QString *error)
{
    return installWidgets(plasmoids,
                          {{kDesktopId, QStringLiteral("desktopcontainment"), {QStringLiteral("FolderViewLayer.qml"), QStringLiteral("FolderView.qml"), QStringLiteral("FolderItemDelegate.qml")},
                            {{QStringLiteral("LAUNCH"), launchLiteral(editor)}}, {QStringLiteral("XpTextStyle.qml")}}},
                          error);
}

bool checkDesktopMenu(const QString &plasmoids, QString *error)
{
    return installWidgets(plasmoids,
                          {{kDesktopId, QStringLiteral("desktopcontainment"), {QStringLiteral("FolderViewLayer.qml"), QStringLiteral("FolderView.qml"), QStringLiteral("FolderItemDelegate.qml")},
                            {{QStringLiteral("LAUNCH"), QStringLiteral("\"\"")}}, {QStringLiteral("XpTextStyle.qml")}}},
                          error, true);
}

void removeDesktopMenu(const QString &plasmoids)
{
    removeWidgets(plasmoids, {kDesktopId});
}

namespace {

const QString kCaptionAnchor = QStringLiteral("    Text {\n        id: caption");
const QStringList kButtonLists{QStringLiteral("buttons: options.titleButtonsLeft"), QStringLiteral("buttons: options.titleButtonsRight")};

// KWin's own aurorae.qml, if it's there, ours to override and laid out as
// XPlasma expects.
std::optional<QString> auroraeSource(const QString &target, QString *error)
{
    const QString existing = readText(target);
    if (!existing.isEmpty() && !existing.startsWith(QLatin1String(kShadowMarker)) && !existing.startsWith(QLatin1String(kLegacyShadowMarker))) {
        fail(error, QStringLiteral("%1 is someone else's Aurorae override; not replacing it").arg(target));
        return std::nullopt;
    }
    const QString system = systemDir(QStringLiteral("kwin/aurorae"));
    const QString source = system.isEmpty() ? QString() : readText(system + QLatin1String("/aurorae.qml"));
    if (source.isEmpty()) {
        fail(error, QStringLiteral("KWin's Aurorae renderer isn't installed"));
        return std::nullopt;
    }
    if (source.count(kCaptionAnchor) != 1 || source.count(QLatin1String("    id: root")) != 1
        || source.count(kButtonLists.at(0)) != 1 || source.count(kButtonLists.at(1)) != 1) {
        fail(error, QStringLiteral("this KWin's aurorae.qml isn't laid out as XPlasma expects"));
        return std::nullopt;
    }
    return source;
}

} // namespace

bool checkTitleShadows(const QString &target, QString *error)
{
    return auroraeSource(target, error).has_value();
}

bool installTitleShadows(const QString &themes, const QString &target, QString *error)
{
    auto checked = auroraeSource(target, error);
    if (!checked)
        return false;
    QString source = *checked;
    const QString system = systemDir(QStringLiteral("kwin/aurorae"));

    // Which themes are XPlasma's, and which of those want a shadow, from
    // their rc files.
    QJsonObject shadows;
    QJsonArray ours;
    for (const QFileInfo &pkg : QDir(themes).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if(pkg.fileName().startsWith(QLatin1String("xplasma-v2-")))continue;
        const QString rcPath = pkg.filePath() + QLatin1Char('/') + pkg.fileName() + QLatin1String("rc");
        QSettings rc(rcPath, QSettings::IniFormat);
        if (!rc.contains(QLatin1String(kGeneratedKey) + QLatin1String("/Look")) && !readText(pkg.filePath() + QLatin1String("/metadata.desktop")).contains(QLatin1String("XPlasma")))
            continue;
        ours.append(pkg.fileName());
        // (QSettings reads an INI file's [General] keys as top-level ones.)
        if (rc.value(QStringLiteral("UseTextShadow")).toString() != QLatin1String("true"))
            continue;
        auto colour = [&](const char *key) {
            const QStringList v = rc.value(QLatin1String(key)).toStringList().join(QLatin1Char(',')).split(QLatin1Char(','));
            if (v.size() < 3)
                return QStringLiteral("#00000000");
            return QColor(v.at(0).toInt(), v.at(1).toInt(), v.at(2).toInt(), v.value(3, QStringLiteral("255")).toInt()).name(QColor::HexArgb);
        };
        shadows.insert(pkg.fileName(), QJsonObject{{QStringLiteral("active"), colour("ActiveTextShadowColor")},
                                                   {QStringLiteral("inactive"), colour("InactiveTextShadowColor")},
                                                   {QStringLiteral("x"), rc.value(QStringLiteral("TextShadowOffsetX"), 1).toInt()},
                                                   {QStringLiteral("y"), rc.value(QStringLiteral("TextShadowOffsetY"), 1).toInt()}});
    }

    // KDE's renderer, importing its own components from where they are, with
    // a second, offset caption under the real one, and XP's rule for the
    // title buttons: a window with neither a minimize nor a maximize box
    // shows neither (Aurorae would grey them), nor its icon.
    const QString &anchor = kCaptionAnchor;
    const QStringList &buttonLists = kButtonLists;
    source.replace(source.indexOf(QLatin1String("import QtQuick")), int(qstrlen("import QtQuick")),
                   QStringLiteral("import \"%1\" as Upstream\nimport QtQuick").arg(QUrl::fromLocalFile(system).toString()));
    if (const int at = int(source.indexOf(QLatin1String("\nDecoration {"))); at >= 0)
        source.replace(at, int(qstrlen("\nDecoration {")), QStringLiteral("\nUpstream.Decoration {"));
    source.replace(QLatin1String("    AuroraeButtonGroup {"), QLatin1String("    Upstream.AuroraeButtonGroup {"));
    source.replace(source.indexOf(QLatin1String("    id: root")), int(qstrlen("    id: root")),
                   QStringLiteral("    id: root\n    property var xpTitleShadows: (")   // (bare braces would be a JS block)
                       + QString::fromUtf8(QJsonDocument(shadows).toJson(QJsonDocument::Compact)) + QLatin1Char(')')
                       + QStringLiteral("\n    property var xpThemes: ") + QString::fromUtf8(QJsonDocument(ours).toJson(QJsonDocument::Compact))
                       + QLatin1Char('\n') + resource(QStringLiteral("title-buttons.qml")));
    for (const QString &list : buttonLists)
        source.replace(list, QStringLiteral("buttons: root.xpButtons(%1)").arg(list.mid(int(qstrlen("buttons: ")))));
    source.replace(anchor, resource(QStringLiteral("title-shadow.qml")) + anchor);
    source.replace(QStringLiteral("id: caption\n"),QStringLiteral("id: caption\n        objectName: \"xplasma-caption\"\n"));
    if (!writeText(target, QLatin1String(kShadowMarker) + QLatin1Char('\n') + source))
        return fail(error, QStringLiteral("can't write %1").arg(target));
    return true;
}

void removeTitleShadows(const QString &target)
{
    const QString text = readText(target);
    if (text.startsWith(QLatin1String(kShadowMarker)) || text.startsWith(QLatin1String(kLegacyShadowMarker)))
        QFile::remove(target);
}

} // namespace xpl::overrides
