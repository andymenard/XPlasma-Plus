#include "editor/customluna.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/apply.h"
#include "app/library.h"

#include "app/colors.h"
#include "core/themefile.h"
#include "app/darkmode.h"
#include "app/crisptext.h"
#include "app/controls.h"
#include "app/decoration.h"
#include "app/forks.h"
#include "app/gtk.h"
#include "app/icons.h"
#include "app/overrides.h"
#include "app/panel.h"
#include "app/pointers.h"
#include "app/project.h"
#include "app/paths.h"
#include "app/maintenance.h"
#include "app/qttarget.h"
#include "app/sounds.h"
#include "app/startmenu.h"
#include "app/state.h"
#include "app/xptext.h"
#include "core/schema.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QSettings>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QUrl>

#include <cstdio>
#include <fcntl.h>

namespace xpl {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString share(const QString &relative)
{
    return paths::localShare() + QLatin1Char('/') + relative;
}

// Plasma keeps memory for every desktop theme name it is shown and never
// gives it back (~2 MB a name, measured): with a name per look, each new
// custom color grew the shell. The taskbar takes turns in two fixed slots
// instead, the one not in use refilled with the look's (cached) theme.
constexpr const char *kTaskbarSlots[] = {"xplasma-taskbar-a", "xplasma-taskbar-b"};

bool install(const QString &from, const QString &to, QString *error);
bool fillTaskbarSlot(const QString &theme, const QString &slot, QString *error)
{
    const QString dest = share(QStringLiteral("plasma/desktoptheme/")) + slot;
    if (!install(theme, dest, error))
        return false;
    QFile meta(dest + QLatin1String("/metadata.json"));
    QJsonObject json = meta.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(meta.readAll()).object() : QJsonObject();
    meta.close();
    QJsonObject plugin = json.value(QStringLiteral("KPlugin")).toObject();
    plugin.insert(QStringLiteral("Id"), slot);
    json.insert(QStringLiteral("KPlugin"), plugin);
    QSaveFile out(dest + QLatin1String("/metadata.json"));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(json).toJson()) < 0 || !out.commit())
        return fail(error, QStringLiteral("%1: %2").arg(out.fileName(), out.errorString()));
    return true;
}

// A package directory built under `from` moved into place at `to`.
// Copied in beside the old one, then swapped for it in one step: whatever
// is watching (every app, for an icon theme in use) never finds it missing
// or half there.
bool install(const QString &from, const QString &to, QString *error)
{
    const QString fresh = to + QLatin1String(".xplasma-new");
    QDir(fresh).removeRecursively();
    QDir().mkpath(fresh);
    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = fresh + file.mid(from.size());
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(file, target)) {
            QDir(fresh).removeRecursively();
            return fail(error, QStringLiteral("can't install %1").arg(to + file.mid(from.size())));
        }
    }
    const QByteArray a = QFile::encodeName(fresh), b = QFile::encodeName(to);
    if (QFileInfo(to).isDir() && ::renameat2(AT_FDCWD, a.constData(), AT_FDCWD, b.constData(), RENAME_EXCHANGE) == 0) {
        QDir(fresh).removeRecursively();   // (the old one, now)
        return true;
    }
    QDir(to).removeRecursively();   // (none there, or a filesystem that can't swap)
    if (!QDir().rename(fresh, to)) {
        QDir(fresh).removeRecursively();
        return fail(error, QStringLiteral("can't install %1").arg(to));
    }
    return true;
}

bool ours(const QString &metadata)
{
    QFile f(metadata);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray text = f.readAll().toLower();
    return text.contains("xplasma") || text.contains("xp2plasma");   // (X-Xp2plasma-Icon-Theme, too)
}

QString resourceText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

void report(const ApplyOptions &o, const QString &line)
{
    if (o.progress)
        o.progress(line);
}

// XPlasma's own window decoration: drawn natively from the look xplasmarc
// names, it follows a new look or new colours by itself -- no theme for KWin
// to load, no reconfigure, none of the pause an Aurorae switch puts on the
// whole desktop. Bound to the KDecoration it was built against, so used
// only on that Plasma series; otherwise taken out again (an Aurorae theme
// stands in).
const QString kNativeDecoration = QStringLiteral("fyi.hotsocket.xplasma.decoration");

QString nativeDecorationPath()
{
    return paths::stylePluginRoot(6) + QLatin1String("/org.kde.kdecoration3/") + kNativeDecoration + QLatin1String(".so");
}

// Whether this copy ships the decoration built for the running Plasma's series.
bool nativeDecorationUsable(Session &session)
{
    const QString shipped = paths::shippedDecoration();
    QString builtFor;
    // (Its stamp beside it: fyi.hotsocket.xplasma.decoration.plasma.)
    const QFileInfo plugin(shipped);
    if (QFile stamp(plugin.dir().filePath(plugin.completeBaseName() + QLatin1String(".plasma")));
        !shipped.isEmpty() && stamp.open(QIODevice::ReadOnly))
        builtFor = QString::fromUtf8(stamp.readAll()).trimmed();
    const QString running = forks::runningPlasma(session);
    return !builtFor.isEmpty() && !running.isEmpty() && forks::seriesOf(builtFor) == forks::seriesOf(running);
}

bool installNativeDecoration(Session &session)
{
    const QString shipped = paths::shippedDecoration();
    const QString installed = nativeDecorationPath();
    if (!nativeDecorationUsable(session)) {
        QFile::remove(installed);
        return false;
    }
    QFile current(installed), from(shipped);
    if (current.size() == from.size() && current.open(QIODevice::ReadOnly) && from.open(QIODevice::ReadOnly)
        && current.readAll() == from.readAll())
        return true;
    // (A fresh name, then a rename: KWin keeps the copy it has loaded.)
    QDir().mkpath(QFileInfo(installed).absolutePath());
    const QString staged = installed + QLatin1String(".new");
    QFile::remove(staged);
    return QFile::copy(shipped, staged)
           && std::rename(QFile::encodeName(staged).constData(), QFile::encodeName(installed).constData()) == 0;
}

void removeNativeDecoration()
{
    QFile::remove(nativeDecorationPath());
    paths::removeEmptyDirs(QFileInfo(nativeDecorationPath()).absolutePath(), QFileInfo(paths::localShare()).absolutePath());
}

// KWin: XPlasma's own decoration where it can be; otherwise Aurorae, with the
// look's theme. (Aurorae keeps one QML renderer for
// every SVG theme, with its shadow palette cached: switching straight from
// one of ours to another kept the old one's, so it goes by Breeze first.)
void applyDecoration(Session &session, const QString &id, bool reloadEngine = true)
{
    const QStringList group{QStringLiteral("org.kde.kdecoration2")};
    if (installNativeDecoration(session)) {
        // Once it's KWin's decoration, a look is only xplasmarc, which it
        // follows: KWin is told nothing.
        if (session.readConfig(QStringLiteral("kwinrc"), group, QStringLiteral("library")) != kNativeDecoration) {
            session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("library"), kNativeDecoration);
            session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("theme"), QString());
            session.reconfigureKWin();
        }
        return;
    }
    if (reloadEngine && session.readConfig(QStringLiteral("kwinrc"), group, QStringLiteral("theme")).startsWith(QLatin1String("__aurorae__svg__"))) {
        session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("library"), QStringLiteral("org.kde.breeze"));
        session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("theme"), QStringLiteral("Breeze"));
        // Long enough for KWin to drop every Aurorae border: only then does
        // it let go of its QML engine, and so of the aurorae.qml it compiled
        // (a new title-shadow override isn't read otherwise). 500 ms wasn't.
        session.reconfigureKWin();
        QThread::msleep(2000);
    }
    session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("library"), QStringLiteral("org.kde.kwin.aurorae"));
    session.writeConfig(QStringLiteral("kwinrc"), group, QStringLiteral("theme"), QStringLiteral("__aurorae__svg__") + id);
    session.reconfigureKWin();
    // Until KWin says it has it: what comes next (the shell's restart, a
    // look's colours applied again) mustn't catch it half-way -- once, on a
    // fresh install's first apply, windows came out with their titles but
    // without borders or buttons (KWin: an Aurorae theme with no SVG), and
    // applying again mended them.
    const QString theme = QStringLiteral("Theme: __aurorae__svg__") + id;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 10000) {
        const auto info = session.query(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"),
                                        QStringLiteral("supportInformation"));
        if (!info)
            return;   // (no KWin to ask)
        const QString text = info->toString();
        const qsizetype at = text.indexOf(theme);
        if (at >= 0 && (at + theme.size() == text.size() || text.at(at + theme.size()) == QLatin1Char('\n'))) {
            return;
        }
        QThread::msleep(200);
    }
}

// KWin rules for XPlasma's own windows that XP gave no minimize or maximize
// box: the copy dialog (plasmashell's, titled "Copying..." and the like) and
// the editor (XPlasma Plus Properties and its dialogs, and Date and Time
// Properties, property sheets on XP).
// Wayland has no way for a window to say so itself. With neither box, the
// decoration shows Close alone, as XP does. Other rules are left as they are.
const QString kRulesFile = QStringLiteral("kwinrulesrc");
using Rule = QList<std::pair<QString, QString>>;
const Rule kNoBoxes{
    {QStringLiteral("types"), QStringLiteral("33")},            // normal, dialog
    {QStringLiteral("minimize"), QStringLiteral("false")},
    {QStringLiteral("minimizerule"), QStringLiteral("2")},      // force
    {QStringLiteral("maximizehoriz"), QStringLiteral("false")},
    {QStringLiteral("maximizehorizrule"), QStringLiteral("2")},
    {QStringLiteral("maximizevert"), QStringLiteral("false")},
    {QStringLiteral("maximizevertrule"), QStringLiteral("2")},
};
const QList<std::pair<QString, Rule>> kRules{
    {QStringLiteral("xplasma-copy-dialog"),
     Rule{{QStringLiteral("Description"), QStringLiteral("XPlasma: XP's copy dialog (Close only)")},
          {QStringLiteral("wmclass"), QStringLiteral("org.kde.plasmashell")},
          {QStringLiteral("wmclassmatch"), QStringLiteral("1")},      // exact
          {QStringLiteral("title"), QStringLiteral("\\.\\.\\.$")},
          {QStringLiteral("titlematch"), QStringLiteral("3")}}        // regular expression
         + kNoBoxes},
    {QStringLiteral("xplasma-display-properties"),
     Rule{{QStringLiteral("Description"), QStringLiteral("XPlasma: XPlasma Plus Properties, Date and Time Properties (Close only)")},
          {QStringLiteral("wmclass"), QStringLiteral("^fyi\\.hotsocket\\.xplasma(\\.datetime)?$")},
          {QStringLiteral("wmclassmatch"), QStringLiteral("3")}}      // regular expression
         + kNoBoxes},
};

QStringList ruleIds(Session &session)
{
    const QString list = session.readConfig(kRulesFile, {QStringLiteral("General")}, QStringLiteral("rules"));
    return list.isEmpty() ? QStringList() : list.split(QLatin1Char(','));
}

void setRuleIds(Session &session, const QStringList &ids)
{
    session.writeConfig(kRulesFile, {QStringLiteral("General")}, QStringLiteral("rules"), ids.join(QLatin1Char(',')), false);
    session.writeConfig(kRulesFile, {QStringLiteral("General")}, QStringLiteral("count"), QString::number(ids.size()), false);
}

void installWindowRules(Session &session)
{
    QStringList ids = ruleIds(session);
    for (const auto &[id, rule] : kRules) {
        for (const auto &[key, value] : rule)
            session.writeConfig(kRulesFile, {id}, key, value, false);
        if (!ids.contains(id))
            ids << id;
    }
    setRuleIds(session, ids);
}

void removeWindowRules(Session &session)
{
    QStringList ids = ruleIds(session);
    for (const auto &[id, rule] : kRules) {
        ids.removeAll(id);
        for (const auto &[key, value] : rule)
            session.deleteConfig(kRulesFile, {id}, key, false);
    }
    setRuleIds(session, ids);
}

// The fonts, and the running session told: the platform theme only re-reads
// them on refreshFonts, the signal the Fonts settings send.
void applyFonts(Session &session, const QList<ConfigValue> &fonts)
{
    for (const ConfigValue &v : fonts)
        session.writeConfig(v.file, v.group.split(QLatin1Char('/')), v.key, v.value);
    session.signal(QStringLiteral("/KDEPlatformTheme"), QStringLiteral("org.kde.KDEPlatformTheme"), QStringLiteral("refreshFonts"));
    session.reconfigureKWin();
}

// This XPlasma's editor (XPlasma Plus Properties, Date and Time Properties),
// beside this program; or whichever is on the PATH.
QString editorPath()
{
    const QString beside = QCoreApplication::applicationDirPath() + QLatin1String("/xplasma-edit");
    return QFileInfo(beside).isExecutable() ? beside : QStringLiteral("xplasma-edit");
}

// Plasma's helper that switches the icon theme: in its libexec, wherever the
// distribution keeps that -- found beside Plasma's own tools first.
QString iconHelper()
{
    QStringList candidates;
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("plasma-apply-colorscheme"));
    if (!tool.isEmpty()) {
        const QDir prefix(QFileInfo(tool).canonicalPath() + QLatin1String("/.."));
        candidates << prefix.filePath(QStringLiteral("libexec/plasma-changeicons"))
                   << prefix.filePath(QStringLiteral("lib/libexec/plasma-changeicons"))
                   << prefix.filePath(QStringLiteral("lib64/libexec/plasma-changeicons"));
        // (Debian's multiarch: lib/x86_64-linux-gnu/libexec)
        for (const QString &triplet : QDir(prefix.filePath(QStringLiteral("lib"))).entryList({QStringLiteral("*-linux-*")}, QDir::Dirs))
            candidates << prefix.filePath(QStringLiteral("lib/%1/libexec/plasma-changeicons").arg(triplet));
    }
    candidates << QStringLiteral("/usr/lib/plasma-changeicons") << QStringLiteral("/usr/lib64/plasma-changeicons")
               << QStringLiteral("/usr/libexec/plasma-changeicons");
    for (const QString &p : std::as_const(candidates))
        if (QFileInfo(p).isExecutable())
            return p;
    return QString();
}

} // namespace

// The system's applications, as desktop file ids (a subfolder's joined on
// with "-", as the spec has it) to their files -- the first found, the
// user's own before the system's.
QVariantMap desktopFiles()
{
    QVariantMap files;
    for (const QString &root : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        QDirIterator it(root, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString id = path.mid(root.size() + 1).replace(QLatin1Char('/'), QLatin1Char('-'));
            if (!files.contains(id))
                files.insert(id, path);
        }
    }
    return files;
}

QString scriptFrom(const QString &name, const QVariantMap &values)
{
    QString text = resourceText(QStringLiteral(":/scripts/") + name + QLatin1String(".js"));
    for (auto it = values.begin(); it != values.end(); ++it) {
        const QJsonValue v = QJsonValue::fromVariant(it.value());
        QString json;
        if (v.isObject())
            json = QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
        else if (v.isArray())
            json = QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
        else   // (a scalar, as JSON: wrap it in an array and take it out again)
            json = QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
        text.replace(QLatin1String("${") + it.key() + QLatin1Char('}'), json);
    }
    return text;
}

QStringList generatedPackages(const QStringList &keep)
{
    QStringList found;
    const struct { QString dir; QStringList meta; } kinds[] = {
        {share(QStringLiteral("aurorae/themes")), {QStringLiteral("metadata.json"), QStringLiteral("metadata.desktop")}},
        {share(QStringLiteral("plasma/desktoptheme")), {QStringLiteral("metadata.json"), QStringLiteral("metadata.desktop")}},
        {share(QStringLiteral("icons")), {QStringLiteral("xplasma-icons.json"), QStringLiteral("xp2plasma-icons.json")}},
    };
    for (const auto &k : kinds)
        for (const QFileInfo &pkg : QDir(k.dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (keep.contains(pkg.absoluteFilePath()) || pkg.isSymLink())
                continue;
            for (const QString &m : k.meta)
                if (ours(pkg.filePath() + QLatin1Char('/') + m)) {
                    found << pkg.absoluteFilePath();
                    break;
                }
        }
    for (const QFileInfo &f : QDir(share(QStringLiteral("color-schemes"))).entryInfoList({QStringLiteral("*.colors")}, QDir::Files))
        if (!keep.contains(f.absoluteFilePath()) && ours(f.absoluteFilePath()))
            found << f.absoluteFilePath();
    return found;
}

// KSvg's elements cache (~/.cache/ksvg-elements) started with the size
// variants ("16-16-shadow-center") of the theme's SVGs in `widgets`: KSvg
// learns them only as it parses an SVG, after the first process to draw one
// has already chosen what to draw -- and after an apply that process is the
// shell (the check box's box, button.svg's 16px shadow, came up without it).
// Each SVG's group carries its file's time, as KSvg keeps it.
static void primeSizeHints(const QString &widgets, const QString &cacheFile)
{
    static const QRegularExpression element(QStringLiteral("<image id=\"([^\"]+)\"[^>]*? width=\"(\\d+)\" height=\"(\\d+)\""));
    static const QRegularExpression variant(QStringLiteral("^(\\d+)-(\\d+)-(.+)$"));
    QString out;
    for (const QFileInfo &svg : QDir(widgets).entryInfoList({QStringLiteral("*.svg")}, QDir::Files, QDir::Name)) {
        QFile f(svg.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(f.readAll());
        QMap<QString, QString> sizes;   // element -> "WxH," for it and each variant
        QSet<QString> varied;
        for (auto it = element.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            QString id = m.captured(1);
            if (const auto v = variant.match(id); v.hasMatch()) {
                id = v.captured(3);
                varied << id;
            }
            sizes[id] += m.captured(2) + QLatin1Char('x') + m.captured(3) + QLatin1Char(',');
        }
        if (varied.isEmpty())
            continue;
        out += QLatin1Char('[') + svg.absoluteFilePath() + QStringLiteral("]\nLastModified=")
               + QString::number(svg.lastModified().toSecsSinceEpoch()) + QLatin1Char('\n');
        for (const QString &id : std::as_const(varied))
            out += id + QLatin1Char('=') + sizes.value(id) + QLatin1Char('\n');
        out += QLatin1Char('\n');
    }
    if (out.isEmpty())
        return;
    // Preserve unrelated applications' warm SVG entries. Only replace the
    // sections belonging to these generated files.
    QFile existing(cacheFile);
    QString retained;
    if(existing.open(QIODevice::ReadOnly)) {
        bool keep=true;
        for(const auto &line:QString::fromUtf8(existing.readAll()).split(QLatin1Char('\n'))) {
            if(line.startsWith(QLatin1Char('[')))keep=!line.startsWith(QLatin1Char('[')+widgets+QLatin1Char('/'));
            if(keep)retained+=line+QLatin1Char('\n');
        }
    }
    QSaveFile f(cacheFile);
    if (f.open(QIODevice::WriteOnly) && f.write((retained+out).toUtf8()) >= 0)
        f.commit();   // (a cache: failing to prime it isn't failing)
}

bool applyStartButton(Session &session, const Look &look, QString *error)
{
    const StartButtonExtras extras = StartButtonExtras::fromJson(State::load().settings.value(QLatin1String(kStartButtonKey)).toObject());
    const QString startDir = paths::data() + QLatin1String("/start/") + look.id;
    QDir(startDir).removeRecursively();
    const QJsonObject theme = writeStartTheme(look, startDir, error, extras);
    if (theme.isEmpty())
        return false;
    session.script(scriptFrom(QStringLiteral("start-theme"),
                              {{QStringLiteral("START"), QLatin1String(kStartPlasmoidId)}, {QStringLiteral("THEME"), theme.toVariantMap()}}));
    // (The Start menu now draws from this folder, until the next look.)
    State state = State::load();
    state.settings.insert(QLatin1String(kStartButtonLookKey), look.id);
    state.save();
    return true;
}

bool applyWidgets(Session &session, const Look &look, const std::function<void(const QString &)> &progress, QString *error)
{
    State state = State::load();
    QStringList log;
    const auto apps = darkmode::applicationLook(look, state.settings.value(QStringLiteral("darkMode")).toBool(), error);
    if (!apps) return false;
    const bool ok = qtstyle::apply(session, *apps, state, &log, error);
    for (const QString &line : std::as_const(log))
        if (progress)
            progress(QStringLiteral("  ") + line);
    state.save();
    return ok;
}

// "[A][B C][D]" -> {"A", "B C", "D"}
static QStringList configGroups(const QString &header)
{
    return header.trimmed().mid(1).chopped(1).split(QLatin1String("]["));
}

void settleTaskbars(Session &session, int height)
{
    auto lines = [](const QString &name) {
        QFile f(paths::config() + QLatin1Char('/') + name);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).split(QLatin1Char('\n')) : QStringList();
    };
    // The bottom panels (location 4), from the layout.
    QSet<QString> bottom;
    QString containment;
    bool panel = false, atBottom = false;
    auto finish = [&] {
        if (!containment.isEmpty() && panel && atBottom)
            bottom.insert(containment);
    };
    for (const QString &line : lines(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"))) {
        if (line.startsWith(QLatin1Char('['))) {
            finish();
            const QStringList g = configGroups(line);
            containment = g.size() == 2 && g.at(0) == QLatin1String("Containments") ? g.at(1) : QString();
            panel = atBottom = false;
        } else if (!containment.isEmpty()) {
            panel |= line.trimmed() == QLatin1String("plugin=org.kde.panel");
            atBottom |= line.trimmed() == QLatin1String("location=4");
        }
    }
    finish();
    // Their views, as the shell keeps them: per panel, its defaults, and per
    // screen size (HorizontalNNNN).
    const QString file = QStringLiteral("plasmashellrc");
    QSet<QString> seen;
    for (const QString &line : lines(file)) {
        if (!line.startsWith(QLatin1String("[PlasmaViews][Panel ")))
            continue;
        const QStringList g = configGroups(line);
        if (g.size() < 2 || !bottom.contains(g.at(1).mid(6)) || seen.contains(g.join(QLatin1Char('/'))))
            continue;
        seen.insert(g.join(QLatin1Char('/')));
        const QString sub = g.value(2);
        if (sub == QLatin1String("Defaults")) {
            session.writeConfig(file, g, QStringLiteral("thickness"), QString::number(height), false);
            continue;
        }
        if (!sub.isEmpty() && !sub.startsWith(QLatin1String("Horizontal")))
            continue;
        session.writeConfig(file, g, QStringLiteral("floating"), QStringLiteral("0"), false);
        session.writeConfig(file, g, QStringLiteral("alignment"), QStringLiteral("1"), false);   // Qt::AlignLeft
        if (!sub.isEmpty()) {
            session.writeConfig(file, g, QStringLiteral("panelLengthMode"), QStringLiteral("0"), false);   // fill
            session.writeConfig(file, g, QStringLiteral("panelOpacity"), QStringLiteral("1"), false);      // opaque
        }
    }
}

QStringList cannotTake(Session &session, bool taskbar, std::optional<forks::Choice> *chosen, bool properties, bool decoration)
{
    // (Widgets this Plasma builds in come from XPlasma's builds of them, if
    // one for this Plasma loads here.)
    QStringList cannot;
    QString why;
    if (taskbar && !overrides::checkPanelWidgets(share(QStringLiteral("plasma/plasmoids")), &why))
        cannot << why.split(QLatin1Char('\n'));
    if (const QStringList builtIn = overrides::builtInPanelWidgets(); taskbar && !builtIn.isEmpty()) {
        const auto builds = forks::choose(forks::runningPlasma(session), builtIn, &why);
        if (!builds)
            cannot << why;
        if (chosen)
            *chosen = builds;
    }
    if (properties && !overrides::checkDesktopMenu(share(QStringLiteral("plasma/plasmoids")), &why))
        cannot << why.split(QLatin1Char('\n'));
    if (decoration && !overrides::checkTitleShadows(share(QStringLiteral("kwin/aurorae/aurorae.qml")), &why))
        cannot << why;
    return cannot;
}

bool lookOutdated(const State &state)
{
    return !state.appliedKind.isEmpty() && state.settings.value(QLatin1String(kAppliedWithKey)).toInt() < project::kVersion;
}

namespace {
// What XP's taskbar still lacks on Plasma's bottom panel, by name ("the
// clock, the Start button"); empty once it has all of it. (Plasma's shell
// not answering is all of it.)
QString missingTaskbar(Session &session)
{
    const auto missing = session.script(QStringLiteral(
        "// XPLASMA_TASKBAR_CHECK\n"
        "var want = {'%1': 'Start button', 'org.kde.plasma.taskmanager': 'task buttons', '%2': 'clock'};\n"
        "var bottom = panels().filter(function (p) { return p.location === 'bottom'; });\n"
        "print(bottom.length ? Object.keys(want).filter(function (t) {\n"
        "    return !bottom.some(function (p) { return p.widgets(t).length > 0; }); })\n"
        "    .map(function (t) { return want[t]; }).join(', ') : 'bottom panel');")
        .arg(QLatin1String(kStartPlasmoidId), QLatin1String(kClockPlasmoidId)));
    return missing ? missing->trimmed() : QStringLiteral("Start button, task buttons, clock");
}

QByteArray styleDigest(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly)?QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex():QByteArray();
}
QByteArray neutralDigest(const QString &path,const QString &variant)
{
    const auto meta=editor::colorMetadata(path,variant);
    if(meta.isEmpty()) {
        const auto theme=Theme::load(path,variant);
        if(!theme || theme->property(u"xplasmacustomluna",u"",u"",u"accent"))return {};
        return styleDigest(path);
    }
    const auto hash=meta.value("sourceHash").toString().toLatin1();
    return hash.isEmpty()?styleDigest(meta.value("source").toString()):hash;
}
bool sameColorBase(const Look &look,const State &state)
{
    if(lookOutdated(state))return false;
    if(look.classic()) {
        if(state.appliedKind!=QLatin1String("classic"))return false;
        const auto old=schemes::load(state.scheme);if(!old)return false;
        const QString previousBase=old->shellBaseName.isEmpty()?old->name:old->shellBaseName;
        const QString nextBase=look.system.shellBaseName.isEmpty()?look.system.name:look.system.shellBaseName;
        return previousBase==nextBase && old->metrics==look.system.metrics && old->icons==look.system.icons;
    }
    if(state.appliedKind!=QLatin1String("style") || state.variant!=look.variant)return false;
    const auto digest=neutralDigest(look.stylePath,look.variant);
    return !digest.isEmpty() && digest==neutralDigest(state.style,state.variant);
}
QString versionedId(const Look &look,const State &state)
{
    QByteArray data=styleDigest(look.stylePath)+look.variant.toUtf8()+QJsonDocument(state.settings.value(QLatin1String(kStartButtonKey)).toObject()).toJson();
    if(look.classic()) {ThemeFile profile;look.system.writeTo(profile);data+=profile.toBytes();}
    // Increment when the generated SVG/layout format changes.
    const QString hash=QString::fromLatin1(QCryptographicHash::hash(QByteArray(data+"assets-v2"),QCryptographicHash::Sha256).toHex().left(24));
    QColor active(Qt::transparent), inactive(Qt::transparent);int x=0,y=0;
    if(look.renderer) {
        const PartRef ref{QStringLiteral("window"),schema::partId(u"window",u"caption"),1,{}};
        if(look.renderer->property(ref,u"textshadowtype").value_or(QString()).compare("single",Qt::CaseInsensitive)==0) {
            QRgb a=qRgb(0,0,0),i=a;
            parseColour(look.renderer->property(ref,u"textshadowcolor").value_or(QString()),&a);
            auto other=ref;other.state=2;
            parseColour(look.renderer->property(other,u"textshadowcolor").value_or(QString()),&i);
            active=QColor(a);inactive=QColor(i);
            const auto xy=look.renderer->property(ref,u"textshadowoffset").value_or("1, 1").split(',');x=xy.value(0).trimmed().toInt();y=xy.value(1,xy.value(0)).trimmed().toInt();
        }
    }
    return "xplasma-v2-"+hash+"-s"+active.name(QColor::HexArgb).mid(1)+'_'+inactive.name(QColor::HexArgb).mid(1)+'_'+QString::number(x)+'_'+QString::number(y);
}
// What a generated look was built from: its style, or "classic:" and its scheme.
QString lookSource(const Look &look)
{
    return look.classic() ? QStringLiteral("classic:") + look.system.name : QFileInfo(look.stylePath).absoluteFilePath();
}
QString appliedSource(const State &state)
{
    if (state.appliedKind == QLatin1String("classic")) return QStringLiteral("classic:") + state.scheme;
    return state.style.isEmpty() ? QString() : QFileInfo(state.style).absoluteFilePath();
}
QString customLunaDir()
{
    return paths::data() + QStringLiteral("/custom-luna/");
}
// Custom colors: a recoloured style, or a Classic scheme the editor named.
bool customScheme(const QString &name)
{
    static const QRegularExpression custom(QStringLiteral("^Custom .+ [0-9a-f]{10}$"));
    return custom.match(name).hasMatch();
}
bool customSource(const QString &source)
{
    return source.startsWith(customLunaDir()) || (source.startsWith(QLatin1String("classic:")) && customScheme(source.mid(8)));
}
void removeLook(const QString &id)
{
    QDir(share(QStringLiteral("aurorae/themes/")) + id).removeRecursively();
    QDir(share(QStringLiteral("plasma/desktoptheme/")) + id + QStringLiteral("-taskbar")).removeRecursively();
    QFile::remove(share(QStringLiteral("color-schemes/")) + id + QStringLiteral(".colors"));
    QFile::remove(paths::home() + QStringLiteral("/.cache/plasma_theme_") + id + QStringLiteral("-taskbar.kcache"));
    QDir(paths::data() + QStringLiteral("/start/") + id).removeRecursively();
    for (const auto &suffix : {QString(), QStringLiteral("-dark-applications")})
        QDir(share(QStringLiteral("themes/XPlasma-")) + id + suffix).removeRecursively();
}

}

void pruneGeneratedLooks(State &state, const QString &previousId, const QString &previousSource)
{
    const QString activeId = state.settings.value(QStringLiteral("appliedPackageId")).toString();
    // Styles to keep: the saved themes', the applied ones, and the originals
    // the custom ones among them were recoloured from.
    QSet<QString> styles;
    for (const QString &style : presets::savedStyles()) styles.insert(style);
    const QStringList applied{appliedSource(state), previousSource};
    for (const QString &source : applied)
        if (!source.isEmpty()) styles.insert(source);
    for (const QString &style : QStringList(styles.begin(), styles.end()))
        if (style.startsWith(customLunaDir()))
            if (const QString base = editor::colorMetadata(style).value(QStringLiteral("source")).toString(); !base.isEmpty())
                styles.insert(QFileInfo(base).absoluteFilePath());
    const auto wanted = [&](const QString &source) { return !customSource(source) || styles.contains(source); };

    // Looks, by id, from every place one is installed; each with its last use.
    QHash<QString, QDateTime> looks;
    const auto found = [&](const QFileInfo &f, QString id) {
        if (!id.startsWith(QLatin1String("xplasma-v2-"))) return;
        looks[id] = qMax(looks.value(id), f.lastModified());
    };
    const QStringList prefix{QStringLiteral("xplasma-v2-*")};
    for (const QFileInfo &f : QDir(share(QStringLiteral("aurorae/themes"))).entryInfoList(prefix, QDir::Dirs | QDir::NoDotAndDotDot))
        found(f, f.fileName());
    for (const QFileInfo &f : QDir(share(QStringLiteral("plasma/desktoptheme"))).entryInfoList({QStringLiteral("xplasma-v2-*-taskbar")}, QDir::Dirs | QDir::NoDotAndDotDot))
        found(f, f.fileName().chopped(8));
    for (const QFileInfo &f : QDir(share(QStringLiteral("color-schemes"))).entryInfoList({QStringLiteral("xplasma-v2-*.colors")}, QDir::Files))
        found(f, f.fileName().chopped(7));
    for (const QFileInfo &f : QDir(paths::data() + QStringLiteral("/start")).entryInfoList(prefix, QDir::Dirs | QDir::NoDotAndDotDot))
        found(f, f.fileName());
    for (const QFileInfo &f : QDir(share(QStringLiteral("themes"))).entryInfoList({QStringLiteral("XPlasma-xplasma-v2-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
        found(f, f.fileName().mid(8).remove(QRegularExpression(QStringLiteral("-dark-applications$"))));
    // (Build staging is all installed by now: none of it is kept.)
    for (const QFileInfo &f : QDir(paths::build()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        QDir(f.absoluteFilePath()).removeRecursively();
    // Start buttons by older names (and retired live previews) go, but for
    // the one a Start-button-only apply left the menu drawing from.
    const QString startLook = state.settings.value(QLatin1String(kStartButtonLookKey)).toString();
    for (const QFileInfo &f : QDir(paths::data() + QStringLiteral("/start")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (!f.fileName().startsWith(QLatin1String("xplasma-v2-")) && f.fileName() != startLook)
            QDir(f.absoluteFilePath()).removeRecursively();

    QSet<QString> referenced;
    for(const QString name:{"plasmarc","kdeglobals","kwinrc","plasma-org.kde.plasma.desktop-appletsrc","gtk-3.0/settings.ini","gtk-4.0/settings.ini"}) {
        QFile config(paths::config()+QLatin1Char('/')+name);
        if(!config.open(QIODevice::ReadOnly))continue;
        auto matches=QRegularExpression(QStringLiteral("xplasma-v2-[A-Za-z0-9_-]+")).globalMatch(QString::fromUtf8(config.readAll()));
        while(matches.hasNext()) {
            QString id=matches.next().captured();
            if(id.endsWith(QLatin1String("-dark-applications")))id.chop(18);
            if(id.endsWith(QLatin1String("-taskbar")))id.chop(8);
            referenced.insert(id);
        }
    }
    referenced.insert(state.settings.value(QLatin1String(kStartButtonLookKey)).toString());
    QJsonObject sources = state.settings.value(QLatin1String(kGeneratedLooksKey)).toObject();
    QList<std::pair<QDateTime, QString>> cached;
    for (auto it = looks.cbegin(); it != looks.cend(); ++it) {
        const QString &id = it.key();
        if (id == activeId || id == previousId || referenced.contains(id)) continue;
        // (Without a record, a look predates them: built again when chosen.)
        const QString source = sources.value(id).toString();
        if (source.isEmpty() || !wanted(source)) {
            removeLook(id);
            sources.remove(id);
        } else if (!customSource(source)) {
            cached.append({it.value(), id});
        }
    }
    std::sort(cached.begin(), cached.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (qsizetype i = 32; i < cached.size(); ++i) {
        removeLook(cached.at(i).second);
        sources.remove(cached.at(i).second);
    }
    for (const QString &id : sources.keys())
        if (!looks.contains(id)) sources.remove(id);
    state.settings.insert(QLatin1String(kGeneratedLooksKey), sources);

    // The recoloured styles themselves, and their dark-application builds.
    for (const QFileInfo &f : QDir(customLunaDir()).entryInfoList({QStringLiteral("*.msstyles")}, QDir::Files))
        if (!styles.contains(f.absoluteFilePath())) {
            QFile::remove(f.absoluteFilePath());
            QFile::remove(f.absoluteFilePath() + QStringLiteral(".json"));
        }
    darkmode::pruneCache(wanted);
    // Classic custom colors are saved as schemes when applied.
    for (const QFileInfo &f : QDir(paths::schemes()).entryInfoList({QStringLiteral("*.theme")}, QDir::Files))
        if (const auto file = ThemeFile::load(f.absoluteFilePath()))
            if (const QString name = file->value(u"Theme", u"DisplayName"); customScheme(name) && !wanted(QStringLiteral("classic:") + name))
                QFile::remove(f.absoluteFilePath());
}

bool applyLook(Session &session, const Look &requestedLook, const ApplyOptions &requestedOptions, QString *error)
{
    QElapsedTimer elapsed;elapsed.start();
    ApplyOptions o=requestedOptions;
    State state=State::load();
    Look look=requestedLook;
    if(!o.themeFile.isEmpty())if(const auto preset=ThemeFile::load(o.themeFile)){
        const auto colors=QJsonDocument::fromJson(preset->value(u"XPlasma",u"LiveColors").toUtf8()).object();
        bool rebuild=colors.value("rendering").toString()!=QLatin1String("static");
        if(!rebuild){
            QFile metadata(look.stylePath+QStringLiteral(".json"));
            if(metadata.open(QIODevice::ReadOnly)){
                const auto baked=QJsonDocument::fromJson(metadata.readAll()).object();
                rebuild=baked.value("generator").toInt()<editor::kCustomLunaGenerator && QFileInfo::exists(baked.value("source").toString());
            }
        }
        if(colors.value("enabled").toBool() && rebuild){
            auto baked=editor::staticColorLook(look,colors,error);if(!baked)return false;look=*baked;
            // A saved theme names its rebuilt style, so keeps it (and the old one goes).
            if(colors.value("rendering").toString()==QLatin1String("static") && presets::owned(o.themeFile)){
                auto saved=*preset;saved.setValue(u"VisualStyles",u"Path",QFileInfo(look.stylePath).absoluteFilePath());saved.save(o.themeFile);
            }
        }
    }
    const bool colorOnly=o.allowColorOnly && sameColorBase(look,state)
        && (!o.taskbar || session.script(QStringLiteral("// XPLASMA_EXISTING_PANEL\nprint(panels().some(function(p){return p.location==='bottom' && p.widgets().some(function(w){return w.type==='fyi.hotsocket.xplasma.start';});}) ? 'ready' : 'missing');")).value_or(QString()).trimmed()==QLatin1String("ready"))
        && QFileInfo::exists(share(QStringLiteral("kwin/aurorae/aurorae.qml")))
        && session.readConfig(QStringLiteral("kdeglobals"),{QStringLiteral("KDE")},QStringLiteral("widgetStyle")).compare(QLatin1String(qtstyle::kStyleKey),Qt::CaseInsensitive)==0;
    // Every asset path identifies its content, even for built-in styles.
    look.id=versionedId(look,state);
    if(colorOnly) {
        o.properties=false;o.fonts=false;o.icons=false;o.sounds=false;
        report(o,QStringLiteral("Updating colors using the installed theme runtime ..."));
    }
    // The whole XP look or none of it: what this Plasma can't take is known
    // before anything changes.
    std::optional<forks::Choice> builds;
    if (const QStringList cannot = cannotTake(session, o.taskbar && !colorOnly, &builds, o.properties, o.decoration); !cannot.isEmpty())
        return fail(error, QStringLiteral("this Plasma can't take the whole XP look yet, so nothing was changed:\n  ")
                               + cannot.join(QStringLiteral("\n  ")));
    const bool dark = o.darkMode.value_or(state.settings.value(QStringLiteral("darkMode")).toBool());
    const auto applications = darkmode::applicationLook(look, dark, error);
    if (!applications) return false;
    const auto shellArt=panelArt(look);
    const QString build = paths::build() + QLatin1Char('/') + look.id;
    QDir(build).removeRecursively();

    const QString auroraeDest = share(QStringLiteral("aurorae/themes/")) + look.id;
    const QString panelDest = share(QStringLiteral("plasma/desktoptheme/")) + look.id + QLatin1String("-taskbar");
    const QString activeTaskbar = session.readConfig(QStringLiteral("plasmarc"), {QStringLiteral("Theme")}, QStringLiteral("name"));
    const QString taskbarSlot = QLatin1String(activeTaskbar == QLatin1String(kTaskbarSlots[0]) ? kTaskbarSlots[1] : kTaskbarSlots[0]);
    const QString slotDest = share(QStringLiteral("plasma/desktoptheme/")) + taskbarSlot;
    // --- generate ------------------------------------------------------------
    report(o, QStringLiteral("Generating %1 ...").arg(look.name));
    // (With XPlasma's own decoration KWin draws the look from xplasmarc: no
    // Aurorae theme is made for it, or installed.)
    const bool nativeDecoration = o.decoration && nativeDecorationUsable(session);
    const QString decoration = nativeDecoration || QFileInfo::exists(auroraeDest) ? auroraeDest
                                                                                 : writeDecoration(look, build + QLatin1String("/aurorae"), error);
    if (decoration.isEmpty())
        return false;
    const QString panel = o.taskbar ? (QFileInfo::exists(panelDest) ? panelDest : writePanelTheme(look, build + QLatin1String("/desktoptheme"), error)) : QString();
    if (o.taskbar && panel.isEmpty())
        return false;
    const QString startDir = paths::data() + QLatin1String("/start/") + look.id;
    const QString startStaging = build + QLatin1String("/start");
    QJsonObject startTheme;
    if(o.taskbar) {
        QFile cached(startDir+QStringLiteral("/theme.json"));
        if(cached.open(QIODevice::ReadOnly))startTheme=QJsonDocument::fromJson(cached.readAll()).object();
    }
    if (o.taskbar && startTheme.isEmpty()) {
        startTheme = writeStartTheme(look, startStaging, error,
                                     StartButtonExtras::fromJson(state.settings.value(QLatin1String(kStartButtonKey)).toObject()));
        if (startTheme.isEmpty()) return false;
        const QString from = QUrl::fromLocalFile(startStaging + QLatin1Char('/')).toString();
        const QString to = QUrl::fromLocalFile(startDir + QLatin1Char('/')).toString();
        std::function<QJsonValue(QJsonValue)> relocate = [&](QJsonValue value) -> QJsonValue {
            if (value.isString() && value.toString().startsWith(from)) return QString(to + value.toString().mid(from.size()));
            if (value.isObject()) {
                auto object = value.toObject();
                for (auto it = object.begin(); it != object.end(); ++it) it.value() = relocate(it.value());
                return object;
            }
            if (value.isArray()) {
                auto array = value.toArray();
                for (int i = 0; i < array.size(); ++i) array[i] = relocate(array[i]);
                return array;
            }
            return value;
        };
        startTheme = relocate(startTheme).toObject();
        QSaveFile themeJson(startStaging + QLatin1String("/theme.json"));
        if (!themeJson.open(QIODevice::WriteOnly) || themeJson.write(QJsonDocument(startTheme).toJson()) < 0 || !themeJson.commit())
            return fail(error, themeJson.errorString());
    }
    QString iconTheme;
    if (o.icons) {
        QString previous = state.settings.value(QStringLiteral("previousIconTheme")).toString();
        const QString current = session.readConfig(QStringLiteral("kdeglobals"), {QStringLiteral("Icons")}, QStringLiteral("Theme"));
        if (previous.isEmpty() && current != QLatin1String(kIconThemeId) && current != QLatin1String("xp2plasma-icons"))
            previous = current.isEmpty() ? QStringLiteral("breeze") : current;
        if (!previous.isEmpty())
            state.settings.insert(QStringLiteral("previousIconTheme"), previous);
        QString why;
        // (the tray's icons XP has no art for, in the tray's text colour)
        iconTheme = writeIconTheme(build + QLatin1String("/icons"), previous, shellArt.trayTextColour, &why, spinIcons(look));
        if (iconTheme.isEmpty())
            report(o, QStringLiteral("  no XP icons: %1").arg(why));
    }

    const QString colorsDest = share(QStringLiteral("color-schemes/")) + look.id + QLatin1String(".colors");
    const QString iconsDest = share(QStringLiteral("icons/")) + QLatin1String(kIconThemeId);
    QString why;
    QList<std::pair<QString, QString>> orders;
    const bool existingPanel = o.taskbar && session.script(QStringLiteral(
        "// XPLASMA_EXISTING_PANEL\n"
        "var ready = panels().some(function(p) { return p.location === 'bottom' && "
        "p.widgets().some(function(w) { return w.type === 'fyi.hotsocket.xplasma.start'; }); }); "
        "print(ready ? 'ready' : 'missing');")).value_or(QString()).trimmed() == QLatin1String("ready");
    const auto prepareLayout = [&] {
        // The clock in the style's clock font (Classic: the status font).
        LogFont clock = look.system.metrics.statusFont;
        if (!look.classic()) {
            const QString spec = look.renderer->property({QStringLiteral("clock"), 0, 0, QStringLiteral("traynotify")}, u"font")
                                     .value_or(QStringLiteral("Tahoma, 8"));
            const QStringList parts = spec.split(QLatin1Char(','));
            clock = LogFont::fromPoints(parts.value(0).trimmed(), parts.value(1).trimmed().toInt(), spec.contains(QLatin1String("bold"), Qt::CaseInsensitive));
        }
        const auto layout = session.script(scriptFrom(QStringLiteral("layout"),
                                                      {{QStringLiteral("START"), QLatin1String(kStartPlasmoidId)},
                                                       {QStringLiteral("THEME"), startTheme.toVariantMap()},
                                                       {QStringLiteral("CLOCK_FAMILY"), clock.face},
                                                       {QStringLiteral("CLOCK_POINTS"), clock.points()},
                                                       {QStringLiteral("CLOCK_WEIGHT"), clock.weight},
                                                       {QStringLiteral("CLOCK"), QLatin1String(kClockPlasmoidId)},
                                                       {QStringLiteral("DESKTOP_FILES"), desktopFiles()},
                                                       {QStringLiteral("CLOCK_COLOR"), shellArt.trayTextColour.name()},
                                                       {QStringLiteral("CLOCK_COMMAND"), overrides::launchCommand(editorPath(), {QStringLiteral("--date-time")})}}),
                                           &why);
        if (!layout) {
            report(o, QStringLiteral("  the panel wasn't rearranged: %1").arg(why));
        } else {
            const QJsonObject result = QJsonDocument::fromJson(layout->toUtf8()).object();
            for (const QJsonValue &line : result.value(QStringLiteral("log")).toArray())
                report(o, QStringLiteral("  ") + line.toString());
            // Plasma's scripting can't reorder a panel: its AppletOrder is
            // written with the shell down (it saves its own on the way out).
            for (const QJsonValue &p : result.value(QStringLiteral("panels")).toArray()) {
                QStringList ids;
                for (const QJsonValue &id : p.toObject().value(QStringLiteral("order")).toArray())
                    ids << QString::number(id.toInt());
                const QString panelNumber = QString::number(p.toObject().value(QStringLiteral("panel")).toInt());
                const QString wanted = ids.join(QLatin1Char(';'));
                if (session.readConfig(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"),
                                       {QStringLiteral("Containments"), panelNumber, QStringLiteral("General")}, QStringLiteral("AppletOrder"))
                    != wanted)
                    orders.append({panelNumber, wanted});
            }
        }
    };
    if (existingPanel && !colorOnly) prepareLayout();
    // KSvg retains renderers for the active theme. Never replace those files,
    // packages or plugins, or announce new colors, while the shell uses them.
    const auto installAndApply = [&]() -> bool {
        // --- install ---------------------------------------------------------------
        report(o, QStringLiteral("Installing ..."));
        if (look.classic() && look.system.shellColors && !schemes::save(look.system, nullptr, error)) return false;
        if (!nativeDecoration && !QFileInfo::exists(auroraeDest) && !install(decoration, auroraeDest, error))
            return false;
        QDir().mkpath(QFileInfo(colorsDest).absolutePath());
        {
            QSaveFile f(colorsDest);
            const auto &art=shellArt;
            std::pair<QColor,QColor> complementary{art.panelColour,art.textColour};
            if((look.classic() && look.system.shellColors) || (look.renderer && look.renderer->theme().property(u"xplasmacustomluna",u"",u"",u"accent")))
                complementary={look.system.colour(u"Hilight"),look.system.colour(u"HilightText")};
            if (!f.open(QIODevice::WriteOnly) || f.write(colorScheme(applications->system, look.id, complementary)) < 0 || !f.commit())
                return fail(error, QStringLiteral("%1: %2").arg(colorsDest, f.errorString()));
        }
        if (o.taskbar) {
            if ((!QFileInfo::exists(startDir) && !install(startStaging, startDir, error)) || (!QFileInfo::exists(panelDest) && !install(panel, panelDest, error))
                || !fillTaskbarSlot(panelDest, taskbarSlot, error) || (!colorOnly && (!installStartPlasmoid(share(QStringLiteral("plasma/plasmoids")), error)
                || !installClockPlasmoid(share(QStringLiteral("plasma/plasmoids")), error))))
                return false;
            if (!colorOnly && !overrides::installPanelWidgets(share(QStringLiteral("plasma/plasmoids")), &why))
                return fail(error, QStringLiteral("the XP widgets could not be installed; stock widgets are not a substitute:\n%1").arg(why));
            if (builds) {
                if (!forks::install(session, *builds, state, error))
                    return false;
                report(o, QStringLiteral("  Plasma's %1 as XPlasma builds them (Plasma %2)")
                              .arg(QStringList(builds->plugins.keys()).join(QStringLiteral(", ")), builds->series));
            } else if(!colorOnly) {
                forks::remove(state);   // (none wanted: an older Plasma's, if any, go)
            }
        }
        if (!iconTheme.isEmpty() && !install(iconTheme, iconsDest, error))
            return false;
        // XP's Properties on the desktop's menu: this XPlasma's editor.
        const QString editor = QCoreApplication::applicationDirPath() + QLatin1String("/xplasma-edit");
        if (!QFileInfo(editor).isExecutable())
            report(o, QStringLiteral("  no XPlasma Plus Properties on the desktop: %1 isn't there").arg(editor));
        else if (o.properties && !overrides::installDesktopMenu(share(QStringLiteral("plasma/plasmoids")), editor, &why))
            report(o, QStringLiteral("  no XPlasma Plus Properties on the desktop: %1").arg(why));
        const QString shadowFile=share(QStringLiteral("kwin/aurorae/aurorae.qml"));
        const QByteArray oldOverride=styleDigest(shadowFile);
        if (o.decoration && !overrides::installTitleShadows(share(QStringLiteral("aurorae/themes")), share(QStringLiteral("kwin/aurorae/aurorae.qml")), &why))
            report(o, QStringLiteral("  no title shadows: %1").arg(why));
        const bool overrideChanged=oldOverride!=styleDigest(shadowFile);
        // --- apply -------------------------------------------------------------------
        report(o, QStringLiteral("Applying ..."));
        // Components are independently selectable by the GUI installer.
        if (o.fonts && !xptext::apply(session, state, &why))
            report(o, QStringLiteral("  text stays as it was: %1").arg(why));
        if (o.decoration && !colorOnly) installWindowRules(session);
        if (o.colours) {
            session.writeConfig(QStringLiteral("kdeglobals"), {QStringLiteral("General")}, QStringLiteral("ColorScheme"), QString(), false);
            if (!session.run({QStringLiteral("plasma-apply-colorscheme"), look.id}).ok())
                report(o, QStringLiteral("  plasma-apply-colorscheme failed"));
        }
        if (o.fonts) applyFonts(session, fontSettings(look.system));
        if (o.decoration) applyDecoration(session, look.id, overrideChanged);
        if (o.widgets) {
            QStringList log;
            if (!qtstyle::apply(session, *applications, state, &log, &why, !colorOnly))
                report(o, QStringLiteral("  Qt apps keep their style: %1").arg(why));
            // (GTK's too: a failure there isn't the look's.)
            if (!gtk::apply(session, *applications, state, &log, &why))
                report(o, QStringLiteral("  GTK apps keep their theme: %1").arg(why));
            for (const QString &line : std::as_const(log))
                report(o, QStringLiteral("  ") + line);
        }
        if (o.taskbar) {
            const QString cache = paths::home() + QLatin1String("/.cache/");
            // (The slot's pixmaps from the look it last held.)
            for (const QString &stale : QDir(cache).entryList({QLatin1String("plasma_theme_") + taskbarSlot + QLatin1String("*.kcache")}, QDir::Files))
                QFile::remove(cache + stale);
            session.run({QStringLiteral("plasma-apply-desktoptheme"), taskbarSlot});
            primeSizeHints(slotDest + QLatin1String("/widgets"), cache + QLatin1String("ksvg-elements"));
            if(!colorOnly)settleTaskbars(session, shellArt.height);
            for (const auto &[panelNumber, order] : orders)
                session.writeConfig(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"),
                                    {QStringLiteral("Containments"), panelNumber, QStringLiteral("General")},
                                    QStringLiteral("AppletOrder"), order, false);
        }
        return true;
    };
    bool installed = false;
    if (o.taskbar && !colorOnly) {
        report(o, QStringLiteral("Stopping Plasma's shell before replacing theme artwork ..."));
        session.restartPlasmaShell([&] { installed = installAndApply(); });
    } else installed = installAndApply();
    if (!installed) return false;
    if(colorOnly && o.taskbar) {
        session.script(scriptFrom(QStringLiteral("start-theme"),{{QStringLiteral("START"),QLatin1String(kStartPlasmoidId)},{QStringLiteral("THEME"),startTheme.toVariantMap()}}));
        // Update only existing clock configuration; leave panel layout alone.
        session.script(QStringLiteral("panels().forEach(function(p){p.widgets('%1').forEach(function(w){w.currentConfigGroup=['General'];w.writeConfig('textColor','%2');});});").arg(QLatin1String(kClockPlasmoidId),shellArt.trayTextColour.name()));
    }
    if (o.taskbar && !colorOnly) {
        // (The shell just restarted: laid out once it has its desktop and
        // panels, or the layout finds none and quietly does nothing.)
        if (!session.ensurePlasmaShell(&why))
            return fail(error, QStringLiteral("the XP taskbar couldn't be set up: %1. Log out and back in, then try again.").arg(why));
        session.script(scriptFrom(QStringLiteral("panel"), {{QStringLiteral("HEIGHT"), shellArt.height}}));
        const auto layOut = [&] {
            orders.clear();
            prepareLayout();
            if (!orders.isEmpty()) {
                session.restartPlasmaShell([&] {
                    for (const auto &[panelNumber, order] : orders)
                        session.writeConfig(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"),
                                            {QStringLiteral("Containments"), panelNumber, QStringLiteral("General")},
                                            QStringLiteral("AppletOrder"), order, false);
                });
            }
        };
        if (!existingPanel)
            layOut();
        // Seen through, not assumed: what the taskbar still lacks, laid out
        // once more after the shell is ready again, and an error if it
        // still lacks it -- never a finished look without its taskbar.
        QString missing = missingTaskbar(session);
        if (!missing.isEmpty()) {
            report(o, QStringLiteral("  the taskbar has no %1 yet; laying it out again").arg(missing));
            if (!session.ensurePlasmaShell(&why))
                return fail(error, QStringLiteral("the XP taskbar couldn't be set up: %1. Log out and back in, then try again.").arg(why));
            layOut();
            missing = missingTaskbar(session);
            if (!missing.isEmpty())
                return fail(error, QStringLiteral("the XP taskbar couldn't be set up: Plasma's bottom panel has no %1. Log out and back in, then try again.").arg(missing));
        }
    }

    // Leave immutable profile assets cached for subsequent switches. Remove
    // legacy packages only after both renderers have switched away from them.
    if(!colorOnly)for(const QString &stale:generatedPackages({auroraeDest,colorsDest,panelDest,iconsDest,share(QStringLiteral("plasma/desktoptheme/"))+QLatin1String(kTaskbarSlots[0]),share(QStringLiteral("plasma/desktoptheme/"))+QLatin1String(kTaskbarSlots[1])})) {
        if(QFileInfo(stale).fileName().startsWith(QLatin1String("xplasma-v2-")))continue;
        if(QFileInfo(stale).isDir())QDir(stale).removeRecursively();else QFile::remove(stale);
    }

    if (o.wallpaper)
        applyWallpaper(session, *o.wallpaper, o.progress);
    if (!iconTheme.isEmpty()) {
        const QString helper = iconHelper();
        if (helper.isEmpty())
            report(o, QStringLiteral("  Plasma's icon helper isn't here; the XP icons are installed but not switched on"));
        else
            session.run({helper, QLatin1String(kIconThemeId)});
    }

    // XP's sounds, as XP had them from the start: the first time, unless
    // they've been turned off. (A failure there isn't the look's.)
    if (o.sounds && sounds::wantedWithLook(state)) {
        if (sounds::apply(session, state, sounds::defaultScheme(), &why))
            report(o, QStringLiteral("Sounds: Windows Default"));
        else
            report(o, QStringLiteral("  XP's sounds stay off: %1").arg(why));
    }

    const QString previousId = state.settings.value(QStringLiteral("appliedPackageId")).toString();
    const QString previousSource = appliedSource(state);
    QDir(build).removeRecursively();   // (staging: all of it is installed)
    state.appliedKind = look.classic() ? QStringLiteral("classic") : QStringLiteral("style");
    state.style = look.stylePath;
    state.variant = look.variant;
    state.scheme = look.classic() ? look.system.name : QString();
    state.themeFile = o.themeFile;
    state.settings.insert(QStringLiteral("appliedPackageId"),look.id);
    state.settings.insert(QLatin1String(kAppliedWithKey), project::kVersion);
    state.settings.insert(QStringLiteral("darkMode"), dark);
    state.settings.remove(QLatin1String(kStartButtonLookKey));
    QJsonObject sources = state.settings.value(QLatin1String(kGeneratedLooksKey)).toObject();
    sources.insert(look.id, lookSource(look));
    state.settings.insert(QLatin1String(kGeneratedLooksKey), sources);
    pruneGeneratedLooks(state, previousId, previousSource);
    state.save();
    // Keep legacy profile data in the .theme file, but never enable overlays.
    if(state.settings.contains(QLatin1String(kLiveStartColorsKey)))state.settings.insert(QStringLiteral("retiredColorProfile"),state.settings.value(QLatin1String(kLiveStartColorsKey)));
    state.settings.remove(QLatin1String(kLiveStartColorsKey));
    state.save();
    darkmode::publish(dark);
    maintenance::run(session,previousId);
    if(!colorOnly && state.settings.value(QStringLiteral("crispText")).toBool())crisptext::apply(session,true);
    report(o, QStringLiteral("  Theme update completed in %1 ms.").arg(elapsed.elapsed()));
    report(o, QStringLiteral("Done."));
    return true;
}

void applyWallpaper(Session &session, const Wallpaper &w, const std::function<void(const QString &)> &progress)
{
    session.script(scriptFrom(QStringLiteral("wallpaper"),
                              {{QStringLiteral("IMAGE"), w.image.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(QUrl::fromLocalFile(w.image).toString())},
                               {QStringLiteral("FILL"), w.fill ? QVariant(*w.fill) : QVariant::fromValue(nullptr)},
                               {QStringLiteral("COLOUR"), w.colour.name()}}));
    if (progress)
        progress(w.image.isEmpty() ? QStringLiteral("Desktop: %1").arg(w.colour.name()) : QStringLiteral("Wallpaper: %1").arg(QFileInfo(w.image).fileName()));
}

bool resetDesktop(Session &session, const std::function<void(const QString &)> &progress, QString *error)
{
    bool paletteChanged=false;
    if(!applyLiveSystemColors({},paths::config(),&paletteChanged,error))return false;
    QFile::remove(paths::config()+QStringLiteral("/xplasma-live-colors.json"));
    if(paletteChanged)session.signal(QStringLiteral("/KGlobalSettings"),QStringLiteral("org.kde.KGlobalSettings"),QStringLiteral("notifyChange"),{0,0});
    auto say = [&](const QString &line) {
        if (progress)
            progress(line);
    };
    State state = State::load();
    QStringList log;
    say(QStringLiteral("Taking XPlasma's widget style off ..."));
    qtstyle::revert(session, state, &log);
    gtk::revert(session, state, &log);
    for (const QString &line : std::as_const(log))
        say(QStringLiteral("  ") + line);
    overrides::removeTitleShadows(share(QStringLiteral("kwin/aurorae/aurorae.qml")));
    removeWindowRules(session);
    // Plasma's own widgets back, in a shell of their own, before Plasma's
    // layout is: the running shell has XPlasma's loaded, and given the
    // default panel it would load Plasma's beside them -- with builds of
    // them, the same QML module registered twice, which Qt won't survive.
    session.restartPlasmaShell([&] {
        overrides::removePanelWidgets(share(QStringLiteral("plasma/plasmoids")));
        overrides::removeDesktopMenu(share(QStringLiteral("plasma/plasmoids")));
        forks::remove(state);
    });

    say(QStringLiteral("Clearing the XP fonts ..."));
    xptext::revert(session, state);
    pointers::revert(session, state);
    sounds::revert(session, state);
    for (const ConfigValue &v : fontSettings(ClassicScheme()))
        session.deleteConfig(v.file, v.group.split(QLatin1Char('/')), v.key);
    session.signal(QStringLiteral("/KDEPlatformTheme"), QStringLiteral("org.kde.KDEPlatformTheme"), QStringLiteral("refreshFonts"));

    // Plasma's own tool does the heavy lifting: the global theme (borders,
    // colours, Plasma style, widget style, icons) and the default panel.
    const QString lookAndFeel = QStringLiteral("org.kde.breeze.desktop");
    say(QStringLiteral("Applying %1 with the default panel ...").arg(lookAndFeel));
    const host::Result r = session.run({QStringLiteral("plasma-apply-lookandfeel"), QStringLiteral("-a"), lookAndFeel, QStringLiteral("--resetLayout")});
    if (!r.ok())
        return fail(error, QStringLiteral("plasma-apply-lookandfeel failed: %1").arg(QString::fromUtf8(r.err).trimmed()));
    session.waitForPlasmaShell();   // (it may restart the shell itself)
    removeNativeDecoration();   // (Breeze's borders are back: nothing uses it)

    // The wallpaper from the global theme's own defaults, set outright.
    for (const QString &root : paths::systemData()) {
        QFile defaults(root + QLatin1String("/plasma/look-and-feel/") + lookAndFeel + QLatin1String("/contents/defaults"));
        if (!defaults.open(QIODevice::ReadOnly))
            continue;
        QString section, image;
        for (const QString &line : QString::fromUtf8(defaults.readAll()).split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1Char('[')))
                section = line.trimmed();
            else if (section == QLatin1String("[Wallpaper]") && line.startsWith(QLatin1String("Image=")))
                image = line.mid(6).trimmed();
        }
        const QString path = image.startsWith(QLatin1Char('/')) ? image : root + QLatin1String("/wallpapers/") + (image.isEmpty() ? QStringLiteral("Next") : image);
        if (QFileInfo::exists(path)) {
            session.script(scriptFrom(QStringLiteral("wallpaper"), {{QStringLiteral("IMAGE"), path},
                                                                     {QStringLiteral("FILL"), QVariant::fromValue(nullptr)},
                                                                     {QStringLiteral("COLOUR"), QStringLiteral("#000000")}}));
            say(QStringLiteral("Wallpaper: %1").arg(QFileInfo(path).fileName()));
        }
        break;
    }

    // The icons the user had.
    const QString previousIcons = state.settings.value(QStringLiteral("previousIconTheme")).toString();
    if (const QString helper = iconHelper(); !helper.isEmpty() && !previousIcons.isEmpty())
        session.run({helper, previousIcons});
    state.settings.remove(QStringLiteral("previousIconTheme"));

    say(QStringLiteral("Removing XPlasma's packages ..."));
    const QStringList packages = generatedPackages();
    for (const QString &p : packages) {
        if (QFileInfo(p).isDir())
            QDir(p).removeRecursively();
        else
            QFile::remove(p);
    }
    const QString start = share(QStringLiteral("plasma/plasmoids/")) + QLatin1String(kStartPlasmoidId);
    QDir(start).removeRecursively();
    QDir(share(QStringLiteral("plasma/plasmoids/")) + QLatin1String(kClockPlasmoidId)).removeRecursively();
    QDir(paths::data() + QLatin1String("/start")).removeRecursively();
    say(QStringLiteral("  %1 removed").arg(packages.size() + 1));

    state.appliedKind.clear();
    state.style.clear();
    state.variant.clear();
    state.scheme.clear();
    state.themeFile.clear();
    state.settings.remove(QLatin1String(kAppliedWithKey));
    state.save();
    session.reconfigureKWin();
    say(QStringLiteral("The desktop is Plasma's default again."));
    return true;
}

} // namespace xpl
