// SPDX-License-Identifier: GPL-3.0-or-later
// QtTest for the app library: archives, host programs, state, adoption.
//
// Every test runs with HOME moved to a scratch directory. The XP install ISO,
// when xp2plasma or XPlasma downloaded one on this machine, is used for the
// real-media tests (skipped otherwise).
#include "app/adopt.h"
#include "app/colors.h"
#include "app/apply.h"
#include "app/editor/customluna.h"
#include "app/startpanel.h"
#include "core/theme.h"
#include "core/render.h"
#include "core/schema.h"
#include "app/darkmode.h"
#include "app/controls.h"
#include "app/overrides.h"
#include "app/session.h"
#include "app/setup.h"
#include "app/decoration.h"
#include "app/look.h"
#include "app/icons.h"
#include "app/media.h"
#include "app/inf.h"
#include "app/panel.h"
#include "app/flatpak.h"
#include "app/forks.h"
#include "app/gtk.h"
#include "app/install.h"
#include "app/pointers.h"
#include "app/project.h"
#include "app/sounds.h"
#include "app/updates.h"
#include "app/timezones.h"
#include "app/startbutton.h"
#include "app/startmenu.h"
#include "app/startpanel.h"
#include "app/archive.h"
#include "app/hivedef.h"
#include "app/host.h"
#include "app/library.h"
#include "app/paths.h"
#include "app/maintenance.h"
#include "app/state.h"
#include "app/themeimport.h"
#include "app/xptext.h"
#include "core/themefile.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QApplication>
#include <QGuiApplication>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QProcess>
#include <QTest>
#include <QElapsedTimer>
#include <ctime>
#include <QTimeZone>
#include <QtEndian>

#include <archive.h>
#include <archive_entry.h>

#include <thread>

using namespace xpl;

namespace {

// A pixel a style's blue made, as custom colors recolour Close and the button
// symbols: blue to mauve, but not the pinks antialiased against magenta.
bool lunaBlueMade(QRgb rgb)
{
    const QColor c(rgb);
    return c.hsvSaturationF() >= .12f && c.hsvHueF() * 360.f >= 180.f && !(c.red() >= 0xe0 && c.blue() >= 0xe0);
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// An archive written with libarchive: `format` is "zip" or "tar.gz".
bool writeArchive(const QString &path, const QString &format, const QList<std::pair<QByteArray, QByteArray>> &members)
{
    ::archive *a = archive_write_new();
    if (format == QLatin1String("zip")) {
        archive_write_set_format_zip(a);
    } else {
        archive_write_set_format_pax_restricted(a);
        archive_write_add_filter_gzip(a);
    }
    if (archive_write_open_filename(a, QFile::encodeName(path).constData()) != ARCHIVE_OK)
        return false;
    for (const auto &[name, data] : members) {
        archive_entry *e = archive_entry_new();
        archive_entry_set_pathname(e, name.constData());
        archive_entry_set_size(e, data.size());
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_write_header(a, e);
        archive_write_data(a, data.constData(), size_t(data.size()));
        archive_entry_free(e);
    }
    archive_write_close(a);
    archive_write_free(a);
    return true;
}

// A minimal ISO 9660 image: sector 16 the primary volume descriptor, 17 the
// terminator, 18 the root directory, 19 "I386/", then file data.
struct IsoBuilder
{
    static constexpr int kSector = 2048;
    QByteArray image = QByteArray(22 * kSector, '\0');

    static QByteArray record(quint32 lba, quint32 size, bool dir, const QByteArray &name, quint8 flags = 0)
    {
        QByteArray r(33 + name.size() + (name.size() % 2 == 0 ? 1 : 0), '\0');
        r[0] = char(r.size());
        qToLittleEndian<quint32>(lba, r.data() + 2);
        qToBigEndian<quint32>(lba, r.data() + 6);
        qToLittleEndian<quint32>(size, r.data() + 10);
        qToBigEndian<quint32>(size, r.data() + 14);
        r[25] = char((dir ? 0x02 : 0) | flags);
        r[32] = char(name.size());
        r.replace(33, name.size(), name);
        return r;
    }
    void put(int sector, const QByteArray &bytes) { image.replace(sector * kSector, bytes.size(), bytes); }

    IsoBuilder(bool loop = false)
    {
        QByteArray pvd(kSector, '\0');
        pvd[0] = 1;
        pvd.replace(1, 5, "CD001");
        pvd[6] = 1;
        pvd.replace(156, 34, record(18, kSector, true, QByteArray(1, '\0')));
        put(16, pvd);
        QByteArray term(kSector, '\0');
        term[0] = char(255);
        term.replace(1, 5, "CD001");
        put(17, term);
        // The root: ".", "..", HELLO.TXT;1, I386/ -- 564 bytes, as XP's own is.
        put(18, record(18, kSector, true, QByteArray(1, '\0')) + record(18, kSector, true, QByteArray(1, '\1'))
                    + record(20, 5, false, "HELLO.TXT;1") + record(19, kSector, true, "I386"));
        QByteArray i386 = record(19, kSector, true, QByteArray(1, '\0')) + record(18, kSector, true, QByteArray(1, '\1'))
                          // a multi-extent file: two records, the first flagged 0x80
                          + record(21, 3, false, "SPLIT.BIN;1", 0x80) + record(21, 3, false, "SPLIT.BIN;1")
                          + record(20, 5, false, "README.;1");
        if (loop)   // a directory claiming to be the root again
            i386 += record(18, kSector, true, "LOOP");
        put(19, i386);
        put(20, "hello");
        put(21, "abc");
    }
};

// A session that only records: what apply would run, script and signal.
class RecordingSession : public Session
{
public:
    QList<QStringList> runs;
    QStringList scripts;
    QStringList calls;
    QHash<QString, QString> config;   // "file|group/group|key" -> value
    QString layoutReply = QStringLiteral("{\"log\":[\"laid out\"],\"panels\":[{\"panel\":1,\"order\":[3,4,5]}]}");
    std::function<void()> onShellStop;
    bool existingPanel = false;
    bool systemd = true;   // (the session's plasmashell a systemd user unit)
    bool runnerActive = false;
    QList<QStringList> detached;
    int shellLoadingFor = 0;                // readiness probes answered "loading" first
    QString activities = QStringLiteral("active");   // the activity manager's unit
    QStringList taskbarChecks;              // what each taskbar check finds missing, in turn ("" after)
    bool shellActive = false;               // plasma-plasmashell.service's is-active
    QString shellActiveSince;               // ...its ActiveEnterTimestampMonotonic, in µs

    host::Result run(const QStringList &argv) override
    {
        if (onShellStop && argv.value(0) == QLatin1String("systemctl") && argv.contains(QStringLiteral("stop"))) onShellStop();
        runs << argv;
        host::Result r;
        r.exitCode = 0;
        if (argv.contains(QStringLiteral("plasma-krunner.service")) && argv.contains(QStringLiteral("is-active")))
            r.out = runnerActive ? "active\n" : "inactive\n";
        if (argv.contains(QStringLiteral("plasma-plasmashell.service")) && argv.contains(QStringLiteral("is-active")))
            r.out = shellActive ? "active\n" : "inactive\n";
        if (argv.contains(QStringLiteral("ActiveEnterTimestampMonotonic")))
            r.out = shellActiveSince.toUtf8() + '\n';
        if (argv.contains(QStringLiteral("plasma-kactivitymanagerd.service"))) {
            if (argv.contains(QStringLiteral("is-active")))
                r.out = activities.toUtf8() + '\n';
            else if (argv.contains(QStringLiteral("start")))
                activities = QStringLiteral("active"), shellLoadingFor = 0;
        }
        if (argv.value(0) == QLatin1String("systemctl") && argv.contains(QStringLiteral("LoadState")))
            r.out = systemd ? "loaded\n" : "not-found\n";
        if (argv.value(0) == QLatin1String("kreadconfig6") || argv.value(0) == QLatin1String("kwriteconfig6")) {
            const int file = int(argv.indexOf(QStringLiteral("--file"))), key = int(argv.indexOf(QStringLiteral("--key")));
            QStringList groups;
            for (int i = 0; i < argv.size() - 1; ++i)
                if (argv.at(i) == QLatin1String("--group"))
                    groups << argv.at(i + 1);
            const QString id = argv.value(file + 1) + QLatin1Char('|') + groups.join(QLatin1Char('/')) + QLatin1Char('|') + argv.value(key + 1);
            if (argv.value(0) == QLatin1String("kreadconfig6"))
                r.out = config.value(id).toUtf8();
            else if (argv.contains(QStringLiteral("--delete")))
                config.remove(id);
            else
                config.insert(id, argv.value(key + 2));
        }
        return r;
    }
    std::optional<QString> script(const QString &js, QString *) override
    {
        scripts << js;
        if (js.contains(QLatin1String("XPLASMA_SHELL_READY")))
            return activities != QLatin1String("active") || shellLoadingFor-- > 0 ? QStringLiteral("loading") : QStringLiteral("up");
        if (js.contains(QLatin1String("XPLASMA_TASKBAR_CHECK")))
            return taskbarChecks.isEmpty() ? QString() : taskbarChecks.takeFirst();
        if (js.contains(QLatin1String("XPLASMA_EXISTING_PANEL")))
            return existingPanel ? QStringLiteral("ready") : QStringLiteral("missing");
        if (js.contains(QLatin1String("LAUNCHER_KEYS")))
            return layoutReply;
        return QString();
    }
    bool call(const QString &, const QString &path, const QString &, const QString &method, const QVariantList &) override
    {
        calls << path + QLatin1Char(' ') + method;
        return true;
    }
    void signal(const QString &path, const QString &, const QString &name, const QVariantList &) override
    {
        calls << path + QLatin1Char(' ') + name;
    }
    bool startDetached(const QStringList &argv, const QStringList & = {}) override
    {
        detached << argv;
        return true;
    }
    bool ran(const QString &program) const
    {
        return std::any_of(runs.begin(), runs.end(), [&](const QStringList &a) { return a.value(0) == program; });
    }
};

} // namespace

class TestApp : public QObject
{
    Q_OBJECT

    QTemporaryDir m_home;
    QString m_realIso;
    QString m_realThemes;   // the asset library's themes, for the real-style tests
    QString m_realFonts;    // XP's fonts, as setup imported them

private Q_SLOTS:
    void initTestCase()
    {
        m_realIso = qEnvironmentVariable("XPLASMA_TEST_ISO");
        // The real XP ISO, if this machine has one (looked up before HOME moves).
        for (const QString &dir : QStringList{QDir::homePath() + QLatin1String("/.cache/xplasma/downloads"),
                                   QDir::homePath() + QLatin1String("/.cache/xp2plasma/downloads")}) {
            const auto isos = QDir(dir).entryInfoList({QStringLiteral("*.iso")}, QDir::Files, QDir::Size);
            if (!isos.isEmpty() && m_realIso.isEmpty())
                m_realIso = isos.first().absoluteFilePath();
        }
        if (QFileInfo::exists(paths::themes() + QLatin1String("/Luna/luna.msstyles")))
            m_realThemes = QFileInfo(paths::themes()).canonicalFilePath();
        if (QFileInfo::exists(paths::xpFonts() + QLatin1String("/tahoma.ttf")))
            m_realFonts = paths::xpFonts();
        QVERIFY(m_home.isValid());
        qputenv("HOME", QFile::encodeName(m_home.path()));
        QCOMPARE(paths::home(), m_home.path());
    }

    void init()
    {
        // A fresh home per test.
        QDir(m_home.path()).removeRecursively();
        QDir().mkpath(m_home.path());
    }

    void customThemeArchive()
    {
        const QString zip = m_home.filePath(QStringLiteral("Halo.zip"));
        const QByteArray theme("[Theme]\nDisplayName=Halo Test\n[Control Panel\\Colors]\nBackground=12 34 56\n[Control Panel\\Desktop]\nWallpaper=Wallpaper.bmp\n");
        QVERIFY(writeArchive(zip, QStringLiteral("zip"), {{"Halo/Halo.theme", theme}, {"Halo/Wallpaper.bmp", "wallpaper-bytes"}, {"installer.exe", "not-executed"}}));
        themeimport::Result imported;
        QString error;
        QVERIFY2(themeimport::install(zip, &imported, &error), qPrintable(error));
        QCOMPARE(imported.themes, 1);
        const auto bundle = Library::bundle(imported.theme);
        QCOMPARE(bundle.label, QStringLiteral("Halo Test"));
        QVERIFY(bundle.wallpaper.startsWith(imported.directory));
        QCOMPARE(readFile(bundle.wallpaper), QByteArray("wallpaper-bytes"));
        // Importing again does not overwrite the first pack.
        themeimport::Result second;
        QVERIFY(themeimport::install(zip, &second, &error));
        QVERIFY(second.directory != imported.directory);
        QVERIFY(QFileInfo::exists(imported.theme));
    }

    void customThemeFolderAndLooseFile()
    {
        const QString file = m_home.filePath(QStringLiteral("download/Custom.theme"));
        writeFile(file, "[Theme]\nDisplayName=Custom\n[Control Panel\\Colors]\nBackground=5 6 7\n[Control Panel\\Desktop]\nWallpaper=wall.bmp\n");
        writeFile(m_home.filePath(QStringLiteral("download/wall.bmp")), "picture");
        writeFile(m_home.filePath(QStringLiteral("download/unrelated.txt")), "not-a-resource");
        themeimport::Result imported;
        QString error;
        QVERIFY2(themeimport::install(file, &imported, &error), qPrintable(error));
        QCOMPARE(readFile(Library::bundle(imported.theme).wallpaper), QByteArray("picture"));
        QVERIFY(!QFileInfo::exists(imported.directory + QStringLiteral("/unrelated.txt")));
        QVERIFY(themeimport::install(QFileInfo(file).absolutePath(), &imported, &error));
        QVERIFY(QFileInfo::exists(imported.directory + QStringLiteral("/unrelated.txt")));
    }

    void deleteImportedPresetRetainsSharedResources()
    {
        const QString source = m_home.filePath(QStringLiteral("download/Removable.theme"));
        writeFile(source, "[Theme]\nDisplayName=Removable\n[Control Panel\\Colors]\nBackground=5 6 7\n[Control Panel\\Desktop]\nWallpaper=wall.bmp\n");
        writeFile(m_home.filePath(QStringLiteral("download/wall.bmp")), "shared-picture");
        themeimport::Result imported;
        QString error;
        QVERIFY2(themeimport::install(source, &imported, &error), qPrintable(error));
        const QString wallpaper = Library::bundle(imported.theme).wallpaper;
        const QString sibling = imported.directory + QStringLiteral("/Other.theme");
        QVERIFY(QFile::copy(imported.theme, sibling));
        QVERIFY(themeimport::removable(imported.theme));
        QVERIFY2(themeimport::remove(imported.theme, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(imported.theme));
        QVERIFY(QFileInfo::exists(sibling));
        QCOMPARE(readFile(wallpaper), QByteArray("shared-picture"));
        QVERIFY(!themeimport::remove(source, &error));
        const QString link = imported.directory + QStringLiteral("/Linked.theme");
        QVERIFY(QFile::link(source, link));
        QVERIFY(!themeimport::removable(link));
        QVERIFY(!themeimport::remove(link, &error));
        QVERIFY(QFileInfo::exists(source));
    }

    void customThemeInvalidAndTraversal()
    {
        QString error;
        themeimport::Result imported;
        const QString zip = m_home.filePath(QStringLiteral("invalid.zip"));
        QVERIFY(writeArchive(zip, QStringLiteral("zip"), {{"fake.msstyles", "not an XP style"}}));
        QVERIFY(!themeimport::install(zip, &imported, &error));
        QVERIFY(error.contains(QStringLiteral("No usable")));
        QVERIFY(QDir(paths::themes() + QStringLiteral("/Custom")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(writeArchive(zip, QStringLiteral("zip"), {{"../../escape.txt", "bad"}}));
        QVERIFY(!themeimport::install(zip, &imported, &error));
        QVERIFY(!QFileInfo::exists(paths::themes() + QStringLiteral("/escape.txt")));
        std::atomic_bool cancel(true);
        QVERIFY(!themeimport::install(zip, &imported, &error, {}, &cancel));
    }

    void customThemeStandaloneStyle()
    {
        if (m_realThemes.isEmpty()) QSKIP("XP styles not installed");
        themeimport::Result imported;
        QString error;
        QVERIFY2(themeimport::install(m_realThemes + QStringLiteral("/Luna/luna.msstyles"), &imported, &error), qPrintable(error));
        QCOMPARE(imported.styles, 1);
        QCOMPARE(imported.themes, 1);
        const auto bundle = Library::bundle(imported.theme);
        QVERIFY(bundle.styleMissing.isEmpty());
        QVERIFY(bundle.style.startsWith(imported.directory));
        QVERIFY(Library::style(bundle.style).readable());
    }

    // --- paths ----------------------------------------------------------------

    void shippedStylePlugin()
    {
        // This is build/<mode>-qt6/tests/app/test-app; build.sh installs the
        // plugins into build/<mode>.
        QDir tree(QDir::cleanPath(QCoreApplication::applicationDirPath() + QLatin1String("/../..")));
        const QString mode = tree.dirName().section(QLatin1String("-qt"), 0, 0);
        const QString expected = QFileInfo(tree.filePath(QLatin1String("../") + mode + QLatin1String("/lib/xplasma/qt6/styles/libxplasma.so")))
                                     .absoluteFilePath();
        if (!QFileInfo::exists(expected))
            QSKIP("build.sh hasn't installed this build yet");
        QCOMPARE(paths::shippedStylePlugin(6), expected);
    }

    // --- archives -------------------------------------------------------------

    void zipAndTar_data()
    {
        QTest::addColumn<QString>("format");
        QTest::newRow("zip") << QStringLiteral("zip");
        QTest::newRow("tar.gz") << QStringLiteral("tar.gz");
    }

    void zipAndTar()
    {
        QFETCH(QString, format);
        const QString file = m_home.filePath(QStringLiteral("pack.") + format);
        QVERIFY(writeArchive(file, format,
                             {{"Luna/luna.msstyles", "MZ-style"}, {"Luna/Shell/NormalColor/shellstyle.dll", "MZ-shell"},
                              {"readme.txt", QByteArray(200000, 'x')}}));

        const auto entries = xpl::archive::list(file);
        QVERIFY(entries);
        QCOMPARE(entries->size(), 3);
        QCOMPARE(entries->at(0).path, QStringLiteral("Luna/luna.msstyles"));
        QCOMPARE(entries->at(2).size, 200000);

        QCOMPARE(xpl::archive::read(file, QStringLiteral("luna\\LUNA.MSSTYLES")).value_or(QByteArray()), QByteArray("MZ-style"));
        QString error;
        QVERIFY(!xpl::archive::read(file, QStringLiteral("nope"), &error));
        QVERIFY(error.contains(QLatin1String("not in")));

        const QString out = m_home.filePath(QStringLiteral("out"));
        qint64 last = -1;
        QVERIFY2(xpl::archive::extract(file, out, [&](qint64 done, qint64) { return (last = done), true; }, &error),
                 qPrintable(error));
        QVERIFY(last > 0);
        QFile f(out + QLatin1String("/Luna/Shell/NormalColor/shellstyle.dll"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("MZ-shell"));

        // Cancelling stops the extraction and says so.
        QVERIFY(!xpl::archive::extract(file, m_home.filePath(QStringLiteral("out2")), [](qint64, qint64) { return false; }, &error));
        QCOMPARE(error, QStringLiteral("cancelled"));
    }

    void refusesTraversal()
    {
        const QString file = m_home.filePath(QStringLiteral("evil.zip"));
        QVERIFY(writeArchive(file, QStringLiteral("zip"), {{"ok.txt", "fine"}, {"../../escaped.txt", "bad"}}));
        QString error;
        QVERIFY(!xpl::archive::extract(file, m_home.filePath(QStringLiteral("dest/inner")), {}, &error));
        QVERIFY(error.contains(QLatin1String("outside")));
        QVERIFY(!QFileInfo::exists(m_home.filePath(QStringLiteral("escaped.txt"))));
    }

    void garbage()
    {
        const QString file = m_home.filePath(QStringLiteral("junk.zip"));
        writeFile(file, QByteArray(5000, '\x5a'));
        QString error;
        QVERIFY(!xpl::archive::list(file, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!xpl::archive::list(m_home.filePath(QStringLiteral("missing.zip")), &error));
        QVERIFY(!xpl::archive::expandCabinet(QByteArray("MSCF but not really"), &error));
    }

    void iso()
    {
        const QString file = m_home.filePath(QStringLiteral("tiny.iso"));
        writeFile(file, IsoBuilder().image);
        QString error;
        const auto entries = xpl::archive::list(file, &error);
        QVERIFY2(entries, qPrintable(error));
        QStringList names;
        for (const auto &e : *entries)
            names << e.path + (e.directory ? QStringLiteral("/") : QString());
        QCOMPARE(names, (QStringList{QStringLiteral("HELLO.TXT"), QStringLiteral("I386/"), QStringLiteral("I386/SPLIT.BIN"),
                                     QStringLiteral("I386/README")}));
        QCOMPARE(xpl::archive::read(file, QStringLiteral("hello.txt")).value_or(QByteArray()), QByteArray("hello"));
        QCOMPARE(xpl::archive::read(file, QStringLiteral("i386\\split.bin")).value_or(QByteArray()), QByteArray("abcabc"));

        const QString out = m_home.filePath(QStringLiteral("iso-out"));
        QVERIFY2(xpl::archive::extract(file, out, {}, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(out + QLatin1String("/I386/README")));
    }

    void isoLoop()
    {
        // A directory pointing back at the root is walked once, not forever.
        const QString file = m_home.filePath(QStringLiteral("loop.iso"));
        writeFile(file, IsoBuilder(true).image);
        const auto entries = xpl::archive::list(file);
        QVERIFY(entries);
        QVERIFY(entries->size() < 10);
    }

    void isoTruncated()
    {
        // Cut off mid-directory: an error or a partial list, never a crash.
        const QString file = m_home.filePath(QStringLiteral("cut.iso"));
        writeFile(file, IsoBuilder().image.left(18 * 2048 + 100));
        xpl::archive::list(file);
        xpl::archive::read(file, QStringLiteral("HELLO.TXT"));
    }

    void realMedia()
    {
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        QString error;
        const auto entries = xpl::archive::list(m_realIso, &error);
        QVERIFY2(entries, qPrintable(error));
        QVERIFY(entries->size() > 1000);
        // The Start flag's source: explorer.exe, CAB-compressed.
        const auto packed = xpl::archive::read(m_realIso, QStringLiteral("I386/EXPLORER.EX_"), &error);
        QVERIFY2(packed, qPrintable(error));
        QVERIFY(packed->startsWith("MSCF"));
        const auto explorer = xpl::archive::expandCabinet(*packed, &error);
        QVERIFY2(explorer, qPrintable(error));
        QVERIFY(explorer->startsWith("MZ"));
        QVERIFY(explorer->size() > 900000);
    }


    // --- the library ----------------------------------------------------------

    void stockSchemes()
    {
        const auto list = schemes::list();
        QCOMPARE(list.size(), 36);
        QVERIFY(std::all_of(list.begin(), list.end(), [](const schemes::Entry &e) { return e.stock; }));
        const auto standard = schemes::load(QStringLiteral("windows standard"));
        QVERIFY(standard);
        QCOMPARE(standard->name, QStringLiteral("Windows Standard"));
        ClassicScheme fresh;   // XP's defaults, which are Windows Standard
        fresh.name = standard->name;
        QCOMPARE(standard->colours, fresh.colours);
        QVERIFY(standard->metrics == fresh.metrics);
        const auto brick = schemes::load(QStringLiteral("Brick"));
        QVERIFY(brick);
        QCOMPARE(brick->colour(u"ActiveTitle"), QColor(128, 0, 0));
        QCOMPARE(brick->metrics.captionFont.face, QStringLiteral("Tahoma"));
        QCOMPARE(brick->metrics.captionFont.points(), 9);
        QVERIFY(!schemes::load(QStringLiteral("Nope")));

        using Split = std::pair<QString, QString>;
        QCOMPARE(schemes::split(QStringLiteral("Windows Standard (large)")),
                 Split(QStringLiteral("Windows Standard"), QStringLiteral("LARGEFONTS")));
        QCOMPARE(schemes::split(QStringLiteral("Pumpkin (large)")), Split(QStringLiteral("Pumpkin (large)"), QStringLiteral("NORMAL")));
        const QStringList bases = schemes::bases();
        QCOMPARE(bases.size(), 22);
        QVERIFY(bases.contains(QStringLiteral("Pumpkin (large)")));
        QVERIFY(!bases.contains(QStringLiteral("Lilac (large)")));
        QVERIFY(schemes::sizeAvailable(QStringLiteral("Lilac"), QStringLiteral("LARGEFONTS")));
        QVERIFY(!schemes::sizeAvailable(QStringLiteral("Brick"), QStringLiteral("LARGEFONTS")));
        QVERIFY(schemes::sizeAvailable(QStringLiteral("Windows Classic"), QStringLiteral("EXTRALARGE")));
    }

    void stockSchemesAreXps()
    {
        // The shipped files are exactly what HIVEDEF.INF says.
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        const auto inf = xpl::archive::read(m_realIso, QStringLiteral("I386/HIVEDEF.INF"));
        QVERIFY(inf);
        const auto fromXp = hivedef::schemes(*inf);
        QCOMPARE(fromXp.size(), 36);
        for (const ClassicScheme &s : fromXp) {
            QFile shipped(QStringLiteral(":/schemes/") + s.name + QLatin1String(".theme"));
            QVERIFY2(shipped.open(QIODevice::ReadOnly), qPrintable(s.name));
            QCOMPARE(shipped.readAll(), hivedef::themeBytes(s));
        }
    }

    void userSchemes()
    {
        ClassicScheme mine = *schemes::load(QStringLiteral("Brick"));
        mine.name = QStringLiteral("Brick");
        mine.setColour(u"Background", QColor(1, 2, 3));
        QString path, error;
        QVERIFY2(schemes::save(mine, &path, &error), qPrintable(error));
        QVERIFY(path.startsWith(paths::schemes()));
        // Yours hides XP's of the same name.
        const auto list = schemes::list();
        QCOMPARE(list.size(), 36);
        QCOMPARE(schemes::load(QStringLiteral("Brick"))->colour(u"Background"), QColor(1, 2, 3));
        mine.name = QStringLiteral("My / Own");
        QVERIFY(schemes::save(mine));
        QCOMPARE(schemes::list().size(), 37);
        QVERIFY(schemes::load(QStringLiteral("My / Own")));
        QVERIFY(schemes::remove(QStringLiteral("Brick")));
        QVERIFY(!schemes::remove(QStringLiteral("Desert")));   // XP's own can't be deleted
        QCOMPARE(schemes::load(QStringLiteral("Brick"))->colour(u"Background"), QColor(66, 0, 0));
        mine.name = QStringLiteral("two\nlines");
        QVERIFY(!schemes::save(mine, nullptr, &error));
    }

    void legacySchemes()
    {
        // xp2plasma's JSON for Brick, as its editor saved it.
        const QByteArray json = R"({"Brick": {
            "metrics": {"border": 1, "scroll": [13, 13], "caption": [18, 18], "smcaption": [15, 15], "menu": [18, 18]},
            "fonts": {"caption": {"face": "Tahoma", "pt": 9, "bold": true, "italic": false},
                      "menu": {"face": "Microsoft Sans Serif", "pt": 8, "bold": false, "italic": false}},
            "colors": {"background": [66, 0, 0], "activetitle": [128, 0, 0], "hotlight": [128, 0, 0],
                       "menuhilight_or_unused": [192, 192, 192]}}})";
        const auto converted = schemes::fromLegacyJson(json);
        QCOMPARE(converted.size(), 1);
        const ClassicScheme &s = converted.first();
        const ClassicScheme xp = *schemes::load(QStringLiteral("Brick"));
        QCOMPARE(s.name, QStringLiteral("Brick"));
        for (const char *c : {"Background", "ActiveTitle", "HotTrackingColor", "ButtonAlternateFace"})
            QCOMPARE(s.colour(QString::fromLatin1(c)), xp.colour(QString::fromLatin1(c)));
        QCOMPARE(s.metrics.scrollWidth, 13);
        QCOMPARE(s.metrics.captionFont, xp.metrics.captionFont);
        QCOMPARE(s.metrics.menuFont.face, QStringLiteral("Microsoft Sans Serif"));
        QVERIFY(schemes::fromLegacyJson("not json").isEmpty());

        // Adoption converts them.
        writeFile(paths::legacyData() + QLatin1String("/schemes/user/brick.json"),
                  QByteArray(json).replace("\"Brick\"", "\"Old Brick\""));
        adoptLegacy();
        QVERIFY(schemes::load(QStringLiteral("Old Brick")));
    }

    void assets()
    {
        const QString root = m_home.filePath(QStringLiteral("lib"));
        writeFile(root + QLatin1String("/Foo.theme"), "[Theme]\r\n");
        writeFile(root + QLatin1String("/Foo/Wall.BMP"), "mine");
        writeFile(root + QLatin1String("/Bar/wall.bmp"), "sibling's");
        writeFile(root + QLatin1String("/Only/deep/a/b/c/far.bmp"), "too deep");
        writeFile(m_home.filePath(QStringLiteral("stock/bliss.jpg")), "jpg");
        const QString theme = root + QLatin1String("/Foo.theme");

        // The bundle's own folder first, by name, any case.
        QCOMPARE(resolveAsset(theme, QStringLiteral("%WinDir%\\Web\\Wallpaper\\wall.bmp")), root + QLatin1String("/Foo/Wall.BMP"));
        // Stock copies may differ in format, but only in the preferred places.
        QCOMPARE(resolveAsset(theme, QStringLiteral("%WinDir%web\\wallpaper\\Bliss.bmp"), {m_home.filePath(QStringLiteral("stock"))}),
                 m_home.filePath(QStringLiteral("stock/bliss.jpg")));
        QCOMPARE(resolveAsset(theme, QStringLiteral("Bliss.bmp")), QString());
        QCOMPARE(resolveAsset(theme, QStringLiteral("far.bmp")), QString());   // deeper than 3 below any root
        // A saved preset's exact path is taken as it is.
        QCOMPARE(resolveAsset(theme, root + QLatin1String("/Bar/wall.bmp")), root + QLatin1String("/Bar/wall.bmp"));
        QCOMPARE(resolveAsset(theme, root + QLatin1String("/Bar/gone.bmp")), QString());
        QCOMPARE(resolveAsset(theme, QString()), QString());
    }

    void fill_data()
    {
        QTest::addColumn<QByteArray>("desktop");
        QTest::addColumn<int>("fill");   // -1: says nothing
        QTest::newRow("stretch") << QByteArray("WallpaperStyle=2") << 0;
        QTest::newRow("centre") << QByteArray("WallpaperStyle=0") << 6;
        QTest::newRow("tile") << QByteArray("WallpaperStyle=0\nTileWallpaper=1") << 3;
        QTest::newRow("fill") << QByteArray("WallpaperStyle=10") << 2;
        QTest::newRow("unknown") << QByteArray("WallpaperStyle=7") << -1;
        QTest::newRow("absent") << QByteArray("Wallpaper=x.bmp") << -1;
    }

    void fill()
    {
        QFETCH(QByteArray, desktop);
        QFETCH(int, fill);
        const auto got = wallpaperFill(ThemeFile::fromBytes("[Control Panel\\Desktop]\n" + desktop));
        QCOMPARE(got.value_or(-1), fill);
    }

    void presetsSave()
    {
        presets::Preset p;
        p.name = QStringLiteral("My Classic");
        p.scheme = *schemes::load(QStringLiteral("Rose"));
        p.desktop.wallpaper = QStringLiteral("/x/bliss.jpg");
        p.desktop.fill = 3;
        QString path, error;
        QVERIFY2(presets::save(p, &path, &error), qPrintable(error));
        QCOMPARE(path, paths::themes() + QLatin1String("/Saved/My-Classic.theme"));
        QVERIFY(presets::owned(path));
        const ThemeBundle b = Library::bundle(path);
        QCOMPARE(b.label, QStringLiteral("My Classic"));
        QVERIFY(b.classic());
        QCOMPARE(b.fill.value_or(-1), 3);
        const ClassicScheme back = ClassicScheme::fromThemeFile(*ThemeFile::load(path));
        QCOMPARE(back.colour(u"ActiveTitle"), p.scheme.colour(u"ActiveTitle"));
        QCOMPARE(back.colour(u"Background"), p.desktop.colour);   // the desktop's colour wins
        // Never replaced.
        QVERIFY(!presets::save(p, nullptr, &error));
        QVERIFY(error.contains(QLatin1String("exists")));
        p.name = QStringLiteral("bad\nname");
        QVERIFY(!presets::save(p));

        // A style preset starting from a bundle: its files are found and
        // written as the paths they are here.
        writeFile(m_home.filePath(QStringLiteral("b/Base.theme")),
                  "[Theme]\r\nDisplayName=Base\r\n[Control Panel\\Cursors]\r\nArrow=%WinDir%cursors\\arrow.cur\r\n");
        writeFile(m_home.filePath(QStringLiteral("b/Base/arrow.cur")), "cur");
        presets::Preset s;
        s.name = QStringLiteral("Styled");
        s.style = QStringLiteral("/x/luna.msstyles");
        s.colorStyle = QStringLiteral("Metallic");
        s.size = QStringLiteral("NormalSize");
        s.base = m_home.filePath(QStringLiteral("b/Base.theme"));
        s.liveColors={{"enabled",true},{"start","#efab00"},{"menu","#315c43"},{"title","#315c43"},{"buttons","#806030"},{"startText",2},{"menuText",1},{"startMenuText",2},{"titleText",1},{"buttonsText",0},{"linkTitleToTaskbar",true}};
        QVERIFY2(presets::save(s, &path, &error), qPrintable(error));
        const ThemeFile saved = *ThemeFile::load(path);
        QCOMPARE(QJsonDocument::fromJson(saved.value(u"XPlasma",u"LiveColors").toUtf8()).object(),s.liveColors);
        QCOMPARE(saved.value(u"VisualStyles", u"ColorStyle"), QStringLiteral("Metallic"));
        QCOMPARE(saved.value(u"Theme", u"DisplayName"), QStringLiteral("Styled"));
        QCOMPARE(saved.value(u"Control Panel\\Cursors", u"Arrow"), m_home.filePath(QStringLiteral("b/Base/arrow.cur")));
        QVERIFY(!presets::owned(s.base));
    }

    void maintenancePolicyIsEmbeddedAndUsesIsolatedHome()
    {
        QVERIFY(QFile::exists(QStringLiteral(":/maintenance/cache_maintenance.py")));
        RecordingSession session;
        maintenance::run(session,QStringLiteral("xplasma-v2-previous"));
        QCOMPARE(session.runs.size(),1);
        const auto args=session.runs.first();
        QVERIFY(args.contains(QStringLiteral("--apply")));
        QVERIFY(args.contains(QStringLiteral("--keep-look")));
        QCOMPARE(args.value(args.indexOf(QStringLiteral("--home"))+1),paths::home());
        QCOMPARE(args.value(args.indexOf(QStringLiteral("--keep-look"))+1),QStringLiteral("xplasma-v2-previous"));
    }

    void unsavedCustomLooksArePruned()
    {
        // Custom colors build a style and a set of packages per choice: kept
        // while saved or applied (and the look just replaced), else removed.
        const QString share = paths::localShare(), luna = paths::data() + QLatin1String("/custom-luna/");
        const QString saved = luna + QLatin1String("saved.msstyles"), applied = luna + QLatin1String("applied.msstyles"),
                      previous = luna + QLatin1String("previous.msstyles"), unsaved = luna + QLatin1String("unsaved.msstyles");
        for (const QString &style : {saved, applied, previous, unsaved}) {
            writeFile(style, "style");
            writeFile(style + QLatin1String(".json"), "{}");
        }
        presets::Preset p;
        p.name = QStringLiteral("Mine");
        p.style = saved;
        QVERIFY(presets::save(p));
        const auto install = [&](const QString &id) {
            writeFile(share + QLatin1String("/aurorae/themes/") + id + QLatin1String("/metadata.json"), "{\"xplasma\":1}");
            writeFile(share + QLatin1String("/plasma/desktoptheme/") + id + QLatin1String("-taskbar/metadata.json"), "{}");
            writeFile(share + QLatin1String("/color-schemes/") + id + QLatin1String(".colors"), "[General]");
            writeFile(paths::data() + QLatin1String("/start/") + id + QLatin1String("/theme.json"), "{}");
            writeFile(share + QLatin1String("/themes/XPlasma-") + id + QLatin1String("-dark-applications/index.theme"), "");
            writeFile(paths::build() + QLatin1Char('/') + id + QLatin1String("/aurorae/x"), "");
        };
        const QStringList ids{QStringLiteral("xplasma-v2-saved"), QStringLiteral("xplasma-v2-applied"), QStringLiteral("xplasma-v2-previous"),
                              QStringLiteral("xplasma-v2-unsaved"), QStringLiteral("xplasma-v2-stock"), QStringLiteral("xplasma-v2-legacy")};
        for (const QString &id : ids) install(id);
        writeFile(paths::build() + QLatin1String("/windows-xp-style-silver/start/x"), "");
        writeFile(paths::data() + QLatin1String("/start/live-preview-1792110882864/theme.json"), "{}");
        writeFile(paths::data() + QLatin1String("/start/windows-xp-style-silver/theme.json"), "{}");
        writeFile(paths::data() + QLatin1String("/start/royale-royale/theme.json"), "{}");
        writeFile(paths::schemes() + QLatin1String("/Custom Rose 0123456789.theme"), "[Theme]\nDisplayName=Custom Rose 0123456789\n");
        writeFile(paths::schemes() + QLatin1String("/Rose Plus.theme"), "[Theme]\nDisplayName=Rose Plus\n");
        const QString dark = paths::cache() + QLatin1String("/dark-mode/");
        writeFile(dark + QLatin1String("unsaved/source.json"), "{\"source\":\"" + unsaved.toUtf8() + "\",\"builder\":\"x\"}");
        writeFile(dark + QLatin1String("unmarked/applications.msstyles"), "");
        writeFile(dark + QLatin1String("in-use/applications.msstyles"), "");
        writeFile(paths::config() + QLatin1String("/xplasmarc"), "[Theme]\nSource=" + dark.toUtf8() + "in-use/applications.msstyles\n");

        State state;
        state.appliedKind = QStringLiteral("style");
        state.style = applied;
        state.settings.insert(QStringLiteral("appliedPackageId"), ids.at(1));
        state.settings.insert(QLatin1String(kStartButtonLookKey), QStringLiteral("royale-royale"));
        state.settings.insert(QLatin1String(kGeneratedLooksKey), QJsonObject{{ids.at(0), saved}, {ids.at(1), applied}, {ids.at(2), previous},
                                                                            {ids.at(3), unsaved}, {ids.at(4), QStringLiteral("/x/luna.msstyles")}});
        pruneGeneratedLooks(state, ids.at(2), previous);

        for (const QString &kept : {saved, applied, previous})
            QVERIFY2(QFileInfo::exists(kept) && QFileInfo::exists(kept + QLatin1String(".json")), qPrintable(kept));
        QVERIFY(!QFileInfo::exists(unsaved) && !QFileInfo::exists(unsaved + QLatin1String(".json")));
        for (const QString &id : ids) {
            const bool keep = id != ids.at(3) && id != ids.at(5);
            for (const QString &path : QStringList{share + QLatin1String("/aurorae/themes/") + id, share + QLatin1String("/plasma/desktoptheme/") + id + QLatin1String("-taskbar"),
                                        share + QLatin1String("/color-schemes/") + id + QLatin1String(".colors"), paths::data() + QLatin1String("/start/") + id,
                                        share + QLatin1String("/themes/XPlasma-") + id + QLatin1String("-dark-applications")})
                QVERIFY2(QFileInfo::exists(path) == keep, qPrintable(path));
            QVERIFY2(!QFileInfo::exists(paths::build() + QLatin1Char('/') + id), qPrintable(id));   // (staging)
        }
        QCOMPARE(QStringList(state.settings.value(QLatin1String(kGeneratedLooksKey)).toObject().keys()),
                 (QStringList{ids.at(1), ids.at(2), ids.at(0), ids.at(4)}));
        QVERIFY(!QFileInfo::exists(paths::build() + QLatin1String("/windows-xp-style-silver")));
        QVERIFY(!QFileInfo::exists(paths::data() + QLatin1String("/start/live-preview-1792110882864")));
        QVERIFY(!QFileInfo::exists(paths::data() + QLatin1String("/start/windows-xp-style-silver")));
        QVERIFY(QFileInfo::exists(paths::data() + QLatin1String("/start/royale-royale")));   // (the menu's)
        QVERIFY(!QFileInfo::exists(paths::schemes() + QLatin1String("/Custom Rose 0123456789.theme")));
        QVERIFY(QFileInfo::exists(paths::schemes() + QLatin1String("/Rose Plus.theme")));
        QVERIFY(!QFileInfo::exists(dark + QLatin1String("unsaved")));
        QVERIFY(!QFileInfo::exists(dark + QLatin1String("unmarked")));
        QVERIFY(QFileInfo::exists(dark + QLatin1String("in-use")));

        // Deleting the saved theme lets its colors go once nothing wears them.
        QVERIFY(QFile::remove(paths::themes() + QLatin1String("/Saved/Mine.theme")));
        pruneGeneratedLooks(state);
        QVERIFY(!QFileInfo::exists(saved));
        QVERIFY(!QFileInfo::exists(share + QLatin1String("/aurorae/themes/") + ids.at(0)));
        QVERIFY(QFileInfo::exists(applied));
        QVERIFY(!QFileInfo::exists(previous));
    }

    void realStyles()
    {
        if (m_realThemes.isEmpty())
            QSKIP("no asset library on this machine");
        const StyleInfo luna = Library::style(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY2(luna.readable(), qPrintable(luna.error));
        QCOMPARE(luna.label, QStringLiteral("Windows XP style"));
        QVERIFY(luna.official);
        QStringList colours;
        for (const StyleOption &c : luna.colours)
            colours << c.label;
        QCOMPARE(colours, (QStringList{QStringLiteral("Default (blue)"), QStringLiteral("Silver"), QStringLiteral("Olive Green")}));
        QCOMPARE(luna.defaultVariant, QStringLiteral("NORMALBLUE_INI"));
        QCOMPARE(luna.resolve(QStringLiteral("Metallic"), QStringLiteral("NormalSize")), QStringLiteral("NORMALMETALLIC_INI"));
        QCOMPARE(luna.resolve(QStringLiteral("homestead"), QString()), QStringLiteral("NORMALHOMESTEAD_INI"));
        QCOMPARE(luna.resolve(QStringLiteral("NormalColor"), QStringLiteral("ExtraLargeFonts")), QStringLiteral("EXTRALARGEBLUE_INI"));
        QCOMPARE(luna.resolve(QStringLiteral("Purple"), QString()), QString());
        QCOMPARE(luna.sizesFor(QStringLiteral("BLUE")).size(), 3);
        // Cached: the second look doesn't open the file (same answer, and the
        // index is on disk).
        QVERIFY(QFileInfo::exists(paths::cache() + QLatin1String("/style-index.json")));
        QCOMPARE(Library::style(luna.path).toJson(), luna.toJson());

        // (Royale Noir is a theme pack's, not the XP disc's: where it's been fetched.)
        if (const QString noirPath = m_realThemes + QLatin1String("/Royale Noir/Royale Noir.msstyles"); QFileInfo::exists(noirPath)) {
            const StyleInfo noir = Library::style(noirPath);
            QCOMPARE(noir.resolve(QStringLiteral("Metallic"), QStringLiteral("NormalSize")), QStringLiteral("NORMALDARKROYALE_INI"));
        }

        const ThemeBundle xp = Library::bundle(m_realThemes + QLatin1String("/Luna/Luna.theme"));
        QCOMPARE(xp.label, QStringLiteral("Windows XP"));
        QCOMPARE(QFileInfo(xp.style).fileName(), QStringLiteral("luna.msstyles"));
        QCOMPARE(xp.variant, QStringLiteral("NORMALBLUE_INI"));

        // (Luna from the disc; four more where the official packs are fetched.)
        const Library lib = Library::scan({m_realThemes});
        QVERIFY(!lib.styles.isEmpty());
        if (QFileInfo::exists(m_realThemes + QLatin1String("/Royale Noir")))
            QVERIFY(lib.styles.size() >= 5);
        QVERIFY(lib.styles.first().official);
    }

    void stockThemeChoices()
    {
        Library library;
        ThemeBundle xp;xp.path=paths::themes()+"/Luna/Luna.theme";xp.label="Windows XP";xp.style="/styles/luna.msstyles";xp.wallpaper="/wallpaper/Bliss.jpg";xp.fill=2;
        auto classic=xp;classic.path=paths::themes()+"/Windows Classic.theme";classic.label="Windows Classic";classic.style.clear();
        library.themes={classic,xp};
        for(const auto &name:{QString("Luna Blue"),QString("Luna Silver"),QString("Luna Olive Green")}){
            auto shortcut=xp;shortcut.label=name;shortcut.path=paths::themes()+"/XP Presets/"+name+".theme";
            QDir().mkpath(QFileInfo(shortcut.path).absolutePath());ThemeFile file;file.setValue(u"Theme",u"DisplayName",name);file.setValue(u"VisualStyles",u"Path",xp.style);QVERIFY(file.save(shortcut.path));library.themes.append(shortcut);
        }
        auto saved=xp;saved.label="My Blue";saved.path=paths::themes()+"/Saved/My-Blue.theme";library.themes.append(saved);
        auto extra=xp;extra.label="Embedded";extra.path=paths::themes()+"/XP Presets/Embedded.theme";extra.style="/styles/embedded.msstyles";library.themes.append(extra);
        auto choices=library.themeChoices();QCOMPARE(choices.size(),4);QCOMPARE(choices[0].label,QString("Windows XP"));QCOMPARE(choices[1].label,QString("Windows Classic"));QCOMPARE(choices[2].label,QString("Embedded"));QCOMPARE(choices[3].label,QString("My Blue"));QCOMPARE(library.themes.size(),7);
        // A customized shortcut is a separate theme and must remain available.
        auto file=*ThemeFile::load(library.themes[2].path);file.setValue(u"XPlasma",u"LiveColors",QStringLiteral("{\"enabled\":true}"));QVERIFY(file.save(library.themes[2].path));QCOMPARE(library.themeChoices().size(),5);
        if(!m_realThemes.isEmpty()){
            QDir(paths::themes()).removeRecursively();
            const auto installed=Library::scan({m_realThemes});
            QStringList names;for(const auto &theme:installed.themeChoices())names<<theme.label;qInfo()<<"Theme choices:"<<names;
            QVERIFY(!names.contains("Luna Blue"));QVERIFY(!names.contains("Luna Silver"));QVERIFY(!names.contains("Luna Olive Green"));QVERIFY(names.contains("Windows XP"));
        }
    }

    // --- generators -------------------------------------------------------------

    void decoration_data()
    {
        QTest::addColumn<QString>("style");
        QTest::addColumn<QString>("variant");
        for (const char *v : {"NORMALBLUE_INI", "NORMALMETALLIC_INI", "NORMALHOMESTEAD_INI"})
            QTest::newRow(v) << QStringLiteral("Luna/luna.msstyles") << QString::fromLatin1(v);
        QTest::newRow("royale") << QStringLiteral("Royale/Royale.msstyles") << QString();
        QTest::newRow("royale-noir") << QStringLiteral("Royale Noir/Royale Noir.msstyles") << QStringLiteral("NORMALDARKROYALE_INI");
        QTest::newRow("zune") << QStringLiteral("Zune/Zune.msstyles") << QString();
        QTest::newRow("embedded") << QStringLiteral("Embedded/Embedded.msstyles") << QString();
    }

    void decoration()
    {
        // Aurorae's frame, as KSvg lays it out, is XP's own caption and
        // frames drawn at that window size -- every pixel of the border.
        if (m_realThemes.isEmpty())
            QSKIP("no asset library on this machine");
        QFETCH(QString, style);
        QFETCH(QString, variant);
        if (!QFileInfo::exists(m_realThemes + QLatin1Char('/') + style))
            QSKIP("its theme pack isn't fetched on this machine");
        QString error;
        const auto look = Look::fromStyle(m_realThemes + QLatin1Char('/') + style, variant, &error);
        QVERIFY2(look, qPrintable(error));
        const QString written = writeDecoration(*look, m_home.filePath(QStringLiteral("decoration-package")), &error);
        QVERIFY2(!written.isEmpty(), qPrintable(error));
        QCOMPARE(QFileInfo(written).fileName(), look->id);
        QVERIFY(QFileInfo::exists(written + QLatin1Char('/') + look->id + QLatin1String("rc")));
        QFile metadata(written + QLatin1String("/metadata.json"));
        QVERIFY(metadata.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(metadata.readAll()).object().value(QStringLiteral("KPlugin")).toObject().value(QStringLiteral("Id")).toString(), look->id);
        const DecorationArt art = decorationArt(*look);
        if (variant == QLatin1String("NORMALMETALLIC_INI")) {
            for (const auto &key : {QStringLiteral("decoration-maximized-center"), QStringLiteral("decoration-maximized-inactive-center")}) {
                QImage image;
                for (const auto &entry : art.frame)
                    if (entry.first == key) image = entry.second;
                QVERIFY(!image.isNull());
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                        QCOMPARE(qAlpha(image.pixel(x, y)), 255);
            }
        }
        const DecorationLayout l = decorationLayout(*look);
        const Renderer &r = *look->renderer;
        const int F = l.frame, H = l.frame + l.caption;
        // (Stretched edges come from an 800 x 600 window: exact there; at
        // other sizes only what XP keeps fixed is, and what it stretches
        // unevenly -- Luna's bottom and sides -- is within a few levels.)
        const QMargins capM = r.margins({QStringLiteral("window"), 1, 1, {}}, u"sizingmargins");
        const QMargins botM = r.margins({QStringLiteral("window"), 9, 1, {}}, u"sizingmargins");
        for (const QSize size : {QSize(800, 600), QSize(160, 120), QSize(437, 311), QSize(1024, 700)})
            for (const bool active : {true, false}) {
                const int state = active ? 1 : 2;
                QImage xp(size, QImage::Format_ARGB32_Premultiplied);
                xp.fill(Qt::transparent);
                {
                    QPainter p(&xp);
                    p.drawImage(0, 0, r.background({QStringLiteral("window"), 1, state, {}}, QSize(size.width(), H)));
                    p.drawImage(0, H, r.background({QStringLiteral("window"), 7, state, {}}, QSize(F, size.height() - H - F)));
                    p.drawImage(size.width() - F, H, r.background({QStringLiteral("window"), 8, state, {}}, QSize(F, size.height() - H - F)));
                    p.drawImage(0, size.height() - F, r.background({QStringLiteral("window"), 9, state, {}}, QSize(size.width(), F)));
                }
                const QImage ours = composeFrame(art.frame, active ? QStringLiteral("decoration-") : QStringLiteral("decoration-inactive-"), size);
                const bool reference = size == QSize(800, 600);
                int fixedDiffer = 0, worst = 0;
                for (int y = 0; y < size.height(); ++y)
                    for (int x = 0; x < size.width(); ++x) {
                        const bool top = y < H, bottom = y >= size.height() - F;
                        if (!top && !bottom && x >= F && x < size.width() - F)
                            continue;   // the client's
                        const QMargins &m = top ? capM : botM;
                        const bool fixed = reference || ((top || bottom) && (x < m.left() || x >= size.width() - m.right()));
                        const QRgb a = xp.pixel(x, y), b = ours.pixel(x, y);
                        if (a == b)
                            continue;
                        if (fixed)
                            ++fixedDiffer;
                        worst = std::max({worst, qAbs(qRed(a) - qRed(b)), qAbs(qGreen(a) - qGreen(b)), qAbs(qBlue(a) - qBlue(b)),
                                      qAbs(qAlpha(a) - qAlpha(b))});
                    }
                const QString where = QStringLiteral("%1x%2 %3").arg(size.width()).arg(size.height()).arg(active ? "active" : "inactive");
                QVERIFY2(fixedDiffer == 0, qPrintable(QStringLiteral("%1 fixed px differ at %2").arg(fixedDiffer).arg(where)));
                QVERIFY2(worst <= 16, qPrintable(QStringLiteral("stretched edge off by %1 at %2").arg(worst).arg(where)));
            }
        // Buttons at XP's size, every state.
        QCOMPARE(art.buttons.size(), r.hasPart({QStringLiteral("window"), 23, 1, {}}) ? 5 : 4);
        for (const auto &[file, states] : art.buttons) {
            QCOMPARE(states.size(), 8);
            for (const auto &[state, image] : states)
                QCOMPARE(image.size(), l.button);
        }
    }

    void panel_data()
    {
        QTest::addColumn<QString>("style");
        QTest::addColumn<QString>("variant");
        for (const char *v : {"NORMALBLUE_INI", "NORMALMETALLIC_INI", "NORMALHOMESTEAD_INI"})
            QTest::newRow(v) << QStringLiteral("Luna/luna.msstyles") << QString::fromLatin1(v);
        QTest::newRow("royale") << QStringLiteral("Royale/Royale.msstyles") << QString();
        QTest::newRow("zune") << QStringLiteral("Zune/Zune.msstyles") << QString();
        QTest::newRow("embedded") << QStringLiteral("Embedded/Embedded.msstyles") << QString();
    }

    void panel()
    {
        // The taskbar, laid out as KSvg does, is XP's at any width; a task
        // button is too wherever XP doesn't stretch it.
        if (m_realThemes.isEmpty())
            QSKIP("no asset library on this machine");
        QFETCH(QString, style);
        QFETCH(QString, variant);
        if (!QFileInfo::exists(m_realThemes + QLatin1Char('/') + style))
            QSKIP("its theme pack isn't fetched on this machine");
        QString error;
        const auto look = Look::fromStyle(m_realThemes + QLatin1Char('/') + style, variant, &error);
        QVERIFY2(look, qPrintable(error));
        const PanelArt art = panelArt(*look);
        const Renderer &r = *look->renderer;
        QCOMPARE(art.height, 30);
        // Text on the tray is the clock's: Royale's is black on its light
        // tray (its taskbar text is white); Luna Blue's white.
        if (style.startsWith(QLatin1String("Royale/")) || variant == QLatin1String("NORMALMETALLIC_INI"))
            QCOMPARE(art.trayTextColour, QColor(Qt::black));
        else if (variant == QLatin1String("NORMALBLUE_INI"))
            QCOMPARE(art.trayTextColour, QColor(Qt::white));
        QVERIFY(art.trayTextColour.isValid());
        for (const int width : {640, 800, 1280, 1366, 1920}) {
            const QImage xp = r.background({QStringLiteral("taskbar"), 1, 0, {}}, QSize(width, 30));
            const QImage ours = composeFrame(art.panel, QString(), QSize(width, 30));
            int differ = 0;
            for (int y = 0; y < 30; ++y)
                for (int x = 0; x < width; ++x)
                    differ += xp.pixel(x, y) != ours.pixel(x, y);
            QVERIFY2(differ == 0, qPrintable(QStringLiteral("bar: %1 px differ at %2").arg(differ).arg(width)));
        }
        const PartRef button{QStringLiteral("toolbar"), 1, 5, QStringLiteral("taskband")};
        const QMargins m = r.margins(button, u"sizingmargins");
        for (const int width : {60, 160, 200}) {
            if (width <= m.left() + m.right())
                continue;   // (narrower than its ends, XP squeezes them; FrameSvg can't)
            const QImage xp = r.background(button, QSize(width, 30));
            const QImage ours = composeFrame(art.tasks, QStringLiteral("focus-"), QSize(width, 30));
            int differ = 0;
            for (int y = 0; y < 30; ++y)
                for (int x = 0; x < width; ++x)
                    if (width == 160 || x < m.left() || x >= width - m.right())
                        differ += xp.pixel(x, y) != ours.pixel(x, y);
            QVERIFY2(differ == 0, qPrintable(QStringLiteral("task button: %1 px differ at %2").arg(differ).arg(width)));
        }
    }

    void startFlag()
    {
        // The flag comes out of explorer.exe on the XP media, once.
        QString error;
        QVERIFY(!media::startFlag(&error));   // no media recorded yet
        QVERIFY(error.contains(QLatin1String("media")));
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        const auto flag = media::startFlag(&error);
        QVERIFY2(flag, qPrintable(error));
        QCOMPARE(flag->size(), QSize(25, 20));
        QVERIFY(flag->hasAlphaChannel());
        QVERIFY(QFileInfo::exists(paths::extracted() + QLatin1String("/explorer.exe")));
        state.media = QStringLiteral("/nowhere.iso");
        QVERIFY(state.save());
        QVERIFY(media::startFlag());   // cached now
    }

    void startButton()
    {
        if (m_realThemes.isEmpty())
            QSKIP("no asset library on this machine");
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"), QStringLiteral("NORMALMETALLIC_INI"));
        QVERIFY(look);
        QString error;
        QVERIFY(!xpl::startButton(*look, QImage(), QStringLiteral("start"), &error));   // never without its flag
        QVERIFY(error.contains(QLatin1String("flag")));

        QImage flag(25, 20, QImage::Format_ARGB32);
        flag.fill(qRgba(255, 0, 0, 255));
        const auto art = xpl::startButton(*look, flag, QStringLiteral("start"), &error);
        QVERIFY2(art, qPrintable(error));
        // Measured on XP (Luna Silver, English): 99 x 30, the flag at (10, 5);
        // right of the label (and its shadow), XP's own button.
        QCOMPARE(art->normal.size(), QSize(99, 30));
        QCOMPARE(art->normal.pixel(10, 5), qRgb(255, 0, 0));
        QCOMPARE(art->normal.pixel(34, 24), qRgb(255, 0, 0));
        QVERIFY(art->normal.pixel(9, 5) != qRgb(255, 0, 0));
        const QImage xp = look->renderer->background({QStringLiteral("button"), 1, 2, QStringLiteral("start")}, QSize(99, 32));
        for (int y = 0; y < 30; ++y)
            for (int x = 80; x < 99; ++x)
                QCOMPARE(art->hover.pixel(x, y), xp.pixel(x, y));
        QVERIFY(art->pressed != art->normal);
        // For a HiDPI screen: twice the pixels, XP's own made bigger.
        const auto twice = xpl::startButton(*look, flag, QStringLiteral("start"), &error, 2);
        QVERIFY2(twice, qPrintable(error));
        QCOMPARE(twice->normal.size(), QSize(198, 60));
        QCOMPARE(twice->normal.deviceIndependentSize().toSize(), QSize(99, 30));
        QCOMPARE(twice->normal.pixel(20, 10), qRgb(255, 0, 0));
        QVERIFY(twice->normal.pixel(19, 10) != qRgb(255, 0, 0));
        for (int y = 0; y < 60; ++y)
            for (int x = 160; x < 198; ++x)
                QCOMPARE(twice->hover.pixel(x, y), xp.pixel(x / 2, y / 2));

        const Look classic = Look::fromScheme(ClassicScheme());
        const auto classicArt = xpl::startButton(classic, flag, QStringLiteral("start"), &error);
        QVERIFY2(classicArt, qPrintable(error));
        QCOMPARE(classicArt->normal.height(), 30);
        const auto classicTwice = xpl::startButton(classic, flag, QStringLiteral("start"), &error, 2);
        QVERIFY2(classicTwice, qPrintable(error));
        QCOMPARE(classicTwice->normal.size(), classicArt->normal.size() * 2);

        // Extras: none is XP's own button; a longer label, a wider one; a
        // logo of characters or a picture in the flag's place, its height.
        const auto plain = xpl::startButton(*look, flag, StartButtonExtras{}, &error);
        QVERIFY2(plain, qPrintable(error));
        QCOMPARE(plain->normal, art->normal);
        const auto longer = xpl::startButton(*look, flag, StartButtonExtras{QStringLiteral("start me up"), {}, {}}, &error);
        QVERIFY(longer && longer->normal.width() > art->normal.width());
        const auto emoji = xpl::startButton(*look, flag, StartButtonExtras{QStringLiteral("fart"), QStringLiteral("\U0001F427"), {}}, &error);
        QVERIFY(emoji);
        QVERIFY(emoji->normal.pixel(10, 5) != qRgb(255, 0, 0));   // no flag
        QImage picture(40, 40, QImage::Format_ARGB32);
        picture.fill(qRgba(0, 255, 0, 255));
        const QString file = m_home.filePath(QStringLiteral("logo.png"));
        QVERIFY(picture.save(file));
        const auto pictured = xpl::startButton(*look, flag, StartButtonExtras{{}, QStringLiteral("ignored"), file}, &error);
        QVERIFY(pictured);
        QCOMPARE(pictured->normal.pixel(10, 5), qRgb(0, 255, 0));
        QCOMPARE(pictured->normal.pixel(29, 24), qRgb(0, 255, 0));   // 20 tall and wide, where the flag was 25 x 20
        // A picture that's gone: XP's flag.
        const auto gone = xpl::startButton(*look, flag, StartButtonExtras{{}, {}, file + QLatin1String(".gone")}, &error);
        QVERIFY(gone && gone->normal == art->normal);
        // Kept as JSON, only what's set.
        const StartButtonExtras e{QStringLiteral("fart"), QStringLiteral("\U0001F427"), {}};
        QCOMPARE(StartButtonExtras::fromJson(e.toJson()), e);
        QVERIFY(StartButtonExtras{}.toJson().isEmpty());
    }

    void iconTheme()
    {
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        QString error;
        QImage drawnIcon(13, 13, QImage::Format_ARGB32_Premultiplied);
        drawnIcon.fill(Qt::red);
        const QString theme = writeIconTheme(m_home.filePath(QStringLiteral("out")), QStringLiteral("breeze"), QColor(Qt::white), &error,
                                             {{QStringLiteral("value-increase"), {{13, drawnIcon}}}});
        QVERIFY2(!theme.isEmpty(), qPrintable(error));
        // Icons drawn for the look, at their own sizes, as actions.
        QCOMPARE(QImage(theme + QLatin1String("/13x13/actions/value-increase.png")).size(), QSize(13, 13));
        // XP's own sizes only: shell32's folder at 16, 32 and 48, never larger.
        for (const char *size : {"16x16", "32x32", "48x48"})
            QVERIFY(QFileInfo::exists(theme + QLatin1Char('/') + QLatin1String(size) + QLatin1String("/places/folder.png")));
        QVERIFY(!QFileInfo::exists(theme + QLatin1String("/96x96/places/folder.png")));
        QCOMPARE(QImage(theme + QLatin1String("/32x32/places/folder.png")).size(), QSize(32, 32));
        // Battery frames come from batmeter's image lists, keyed.
        const QImage battery(theme + QLatin1String("/16x16/status/battery-050.png"));
        QCOMPARE(battery.size(), QSize(16, 16));
        QCOMPARE(qAlpha(battery.pixel(0, 0)), 0);
        QFile index(theme + QLatin1String("/index.theme"));
        QVERIFY(index.open(QIODevice::ReadOnly));
        {
            const QByteArray text = index.readAll();
            QVERIFY(text.contains("Inherits=breeze,hicolor"));
            QVERIFY(text.contains("[13x13/actions]\nSize=13\nContext=Actions"));
        }
        // The shortcut arrow in KDE's emblem squares, bottom-right, XP's size:
        // the 7px arrow for small icons, the 11px one at 32.
        for (const auto &[square, arrow] : {std::pair(8, 7), std::pair(16, 11), std::pair(22, 22)}) {
            const QImage emblem(theme + QStringLiteral("/%1x%1/emblems/emblem-symbolic-link.png").arg(square));
            QCOMPARE(emblem.size(), QSize(square, square));
            QRect box;
            for (int y = 0; y < square; ++y)
                for (int x = 0; x < square; ++x)
                    if (qAlpha(emblem.pixel(x, y)) > 0)
                        box |= QRect(x, y, 1, 1);
            QCOMPARE(box, QRect(square - arrow, square - arrow, arrow, arrow));
        }
        // Updates as Automatic Updates' shields; brightness as Power Options'
        // monitor, 32px only on XP, so scaled for the tray's 16.
        QCOMPARE(QImage(theme + QLatin1String("/16x16/status/update-none-symbolic.png")).size(), QSize(16, 16));
        QVERIFY(QImage(theme + QLatin1String("/16x16/status/update-high.png")) != QImage(theme + QLatin1String("/16x16/status/update-none.png")));
        QCOMPARE(QImage(theme + QLatin1String("/16x16/status/brightness-high-symbolic.png")).size(), QSize(16, 16));
        QCOMPARE(QImage(theme + QLatin1String("/32x32/status/brightness-high-symbolic.png")).size(), QSize(32, 32));
        // Charging: the level's frame, the plug's head over its bottom-right
        // corner only.
        {
            const QImage idle(theme + QLatin1String("/16x16/status/battery-050.png"));
            const QImage charging(theme + QLatin1String("/16x16/status/battery-050-charging.png"));
            QCOMPARE(idle.copy(0, 0, 8, 16), charging.copy(0, 0, 8, 16));
            QVERIFY(idle.copy(8, 9, 8, 7) != charging.copy(8, 9, 8, 7));
        }
        // The tray's icons XP has none for: the inherited theme's, in the
        // tray's text colour, beyond the icon loader's recolouring.
        if (QFileInfo::exists(QStringLiteral("/usr/share/icons/breeze/index.theme"))) {
            QFile sun(theme + QLatin1String("/16x16/status/redshift-status-on-symbolic.svg"));
            QVERIFY(sun.open(QIODevice::ReadOnly));
            const QByteArray svg = sun.readAll();
            QVERIFY2(svg.contains("#ffffff") && !svg.contains("currentColor") && !svg.contains("current-color-scheme"), svg.left(400).constData());
            QVERIFY(!QFileInfo::exists(theme + QLatin1String("/16x16/status/folder-symbolic.svg")));   // (not the tray's)
            // (the update icon: the tray's glyph small, Breeze's full-colour one at 32 and 48)
            if (QFileInfo::exists(QStringLiteral("/usr/share/icons/breeze/apps/48/system-software-update.svg"))) {
                auto read = [&](const QString &path) {
                    QFile f(path);
                    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
                };
                const QByteArray breeze = read(QStringLiteral("/usr/share/icons/breeze/apps/48/system-software-update.svg"));
                QCOMPARE(read(theme + QLatin1String("/48x48/status/system-software-update.svg")), breeze);
                QCOMPARE(read(theme + QLatin1String("/32x32/status/system-software-update.svg")), breeze);
                QVERIFY(read(theme + QLatin1String("/22x22/status/system-software-update.svg")).contains("#ffffff"));
            }
        }
    }

    void symbolicControlsSurviveRegeneration()
    {
        if (m_realIso.isEmpty()) QSKIP("no XP ISO for native icon-theme test");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        const QString extended = paths::localShare() + QStringLiteral("/icons/xplasma-xp-extended");
        writeFile(extended + QStringLiteral("/index.theme"), "[Icon Theme]\n");
        writeFile(extended + QStringLiteral("/ALIAS-PROVENANCE.json"),
                  "{\"go-next-symbolic\":{\"reason\":\"control-glyph\"}}");
        const QByteArray control="<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\"><style id=\"current-color-scheme\">.ColorScheme-Text {color:#000000;}</style><path class=\"ColorScheme-Text\" fill=\"currentColor\" d=\"M6 3L11 8L6 13Z\"/></svg>";
        writeFile(extended + QStringLiteral("/16x16/actions/go-next-symbolic.svg"),control);
        for (int i = 0; i < 3; ++i) {
            QString error;
            const QString theme = writeIconTheme(m_home.filePath(QStringLiteral("out")), QStringLiteral("breeze"), QColor(), &error);
            QVERIFY2(!theme.isEmpty(), qPrintable(error));
            QCOMPARE(readFile(theme + QStringLiteral("/16x16/actions/go-next-symbolic.svg")),control);
            QVERIFY(!QFileInfo::exists(theme + QStringLiteral("/16x16/actions/go-next-symbolic.png")));
            QVERIFY(readFile(theme + QStringLiteral("/index.theme")).contains("FollowsColorScheme=true"));
            QVERIFY(readFile(theme + QStringLiteral("/index.theme")).contains("Inherits=xplasma-xp-extended,"));
        }
    }

    void darkApplicationsPreserveShellAndFonts()
    {
        if(m_realThemes.isEmpty())QSKIP("No installed XP styles");
        for(const QString &relative:{QStringLiteral("Luna/luna.msstyles"),QStringLiteral("Royale Noir/Royale Noir.msstyles"),QStringLiteral("Zune/Zune.msstyles")}) {
            QString error;
            const auto source=Look::fromStyle(m_realThemes+QLatin1Char('/')+relative,QString(),&error);
            QVERIFY2(source.has_value(),qPrintable(error));
            const auto dark=darkmode::applicationLook(*source,true,&error);
            QVERIFY2(dark.has_value(),qPrintable(error));
            QVERIFY(dark->system.colour(u"Window").lightness()<60);
            QVERIFY(dark->system.colour(u"WindowText").lightness()>200);
            QCOMPARE(dark->system.metrics.captionHeight,source->system.metrics.captionHeight);
            QCOMPARE(dark->system.metrics.messageFont.face,source->system.metrics.messageFont.face);
            QCOMPARE(dark->system.metrics.messageFont.height,source->system.metrics.messageFont.height);
            int checked=0;
            for(const QString &name:source->renderer->theme().bitmapNames()) {
                if(!name.contains(QStringLiteral("CAPTION"))&&!name.contains(QStringLiteral("START"))&&!name.contains(QStringLiteral("TASK"))&&!name.contains(QStringLiteral("TRAY")))continue;
                QCOMPARE(dark->renderer->theme().bitmap(name).image,source->renderer->theme().bitmap(name).image);++checked;
            }
            QVERIFY(checked>10);
            const auto light=darkmode::applicationLook(*source,false,&error);
            QCOMPARE(light->stylePath,source->stylePath);
            QCOMPARE(light->system.colour(u"Window"),source->system.colour(u"Window"));
        }
        const Look classic=Look::fromScheme(ClassicScheme());
        const auto darkClassic=darkmode::applicationLook(classic,true);
        QVERIFY(darkClassic->classic());
        QVERIFY(darkClassic->system.colour(u"Window").lightness()<60);
        QCOMPARE(darkClassic->system.metrics.captionHeight,classic.system.metrics.captionHeight);
        QCOMPARE(darkClassic->system.colour(u"ActiveTitle"),classic.system.colour(u"ActiveTitle"));
        QVERIFY(darkmode::publish(true));
        QVERIFY(readFile(paths::data()+QStringLiteral("/appearance.json")).contains("true"));
        QVERIFY(darkmode::publish(false));
        QVERIFY(readFile(paths::data()+QStringLiteral("/appearance.json")).contains("false"));
    }

    void iconResolutionSurvivesRegeneration()
    {
        if (m_realIso.isEmpty()) QSKIP("no XP ISO for native icon-theme test");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        const QString extended = paths::localShare() + QStringLiteral("/icons/xplasma-xp-extended");
        writeFile(extended + QStringLiteral("/index.theme"), "[Icon Theme]\n");
        writeFile(extended + QStringLiteral("/ALIAS-PROVENANCE.json"),
                  "{\"folder\":{\"reason\":\"exact\",\"context\":\"places\"}}");
        QImage high(128, 128, QImage::Format_ARGB32);
        high.fill(Qt::blue);
        QDir().mkpath(extended + QStringLiteral("/128x128/places"));
        QVERIFY(high.save(extended + QStringLiteral("/128x128/places/folder.png")));
        for (const bool hidpi : {false, true}) {
            writeFile(extended + QStringLiteral("/RESOLUTION.json"), hidpi ? "{\"hidpi\":true}" : "{\"hidpi\":false}");
            for (int i = 0; i < 2; ++i) {
                QString error;
                const QString theme = writeIconTheme(m_home.filePath(hidpi ? QStringLiteral("high") : QStringLiteral("original")), QStringLiteral("breeze"), QColor(), &error);
                QVERIFY2(!theme.isEmpty(), qPrintable(error));
                const QImage generated(theme + QStringLiteral("/128x128/places/folder.png"));
                QCOMPARE(!generated.isNull(), hidpi);
                if (hidpi) QCOMPARE(generated.pixelColor(64, 64), QColor(Qt::blue));
                QVERIFY(!QImage(theme + QStringLiteral("/16x16/places/folder.png")).isNull());
            }
        }
    }

    void recolouredSymbolic()
    {
        const QByteArray in = R"(<svg><defs><style id="current-color-scheme" type="text/css">.ColorScheme-Text { color:#232629; } .ColorScheme-Highlight { color:#3daee9; }</style></defs>)"
                              R"(<path class="ColorScheme-Text" fill="currentColor" d="m0 0"/><rect class="ColorScheme-Highlight" style="fill:currentColor"/><g fill="currentColor"/></svg>)";
        const QByteArray out = xpl::recolouredSvg(in, QColor(0x41, 0x40, 0x0a));
        QVERIFY2(out == R"(<svg><defs></defs><path class="ColorScheme-Text" fill="#41400a" d="m0 0"/><rect class="ColorScheme-Highlight" style="fill:#3daee9"/><g fill="#41400a"/></svg>)",
                 out.constData());
    }

    void startPanel()
    {
        if (m_realThemes.isEmpty())
            QSKIP("no asset library on this machine");
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(look);
        QString error;
        const QString dir = m_home.filePath(QStringLiteral("menu"));
        const QJsonObject theme = writeStartPanel(*look, dir, &error);
        QVERIFY2(!theme.isEmpty(), qPrintable(error));
        const QJsonObject parts = theme.value(QStringLiteral("parts")).toObject();
        QVERIFY(parts.contains(QStringLiteral("moreprogramsarrow-hot")));
        QCOMPARE(parts.value(QStringLiteral("logoff")).toObject().value(QStringLiteral("sizing")).toArray().at(0).toInt(), 48);
        QCOMPARE(theme.value(QStringLiteral("layout")).toObject().value(QStringLiteral("headerHeight")).toInt(), 64);
        QVERIFY(QFileInfo::exists(dir + QLatin1String("/button-shutdown-hot.png")));
        QVERIFY(QFileInfo::exists(dir + QLatin1String("/theme.json")));
        QVERIFY(writeStartPanel(Look::fromScheme(ClassicScheme()), dir, &error).isEmpty());
    }

    void startPlasmoid()
    {
        // The package ships inside XPlasma; installing replaces what had the
        // id (xp2plasma's Kickoff fork) entirely.
        const QString plasmoids = m_home.filePath(QStringLiteral("plasmoids"));
        writeFile(plasmoids + QLatin1String("/fyi.hotsocket.xplasma.start/contents/ui/Kickoff-leftover.qml"), "x");
        QString error;
        QVERIFY2(installStartPlasmoid(plasmoids, &error), qPrintable(error));
        const QString pkg = plasmoids + QLatin1String("/fyi.hotsocket.xplasma.start");
        QVERIFY(!QFileInfo::exists(pkg + QLatin1String("/contents/ui/Kickoff-leftover.qml")));
        QFile guard(pkg + QLatin1String("/contents/ui/popupguard-v8/libxpmenuguard.so"));
        QVERIFY(guard.open(QIODevice::ReadOnly));
        QCOMPARE(guard.read(4), QByteArray("\x7f" "ELF", 4));
        for (const char *file : {"metadata.json", "contents/config/main.xml", "contents/ui/main.qml", "contents/ui/StartButton.qml",
                                 "contents/ui/StartMenu.qml", "contents/ui/XpFullRepresentation.qml", "contents/ui/ActionMenu.qml"})
            QVERIFY2(QFileInfo::exists(pkg + QLatin1Char('/') + QLatin1String(file)), file);
        QFile meta(pkg + QLatin1String("/metadata.json"));
        QVERIFY(meta.open(QIODevice::ReadOnly));
        const QJsonObject json = QJsonDocument::fromJson(meta.readAll()).object();
        QCOMPARE(json.value(QStringLiteral("KPlugin")).toObject().value(QStringLiteral("Id")).toString(), QStringLiteral("fyi.hotsocket.xplasma.start"));
        QVERIFY(json.value(QStringLiteral("X-XPlasma-Generated")).toBool());
    }

    void plasmaControlsDrawn()
    {
        // Plasma's popup controls, drawn by XPlasma's style: every SVG, for a
        // visual style and for Classic, with the states Plasma lays over.
        if (m_realThemes.isEmpty())
            QSKIP("needs the asset library");
        const auto luna = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(luna);
        if (!xpStyle(*luna))
            QSKIP("XPlasma's style plugin isn't where this test can load it");
        const auto classic = Look::fromScheme(ClassicScheme());
        for (const Look *look : {&*luna, &classic}) {
            const auto controls = plasmaControls(*look);
            for (const char *svg : {"button", "checkmarks", "radiobutton", "lineedit", "listitem", "viewitem",
                                    "bar_meter_horizontal", "slider", "scrollbar", "tabbar", "switch", "arrows"})
                QVERIFY2(controls.contains(QLatin1String(svg)), svg);
            auto has = [&](const char *svg, const char *element) {
                const auto &elements = controls.value(QLatin1String(svg));
                return std::any_of(elements.begin(), elements.end(), [&](const auto &e) {
                    return e.first == QLatin1String(element) && !e.second.isNull();
                });
            };
            QVERIFY(has("button", "normal-center"));
            QVERIFY(has("button", "pressed-center"));
            QCOMPARE(has("button", "hover-center"), !look->classic());   // (Classic's buttons don't light up)
            QVERIFY(has("radiobutton", "symbol"));
            QVERIFY(has("slider", "horizontal-slider-handle"));
            QVERIFY(has("scrollbar", "hint-scrollbar-size"));
            QVERIFY(has("tabbar", "north-active-tab-center"));
            // A pushed button isn't an unpushed one.
            const auto &button = controls.value(QStringLiteral("button"));
            auto element = [&](const char *name) {
                return std::find_if(button.begin(), button.end(), [&](const auto &e) { return e.first == QLatin1String(name); })->second;
            };
            QVERIFY(element("normal-topleft") != element("pressed-topleft"));
            // A check box's empty box: the button shadow's 16px variant only
            // (Plasma draws a check box as a button frame, the shadow over
            // it); the shadow itself nothing, under buttons and combo boxes.
            const QImage box = element("16-16-shadow-center");
            QCOMPARE(box.size(), QSize(16, 16));
            QCOMPARE(qAlpha(box.pixel(0, 0)), 255);   // square, filling it: no button frame round it
            QCOMPARE(qAlpha(box.pixel(15, 15)), 255);
            const QImage none = element("shadow-center");
            for (int y = 0; y < none.height(); ++y)
                for (int x = 0; x < none.width(); ++x)
                    QCOMPARE(qAlpha(none.pixel(x, y)), 0);
            // The ticked box on it: the same box, the tick added.
            const auto &marks = controls.value(QStringLiteral("checkmarks"));
            const QImage ticked = std::find_if(marks.begin(), marks.end(), [](const auto &e) { return e.first == QLatin1String("checkbox"); })->second;
            QCOMPARE(ticked.size(), QSize(16, 16));
            QCOMPARE(ticked.pixel(0, 0), box.pixel(0, 0));
            QVERIFY(ticked != box);
            // A switch (XP's check box): the active bar, drawn from the bar's
            // left to the handle's right edge (Plasma 6.4 even when off), is
            // one piece and the handle 3px, so off it has no width to draw in.
            const auto &sw = controls.value(QStringLiteral("switch"));
            auto swElement = [&](const char *name) {
                const auto it = std::find_if(sw.begin(), sw.end(), [&](const auto &e) { return e.first == QLatin1String(name); });
                return it == sw.end() ? QImage() : it->second;
            };
            QCOMPARE(swElement("handle").width(), 3);
            QVERIFY(!swElement("active-center").isNull());
            QVERIFY(swElement("active-topleft").isNull() && swElement("active-left").isNull());
            QCOMPARE(swElement("active-center").width(), swElement("hint-bar-size").width() - 3);   // the box (bar less 2) less its left edge
            // Focus (and hover) only what they change of the button: not over
            // a check box's middle, where the tick is.
            QCOMPARE(qAlpha(element("focus-center").pixel(element("focus-center").width() / 2, element("focus-center").height() / 2)), 0);

            // The spin box's buttons, XP's up-down on its side: each size a
            // label might ask for, drawn at that size, left unlike right.
            const auto spin = spinIcons(*look);
            for (const char *name : {"value-decrease", "value-increase"}) {
                const auto sizes = spin.value(QLatin1String(name));
                for (const int size : {12, 13, 16, 24, 32}) {
                    QVERIFY2(sizes.contains(size), name);
                    QCOMPARE(sizes.value(size).size(), QSize(size, size));
                }
            }
            QVERIFY(spin.value(QStringLiteral("value-decrease")).value(13) != spin.value(QStringLiteral("value-increase")).value(13));
        }
    }

    void startTheme()
    {
        if (m_realThemes.isEmpty() || m_realIso.isEmpty())
            QSKIP("needs the asset library and the XP ISO");
        State state;
        state.media = m_realIso;
        state.settings.insert(QStringLiteral("installerFeatures"), QJsonObject{{QStringLiteral("explorer"), true}, {QStringLiteral("control_panel"), true}});
        QVERIFY(state.save());
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QString error;
        const QJsonObject theme = writeStartTheme(*look, m_home.filePath(QStringLiteral("start")), &error);
        QVERIFY2(!theme.isEmpty(), qPrintable(error));
        QCOMPARE(theme.value(QStringLiteral("xpExplorerEnabled")).toBool(), true);
        QCOMPARE(theme.value(QStringLiteral("xpControlPanelEnabled")).toBool(), true);
        const QJsonObject button = theme.value(QStringLiteral("button")).toObject();
        QCOMPARE(button.value(QStringLiteral("height")).toInt(), 30);
        for (const char *state : {"normal", "hover", "pressed"})
            QVERIFY(QFileInfo::exists(QUrl(button.value(QLatin1String(state)).toString()).toLocalFile()));
        QVERIFY(theme.value(QStringLiteral("parts")).toObject().value(QStringLiteral("userpane")).toObject()
                    .value(QStringLiteral("url")).toString().startsWith(QLatin1String("file:///")));
        // One of XP's account pictures, for a user without their own: 48x48.
        const QImage picture(QUrl(theme.value(QStringLiteral("userPicture")).toString()).toLocalFile());
        QCOMPARE(picture.size(), QSize(48, 48));
    }

    void flatpakStyle()
    {
        // The style as a KDE runtime extension, one per org.kde.Platform 6
        // branch; the override letting apps read the look, added to the
        // user's own and taken back out alone.
        struct FlatpakSession : RecordingSession {
            QByteArray runtimes;
            host::Result run(const QStringList &argv) override
            {
                host::Result r = RecordingSession::run(argv);
                if (argv.value(0) == QLatin1String("flatpak") && argv.contains(QStringLiteral("--runtime")))
                    r.out = runtimes;
                else if (argv == QStringList{QStringLiteral("flatpak"), QStringLiteral("--default-arch")})
                    r.out = "x86_64\n";
                return r;
            }
        } session;
        session.runtimes = "org.kde.Platform\t6.10\norg.kde.Sdk\t6.10\norg.kde.Platform\t6.9\norg.kde.Platform\t6.10\n"
                           "org.kde.Platform\t5.15-24.08\norg.freedesktop.Platform\t24.08\n";
        QCOMPARE(flatpak::qtRuntimes(session), (QStringList{QStringLiteral("6.10"), QStringLiteral("6.9")}));
        const QString plugin = m_home.filePath(QStringLiteral("libxplasma.so"));
        writeFile(plugin, "not really a plugin");
        // (With what an earlier XPlasma let apps read, to be taken out.)
        writeFile(flatpak::overrideFile(), "[Context]\nfilesystems=home;xdg-config/xplasmarc:ro;\n\n[Environment]\nFOO=1\n");
        // (An empty stand-in Flatpak left in an app's own config folder goes;
        // a real file there stays.)
        writeFile(paths::home() + QLatin1String("/.var/app/org.example.A/config/xplasmarc"), "");
        writeFile(paths::home() + QLatin1String("/.var/app/org.example.B/config/xplasmarc"), "[Theme]\n");
        QStringList log;
        QString error;
        QVERIFY2(flatpak::install(session, plugin, &log, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(paths::home() + QLatin1String("/.var/app/org.example.A/config/xplasmarc")));
        QVERIFY(QFileInfo::exists(paths::home() + QLatin1String("/.var/app/org.example.B/config/xplasmarc")));
        for (const char *branch : {"6.10", "6.9"})
            QVERIFY(QFileInfo::exists(flatpak::extensionRoot() + QLatin1String("/x86_64/") + QLatin1String(branch) + QLatin1String("/styles/libxplasma.so")));
        auto overrides = [] {
            QFile f(flatpak::overrideFile());
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        QVERIFY2(overrides().contains(QLatin1String("filesystems=home;xdg-data/xplasma/active:ro;")), qPrintable(overrides()));
        QVERIFY(overrides().contains(QLatin1String("[Environment]\nFOO=1")));
        // Again: nothing doubled. A runtime gone: its extension too.
        session.runtimes = "org.kde.Platform\t6.10\n";
        QVERIFY(flatpak::install(session, plugin, &log, &error));
        QCOMPARE(overrides().count(QLatin1String("xplasma/active")), 1);
        QVERIFY(!QFileInfo::exists(flatpak::extensionRoot() + QLatin1String("/x86_64/6.9")));
        // Gone: ours only.
        flatpak::remove(&log);
        QVERIFY(!QFileInfo::exists(flatpak::extensionRoot()));
        QVERIFY2(overrides().contains(QLatin1String("filesystems=home;")) && !overrides().contains(QLatin1String("xplasma")), qPrintable(overrides()));
        QVERIFY(overrides().contains(QLatin1String("FOO=1")));
        // An override that was only ours goes whole; no Flatpak: nothing done.
        QFile::remove(flatpak::overrideFile());
        session.runtimes = "org.kde.Platform\t6.10\n";
        QVERIFY(flatpak::install(session, plugin, &log, &error));
        flatpak::remove();
        QVERIFY(!QFileInfo::exists(flatpak::overrideFile()));
        session.runtimes.clear();
        QVERIFY(flatpak::install(session, plugin, &log, &error));
        QVERIFY(!QFileInfo::exists(flatpak::extensionRoot()) && !QFileInfo::exists(flatpak::overrideFile()));
    }

    void installPackage()
    {
        // A package (as the Setup wizard unpacks its payload) into a prefix:
        // the programs, the plugins, the builds of Plasma's widgets, the menu
        // entries pointed at the installed editor; and out again.
        const QString src = m_home.filePath(QStringLiteral("pkg-src"));
        writeFile(src + QLatin1String("/bin/xplasma"), "#!/bin/sh\n");
        writeFile(src + QLatin1String("/bin/xplasma-edit"), "#!/bin/sh\n");
        QFile::setPermissions(src + QLatin1String("/bin/xplasma"), QFile::permissions(src + QLatin1String("/bin/xplasma")) | QFile::ExeOwner);
        QFile::setPermissions(src + QLatin1String("/bin/xplasma-edit"), QFile::permissions(src + QLatin1String("/bin/xplasma-edit")) | QFile::ExeOwner);
        writeFile(src + QLatin1String("/lib/xplasma/qt6/styles/libxplasma.so"), "plugin");
        writeFile(src + QLatin1String("/lib/xplasma/forks/6.7/plasma/applets/org.kde.panel.so"), "fork");
        writeFile(src + QLatin1String("/share/applications/fyi.hotsocket.xplasma.desktop"), "[Desktop Entry]\nName=Display Properties\nExec=xplasma-edit\n");
        writeFile(src + QLatin1String("/share/xplasma/autostart/fyi.hotsocket.xplasma.updates.desktop"),
                  "[Desktop Entry]\nName=XPlasma Updates\nExec=xplasma-edit --check-updates\n");
        const QString tar = m_home.filePath(QStringLiteral("pkg.tar"));
        QCOMPARE(QProcess::execute(QStringLiteral("tar"), {QStringLiteral("-C"), src, QStringLiteral("-cf"), tar, QStringLiteral("bin"), QStringLiteral("lib"),
                                                          QStringLiteral("share")}),
                 0);
        const QString pkg = m_home.filePath(QStringLiteral("pkg"));
        QString error;
        QVERIFY2(archive::extract(tar, pkg, {}, &error), qPrintable(error));
        QVERIFY(installer::isPackage(pkg) || QFileInfo::exists(pkg + QLatin1String("/bin/xplasma")));
        QFile::setPermissions(pkg + QLatin1String("/bin/xplasma"), QFile::permissions(pkg + QLatin1String("/bin/xplasma")) | QFile::ExeOwner);
        const QString prefix = m_home.filePath(QStringLiteral("prefix"));
        QVERIFY2(installer::installFiles(pkg, prefix, {}, &error), qPrintable(error));
        QVERIFY(QFileInfo(prefix + QLatin1String("/bin/xplasma")).isExecutable());
        QVERIFY(QFileInfo::exists(prefix + QLatin1String("/lib/xplasma/qt6/styles/libxplasma.so")));
        QVERIFY(QFileInfo::exists(prefix + QLatin1String("/lib/xplasma/forks/6.7/plasma/applets/org.kde.panel.so")));
        QFile desktop(prefix + QLatin1String("/share/applications/fyi.hotsocket.xplasma.desktop"));
        QVERIFY(desktop.open(QIODevice::ReadOnly));
        QVERIFY(desktop.readAll().contains(("Exec=" + prefix + "/bin/xplasma-edit\n").toUtf8()));
        // (the update check run at login, pointed at it too)
        const QString autostart = paths::config() + QLatin1String("/autostart/fyi.hotsocket.xplasma.updates.desktop");
        QVERIFY(readFile(autostart).contains(("Exec=" + prefix + "/bin/xplasma-edit --check-updates").toUtf8()));
        QCOMPARE(installer::installedPrefix(), prefix);
        // (The downloads stay unless asked, apart from what was taken out.)
        writeFile(paths::downloads() + QLatin1String("/xp.iso"), "iso");
        QCOMPARE(installer::downloadsSize(), qint64(3));
        installer::removeFiles(prefix, false, false, {});
        QVERIFY(!QFileInfo::exists(autostart));
        QVERIFY(QFileInfo::exists(paths::downloads() + QLatin1String("/xp.iso")));
        QVERIFY(!QFileInfo::exists(prefix + QLatin1String("/bin/xplasma")) && !QFileInfo::exists(prefix + QLatin1String("/lib/xplasma")));
        QVERIFY(!QFileInfo::exists(prefix + QLatin1String("/lib")));   // (its folders too, emptied)
        QVERIFY(QFileInfo::exists(prefix));
        QVERIFY(installer::installedPrefix().isEmpty());
        // What was taken out gone, the image kept; then the image too.
        writeFile(paths::xpFonts() + QLatin1String("/tahoma.ttf"), "font");
        installer::removeFiles(prefix, true, false, {});
        QVERIFY(!QFileInfo::exists(paths::xpFonts()) && QFileInfo::exists(paths::downloads() + QLatin1String("/xp.iso")));
        installer::removeFiles(prefix, false, true, {});
        QVERIFY(!QFileInfo::exists(paths::downloads() + QLatin1String("/xp.iso")));
    }

    void gtkTheme()
    {
        // The booster: every selector's subject outranks Adwaita's; @-rules
        // and comments as they were.
        const QString css = gtk::boosted(QStringLiteral("@define-color a #fff;\n/* note */\nbutton, notebook > header tab:checked { x: 1; }\n"));
        QVERIFY2(css.contains(QLatin1String("@define-color a #fff;")) && css.contains(QLatin1String("/* note */")), qPrintable(css));
        QVERIFY2(css.contains(QLatin1String("button:not(.xp-1):not(.xp-2):not(.xp-3):not(.xp-4),\nnotebook > header tab:checked:not(.xp-1)")),
                 qPrintable(css));

        if (m_realThemes.isEmpty())
            QSKIP("needs the asset library");
        const auto luna = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(luna);
        if (!xpStyle(*luna))
            QSKIP("XPlasma's style plugin isn't where this test can load it");
        const Look classic = Look::fromScheme(ClassicScheme());
        for (const Look *look : {&*luna, &classic}) {
            QString error;
            const QString root = gtk::writeTheme(*look, m_home.filePath(QStringLiteral("gtk-out")), &error);
            QVERIFY2(!root.isEmpty(), qPrintable(error));
            QFile gtk3(root + QLatin1String("/gtk-3.0/gtk.css")), layer(root + QLatin1String("/gtk-4.0/xplasma.css")), gtk4(root + QLatin1String("/gtk-4.0/gtk.css"));
            QVERIFY(gtk3.open(QIODevice::ReadOnly) && layer.open(QIODevice::ReadOnly) && gtk4.open(QIODevice::ReadOnly));
            const QString text3 = QString::fromUtf8(gtk3.readAll()), text4 = QString::fromUtf8(layer.readAll());
            // On Adwaita, every placeholder filled, every image there.
            QVERIFY(text3.startsWith(QLatin1String("@import url(\"resource:///org/gtk/libgtk/theme/Adwaita/gtk-contained.css\");")));
            QVERIFY(QString::fromUtf8(gtk4.readAll()).contains(QLatin1String("@import url(\"xplasma.css\");")));
            for (const QString &t : {text3, text4}) {
                QVERIFY2(!t.contains(QLatin1String("${")), qPrintable(t.mid(t.indexOf(QLatin1String("${")) - 40, 80)));
                static const QRegularExpression url(QStringLiteral("url\\(\"\\.\\./assets/([^\"]+)\"\\)"));
                for (auto it = url.globalMatch(t); it.hasNext();) {
                    const QString file = it.next().captured(1);
                    QVERIFY2(QFileInfo::exists(root + QLatin1String("/assets/") + file), qPrintable(file));
                }
            }
            // XP's scroll bar arrows in GTK 3 (steppers), none asked of GTK 4.
            QVERIFY(text3.contains(QLatin1String("-GtkScrollbar-has-backward-stepper: true")));
            QVERIFY(!text4.contains(QLatin1String("Stepper")));
            // A button's middle as the background (GTK 3 leaves a border-image's undrawn).
            QVERIFY(QFileInfo::exists(root + QLatin1String("/assets/button-normal-fill.png")));
        }

        // Applied: GTK switched, overlay scroll bars off, the libadwaita line
        // added after KDE's own; reset takes back exactly that.
        RecordingSession session;
        session.config.insert(QStringLiteral("gtk-3.0/settings.ini|Settings|gtk-theme-name"), QStringLiteral("Breeze"));
        const QString user = paths::config() + QLatin1String("/gtk-4.0/gtk.css");
        writeFile(user, "@import 'colors.css';");
        State state;
        QStringList log;
        QString error;
        QVERIFY2(gtk::apply(session, *luna, state, &log, &error), qPrintable(error));
        QCOMPARE(session.config.value(QStringLiteral("gtk-3.0/settings.ini|Settings|gtk-theme-name")), gtk::themeName(*luna));
        QCOMPARE(session.config.value(QStringLiteral("gtk-4.0/settings.ini|Settings|gtk-theme-name")), gtk::themeName(*luna));
        QVERIFY(session.calls.join(QLatin1Char(' ')).contains(QLatin1String("setGtkTheme")));
        QCOMPARE(state.settings.value(QStringLiteral("previousGtkTheme")).toString(), QStringLiteral("Breeze"));
        QVERIFY(QFileInfo::exists(paths::localShare() + QLatin1String("/themes/") + gtk::themeName(*luna) + QLatin1String("/gtk-3.0/gtk.css")));
        auto userText = [&] {
            QFile f(user);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        QVERIFY2(userText().startsWith(QLatin1String("@import 'colors.css';\n@import url(\"file://")) && userText().contains(QLatin1String("xplasma.css")),
                 qPrintable(userText()));
        // Flatpak's GTK apps: the theme as an org.gtk.Gtk3theme extension (the
        // theme's gtk-3.0 folder, its images inside), and the GTK 4 stylesheet
        // and the themes readable, for libadwaita apps.
        {
            const QString ext = flatpak::gtkExtensionRoot(gtk::themeName(*luna)) + QLatin1String("/x86_64/3.22");
            QFile css(ext + QLatin1String("/gtk.css"));
            QVERIFY(css.open(QIODevice::ReadOnly));
            const QByteArray text = css.readAll();
            QVERIFY(text.contains("url(\"assets/button-normal.png\")") && !text.contains("../assets/"));
            QVERIFY(QFileInfo::exists(ext + QLatin1String("/assets/button-normal.png")));
            QFile overrides(flatpak::overrideFile());
            QVERIFY(overrides.open(QIODevice::ReadOnly));
            const QByteArray o = overrides.readAll();
            QVERIFY2(o.contains("xdg-config/gtk-4.0:ro") && o.contains("xdg-data/themes:ro"), o.constData());
        }
        // Again: one line, not two.
        QVERIFY(gtk::apply(session, *luna, state, &log, &error));
        QCOMPARE(userText().count(QLatin1String("xplasma.css")), 1);
        gtk::revert(session, state, &log);
        QCOMPARE(session.config.value(QStringLiteral("gtk-3.0/settings.ini|Settings|gtk-theme-name")), QStringLiteral("Breeze"));
        QCOMPARE(userText(), QStringLiteral("@import 'colors.css';\n"));
        QVERIFY(!QFileInfo::exists(paths::localShare() + QLatin1String("/themes/") + gtk::themeName(*luna)));
        QVERIFY(!state.settings.contains(QStringLiteral("previousGtkTheme")));
        QVERIFY(!QFileInfo::exists(flatpak::gtkExtensionRoot(gtk::themeName(*luna))));
        QVERIFY(!QFileInfo::exists(flatpak::overrideFile()));
    }

    void startButtonExtrasApplied()
    {
        // Extras... applied alone: the button redrawn with the user's label
        // (State's settings) and handed to the running Start buttons.
        if (m_realThemes.isEmpty() || m_realIso.isEmpty())
            QSKIP("needs the asset library and the XP ISO");
        State state;
        state.media = m_realIso;
        state.settings.insert(QLatin1String(kStartButtonKey), StartButtonExtras{QStringLiteral("start me up"), {}, {}}.toJson());
        QVERIFY(state.save());
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        RecordingSession session;
        QString error;
        QVERIFY2(applyStartButton(session, *look, &error), qPrintable(error));
        QCOMPARE(session.scripts.size(), 1);
        const QString js = session.scripts.first();
        QVERIFY(js.contains(QLatin1String(kStartPlasmoidId)) && js.contains(QLatin1String("writeConfig('theme'")));
        const QRegularExpression width(QStringLiteral("\\\\?\"width\\\\?\":(\\d+)"));
        const auto m = width.match(js);
        QVERIFY2(m.hasMatch(), qPrintable(js.left(300)));
        QVERIFY(m.captured(1).toInt() > 99);   // wider than XP's "start"
    }

    // --- applying to the session --------------------------------------------------

    void scripts()
    {
        const QString js = scriptFrom(QStringLiteral("wallpaper"), {{QStringLiteral("IMAGE"), QStringLiteral("file:///a \"b\".jpg")},
                                                                     {QStringLiteral("FILL"), 2},
                                                                     {QStringLiteral("COLOUR"), QStringLiteral("#3a6ea5")}});
        QVERIFY(js.contains(QLatin1String("d.writeConfig('Image', \"file:///a \\\"b\\\".jpg\")")));
        QVERIFY(js.contains(QLatin1String("d.writeConfig('FillMode', 2)")));
        QVERIFY(!js.contains(QLatin1String("${")));
        const QString none = scriptFrom(QStringLiteral("wallpaper"), {{QStringLiteral("IMAGE"), QVariant::fromValue(nullptr)},
                                                                       {QStringLiteral("FILL"), QVariant::fromValue(nullptr)},
                                                                       {QStringLiteral("COLOUR"), QStringLiteral("#000000")}});
        QVERIFY(none.contains(QLatin1String("if (null === null)")));
    }

    void overridesPatchThisPlasma()
    {
        // The anchors hold against the Plasma on this machine.
        const QString tray = QStringLiteral("/usr/share/plasma/plasmoids/org.kde.plasma.systemtray/contents/ui/main.qml");
        if (!QFileInfo::exists(tray))
            QSKIP("no Plasma here");
        const QString plasmoids = m_home.filePath(QStringLiteral("plasmoids"));
        QString error;
        // A copy of the Plasma clock XPlasma once patched goes: the clock is
        // XPlasma's own now.
        writeFile(plasmoids + QLatin1String("/org.kde.plasma.digitalclock/metadata.json"), "{\"X-XPlasma-Panel-Override\": true}");
        QVERIFY2(overrides::installPanelWidgets(plasmoids, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.digitalclock")));
        // What the clock runs on a double-click: the editor, quoted, as its own unit.
        const QString run = QStringLiteral("'/opt/it'\\''s here/xplasma-edit' '--date-time'");
        QCOMPARE(overrides::launchCommand(QStringLiteral("/opt/it's here/xplasma-edit"), {QStringLiteral("--date-time")}),
                 QStringLiteral("systemd-run --user --collect --quiet -- %1 || %1").arg(run));
        QFile patchedTray(plasmoids + QLatin1String("/org.kde.plasma.systemtray/contents/ui/main.qml"));
        QVERIFY(patchedTray.open(QIODevice::ReadOnly));
        const QByteArray text = patchedTray.readAll();
        QVERIFY(text.contains("xpTrayAvailable") && text.contains("imagePath: \"widgets/xp-tray\"") && !text.contains("{{"));
        QVERIFY(QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.systemtray/contents/ui/PlasmaExpanderArrow.qml")));
        QFile metrics(plasmoids + QLatin1String("/org.kde.plasma.taskmanager/contents/ui/code/layoutmetrics.js"));
        QVERIFY(metrics.open(QIODevice::ReadOnly) && metrics.readAll().contains("tasks.xpTaskband"));
        // Balloons: the popups' delegate chosen by the art, the queue's icons in the tray.
        const QString notifications = plasmoids + QLatin1String("/org.kde.plasma.notifications/contents/ui/");
        QFile globals(notifications + QLatin1String("global/Globals.qml"));
        QVERIFY(globals.open(QIODevice::ReadOnly));
        const QByteArray globalsText = globals.readAll();
        QVERIFY(globalsText.contains("delegate: globals.xpBalloons ? xpDelegate : stockDelegate")
                && globalsText.contains("function xpAdvance()") && !globalsText.contains("{{"));
        for (const char *f : {"XpPopup.qml", "XpBalloon.qml", "XpCopyDialog.qml", "XpButton.qml", "main.qml"})
            QVERIFY2(QFileInfo(notifications + QLatin1String(f)).size() > 0, f);
        QVERIFY(text.contains("XpNotificationIcons {"));
        QVERIFY(QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.systemtray/contents/ui/XpNotificationIcons.qml")));
        // The desktop's menu: XPlasma Plus Properties, starting the editor outside the shell.
        QVERIFY2(overrides::installDesktopMenu(plasmoids, QStringLiteral("/opt/it's here/xplasma-edit"), &error), qPrintable(error));
        QFile layer(plasmoids + QLatin1String("/org.kde.desktopcontainment/contents/ui/FolderViewLayer.qml"));
        QVERIFY(layer.open(QIODevice::ReadOnly));
        const QByteArray desktop = layer.readAll();
        QVERIFY(desktop.contains("text: \"XPlasma Plus Properties\""));
        QVERIFY2(desktop.contains(R"(connectSource("systemd-run --user --collect --quiet -- '/opt/it'\\''s here/xplasma-edit' || '/opt/it'\\''s here/xplasma-edit'"))"),
                 desktop.mid(desktop.indexOf("connectSource"), 160).constData());
        QVERIFY(desktop.contains("Plasmoid.contextualActions.push(xpPropertiesAction)") && !desktop.contains("@LAUNCH@") && !desktop.contains("{{"));
        QFile folderView(plasmoids + QLatin1String("/org.kde.desktopcontainment/contents/ui/FolderView.qml"));
        QVERIFY(folderView.open(QIODevice::ReadOnly));
        const QByteArray drops = folderView.readAll();
        QVERIFY(drops.contains("dir.drop(target, xpShortcutDrop(event)") && drops.contains("function xpShortcutDrop(event)"));
        // Theme application regenerates the desktop override from stock QML.
        // Each regeneration must retain one clear, dashed XP selection outline.
        QVERIFY(drops.contains("XPlasma: classic XP desktop selection"));
        QVERIFY(drops.contains("ctx.setLineDash([2, 2])"));
        for (int i = 0; i < 2; ++i) {
            QVERIFY2(overrides::installDesktopMenu(plasmoids, QStringLiteral("/opt/xplasma-edit"), &error), qPrintable(error));
            QFile reapplied(plasmoids + QLatin1String("/org.kde.desktopcontainment/contents/ui/FolderView.qml"));
            QVERIFY(reapplied.open(QIODevice::ReadOnly));
            const QByteArray qml = reapplied.readAll();
            QCOMPARE(qml.count("XPlasma: classic XP desktop selection"), 1);
            QVERIFY(qml.contains("color: \"transparent\""));
        }
        overrides::removeDesktopMenu(plasmoids);
        QVERIFY(!QFileInfo::exists(plasmoids + QLatin1String("/org.kde.desktopcontainment")));
        // Again (it's ours now), then gone.
        QVERIFY2(overrides::installPanelWidgets(plasmoids, &error), qPrintable(error));
        overrides::removePanelWidgets(plasmoids);
        QVERIFY(!QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.systemtray")));
        // Someone else's is left alone.
        writeFile(plasmoids + QLatin1String("/org.kde.plasma.taskmanager/metadata.json"), "{}");
        QVERIFY(!overrides::installPanelWidgets(plasmoids, &error));
        QVERIFY(error.contains(QLatin1String("someone else")));
        QVERIFY(!overrides::patched(QStringLiteral("systemtray/main.qml"), QStringLiteral("not KDE's QML")));
    }

    void recoversOrphanedDesktopTextHelper()
    {
        if (!QFileInfo::exists(QStringLiteral("/usr/share/plasma/plasmoids/org.kde.desktopcontainment/contents/ui/FolderView.qml")))
            QSKIP("no desktop containment here");
        const QString plasmoids = m_home.filePath(QStringLiteral("orphan-plasmoids"));
        QString error;
        // An old uninstall may leave only the shared helper, without metadata.
        const QString orphan = plasmoids + QStringLiteral("/org.kde.desktopcontainment");
        writeFile(orphan + QStringLiteral("/contents/ui/XpTextStyle.qml"),
                  overrides::resourceText(QStringLiteral("XpTextStyle.qml")).toUtf8());
        // Additional files still make this an unknown override: preserve it.
        writeFile(orphan + QStringLiteral("/custom.qml"), "// user's widget");
        QVERIFY(!overrides::installDesktopMenu(plasmoids, QStringLiteral("/opt/xplasma-edit"), &error));
        QVERIFY(QFileInfo::exists(orphan + QStringLiteral("/custom.qml")));
        QVERIFY(QFile::remove(orphan + QStringLiteral("/custom.qml")));
        QVERIFY2(overrides::installDesktopMenu(plasmoids, QStringLiteral("/opt/xplasma-edit"), &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(orphan + QStringLiteral("/metadata.json")));
        overrides::removeDesktopMenu(plasmoids);
    }

    void desktopSelectionSurvivesThemeReapply()
    {
        if (!QFileInfo::exists(QStringLiteral("/usr/share/plasma/plasmoids/org.kde.desktopcontainment/contents/ui/FolderView.qml")))
            QSKIP("no desktop containment here");
        const QString plasmoids = m_home.filePath(QStringLiteral("selection-plasmoids"));
        QString error;
        for (int i = 0; i < 3; ++i) {
            QVERIFY2(overrides::installDesktopMenu(plasmoids, QStringLiteral("/opt/xplasma-edit"), &error), qPrintable(error));
            QFile file(plasmoids + QLatin1String("/org.kde.desktopcontainment/contents/ui/FolderView.qml"));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray qml = file.readAll();
            QCOMPARE(qml.count("XPlasma: classic XP desktop selection"), 1);
            QVERIFY(qml.contains("ctx.setLineDash([2, 2])"));
            QFile iconFile(plasmoids + QLatin1String("/org.kde.desktopcontainment/contents/ui/FolderItemDelegate.qml"));
            QVERIFY(iconFile.open(QIODevice::ReadOnly));
            const QByteArray iconQml = iconFile.readAll();
            QCOMPARE(iconQml.count("XPlasma: XP desktop icon selection:"), 1);
            QCOMPARE(iconQml.count("XPlasma: XP blends a selected desktop icon"), 1);
            QVERIFY(iconQml.contains("width: label.contentWidth + 4"));
            QVERIFY(iconQml.contains("opacity: 0.5"));
            QVERIFY(iconQml.contains("visible: !main.xpOnDesktop"));
            QVERIFY(iconQml.contains("visible: main.xpOnDesktop && main.xpSelected"));

            QVERIFY(qml.contains("color: \"transparent\""));
        }
        overrides::removeDesktopMenu(plasmoids);
    }

    void overridesAllOrNothing()
    {
        // A Plasma that builds some widgets into their plugins (6.5 on): those
        // are the ones XPlasma's own builds stand in for; the rest are
        // patched, all or nothing, and a copy of ours of a built-in one goes.
        const QString tray = QStringLiteral("/usr/share/plasma/plasmoids/org.kde.plasma.systemtray");
        if (!QFileInfo::exists(tray))
            QSKIP("no Plasma here");
        const QString system = m_home.filePath(QStringLiteral("system-share"));
        QDir().mkpath(system + QLatin1String("/plasma/plasmoids"));
        QVERIFY(QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-r"), tray, system + QLatin1String("/plasma/plasmoids/")}) == 0);
        const QByteArray saved = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", system.toUtf8());
        const QString plasmoids = m_home.filePath(QStringLiteral("plasmoids-6.6"));
        writeFile(plasmoids + QLatin1String("/org.kde.plasma.taskmanager/metadata.json"), "{\"X-XPlasma-Panel-Override\": true}");
        const QStringList builtIn = overrides::builtInPanelWidgets();
        QString checked, installed;
        const bool can = overrides::checkPanelWidgets(plasmoids, &checked);
        const bool did = overrides::installPanelWidgets(plasmoids, &installed);
        qputenv("XDG_DATA_DIRS", saved);
        QCOMPARE(builtIn, (QStringList{QStringLiteral("org.kde.plasma.taskmanager"), QStringLiteral("org.kde.panel"),
                                       QStringLiteral("org.kde.plasma.notifications")}));
        QVERIFY2(can && did, qPrintable(checked + installed));
        QVERIFY(QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.systemtray/contents/ui/XpNotificationIcons.qml")));
        QVERIFY(!QFileInfo::exists(plasmoids + QLatin1String("/org.kde.plasma.taskmanager")));
    }

    void forkChoice()
    {
        // Down the shipped builds, the first that loads here: on this Plasma
        // a 6.6 build doesn't (older Qt than its floor, libPlasma.so.6 not
        // .7 -- whichever the loader meets first), and there is none for 6.4.
        if (paths::shippedForks().isEmpty() || !QFileInfo::exists(paths::shippedForks() + QLatin1String("/6.6")))
            QSKIP("no 6.6 builds here (./build-forks.sh 6.6, then ./build.sh)");
        if (!QFileInfo::exists(QStringLiteral("/usr/lib/libPlasma.so.6")))
            QSKIP("this test expects Plasma 6.4's libraries");
        const QStringList ids{QStringLiteral("org.kde.plasma.systemtray")};
        QString error;
        QVERIFY(!forks::choose(QStringLiteral("6.6.4"), ids, &error));
        QVERIFY2(error.contains(QLatin1String("the 6.6 build")) && error.contains(QLatin1String("org.kde.plasma.systemtray.so")), qPrintable(error));
        QVERIFY(!forks::choose(QStringLiteral("6.4.3"), ids, &error));
        QVERIFY2(error.contains(QLatin1String("no build")), qPrintable(error));
        QVERIFY(!forks::choose(QString(), ids, &error));
    }

    void applyRefusesPartialLook()
    {
        // Applying on a Plasma that can't take every override changes
        // nothing at all: no programs run, no settings written.
        const Look look = Look::fromScheme(ClassicScheme());
        const QString system = m_home.filePath(QStringLiteral("empty-share"));
        QDir().mkpath(system);
        const QByteArray saved = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", system.toUtf8());
        RecordingSession session;
        ApplyOptions o;
        QString error;
        const bool applied = applyLook(session, look, o, &error);
        qputenv("XDG_DATA_DIRS", saved);
        QVERIFY(!applied);
        QVERIFY2(error.contains(QLatin1String("nothing was changed")), qPrintable(error));
        // Shipped widget plugins may load even with an empty XDG_DATA_DIRS.
        // The absent Aurorae renderer must still reject application.
        QVERIFY2(error.contains(QLatin1String("Aurorae")), qPrintable(error));
        // (Only asking which Plasma it is.)
        for (const QStringList &run : std::as_const(session.runs))
            QVERIFY2(run.contains(QStringLiteral("plasmashell")) && run.contains(QStringLiteral("--version")), qPrintable(run.join(QLatin1Char(' '))));
        QVERIFY(session.config.isEmpty());
        QVERIFY(session.scripts.isEmpty());
    }

    void strictPatchVariants()
    {
        const QJsonArray rules{QJsonObject{
            {"find", "modern-layout"}, {"replace", "xp-layout"},
            {"alternatives", QJsonArray{
                QJsonObject{{"find", "legacy-a"}, {"replace", "xp-layout"}},
                QJsonObject{{"find", "legacy-b"}, {"replace", "xp-layout"}}
            }}
        }};
        QString error;
        for (const QString &source : {QStringLiteral("modern-layout"), QStringLiteral("legacy-a"), QStringLiteral("legacy-b")}) {
            const auto result = overrides::patched(QStringLiteral("test.qml"), source, &error, &rules);
            QVERIFY2(result, qPrintable(error));
            QCOMPARE(*result, QStringLiteral("xp-layout"));
        }
        QVERIFY(!overrides::patched(QStringLiteral("test.qml"), QStringLiteral("unknown-layout"), &error, &rules));
        QVERIFY(!overrides::patched(QStringLiteral("test.qml"), QStringLiteral("modern-layout modern-layout"), &error, &rules));
        QVERIFY(!overrides::patched(QStringLiteral("test.qml"), QStringLiteral("legacy-a legacy-b"), &error, &rules));
        QVERIFY(error.contains(QStringLiteral("ambiguous")));
    }

    void retiredLiveColorsOnlyDisableConfiguration()
    {
        struct LiveSessionRecorder : RecordingSession {
            bool available=true;
            std::optional<QString> script(const QString &js,QString *) override {scripts<<js;return available ? QStringLiteral("1") : QStringLiteral("0");}
        } session;
        QString error;
        const QJsonObject colors{{QStringLiteral("enabled"),true},{QStringLiteral("start"),QStringLiteral("#3a7040")},
            {QStringLiteral("menu"),QStringLiteral("#26384a")},{QStringLiteral("startText"),1},{QStringLiteral("menuText"),2}};
        QVERIFY2(applyLiveStartColors(session,colors,&error),qPrintable(error));
        QVERIFY(session.runs.isEmpty());
        QVERIFY(session.scripts.last().contains(QLatin1String("writeConfig('liveColors'")));
        QVERIFY(!session.scripts.last().contains(QLatin1String("#26384a")));
        QCOMPARE(QJsonDocument::fromJson(readFile(paths::config()+"/xplasma-live-colors.json")).object(),QJsonObject());
        QVERIFY(applyLiveStartColors(session,{},&error));
        session.available=false;
        QVERIFY(!applyLiveStartColors(session,colors,&error));
        QVERIFY(error.contains(QLatin1String("Apply the base theme once")));
    }

    void liveSystemPaletteRestoresOwnedKeys()
    {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        QSettings base(directory.filePath("kdeglobals"),QSettings::IniFormat);
        writeFile(directory.filePath("kdeglobals"),"[Colors:Selection]\nBackgroundNormal=49,106,197\n[Colors:Window]\nBackgroundNormal=236,233,216\n[General]\nfont=Tahoma,8\n");base.sync();
        bool changed=false;QString error;
        QJsonObject colors{{"enabled",true},{"menu","#315c43"},{"menuText",2},{"title","#644576"},{"titleText",1}};
        QVERIFY2(applyLiveSystemColors(colors,directory.path(),&changed,&error),qPrintable(error));QVERIFY(changed);
        base.sync();QCOMPARE(base.value("Colors:Selection/BackgroundNormal").toStringList().join(','),QString("49,92,67"));
        QCOMPARE(base.value("Colors:Selection/ForegroundNormal").toStringList().join(','),QString("255,255,255"));
        QCOMPARE(base.value("WM/activeBackground").toStringList().join(','),QString("100,69,118"));
        QCOMPARE(base.value("Colors:Window/BackgroundNormal").toStringList().join(','),QString("236,233,216"));
        QCOMPARE(base.value("font").toStringList().join(','),QString("Tahoma,8"));
        QVERIFY(applyLiveSystemColors(colors,directory.path(),&changed,&error));QVERIFY(!changed);
        colors.insert("menu","#644576");QVERIFY(applyLiveSystemColors(colors,directory.path(),&changed,&error));
        QFile raw(directory.filePath("kdeglobals"));QVERIFY(raw.open(QIODevice::ReadOnly));auto bytes=raw.readAll();raw.close();
        QVERIFY(bytes.contains("[Colors:Selection]"));QVERIFY(!bytes.contains("%3A"));QVERIFY(bytes.contains("BackgroundNormal=100,69,118"));
        bytes.replace("ForegroundLink=100,69,118","ForegroundLink=9,8,7");writeFile(directory.filePath("kdeglobals"),bytes);base.sync(); // External edits are preserved.
        QVERIFY(applyLiveSystemColors({},directory.path(),&changed,&error));base.sync();
        QCOMPARE(base.value("Colors:Selection/BackgroundNormal").toStringList().join(','),QString("49,106,197"));
        QCOMPARE(base.value("Colors:View/ForegroundLink").toStringList().join(','),QString("9,8,7"));
        QVERIFY(!base.contains("WM/activeBackground"));QVERIFY(!QFileInfo::exists(directory.filePath("xplasma-live-system-base.json")));
    }

    void liveSystemPaletteRepairsLegacyQtEncoding()
    {
        QTemporaryDir directory;const QString path=directory.filePath("kdeglobals");
        writeFile(path,"[Colors%3ASelection]\nBackgroundNormal=49,106,197\n[Colors%3AWindow]\nBackgroundNormal=236, 233, 216\n[General]\nfont=Tahoma,8\n");
        bool changed=false;QString error;
        QVERIFY2(applyLiveSystemColors({{"enabled",true},{"menu","#315c43"}},directory.path(),&changed,&error),qPrintable(error));
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));auto data=file.readAll();file.close();
        QVERIFY(!data.contains("%3A"));QVERIFY(data.contains("[Colors:Window]"));QVERIFY(data.contains("BackgroundNormal=236, 233, 216"));QVERIFY(data.contains("BackgroundNormal=49,92,67"));
        QVERIFY(applyLiveSystemColors({},directory.path(),&changed,&error));QVERIFY(file.open(QIODevice::ReadOnly));data=file.readAll();
        QVERIFY(data.contains("BackgroundNormal=49,106,197"));QVERIFY(data.contains("font=Tahoma,8"));
    }

    void themeUpdateStopsRunningKRunner()
    {
        RecordingSession session;
        session.runnerActive = true;
        const QStringList stop{QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("stop"), QStringLiteral("plasma-krunner.service")};
        const QStringList start{QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("start"), QStringLiteral("plasma-krunner.service")};
        session.restartPlasmaShell([&] {
            QVERIFY(session.runs.contains(stop));
            QVERIFY(!session.runs.contains(start));
        });
        QVERIFY(session.runs.indexOf(start) > session.runs.indexOf(stop));
        session.runs.clear();
        session.runnerActive = false;
        session.restartPlasmaShell([] {});
        QVERIFY(!session.runs.contains(stop));
        QVERIFY(!session.runs.contains(start));
    }

    void restartWithoutSystemd()
    {
        // A session without systemd (Slackware's): no user unit to stop and
        // start, so plasmashell is asked to quit and started again, in this
        // program's environment (XPlasma's plugin path in it).
        RecordingSession session;
        session.systemd = false;
        bool stopped = false;
        session.restartPlasmaShell([&] {
            stopped = std::any_of(session.runs.begin(), session.runs.end(), [](const QStringList &a) {
                return a == QStringList{QStringLiteral("kquitapp6"), QStringLiteral("plasmashell")};
            });
        });
        QVERIFY(stopped);
        QCOMPARE(session.detached, (QList<QStringList>{{QStringLiteral("plasmashell")}}));
        QVERIFY(std::none_of(session.runs.begin(), session.runs.end(), [](const QStringList &a) {
            return a.value(0) == QLatin1String("systemctl") && (a.contains(QStringLiteral("stop")) || a.contains(QStringLiteral("start")));
        }));
    }

    void optionalPatchRules()
    {
        // An optional rule is for what only some Plasmas have: skipped where
        // its text isn't, applied where it is -- but twice is still an error.
        const QJsonArray rules{
            QJsonObject{{QStringLiteral("find"), QStringLiteral("new line\n")}, {QStringLiteral("replace"), QString()},
                        {QStringLiteral("optional"), true}},
            QJsonObject{{QStringLiteral("find"), QStringLiteral("id: grid\n")}, {QStringLiteral("replace"), QStringLiteral("id: grid\nours\n")}},
        };
        QString error;
        QCOMPARE(overrides::patched(QStringLiteral("x.qml"), QStringLiteral("id: grid\n"), &error, &rules).value_or(QString()),
                 QStringLiteral("id: grid\nours\n"));
        QCOMPARE(overrides::patched(QStringLiteral("x.qml"), QStringLiteral("id: grid\nnew line\n"), &error, &rules).value_or(QString()),
                 QStringLiteral("id: grid\nours\n"));
        QVERIFY(!overrides::patched(QStringLiteral("x.qml"), QStringLiteral("id: grid\nnew line\nnew line\n"), &error, &rules));
    }

    void forkSources()
    {
        // A Plasma series' source, patched into a work tree: the patch, the
        // build file's changes, an added file; a second sync writes nothing;
        // one that can't patch writes nothing at all.
        QVERIFY(forks::seriesNames().contains(QStringLiteral("6.6")));
        const QString pristine = m_home.filePath(QStringLiteral("fork/pristine")), work = m_home.filePath(QStringLiteral("fork/work"));
        const QString js = QStringLiteral("function preferredMaxWidth() {\n    if (tasks.iconsOnly) {\n        return 1;\n    }\n}\n");
        writeFile(pristine + QLatin1String("/plasma-desktop-9.9/applets/tm/code/metrics.js"), js.toUtf8());
        writeFile(pristine + QLatin1String("/plasma-desktop-9.9/applets/tm/CMakeLists.txt"), "plasma_add_applet(org.example.tm)\n");
        writeFile(pristine + QLatin1String("/plasma-desktop-9.9/README"), "unchanged\n");
        forks::Series series;
        series.name = QStringLiteral("test");
        series.sources = {{QStringLiteral("plasma-desktop"), QStringLiteral("9.9"), QString()}};
        forks::Widget w;
        w.id = QStringLiteral("org.example.tm");
        w.project = QStringLiteral("plasma-desktop");
        w.dir = QStringLiteral("applets/tm");
        w.files.insert(QStringLiteral("taskmanager/code/layoutmetrics.js"), QStringLiteral("code/metrics.js"));
        w.extras.insert(QStringLiteral("XpButton.qml"), QStringLiteral("XpButton.qml"));
        w.registered.insert(QString(), {QStringLiteral("XpButton.qml")});
        series.widgets = {w};
        QString error;
        auto written = forks::syncSources(series, pristine, work, &error);
        QVERIFY2(written, qPrintable(error));
        QCOMPARE(*written, 4);
        const QString tree = work + QLatin1String("/plasma-desktop-9.9");
        QVERIFY(readFile(tree + QLatin1String("/applets/tm/code/metrics.js")).contains("xpTaskband"));
        const QByteArray cmake = readFile(tree + QLatin1String("/applets/tm/CMakeLists.txt"));
        QVERIFY(cmake.contains("QT_QML_SKIP_CACHEGEN TRUE") && cmake.indexOf("QT_QML_SKIP_CACHEGEN") < cmake.indexOf("plasma_add_applet"));
        QVERIFY(cmake.contains("ecm_target_qml_sources(org.example.tm SOURCES XpButton.qml)"));
        QVERIFY(QFileInfo::exists(tree + QLatin1String("/applets/tm/XpButton.qml")));
        written = forks::syncSources(series, pristine, work, &error);
        QVERIFY(written);
        QCOMPARE(*written, 0);
        // A source that has moved on: refused, nothing touched.
        writeFile(pristine + QLatin1String("/plasma-desktop-9.9/applets/tm/code/metrics.js"), "// something else\n");
        writeFile(pristine + QLatin1String("/plasma-desktop-9.9/README"), "changed\n");
        QVERIFY(!forks::syncSources(series, pristine, work, &error));
        QVERIFY(error.contains(QLatin1String("org.example.tm")));
        QCOMPARE(readFile(tree + QLatin1String("/README")), QByteArray("unchanged\n"));
    }

    void xpTextRules()
    {
        // XP's Tahoma 8 (11 px) is one-bit text: its gasp table says no
        // greyscale there, and the rules say so to fontconfig.
        if (m_realFonts.isEmpty())
            QSKIP("no XP fonts on this machine");
        const QString rules = QString::fromUtf8(xptext::rules(m_realFonts));
        QVERIFY(rules.contains(QLatin1String("<family>sans-serif</family>")));
        QVERIFY(!rules.contains(QLatin1String("<string>Arial</string>")));
        QVERIFY(!rules.contains(QLatin1String("<string>Times New Roman</string>")));
        QVERIFY(rules.contains(QLatin1String("<const>hintfull</const>")));
        QVERIFY(rules.contains(QLatin1String("<const>none</const>")));
        bool crisp11 = false, kern = false;
        for (const QString &block : rules.split(QLatin1String("<match target=\"font\">"))) {
            if (!block.contains(QLatin1String("<string>Tahoma OS</string>")))
                continue;
            kern |= block.contains(QLatin1String("kern off"));
            if (!block.contains(QLatin1String("name=\"antialias\"><bool>false")))
                continue;
            const QRegularExpression lo(QStringLiteral("more_eq\"><double>([0-9.]+)")), hi(QStringLiteral("less\"><double>([0-9.]+)"));
            const auto l = lo.match(block), h = hi.match(block);
            const double from = l.hasMatch() ? l.captured(1).toDouble() : 0, to = h.hasMatch() ? h.captured(1).toDouble() : 1e9;
            crisp11 |= from <= 10.67 && 10.67 < to && from <= 11 && 11 < to;
        }
        QVERIFY2(crisp11, qPrintable(rules));
        QVERIFY(kern);

        // On a 200% screen fontconfig is asked about 11 px, drawn at 22: the
        // one-bit band (9-16 px drawn) is 4.5-8.5 px asked, so Tahoma 11
        // is smoothed there, as XP smoothed text that size.
        const QString doubled = QString::fromUtf8(xptext::rules(m_realFonts, 2.0));
        bool crisp11At2 = false, crisp7At2 = false;
        for (const QString &block : doubled.split(QLatin1String("<match target=\"font\">"))) {
            if (!block.contains(QLatin1String("<string>Tahoma OS</string>")) || !block.contains(QLatin1String("name=\"antialias\"><bool>false")))
                continue;
            const QRegularExpression lo(QStringLiteral("more_eq\"><double>([0-9.]+)")), hi(QStringLiteral("less\"><double>([0-9.]+)"));
            const auto l = lo.match(block), h = hi.match(block);
            const double from = l.hasMatch() ? l.captured(1).toDouble() : 0, to = h.hasMatch() ? h.captured(1).toDouble() : 1e9;
            crisp11At2 |= from <= 11 && 11 < to;
            crisp7At2 |= from <= 7 && 7 < to;
        }
        QVERIFY(!crisp11At2);
        QVERIFY(crisp7At2);
    }

    void displayScale()
    {
        // KWin's saved setups: the one for the screens connected now, its
        // first-priority screen's scale.
        const QByteArray config = R"([
            {"name": "outputs", "data": [
                {"connectorName": "eDP-1", "scale": 1},
                {"connectorName": "DP-1", "scale": 1.5}]},
            {"name": "setups", "data": [
                {"outputs": [{"outputIndex": 0, "enabled": false, "priority": -1},
                             {"outputIndex": 1, "enabled": true, "priority": 0}]},
                {"outputs": [{"outputIndex": 0, "enabled": true, "priority": 0}]}]}
        ])";
        QCOMPARE(xptext::displayScale(config, {QStringLiteral("eDP-1")}), 1.0);
        QCOMPARE(xptext::displayScale(config, {QStringLiteral("eDP-1"), QStringLiteral("DP-1")}), 1.5);
        QCOMPARE(xptext::displayScale(config, {QStringLiteral("HDMI-A-1")}), 1.0);   // (no setup for it)
        QCOMPARE(xptext::displayScale(QByteArray("not json"), {QStringLiteral("eDP-1")}), 1.0);

        // GTK 4 smooths its text at fractional scales (its one-bit glyphs lose
        // stems stretched), and the user's setting is back at whole ones.
        RecordingSession session;
        State state;
        auto lastGsettings = [&] {
            for (qsizetype i = session.runs.size() - 1; i >= 0; --i)
                if (session.runs.at(i).value(0) == QLatin1String("gsettings") && session.runs.at(i).value(1) == QLatin1String("set"))
                    return session.runs.at(i).last();
            return QString();
        };
        xptext::setGtk4Rendering(session, state, 1.25);
        QCOMPARE(lastGsettings(), QStringLiteral("manual"));
        xptext::setGtk4Rendering(session, state, 1.5);   // (the first previous kept)
        QVERIFY(state.settings.contains(QStringLiteral("previousGtkFontRendering")));
        xptext::setGtk4Rendering(session, state, 2.0);
        QCOMPARE(lastGsettings(), QStringLiteral("automatic"));   // (the recording session's empty answer: GTK's default)
        QVERIFY(!state.settings.contains(QStringLiteral("previousGtkFontRendering")));
    }

    void xpTextMigrationPreservesRenderingPreferences()
    {
        RecordingSession session;
        session.config.insert(QStringLiteral("kdeglobals|General|XftHintStyle"), QStringLiteral("hintfull"));
        session.config.insert(QStringLiteral("kdeglobals|General|XftSubPixel"), QStringLiteral("none"));
        session.config.insert(QStringLiteral("kdeglobals|General|XftAntialias"), QStringLiteral("true"));
        State state;
        state.settings.insert(QStringLiteral("previousXft"), QJsonObject{
            {QStringLiteral("XftHintStyle"), QStringLiteral("hintslight")},
            {QStringLiteral("XftSubPixel"), QStringLiteral("rgb")},
            {QStringLiteral("XftAntialias"), QStringLiteral("true")}});
        QString error;
        QVERIFY2(xptext::apply(session, state, &error), qPrintable(error));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|General|XftHintStyle")), QStringLiteral("hintslight"));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|General|XftSubPixel")), QStringLiteral("rgb"));
        QCOMPARE(state.settings.value(QStringLiteral("xpTextRulesVersion")).toInt(), 3);
        // Same-scale maintenance must migrate old policies too.
        state.settings.remove(QStringLiteral("xpTextRulesVersion"));
        QVERIFY(xptext::refresh(session, state));
        QCOMPARE(state.settings.value(QStringLiteral("xpTextRulesVersion")).toInt(), 3);
        // A customized legacy configuration must not be overwritten.
        const QString legacyFile = paths::config() + QLatin1String("/fontconfig/conf.d/99-xp-fonts.conf");
        const QByteArray custom = "<fontconfig><!-- user customization --></fontconfig>";
        writeFile(legacyFile, custom);
        QVERIFY(xptext::apply(session, state, &error));
        QCOMPARE(readFile(legacyFile), custom);
        // A later user change must survive repeated applies.
        session.config.insert(QStringLiteral("kdeglobals|General|XftHintStyle"), QStringLiteral("hintnone"));
        QVERIFY(xptext::apply(session, state, &error));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|General|XftHintStyle")), QStringLiteral("hintnone"));
    }

    void xpTextApplyAndRevert()
    {
        // Included at the end of KDE's fonts.conf (read after its settings),
        // KDE's Xft settings switched, and all of it undone.
        const QString conf = paths::config() + QLatin1String("/fontconfig/fonts.conf");
        const QByteArray kde = "<?xml version='1.0'?>\n<fontconfig>\n <match target=\"font\">\n  <edit mode=\"assign\" name=\"hintstyle\">\n"
                               "   <const>hintnone</const>\n  </edit>\n </match>\n</fontconfig>\n";
        writeFile(conf, kde);
        RecordingSession session;
        session.config.insert(QStringLiteral("kdeglobals|General|XftHintStyle"), QStringLiteral("hintnone"));
        State state;
        QString error;
        QVERIFY2(xptext::apply(session, state, &error), qPrintable(error));
        QFile f(conf);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        f.close();
        QVERIFY(text.indexOf("xplasma-xp-text.conf") > text.indexOf("hintnone"));
        QVERIFY(text.indexOf("xplasma-xp-text.conf") < text.indexOf("</fontconfig>"));
        QVERIFY(QFileInfo::exists(paths::config() + QLatin1String("/fontconfig/xplasma-xp-text.conf")));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|General|XftHintStyle")), QStringLiteral("hintnone"));
        QVERIFY(!session.config.contains(QStringLiteral("kdeglobals|General|XftSubPixel")));
        QVERIFY(xptext::apply(session, state, &error));   // (twice: one include, the first previous kept)
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll().count("xplasma-xp-text.conf"), 1);
        f.close();

        xptext::revert(session, state);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), kde);
        f.close();
        QVERIFY(!QFileInfo::exists(paths::config() + QLatin1String("/fontconfig/xplasma-xp-text.conf")));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|General|XftHintStyle")), QStringLiteral("hintnone"));
        QVERIFY(!session.config.contains(QStringLiteral("kdeglobals|General|XftSubPixel")));
    }

    void taskbarsSettleWhileShellIsDown()
    {
        // A floating bottom panel is put back in the shell's saved state; a
        // top panel, and panels that aren't in the layout, are left alone.
        writeFile(paths::config() + QLatin1String("/plasma-org.kde.plasma.desktop-appletsrc"),
                  "[Containments][7]\nformfactor=2\nlocation=4\nplugin=org.kde.panel\n\n"
                  "[Containments][7][Applets][9]\nplugin=org.kde.plasma.digitalclock\n\n"
                  "[Containments][8]\nlocation=3\nplugin=org.kde.panel\n\n"
                  "[Containments][1]\nlocation=0\nplugin=org.kde.plasma.folder\n");
        writeFile(paths::config() + QLatin1String("/plasmashellrc"),
                  "[PlasmaViews][Panel 7]\nfloating=1\n\n[PlasmaViews][Panel 7][Defaults]\nthickness=44\n\n"
                  "[PlasmaViews][Panel 7][Horizontal1280]\nfloating=1\npanelOpacity=2\n\n"
                  "[PlasmaViews][Panel 8]\nfloating=1\n\n[PlasmaViews][Panel 5]\nfloating=1\n");
        RecordingSession session;
        settleTaskbars(session, 30);
        const QString rc = QStringLiteral("plasmashellrc|PlasmaViews/Panel 7");
        QCOMPARE(session.config.value(rc + QLatin1String("|floating")), QStringLiteral("0"));
        QCOMPARE(session.config.value(rc + QLatin1String("/Defaults|thickness")), QStringLiteral("30"));
        QCOMPARE(session.config.value(rc + QLatin1String("/Horizontal1280|floating")), QStringLiteral("0"));
        QCOMPARE(session.config.value(rc + QLatin1String("/Horizontal1280|panelOpacity")), QStringLiteral("1"));
        QCOMPARE(session.config.value(rc + QLatin1String("/Horizontal1280|panelLengthMode")), QStringLiteral("0"));
        for (auto it = session.config.cbegin(); it != session.config.cend(); ++it)
            QVERIFY2(it.key().startsWith(rc), qPrintable(it.key()));
    }

    void wallpaperAlone()
    {
        // The editor's Apply when only the desktop changed: one script, nothing rebuilt.
        RecordingSession session;
        QStringList said;
        applyWallpaper(session, Wallpaper{QStringLiteral("/w/stoneh.jpg"), 6, QColor(0, 78, 152)},
                       [&](const QString &line) { said << line; });
        QCOMPARE(session.scripts.size(), 1);
        QVERIFY(session.scripts.first().contains(QLatin1String("file:///w/stoneh.jpg")));
        QVERIFY(session.runs.isEmpty() && session.calls.isEmpty());
        QCOMPARE(said, QStringList{QStringLiteral("Wallpaper: stoneh.jpg")});
        QVERIFY(!QFileInfo::exists(paths::data() + QLatin1String("/state.json")));
    }

    void titleShadowsFromRc()
    {
        if (!QFileInfo::exists(QStringLiteral("/usr/share/kwin/aurorae/aurorae.qml")))
            QSKIP("no KWin Aurorae here");
        const QString themes = m_home.filePath(QStringLiteral("aurorae/themes"));
        const QString target = m_home.filePath(QStringLiteral("kwin/aurorae/aurorae.qml"));
        auto read = [&] {
            QFile f(target);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        QString error;
        // None wanted: still an object (bare braces in QML are an empty block, i.e. undefined).
        QVERIFY2(overrides::installTitleShadows(themes, target, &error), qPrintable(error));
        QVERIFY(read().contains(QLatin1String("property var xpTitleShadows: ({})")));
        // As decoration.cpp writes it: the shadow keys are in [General].
        writeFile(themes + QLatin1String("/xp-blue/xp-bluerc"),
                  "[General]\nUseTextShadow=true\nActiveTextShadowColor=10,24,131,255\nInactiveTextShadowColor=10,24,131,255\n"
                  "TextShadowOffsetX=1\nTextShadowOffsetY=1\n\n[X-XPlasma-Generated]\nLook=Windows XP style (Default (blue))\n");
        writeFile(themes + QLatin1String("/plain/plainrc"),
                  "[General]\nUseTextShadow=false\n\n[X-XPlasma-Generated]\nLook=Windows Standard\n");
        QVERIFY2(overrides::installTitleShadows(themes, target, &error), qPrintable(error));
        QVERIFY2(read().contains(QLatin1String(
                     R"(property var xpTitleShadows: ({"xp-blue":{"active":"#ff0a1883","inactive":"#ff0a1883","x":1,"y":1}}))")),
                 qPrintable(read().section(QLatin1String("xpTitleShadows"), 1, 1).left(200)));
        // XP's title buttons, for XPlasma's themes (shadowed or not).
        QVERIFY(read().contains(QLatin1String(R"(property var xpThemes: ["plain","xp-blue"])")));
        QVERIFY(read().contains(QLatin1String("buttons: root.xpButtons(options.titleButtonsLeft)"))
                && read().contains(QLatin1String("buttons: root.xpButtons(options.titleButtonsRight)"))
                && read().contains(QLatin1String("function xpButtons(list)")));
    }

    void selectableLookLeavesDisabledSettings()
    {
        if (m_realThemes.isEmpty()) QSKIP("needs an installed XP visual style");
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"));
        QVERIFY(look);
        RecordingSession session;
        session.config.insert(QStringLiteral("kdeglobals|KDE|widgetStyle"), QStringLiteral("Breeze"));
        session.config.insert(QStringLiteral("kdeglobals|General|ColorScheme"), QStringLiteral("Breeze"));
        session.config.insert(QStringLiteral("kdeglobals|General|font"), QStringLiteral("original-font"));
        session.config.insert(QStringLiteral("kwinrc|org.kde.kdecoration2|theme"), QStringLiteral("Breeze"));
        ApplyOptions o;
        o.properties = o.colours = o.decoration = o.fonts = false;
        o.widgets = o.taskbar = o.icons = o.sounds = false;
        QString error;
        QVERIFY2(applyLook(session, *look, o, &error), qPrintable(error));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|KDE|widgetStyle")), QStringLiteral("Breeze"));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|General|ColorScheme")), QStringLiteral("Breeze"));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|General|font")), QStringLiteral("original-font"));
        QCOMPARE(session.config.value(QStringLiteral("kwinrc|org.kde.kdecoration2|theme")), QStringLiteral("Breeze"));
        QVERIFY(session.scripts.isEmpty());
        QVERIFY(!QFileInfo::exists(paths::config() + QLatin1String("/fontconfig/xplasma-xp-text.conf")));
        for (const auto &run : session.runs) {
            QVERIFY(run.value(0) != QLatin1String("plasma-apply-colorscheme"));
            QVERIFY(run.value(0) != QLatin1String("plasma-apply-desktoptheme"));
            QVERIFY(run.value(0) != QLatin1String("kwriteconfig6"));
        }
    }

    void customScrollbarArrowsKeepContrast()
    {
        const QString source=m_realThemes+"/Luna/luna.msstyles";
        if(!QFileInfo::exists(source))QSKIP("needs Luna");
        QString error;const auto base=Look::fromStyle(source,"NORMALBLUE_INI",&error);QVERIFY2(base,qPrintable(error));
        for(const QColor accent:{QColor("#111117"),QColor("#7c7c5c"),QColor("#e5e5d0")}) {
            const QJsonObject colors{{"enabled",true},{"start","#315c43"},{"menu",accent.name()},{"title","#445566"},{"buttons","#728392"}};
            const auto look=editor::staticColorLook(*base,colors,&error);QVERIFY2(look,qPrintable(error));
            const auto dark=darkmode::applicationLook(*look,true,&error);QVERIFY2(dark,qPrintable(error));
            for(int state:{1,2,3,5,9,13}) {
                const PartRef ref{QStringLiteral("scrollbar"),schema::partId(u"scrollbar",u"arrowbtn"),state,{}};
                const auto glyph=look->renderer->stateImage(ref,true).convertToFormat(QImage::Format_ARGB32);
                QVERIFY(!glyph.isNull());
                int ink=0;
                for(int y=0;y<glyph.height();++y)for(int x=0;x<glyph.width();++x) {
                    const QColor c=glyph.pixelColor(x,y);if(c.alpha()<128)continue;
                    ++ink;
                    // Colored shell surfaces get neutral, contrasting glyph ink,
                    // not Luna's original low-contrast blue glyph pixels.
                    QCOMPARE(c.red(),c.green());QCOMPARE(c.green(),c.blue());
                    if(accent.lightness()<160)QVERIFY(c.red()>220);else QVERIFY(c.red()<35);
                }
                QVERIFY(ink>0);
                const auto darkGlyph=dark->renderer->stateImage(ref,true).convertToFormat(QImage::Format_ARGB32);
                QVERIFY(!darkGlyph.isNull());
                for(int y=0;y<darkGlyph.height();++y)for(int x=0;x<darkGlyph.width();++x) {
                    const QColor c=darkGlyph.pixelColor(x,y);
                    if(c.alpha()>128)QVERIFY(c.lightness()>200);
                }
                // The custom-color scrollbar face remains tinted.
                QVERIFY(look->renderer->stateImage(ref,false)!=base->renderer->stateImage(ref,false));
            }
        }
    }

    void savedLiveProfilesBecomeStaticStyles()
    {
        const QString source=m_realThemes+"/Royale/Royale.msstyles";
        if(!QFileInfo::exists(source))QSKIP("needs Royale");
        QString error;const auto base=Look::fromStyle(source,"NORMALROYALE_INI",&error);QVERIFY2(base,qPrintable(error));
        const QJsonObject colors{{"enabled",true},{"start","#315c43"},{"menu","#644576"},{"title","#445566"},{"buttons","#728392"},{"linkTitleToTaskbar",false},{"menuText",2},{"startMenuText",1}};
        const auto result=editor::staticColorLook(*base,colors,&error);QVERIFY2(result,qPrintable(error));QVERIFY(result->stylePath!=source);
        QCOMPARE(result->system.colour(u"Hilight"),QColor("#644576"));
        QCOMPARE(result->system.colour(u"HotTrackingColor"),QColor("#644576"));
        const PartRef scrollbar{QStringLiteral("scrollbar"),schema::partId(u"scrollbar",u"thumbbtnvert"),1,{}};
        QVERIFY(result->renderer->background(scrollbar,QSize(17,40))!=base->renderer->background(scrollbar,QSize(17,40)));
        const auto theme=Theme::load(result->stylePath,result->variant);QVERIFY(theme);QRgb text;
        QVERIFY(parseColour(theme->property(u"startpanel",u"placeslist",u"",u"textcolor").value_or(QString()),&text));QCOMPARE(QColor(text),QColor(Qt::white));
        // The user pane and Log Off bar are taskbar-coloured: the taskbar's text.
        for(const auto &band : {u"userpane",u"logoff"}){QVERIFY(parseColour(theme->property(u"startpanel",band,u"",u"textcolor").value_or(QString()),&text));QCOMPARE(QColor(text),QColor(Qt::black));}
        QVERIFY(parseColour(theme->property(u"sysmetrics",u"",u"",u"highlighttext").value_or(QString()),&text));QCOMPARE(QColor(text),QColor(Qt::black));
        const auto panel=panelArt(*result);
        QCOMPARE(panel.textColour,QColor(Qt::black));QCOMPARE(panel.trayTextColour,QColor(Qt::black));
        const auto menu=writeStartPanel(*result,paths::data()+"/text-save-probe",&error);
        QVERIFY2(!menu.isEmpty(),qPrintable(error));QCOMPARE(menu.value("palette").toObject().value("programText").toString(),QString("#ffffff"));
        auto inverse=colors;inverse.insert("menuText",1);inverse.insert("startMenuText",2);
        const auto inverseLook=editor::staticColorLook(*base,inverse,&error);QVERIFY2(inverseLook,qPrintable(error));
        const auto inversePanel=panelArt(*inverseLook);QCOMPARE(inversePanel.textColour,QColor(Qt::white));QCOMPARE(inversePanel.trayTextColour,QColor(Qt::white));
        const auto inverseMenu=writeStartPanel(*inverseLook,paths::data()+"/text-save-inverse",&error);QVERIFY2(!inverseMenu.isEmpty(),qPrintable(error));QCOMPARE(inverseMenu.value("palette").toObject().value("programText").toString(),QString("#000000"));
        for(const char *band : {"userText","footerText"})QCOMPARE(inverseMenu.value("palette").toObject().value(QLatin1String(band)).toString(),QString("#ffffff"));
        QCOMPARE(inverseMenu.value("palette").toObject().value("userShadow").toString(),QString("#000000"));   // (white text: XP's shadow)
        QCOMPARE(menu.value("palette").toObject().value("userText").toString(),QString("#000000"));
        QCOMPARE(menu.value("palette").toObject().value("userShadow").toString(),QString("#00000000"));   // (black text: none)
        QCOMPARE(QJsonDocument::fromJson(readFile(result->stylePath+".json")).object().value("startMenuText").toInt(),1);
        const auto repeated=editor::staticColorLook(*result,colors,&error);QVERIFY(repeated);QCOMPARE(repeated->stylePath,result->stylePath);
        auto classic=editor::staticColorLook(Look::fromScheme(ClassicScheme()),colors,&error);QVERIFY(classic);QCOMPARE(classic->system.shellMenuTextMode,1);
        ThemeFile saved;classic->system.writeTo(saved);QCOMPARE(ClassicScheme::fromThemeFile(saved).shellMenuTextMode,1);
    }

    void customStartMenuSharesItsOutline()
    {
        // XP draws the Start menu's outline, and the line above Log Off, in
        // one blue across its pieces: custom colors keep them one color.
        const QString source=m_realThemes+"/Luna/luna.msstyles";
        if(!QFileInfo::exists(source))QSKIP("needs Luna");
        QString error;const auto base=Look::fromStyle(source,"NORMALBLUE_INI",&error);QVERIFY(base);
        const QJsonObject colors{{"enabled",true},{"start","#eb9d00"},{"menu","#7c7c5c"},{"title","#7c7c5c"},{"buttons","#d99100"},{"startMenuText",2},{"menuText",1}};
        const auto baked=editor::staticColorLook(*base,colors,&error);QVERIFY2(baked,qPrintable(error));
        const QString dir=paths::data()+"/outline-probe";
        QVERIFY2(!writeStartPanel(*baked,dir,&error).isEmpty(),qPrintable(error));
        const auto art=[&](const char *name){return QImage(dir+"/"+QLatin1String(name)+".png").convertToFormat(QImage::Format_RGB32);};
        const QImage header=art("userpane"),places=art("placeslist"),footer=art("logoff"),programs=art("proglist"),more=art("moreprograms");
        const auto near=[](QRgb a,QRgb b){return qAbs(qRed(a)-qRed(b))<=12 && qAbs(qGreen(a)-qGreen(b))<=12 && qAbs(qBlue(a)-qBlue(b))<=12;};
        const QRgb outline=header.pixel(header.width()-1,header.height()-1);
        for(const auto &[name,pixel] : {std::pair{"places",places.pixel(places.width()-1,places.height()/2)},std::pair{"footer",footer.pixel(footer.width()-1,footer.height()/2)},std::pair{"programs",programs.pixel(0,programs.height()-1)}})
            QVERIFY2(near(pixel,outline),qPrintable(QStringLiteral("%1 outline %2 vs header %3").arg(QLatin1String(name),QColor(pixel).name(),QColor(outline).name())));
        const QRgb under=places.pixel(places.width()/2,places.height()-1),underPrograms=more.pixel(more.width()/2,more.height()-1);
        QVERIFY2(near(under,underPrograms),qPrintable(QColor(under).name()+" vs "+QColor(underPrograms).name()));
    }

    void bakedProfileWithoutSidecar()
    {
        const QString source=m_realThemes+"/Luna/luna.msstyles";
        if(!QFileInfo::exists(source))QSKIP("needs Luna");
        QString error;const auto base=Look::fromStyle(source,"NORMALBLUE_INI",&error);QVERIFY(base);
        QJsonObject colors{{"enabled",true},{"rendering","static"},{"menu","#644576"},{"startMenuText",2}};
        const auto baked=editor::staticColorLook(*base,colors,&error);QVERIFY2(baked,qPrintable(error));
        const auto pixels=decorationArt(*baked).element("decoration-top");
        const QString imported=paths::data()+"/imported.msstyles";QVERIFY(QFile::copy(baked->stylePath,imported));
        QVERIFY(QFile::remove(baked->stylePath+".json"));
        const auto importedLook=Look::fromStyle(imported,baked->variant);QVERIFY(importedLook);
        const auto metadata=editor::colorMetadata(imported,baked->variant);QCOMPARE(metadata.value("generator").toInt(),editor::kCustomLunaGenerator);
        const auto repeat=editor::staticColorLook(*importedLook,colors,&error);QVERIFY2(repeat,qPrintable(error));
        QCOMPARE(decorationArt(*repeat).element("decoration-top"),pixels);
        // A missing original may be viewed/reapplied, but never tinted twice.
        const QString originalCopy=paths::data()+"/neutral.msstyles";QVERIFY(QFile::copy(source,originalCopy));
        const auto localBase=Look::fromStyle(originalCopy,base->variant);QVERIFY(localBase);
        const auto localBaked=editor::staticColorLook(*localBase,colors,&error);QVERIFY2(localBaked,qPrintable(error));
        QVERIFY(QFile::remove(originalCopy));QVERIFY(QFile::remove(localBaked->stylePath+".json"));
        const auto preserved=editor::staticColorLook(*localBaked,colors,&error);QVERIFY2(preserved,qPrintable(error));QCOMPARE(preserved->stylePath,localBaked->stylePath);
        colors.insert("menu","#123456");QVERIFY(!editor::staticColorLook(*localBaked,colors,&error));QVERIFY(error.contains("original"));
        const auto direct=editor::customLuna(localBaked->stylePath,{Qt::red,Qt::red,Qt::red,Qt::red},paths::data()+"/bad",&error,base->variant);QVERIFY(direct.isEmpty());
    }

    void colorOnlyApplyPreservesRuntimeAndCaches()
    {
        const QString source=m_realThemes+"/Luna/luna.msstyles";
        if(!QFileInfo::exists(source) || m_realIso.isEmpty())QSKIP("needs Luna and XP media for the XP Start menu");
        QString error;const auto base=Look::fromStyle(source,"NORMALBLUE_INI",&error);QVERIFY(base);
        State state;state.media=m_realIso;state.appliedKind="style";state.style=source;state.variant=base->variant;state.settings.insert(kAppliedWithKey,project::kVersion);QVERIFY(state.save());
        const QString share=paths::localShare();
        QVERIFY(overrides::installTitleShadows(share+"/aurorae/themes",share+"/kwin/aurorae/aurorae.qml",&error));
        const QString unrelated="[some-other-application.svg]\nLastModified=123\nelement=16x16,\n";
        writeFile(paths::home()+"/.cache/ksvg-elements",unrelated.toUtf8());
        const QJsonObject colors{{"enabled",true},{"menu","#644576"},{"startMenuText",2}};
        const auto baked=editor::staticColorLook(*base,colors,&error);QVERIFY2(baked,qPrintable(error));
        RecordingSession session;session.existingPanel=true;
        session.config.insert("kdeglobals|KDE|widgetStyle","XPlasma");
        session.config.insert("kwinrc|org.kde.kdecoration2|theme","__aurorae__svg__old");
        ApplyOptions options;options.icons=false;options.fonts=false;options.sounds=false;options.widgets=false;
        QStringList progress;options.progress=[&](const QString &line){progress<<line;};
        QVERIFY2(applyLook(session,*baked,options,&error),qPrintable(error));
        QVERIFY(progress.join(' ').contains("installed theme runtime"));
        QVERIFY(!session.ran("systemctl"));QVERIFY(!session.calls.contains("/KDEPlatformTheme refreshFonts"));
        QVERIFY(!session.config.contains("kwinrulesrc|General|rules"));
        QVERIFY(session.scripts.filter("LAUNCHER_KEYS").isEmpty());
        QVERIFY(!session.scripts.filter("JSON.stringify").isEmpty());
        const QString id=State::load().settings.value("appliedPackageId").toString();QVERIFY(id.startsWith("xplasma-v2-"));
        const bool native=session.config.value("kwinrc|org.kde.kdecoration2|library")=="fyi.hotsocket.xplasma.decoration";
        QString asset;
        if(native)asset=paths::stylePluginRoot(6)+"/org.kde.kdecoration3/fyi.hotsocket.xplasma.decoration.so";
        else asset=share+"/aurorae/themes/"+id+"/decoration.svg";
        const auto before=QFileInfo(asset).lastModified();QVERIFY(before.isValid());
        QVERIFY(readFile(paths::home()+"/.cache/ksvg-elements").contains(unrelated.toUtf8()));
        // Applying again reuses the immutable artwork; no mapped file is replaced.
        QVERIFY2(applyLook(session,*baked,options,&error),qPrintable(error));QCOMPARE(QFileInfo(asset).lastModified(),before);
        QCOMPARE(editor::colorMetadata(State::load().style).value("startMenuText").toInt(),2);
    }

    void independentThemeTextModes()
    {
        const QString source=m_realThemes+QLatin1String("/Royale/Royale.msstyles");
        if(!QFileInfo::exists(source)) QSKIP("needs Royale");
        const QColor gray("#464646");
        for(const auto &modes : {std::array<int,4>{1,2,1,2},std::array<int,4>{2,1,2,1}}) {
            QString error;const auto path=editor::customLuna(source,{gray,gray,gray,gray},paths::data()+QLatin1String("/custom-luna"),&error,QStringLiteral("NORMALROYALE_INI"),0,modes);
            QVERIFY2(!path.isEmpty(),qPrintable(error));const auto t=Theme::load(path,QStringLiteral("NORMALROYALE_INI"));QVERIFY(t);
            const auto expected=[&](int part){return QColor(modes[part]==1?Qt::white:Qt::black);};
            QRgb text;
            QVERIFY(parseColour(t->property(u"button",u"pushbutton",u"",u"textcolor",u"start").value_or(QString()),&text));QCOMPARE(QColor(text),expected(0));
            QVERIFY(parseColour(t->property(u"startpanel",u"placeslist",u"",u"textcolor").value_or(QString()),&text));QCOMPARE(QColor(text),expected(1));
            QVERIFY(parseColour(t->property(u"sysmetrics",u"",u"",u"captiontext").value_or(QString()),&text));QCOMPARE(QColor(text),expected(2));
            Renderer renderer(t);
            const auto glyph=renderer.stateImage({QStringLiteral("window"),schema::partId(u"window",u"closebutton"),1,{}},true,QSize(12,12));
            const auto original=Theme::load(source,QStringLiteral("NORMALROYALE_INI"));Renderer before(original);
            const auto oldGlyph=before.stateImage({QStringLiteral("window"),schema::partId(u"window",u"closebutton"),1,{}},true,QSize(12,12));
            QCOMPARE(glyph.size(),oldGlyph.size());int visible=0;
            for(int y=0;y<glyph.height();++y)for(int x=0;x<glyph.width();++x){
                const auto pixel=glyph.pixel(x,y);QCOMPARE(qAlpha(pixel),qAlpha(oldGlyph.pixel(x,y)));
                if(qAlpha(pixel)){++visible;const int value=modes[3]==1?qAlpha(pixel):0;QCOMPARE(qRed(pixel),value);QCOMPARE(qGreen(pixel),value);QCOMPARE(qBlue(pixel),value);}
            }
            QVERIFY(visible>0);
            const auto meta=QJsonDocument::fromJson(readFile(path+QLatin1String(".json"))).object().value(QStringLiteral("textModes")).toArray();QCOMPARE(meta.size(),4);
            ClassicScheme scheme;scheme.shellColors=std::array<QColor,4>{gray,gray,gray,gray};scheme.shellTextModes=modes;
            ThemeFile file;scheme.writeTo(file);const auto restored=ClassicScheme::fromThemeFile(file);
            for(int i=0;i<4;++i){QCOMPARE(meta[i].toInt(),modes[i]);QCOMPARE(restored.shellTextModes[i],modes[i]);QCOMPARE(restored.shellPalette(i).color(QPalette::ButtonText),expected(i));}
        }
    }

    void customThemeTextModes()
    {
        const QString source=m_realThemes+QLatin1String("/Royale/Royale.msstyles");
        if (!QFileInfo::exists(source)) QSKIP("needs Royale");
        const QColor gray("#464646"); QString previous;
        for(int mode : {0,1,2}) {
            QString error;
            const auto generated=editor::customLuna(source,{gray,gray,gray,gray},paths::data()+QLatin1String("/custom-luna"),&error,QStringLiteral("NORMALROYALE_INI"),mode);
            QVERIFY2(!generated.isEmpty(),qPrintable(error)); QVERIFY(generated!=previous);previous=generated;
            const auto theme=Theme::load(generated,QStringLiteral("NORMALROYALE_INI")); QVERIFY(theme);
            const QColor expected=mode==2 ? QColor(Qt::black) : QColor(Qt::white);
            QRgb text;
            for(const auto &part : {QStringLiteral("proglist"),QStringLiteral("placeslist"),QStringLiteral("userpane"),QStringLiteral("logoff")}) {
                QVERIFY(parseColour(theme->property(u"startpanel",part,u"",u"textcolor").value_or(QString()),&text));
                QCOMPARE(QColor(text),expected);
            }
            QVERIFY(parseColour(theme->property(u"sysmetrics",u"",u"",u"captiontext").value_or(QString()),&text)); QCOMPARE(QColor(text),expected);
            QVERIFY(parseColour(theme->property(u"toolbar",u"",u"",u"textcolor",u"startmenu").value_or(QString()),&text)); QCOMPARE(QColor(text),expected);
            const auto look=Look::fromStyle(generated,QStringLiteral("NORMALROYALE_INI"),&error); QVERIFY(look);
            const auto dark=darkmode::applicationLook(*look,true,&error); QVERIFY2(dark,qPrintable(error));
            QCOMPARE(dark->system.colour(u"HilightText"),expected);
            const auto metadata=QJsonDocument::fromJson(readFile(generated+QLatin1String(".json"))).object();
            QCOMPARE(metadata.value(QStringLiteral("textMode")).toInt(),mode);
            ClassicScheme scheme;scheme.shellColors=std::array<QColor,4>{gray,gray,gray,gray};scheme.shellTextMode=mode;
            ThemeFile file;scheme.writeTo(file);const auto restored=ClassicScheme::fromThemeFile(file);
            QCOMPARE(restored.shellTextMode,mode);QCOMPARE(restored.shellPalette(1).color(QPalette::ButtonText),expected);
        }
    }

    void customLunaKeepsStructure()
    {
        // Custom colors change Luna's blue artwork and nothing else about it:
        // the same bitmaps at the same sizes, the same alpha, the magenta key
        // kept, the other colour schemes' artwork untouched. The Start
        // button's rounded edge takes the new taskbar behind it, and the
        // same colours again are the same (cached) style.
        const QString source=m_realThemes+"/Luna/luna.msstyles";if(!QFileInfo::exists(source))QSKIP("needs Luna");
        const std::array<QColor,4> colors{QColor("#862ba8"),QColor("#9d3535"),QColor("#268a64"),QColor("#bd8825")};
        const QString dir=paths::data()+"/custom-luna";
        QString error;
        const auto generated=editor::customLuna(source,colors,dir,&error);QVERIFY2(!generated.isEmpty(),qPrintable(error));
        const auto original=Theme::load(source,"NORMALBLUE_INI"),custom=Theme::load(generated,"NORMALBLUE_INI");
        QVERIFY(original);QVERIFY(custom);
        QCOMPARE(custom->bitmapNames(),original->bitmapNames());
        const QStringList changed{"BLUE_STARTBUTTON_BMP","BLUE_TASKBARBACKGROUND_BMP","BLUE_FRAMECAPTION_BMP","BLUE_CAPTIONBUTTON_BMP",
                                  "BLUE_STARTUSERPANEL_BMP","BLUE_STARTPANELPLACESBACKGROUND_BMP","BLUE_STARTPANELLOGOFFBACKGROUND_BMP"};
        for(const auto &name:original->bitmapNames()){
            const auto a=original->bitmap(name).image,b=custom->bitmap(name).image;
            QVERIFY2(a.size()==b.size(),qPrintable(name));
            if(changed.contains(name))QVERIFY2(a!=b,qPrintable(name+QStringLiteral(" unchanged")));
            if(!name.startsWith(QLatin1String("BLUE_"))){QVERIFY2(a==b,qPrintable(name+QStringLiteral(" changed")));continue;}
            for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x){
                const QRgb p=a.pixel(x,y),q=b.pixel(x,y);
                if(qAlpha(p)!=qAlpha(q))QFAIL(qPrintable(QStringLiteral("%1: alpha changed at %2,%3").arg(name).arg(x).arg(y)));
                if(p==qRgb(255,0,255)&&q!=p)QFAIL(qPrintable(QStringLiteral("%1: magenta key recoloured at %2,%3").arg(name).arg(x).arg(y)));
            }
        }
        // The rounded Start edge: the taskbar backdrop's hue, not Start's.
        const auto edge=custom->bitmap("BLUE_STARTBUTTON_BMP").image,oldEdge=original->bitmap("BLUE_STARTBUTTON_BMP").image;
        int checkedEdge=0;
        for(int y=0;y<edge.height();++y){
            const QColor old(oldEdge.pixel(edge.width()-1,y));
            if(old.rgb()==qRgb(255,0,255)||old.hsvSaturationF()<.12)continue;
            const float distance=qAbs(QColor(edge.pixel(edge.width()-1,y)).hsvHueF()-colors[1].hsvHueF());
            QVERIFY2(qMin(distance,1-distance)<=.04,qPrintable(QStringLiteral("Start edge row %1").arg(y)));
            ++checkedEdge;
        }
        QVERIFY(checkedEdge>0);
        QCOMPARE(editor::customLuna(source,colors,dir,&error),generated);
        QVERIFY(editor::customLuna(dir+"/missing.msstyles",colors,dir,&error).isEmpty());
    }

    void customTrayKeepsItsLightnessAgainstTheTaskbar()
    {
        // Luna Blue's notification area is lighter than its taskbar. In custom
        // colours it stays so, by about as much, not the taskbar's colour exactly.
        const QString source=m_realThemes+"/Luna/luna.msstyles";if(!QFileInfo::exists(source))QSKIP("needs Luna");
        QString error;const auto base=Look::fromStyle(source,"NORMALBLUE_INI",&error);QVERIFY2(base,qPrintable(error));
        const auto light=[](const QColor &c){return .2126*c.redF()+.7152*c.greenF()+.0722*c.blueF();};
        const PanelArt before=panelArt(*base);
        const qreal lunaLift=light(before.trayColour)-light(before.panelColour);
        QVERIFY2(lunaLift>.03,qPrintable(QString::number(lunaLift)));
        for(const QColor taskbar:{QColor("#9d3535"),QColor("#2e7d32"),QColor("#404040")}){
            const auto file=editor::customLuna(source,{QColor("#862ba8"),taskbar,QColor("#268a64"),QColor("#bd8825")},paths::data()+"/custom-luna",&error,base->variant);
            QVERIFY2(!file.isEmpty(),qPrintable(error));
            const auto look=Look::fromStyle(file,base->variant,&error);QVERIFY2(look,qPrintable(error));
            const PanelArt after=panelArt(*look);
            const qreal lift=light(after.trayColour)-light(after.panelColour);
            QVERIFY2(lift>lunaLift/2,qPrintable(QStringLiteral("%1: tray %2 vs taskbar %3").arg(taskbar.name(),after.trayColour.name(),after.panelColour.name())));
            // (still the chosen hue: lighter, not another colour)
            const float distance=qAbs(after.trayColour.hsvHueF()-taskbar.hsvHueF());
            if(taskbar.hsvSaturationF()>.2)QVERIFY2(qMin(distance,1-distance)<=.05,qPrintable(after.trayColour.name()));
        }
    }

    void closeKeepsItsRedNotLunasBlue()
    {
        // Custom colors keep Close's red, but what Luna blended with its blue
        // caption -- the edges, the inactive state's mauve -- takes the
        // buttons' colour, as the other buttons do.
        const QString source=m_realThemes+"/Luna/luna.msstyles";if(!QFileInfo::exists(source))QSKIP("needs Luna");
        QString error;const auto base=Look::fromStyle(source,"NORMALBLUE_INI",&error);QVERIFY(base);
        const std::array<QColor,4> colors{QColor("#eb9d00"),QColor("#7c7c5c"),QColor("#7c7c5c"),QColor("#d99100")};
        const auto file=editor::customLuna(source,colors,paths::data()+"/custom-luna",&error,base->variant);QVERIFY2(!file.isEmpty(),qPrintable(error));
        const auto result=Look::fromStyle(file,base->variant,&error);QVERIFY(result);
        const auto before=decorationArt(*base),after=decorationArt(*result);
        auto button=[](const DecorationArt &art,const QString &name,const QString &state){for(const auto &b:art.buttons)if(b.first==name)for(const auto &part:b.second)if(part.first==state)return part.second.convertToFormat(QImage::Format_ARGB32);return QImage();};
        for(const QString state:{"active-center","inactive-center","hover-center","hover-inactive-center","pressed-center","pressed-inactive-center"}){
            const auto original=button(before,"close",state),changed=button(after,"close",state);QVERIFY(!original.isNull());QCOMPARE(changed.size(),original.size());
            int red=0,blue=0;
            for(int y=0;y<original.height();++y)for(int x=0;x<original.width();++x){
                // (An inactive state's symbol is drawn over recoloured pixels:
                // only an active one's red, and its symbol, are exactly Luna's.)
                if(!state.contains(QLatin1String("inactive")) && !lunaBlueMade(original.pixel(x,y))){QCOMPARE(changed.pixel(x,y),original.pixel(x,y));++red;}
                if(qAlpha(changed.pixel(x,y))>128 && lunaBlueMade(changed.pixel(x,y)))++blue;
            }
            QVERIFY2(state.contains(QLatin1String("inactive")) || red>0,qPrintable(state));
            QVERIFY2(blue==0,qPrintable(state+QStringLiteral(": Luna's blue is left")));
        }
        QVERIFY(button(after,"close","inactive-center")!=button(before,"close","inactive-center"));
        QVERIFY(button(after,"minimize","inactive-center")!=button(before,"minimize","inactive-center"));
    }

    void royaleFamilyCustomColorsShareBaseAndCaptionFringes()
    {
        const QString source=m_realThemes+QStringLiteral("/Royale/Royale.msstyles");
        if(!QFileInfo::exists(source))QSKIP("needs Royale");
        QString error;
        const std::array<QColor,4> colors{QColor("#315c43"),QColor("#644576"),QColor("#225577"),QColor("#e58a22")};
        const QString directory=paths::data()+QStringLiteral("/custom-luna");
        const QString shared=editor::customLuna(source,colors,directory,&error);
        QVERIFY2(!shared.isEmpty(),qPrintable(error));
        const auto metadata=editor::colorMetadata(shared);
        QCOMPARE(QFileInfo(metadata.value("source").toString()).canonicalFilePath(),QFileInfo(source).canonicalFilePath());
        for(const QString relative:{QStringLiteral("Royale Noir/Royale Noir.msstyles"),QStringLiteral("Embedded/Embedded.msstyles"),QStringLiteral("Zune/Zune.msstyles")}) {
            const QString derivative=m_realThemes+QLatin1Char('/')+relative;
            QVERIFY2(QFileInfo::exists(derivative),qPrintable(derivative));
            const auto style=Library::style(derivative);
            QCOMPARE(editor::customLuna(derivative,colors,directory,&error,style.defaultVariant),shared);
            const auto base=Look::fromStyle(derivative,style.defaultVariant,&error);QVERIFY2(base,qPrintable(error));
            const QJsonObject profile{{"enabled",true},{"start",colors[0].name()},{"menu",colors[1].name()},
                {"title",colors[2].name()},{"buttons",colors[3].name()},{"linkTitleToTaskbar",false}};
            const auto baked=editor::staticColorLook(*base,profile,&error);QVERIFY2(baked,qPrintable(error));
            QCOMPARE(baked->stylePath,shared);
        }
        auto changedColors=colors;changedColors[3]=QColor("#784caa");
        const auto changed=editor::customLuna(source,changedColors,directory,&error);
        QVERIFY2(!changed.isEmpty(),qPrintable(error));
        const auto first=Theme::load(shared),second=Theme::load(changed);QVERIFY(first && second);
        for(const QString part:{QStringLiteral("closebutton"),QStringLiteral("maxbutton")}) {
            const QString resource=first->property(u"window",part,u"",u"imagefile").value_or(QString());
            const QImage a=first->bitmap(resource).image,b=second->bitmap(resource).image;
            QVERIFY(!a.isNull());QCOMPARE(a.size(),b.size());
            const int count=first->property(u"window",part,u"",u"imagecount").value_or(QStringLiteral("1")).toInt();
            const int frameHeight=a.height()/qMax(1,count);int checked=0,faceChanged=0;
            for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x) {
                const int fy=y%frameHeight;
                if(x==0 || x==a.width()-1 || fy==0 || fy==frameHeight-1) {
                    QCOMPARE(a.pixel(x,y),b.pixel(x,y));++checked;
                } else if(a.pixel(x,y)!=b.pixel(x,y))++faceChanged;
            }
            QVERIFY(checked>0);QVERIFY(faceChanged>0);
        }
    }

    void customRoyalePreservesIconsAndLiveShading()
    {
        const QString source=m_realThemes+QLatin1String("/Royale/Royale.msstyles");
        if (!QFileInfo::exists(source)) QSKIP("needs Royale");
        QString error;
        const QColor gray("#464646");
        const auto generated=editor::customLuna(source,{gray,gray,gray,gray},paths::data()+QLatin1String("/custom-luna"),&error,QStringLiteral("NORMALROYALE_INI"));
        QVERIFY2(!generated.isEmpty(),qPrintable(error));
        const auto before=Theme::load(source,QStringLiteral("NORMALROYALE_INI"));
        const auto after=Theme::load(generated,QStringLiteral("NORMALROYALE_INI"));
        QVERIFY(before && after);
        int protectedImages=0;
        // Button symbols keep their art; only what the style's blue made (an
        // inactive symbol's tint) turns grey, readable on any colour. The
        // scrollbar arrows take neutral ink contrasting with their face.
        static const QRegularExpression buttonSymbol(QStringLiteral("(CLOSE|MINIMIZE|MAXIMIZE|RESTORE|HELP)GLYPH"));
        for(const auto &name:before->bitmapNames()) if(name.contains(QLatin1String("GLYPH"))) {
            const bool symbol=buttonSymbol.match(name).hasMatch();
            const bool scrollArrow=name.contains(QLatin1String("SCROLLARROWGLYPH"));
            const QImage original=before->bitmap(name).image.convertToFormat(QImage::Format_RGB32),changed=after->bitmap(name).image.convertToFormat(QImage::Format_RGB32);
            QCOMPARE(changed.size(),original.size());
            for(int y=0;y<original.height();++y)for(int x=0;x<original.width();++x){
                const QRgb was=original.pixel(x,y),now=changed.pixel(x,y);
                if((symbol && lunaBlueMade(was)) || (scrollArrow && now!=was))QVERIFY2(qRed(now)==qGreen(now) && qGreen(now)==qBlue(now),qPrintable(name));
                else QVERIFY2(now==was,qPrintable(name));
            }
            ++protectedImages;
        }
        for(auto it=before->ini().sections().cbegin();it!=before->ini().sections().cend();++it)
            if(it.key().cls==QLatin1String("startpanel") && (it.key().part.contains(QLatin1String("arrow")) || it.key().part==QLatin1String("logoffbuttons")))
                for(auto p=it.value().cbegin();p!=it.value().cend();++p) if(p.key().startsWith(QLatin1String("imagefile"))) {
                    QCOMPARE(after->bitmap(p.value()).image,before->bitmap(p.value()).image); ++protectedImages;
                }
        QVERIFY(protectedImages>0);
        Renderer renderer(after);
        for(const auto &ref:{PartRef{QStringLiteral("startpanel"),schema::partId(u"startpanel",u"userpane"),1,{}},
                            PartRef{QStringLiteral("taskbar"),schema::partId(u"taskbar",u"backgroundbottom"),0,{}},
                            PartRef{QStringLiteral("window"),schema::partId(u"window",u"caption"),1,{}}}) {
            auto image=renderer.background(ref,QSize(100,30)); QVERIFY(!image.isNull());
            for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) if(qAlpha(image.pixel(x,y))) {
                const QColor pixel(image.pixel(x,y));
                QVERIFY2(qAbs(pixel.red()-pixel.green())<=1 && qAbs(pixel.green()-pixel.blue())<=1,qPrintable(ref.cls+QLatin1String(" retained an unwanted hue")));
            }
        }
        // Close keeps Royale's red, every frame of it; what its blue made (the
        // inactive and disabled frames) follows the colour, no blue left.
        // (In its bitmap: drawn at another size, red and white blend to pink.)
        const QString closeFile=before->property(u"window",u"closebutton",u"",u"imagefile").value_or(QString());
        const QImage closeWas=before->bitmap(closeFile).image,closeNow=after->bitmap(closeFile).image;
        QVERIFY(!closeWas.isNull());QCOMPARE(closeNow.size(),closeWas.size());
        int kept=0,recoloured=0;
        for(int y=0;y<closeWas.height();++y) for(int x=0;x<closeWas.width();++x) {
            const QRgb was=closeWas.pixel(x,y),now=closeNow.pixel(x,y);
            if(!lunaBlueMade(was)){QVERIFY2(now==was,"Close keeps Royale's red");++kept;}
            else{QVERIFY2(!lunaBlueMade(now),"Close retained Royale's blue");++recoloured;}
        }
        QVERIFY(kept>0 && recoloured>0);
        QRgb text;
        QVERIFY(parseColour(after->property(u"startpanel",u"placeslist",u"",u"textcolor").value_or(QString()),&text));
        QCOMPARE(QColor(text),QColor(Qt::white));
        QVERIFY(parseColour(after->property(u"startpanel",u"proglist",u"",u"textcolor").value_or(QString()),&text));
        QCOMPARE(QColor(text),QColor(Qt::white));
    }

    void customColorsForInstalledSkins()
    {
        if (m_realThemes.isEmpty()) QSKIP("needs the default theme library");
        int checked = 0;
        QDirIterator files(m_realThemes, {QStringLiteral("*.msstyles")}, QDir::Files, QDirIterator::Subdirectories);
        const std::array<QColor,4> colors{QColor("#123456"), QColor("#8a257a"), QColor("#409b52"), QColor("#ad7a10")};
        while (files.hasNext()) {
            const QString source = files.next();
            if (source.contains(QLatin1String("/Custom/")) || source.contains(QLatin1String("/__MACOSX/"))) continue;
            const auto theme = Theme::load(source); if (!theme) continue;
            for (const auto &variant : theme->variants().iniNames) {
                if (variant == QLatin1String("THEMES_INI")) continue;
                QString error;
                const auto generated = editor::customLuna(source, colors, paths::data() + QLatin1String("/custom-luna"), &error, variant);
                QVERIFY2(!generated.isEmpty(), qPrintable(source + QLatin1Char(':') + variant + QLatin1Char(' ') + error));
                const auto metadata=editor::colorMetadata(generated);
                const auto generatedVariant=metadata.value(QStringLiteral("variant")).toString(variant);
                const auto custom = Theme::load(generated, generatedVariant); QVERIFY(custom);
                QRgb color;
                QVERIFY(parseColour(custom->property(u"sysmetrics", u"", u"", u"highlight").value_or(QString()), &color));
                QCOMPARE(QColor(color), colors[1]);
                const auto originalVariant = Theme::load(metadata.value(QStringLiteral("source")).toString(source), generatedVariant); QVERIFY(originalVariant);
                QCOMPARE(custom->bitmapNames(), originalVariant->bitmapNames());
                Renderer before(originalVariant), after(custom);
                for (const auto &ref : {PartRef{QStringLiteral("button"), 1, 1, QStringLiteral("start")},
                    PartRef{QStringLiteral("taskbar"), schema::partId(u"taskbar",u"backgroundbottom"),0,{}},
                    PartRef{QStringLiteral("window"), schema::partId(u"window",u"caption"),1,{}}}) {
                    const auto oldImage = before.background(ref,QSize(100,30)), image = after.background(ref,QSize(100,30));
                    QCOMPARE(image.size(),oldImage.size());
                    QVERIFY2(image != oldImage, qPrintable(source + QLatin1Char(':') + variant + QLatin1Char(' ') + ref.cls));
                }
                ++checked;
                // (Each style is ~4 MB, and there is one per variant of every theme.)
                QFile::remove(generated);
                QFile::remove(generated + QStringLiteral(".json"));
            }
        }
        QVERIFY(checked > 0);
        qInfo() << "Custom colors checked for" << checked << "installed style variants";
    }

    void customClassicShellColors()
    {
        int checked = 0;
        for (const auto &entry : schemes::list()) {
            if (!entry.stock) continue;
            auto scheme = schemes::load(entry.name); QVERIFY(scheme);
            const auto original = *scheme;
            scheme->shellBaseName = entry.name;
            scheme->shellColors = std::array<QColor,4>{QColor("#163656"), QColor("#8a257a"), QColor("#409b52"), QColor("#ad7a10")};
            ThemeFile file; scheme->writeTo(file);
            const auto loaded = ClassicScheme::fromThemeFile(file);
            QVERIFY(loaded.shellColors == scheme->shellColors);
            QCOMPARE(loaded.shellBaseName,entry.name);
            QCOMPARE(loaded.shellPalette(0).color(QPalette::Button),(*scheme->shellColors)[0]);
            QCOMPARE(loaded.shellPalette(1).color(QPalette::Button),(*scheme->shellColors)[1]);
            QCOMPARE(loaded.shellPalette(3).color(QPalette::Button),(*scheme->shellColors)[3]);
            QCOMPARE(loaded.shellPalette(4).color(QPalette::Button),QColor("#c75032"));
            QCOMPARE(loaded.palette().color(QPalette::Button),original.palette().color(QPalette::Button));
            ++checked;
        }
        QVERIFY(checked > 0);
        qInfo() << "Classic overrides checked for" << checked << "stock schemes";
    }

    void customLunaAccentSurvivesDarkMode()
    {
        if (m_realThemes.isEmpty()) QSKIP("needs an installed XP visual style");
        for (const QColor accent : {QColor("#005500"), QColor("#ffde90")}) {
            const std::array<QColor, 4> palette{QColor("#242424"), accent, QColor("#8a2828"), QColor("#287a28")};
            QString error;
            const QString file = editor::customLuna(m_realThemes + QLatin1String("/Luna/luna.msstyles"), palette,
                                                    paths::data() + QLatin1String("/custom-luna"), &error);
            QVERIFY2(!file.isEmpty(), qPrintable(error));
            const auto look = Look::fromStyle(file, QStringLiteral("NORMALBLUE_INI"), &error);
            QVERIFY2(look, qPrintable(error));
            QCOMPARE(look->system.colour(u"Hilight"), accent);
            const auto apps = darkmode::applicationLook(*look, true, &error);
            QVERIFY2(apps, qPrintable(error));
            QCOMPARE(apps->system.colour(u"Hilight"), accent);
            QCOMPARE(apps->system.colour(u"HilightText"), look->system.colour(u"HilightText"));
            const QColor text = look->system.colour(u"HilightText");
            QCOMPARE(text, accent == QColor("#005500") ? QColor(Qt::white) : QColor(Qt::black));
            if (!m_realIso.isEmpty()) {
                State state; state.media = m_realIso; QVERIFY(state.save());
                const auto start = writeStartPanel(*look, paths::build() + QLatin1String("/start"), &error);
                QVERIFY2(!start.isEmpty(), qPrintable(error));
                QCOMPARE(start.value(QStringLiteral("palette")).toObject().value(QStringLiteral("highlight")).toString(), accent.name());
            }
        }
    }

    void applyAndReset()
    {
        if (m_realThemes.isEmpty() || m_realIso.isEmpty())
            QSKIP("needs the asset library and the XP ISO");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        const QString share = paths::localShare();
        // Another look's package, and one of the user's own.
        writeFile(share + QLatin1String("/aurorae/themes/old-look/metadata.json"), "{\"X-XPlasma-Generated\": true}");
        writeFile(share + QLatin1String("/aurorae/themes/Mine/metadata.json"), "{\"KPlugin\": {\"Name\": \"Mine\"}}");
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"), QStringLiteral("NORMALMETALLIC_INI"));
        QVERIFY(look);
        RecordingSession session;
        session.existingPanel = true;
        session.config.insert(QStringLiteral("kwinrulesrc|General|rules"), QStringLiteral("someone-elses"));
        session.config.insert(QStringLiteral("kdeglobals|KDE|widgetStyle"), QStringLiteral("Breeze"));
        session.config.insert(QStringLiteral("kdeglobals|Icons|Theme"), QStringLiteral("Papirus"));
        const QString liveSvg = share + QLatin1String("/plasma/desktoptheme/windows-xp-style-silver-taskbar/widgets/panel-background.svg");
        writeFile(liveSvg, "old-renderer-still-uses-this");
        session.onShellStop = [&] { QCOMPARE(readFile(liveSvg), QByteArray("old-renderer-still-uses-this")); };
        ApplyOptions o;
        o.wallpaper = Wallpaper{m_home.filePath(QStringLiteral("bliss.jpg")), 2, QColor(0, 0, 0)};
        QStringList said;
        o.progress = [&](const QString &line) { said << line; };
        QString error;
        QVERIFY2(applyLook(session, *look, o, &error), qPrintable(error + said.join(QLatin1Char('\n'))));
        session.onShellStop = {};
        int shellStops = 0;
        for (const auto &command : session.runs)
            if (command.value(0) == QLatin1String("systemctl") && command.contains(QStringLiteral("stop"))
                && command.contains(QStringLiteral("plasma-plasmashell.service"))) ++shellStops;
        QCOMPARE(shellStops, 1); // Artwork and AppletOrder share one restart.
        QVERIFY(!xpl::lookOutdated(State::load()));   // (applied by this XPlasma)

        const QString installedId=State::load().settings.value("appliedPackageId").toString();QVERIFY(installedId.startsWith("xplasma-v2-"));
        // Installed where Plasma looks.
        if(session.config.value("kwinrc|org.kde.kdecoration2|library")=="fyi.hotsocket.xplasma.decoration")
            QVERIFY(QFileInfo::exists(paths::stylePluginRoot(6)+"/org.kde.kdecoration3/fyi.hotsocket.xplasma.decoration.so"));
        else
            QVERIFY(QFileInfo::exists(share + (QStringLiteral("/aurorae/themes/")+installedId+QStringLiteral("/decoration.svg"))));
        QVERIFY(QFileInfo::exists(share + (QStringLiteral("/color-schemes/")+installedId+QStringLiteral(".colors"))));
        QVERIFY(QFileInfo::exists(share + (QStringLiteral("/plasma/desktoptheme/")+installedId+QStringLiteral("-taskbar/widgets/tasks.svg"))));
        {
            // KSvg's cache primed with the theme's size variants (the check
            // box's 16px box), stamped with the SVG's own time.
            const QString button = share + (QStringLiteral("/plasma/desktoptheme/")+installedId+QStringLiteral("-taskbar/widgets/button.svg"));
            QFile cache(paths::home() + QLatin1String("/.cache/ksvg-elements"));
            QVERIFY(cache.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(cache.readAll());
            QVERIFY2(text.contains(QLatin1Char('[') + button + QStringLiteral("]\nLastModified=")
                                   + QString::number(QFileInfo(button).lastModified().toSecsSinceEpoch())),
                     qPrintable(text.left(300)));
            QVERIFY(text.contains(QLatin1String("shadow-center=4x4,16x16,")));
        }
        {
            // A Plasma 5+ theme, or Plasma gives its colour sets the Button colours.
            QFile meta(share + (QStringLiteral("/plasma/desktoptheme/")+installedId+QStringLiteral("-taskbar/metadata.json")));
            QVERIFY(meta.open(QIODevice::ReadOnly));
            QCOMPARE(QJsonDocument::fromJson(meta.readAll()).object().value(QStringLiteral("X-Plasma-API")).toString(), QStringLiteral("6.0"));
        }
        QVERIFY(QFileInfo::exists(share + QLatin1String("/plasma/plasmoids/fyi.hotsocket.xplasma.start/contents/ui/main.qml")));
        QVERIFY(QFileInfo::exists(share + QLatin1String("/icons/xplasma-icons/index.theme")));
        QVERIFY(!QFileInfo::exists(share + QLatin1String("/aurorae/themes/old-look")));
        QVERIFY(QFileInfo::exists(share + QLatin1String("/aurorae/themes/Mine")));
        // Switched to.
        if(session.config.value("kwinrc|org.kde.kdecoration2|library")=="fyi.hotsocket.xplasma.decoration")
            QCOMPARE(session.config.value("kwinrc|org.kde.kdecoration2|theme"),QString());
        else
            QCOMPARE(session.config.value(QStringLiteral("kwinrc|org.kde.kdecoration2|theme")), QStringLiteral("__aurorae__svg__")+installedId);
        // The copy dialog's and the editor's rules, beside the user's own.
        QCOMPARE(session.config.value(QStringLiteral("kwinrulesrc|General|rules")),
                 QStringLiteral("someone-elses,xplasma-copy-dialog,xplasma-display-properties"));
        QCOMPARE(session.config.value(QStringLiteral("kwinrulesrc|General|count")), QStringLiteral("3"));
        QCOMPARE(session.config.value(QStringLiteral("kwinrulesrc|xplasma-display-properties|wmclass")),
                 QStringLiteral("^fyi\\.hotsocket\\.xplasma(\\.datetime)?$"));
        QCOMPARE(session.config.value(QStringLiteral("kwinrulesrc|xplasma-copy-dialog|minimizerule")), QStringLiteral("2"));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|KDE|widgetStyle")), QStringLiteral("XPlasma"));
        QVERIFY(session.config.value(QStringLiteral("kdeglobals|WM|activeFont")).startsWith(QLatin1String("Trebuchet MS,10")));
        QVERIFY(session.ran(QStringLiteral("plasma-apply-colorscheme")));
        // (The current scheme's name forgotten just before: plasma-apply-colorscheme
        // won't apply a scheme already current, a look applied again regenerated.)
        {
            const auto at = [&](auto match) { return int(std::find_if(session.runs.begin(), session.runs.end(), match) - session.runs.begin()); };
            const int forget = at([](const QStringList &a) { return a.contains(QStringLiteral("ColorScheme")) && a.last().isEmpty(); });
            const int apply = at([](const QStringList &a) { return a.value(0) == QLatin1String("plasma-apply-colorscheme"); });
            QVERIFY(forget < apply);
            QCOMPARE(apply - forget, 1);
        }
        QVERIFY(session.ran(QStringLiteral("plasma-apply-desktoptheme")));
        QVERIFY(session.calls.contains(QStringLiteral("/KDEPlatformTheme refreshFonts")));
        QVERIFY(session.calls.contains(QStringLiteral("/KWin reconfigure")));
        const QString layout = session.scripts.filter(QStringLiteral("LAUNCHER_KEYS")).value(0);
        QVERIFY(layout.contains(QLatin1String("var START = \"fyi.hotsocket.xplasma.start\"")));
        QVERIFY2(!layout.contains(QLatin1String("${")), "a value the layout script wants wasn't given");
        QVERIFY(layout.contains(QLatin1String("var DESKTOP_FILES = {")));
        QVERIFY(layout.contains(QLatin1String("\"button\":{")));
        QVERIFY(!session.scripts.filter(QStringLiteral("p.height = 30")).isEmpty());
        QVERIFY(!session.scripts.filter(QStringLiteral("bliss.jpg")).isEmpty());
        // The panel's order, written with the shell down.
        QCOMPARE(session.config.value(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc|Containments/1/General|AppletOrder")),
                 QStringLiteral("3;4;5"));
        // Themes swapped into place whole: nothing staged is left beside them.
        QDirIterator staged(share, {QStringLiteral("*.xplasma-new")}, QDir::Dirs, QDirIterator::Subdirectories);
        QVERIFY2(!staged.hasNext(), qPrintable(staged.hasNext() ? staged.next() : QString()));
        // Qt: the plugin, the frozen style, xplasmarc.
        QVERIFY(QFileInfo::exists(paths::stylePluginRoot(6) + QLatin1String("/styles/libxplasma.so")));
        QSettings rc(paths::config() + QLatin1String("/xplasmarc"), QSettings::IniFormat);
        QVERIFY(rc.value(QStringLiteral("Theme/Path")).toString().startsWith(paths::data() + QLatin1String("/active/")));
        QCOMPARE(rc.value(QStringLiteral("Theme/Variant")).toString(), QStringLiteral("NORMALMETALLIC_INI"));
        // (A copy beside the look, for Flatpak apps' sandboxes.)
        QCOMPARE(QSettings(paths::data() + QLatin1String("/active/xplasmarc"), QSettings::IniFormat).value(QStringLiteral("Theme/Path")),
                 rc.value(QStringLiteral("Theme/Path")));
        const State applied = State::load();
        QCOMPARE(applied.appliedKind, QStringLiteral("style"));
        QCOMPARE(applied.variant, QStringLiteral("NORMALMETALLIC_INI"));
        QCOMPARE(applied.settings.value(QStringLiteral("previousWidgetStyle")).toString(), QStringLiteral("Breeze"));
        QCOMPARE(applied.settings.value(QStringLiteral("previousIconTheme")).toString(), QStringLiteral("Papirus"));

        // Reset takes it all off.
        RecordingSession resetSession;
        resetSession.config = session.config;
        QVERIFY2(resetDesktop(resetSession, {}, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(share + (QStringLiteral("/aurorae/themes/")+installedId+QStringLiteral(""))));
        QVERIFY(!QFileInfo::exists(share + (QStringLiteral("/color-schemes/")+installedId+QStringLiteral(".colors"))));
        QVERIFY(!QFileInfo::exists(share + QLatin1String("/plasma/plasmoids/fyi.hotsocket.xplasma.start")));
        QVERIFY(!QFileInfo::exists(share + QLatin1String("/icons/xplasma-icons")));
        QVERIFY(!QFileInfo::exists(paths::config() + QLatin1String("/xplasmarc")));
        QVERIFY(QFileInfo::exists(share + QLatin1String("/aurorae/themes/Mine")));
        QCOMPARE(resetSession.config.value(QStringLiteral("kdeglobals|KDE|widgetStyle")), QStringLiteral("Breeze"));
        QCOMPARE(resetSession.config.value(QStringLiteral("kwinrulesrc|General|rules")), QStringLiteral("someone-elses"));
        QVERIFY(!resetSession.config.contains(QStringLiteral("kwinrulesrc|xplasma-copy-dialog|minimizerule")));
        QVERIFY(!resetSession.config.contains(QStringLiteral("kdeglobals|WM|activeFont")));
        QVERIFY(std::any_of(resetSession.runs.begin(), resetSession.runs.end(), [](const QStringList &a) {
            return a.value(0) == QLatin1String("plasma-apply-lookandfeel") && a.contains(QStringLiteral("--resetLayout"));
        }));
        QCOMPARE(State::load().appliedKind, QString());
    }
    void resetUnloadsForksFirst()
    {
        // XPlasma's builds of Plasma's widgets go with the shell down, and
        // the shell's back without them before Plasma's layout is -- given
        // the default panel, a shell that had them loaded would load
        // Plasma's own beside them, and die of the module registered twice.
        struct Watching : RecordingSession {
            QString plugin;
            QStringList seen;
            host::Result run(const QStringList &argv) override
            {
                if (argv.value(0) != QLatin1String("kwriteconfig6") && argv.value(0) != QLatin1String("kreadconfig6"))
                    seen << argv.join(QLatin1Char(' ')) + (QFileInfo::exists(plugin) ? QLatin1String(" [loaded]") : QLatin1String(" [gone]"));
                return RecordingSession::run(argv);
            }
        } session;
        session.plugin = paths::stylePluginRoot(6) + QLatin1String("/plasma/applets/org.kde.plasma.taskmanager.so");
        QVERIFY(QDir().mkpath(QFileInfo(session.plugin).path()));
        QFile plugin(session.plugin);
        QVERIFY(plugin.open(QIODevice::WriteOnly));
        plugin.close();
        State state = State::load();
        state.settings.insert(QStringLiteral("forkPlugins"), QJsonArray{session.plugin});
        state.save();

        QString error;
        QVERIFY2(resetDesktop(session, {}, &error), qPrintable(error));
        const auto at = [&](const QString &line) { return int(session.seen.indexOf(line)); };
        const int stop = at(QStringLiteral("systemctl --user stop plasma-plasmashell.service [loaded]"));
        const int start = at(QStringLiteral("systemctl --user start plasma-plasmashell.service [gone]"));
        const int layout = int(session.seen.indexOf(QRegularExpression(QStringLiteral("^plasma-apply-lookandfeel .*--resetLayout \\[gone\\]$"))));
        QVERIFY2(stop >= 0 && start > stop && layout > start, qPrintable(session.seen.join(QLatin1Char('\n'))));
        QVERIFY(!forks::installed(State::load()));
    }

    void shellIsLeftUpBeforeItsStopped()
    {
        // A shell stopped moments after it started can crash on its way out
        // (its wallpaper still loading): it's left up a few seconds first,
        // by systemd's word of when it started, or this session's own.
        const auto now = [] { timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t); return qint64(t.tv_sec) * 1000000 + t.tv_nsec / 1000; };
        const auto timed = [](RecordingSession &s) { QElapsedTimer t; t.start(); s.restartPlasmaShell(); return t.elapsed(); };
        RecordingSession session;
        session.shellSettleMs = 600;
        session.shellActive = true;
        session.shellActiveSince = QString::number(now());          // (just started)
        QVERIFY(timed(session) >= 450);
        session.shellActiveSince = QString::number(now() - 60000000);   // (a minute ago)
        QVERIFY(timed(session) < 300);
        // No word from systemd: since this session last started it -- just now.
        session.shellActiveSince.clear();
        QVERIFY(timed(session) >= 450);
        // ...and a shell that isn't running is just started.
        session.shellActive = false;
        QVERIFY(timed(session) < 300);
    }

    void shellReadyMeansItsDesktopLoaded()
    {
        // Answering scripts isn't ready: a desktop loaded is. A shell that
        // has none with the activity manager down gets the manager started
        // again, and is restarted after it.
        RecordingSession session;
        session.shellLoadingFor = 2;
        QVERIFY(session.waitForPlasmaShell(5000));
        QVERIFY(!session.ran(QStringLiteral("systemctl")));

        session.activities = QStringLiteral("failed");
        QString why;
        QVERIFY2(session.ensurePlasmaShell(&why, 1000), qPrintable(why));
        const auto ranWith = [&](const QStringList &args) {
            return std::any_of(session.runs.begin(), session.runs.end(), [&](const QStringList &a) {
                return std::all_of(args.begin(), args.end(), [&](const QString &x) { return a.contains(x); }); });
        };
        QVERIFY(ranWith({QStringLiteral("start"), QStringLiteral("plasma-kactivitymanagerd.service")}));
        QVERIFY(ranWith({QStringLiteral("start"), QStringLiteral("plasma-plasmashell.service")}));

        // ...and a shell that never loads one says why.
        session.shellLoadingFor = 1000000;
        QVERIFY(!session.ensurePlasmaShell(&why, 1000));
        QVERIFY2(why.contains(QLatin1String("didn't finish loading")), qPrintable(why));
    }

    void taskbarSeenThroughAfterLayout()
    {
        // The taskbar is checked once laid out: what it lacks is laid out
        // again, and the look fails, not reports success, if it still lacks it.
        if (m_realThemes.isEmpty() || m_realIso.isEmpty())
            QSKIP("needs the asset library and the XP ISO");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        const auto look = Look::fromStyle(m_realThemes + QLatin1String("/Luna/luna.msstyles"), QStringLiteral("NORMALBLUE_INI"));
        QVERIFY(look);
        const auto layouts = [](const RecordingSession &s) {
            return int(std::count_if(s.scripts.begin(), s.scripts.end(), [](const QString &js) { return js.contains(QLatin1String("LAUNCHER_KEYS")); }));
        };
        QString error;
        {
            RecordingSession session;
            session.taskbarChecks = {QStringLiteral("clock")};
            QVERIFY2(applyLook(session, *look, ApplyOptions{}, &error), qPrintable(error));
            QCOMPARE(layouts(session), 2);
        }
        {
            RecordingSession session;
            session.taskbarChecks = {QStringLiteral("Start button, clock"), QStringLiteral("clock")};
            QVERIFY(!applyLook(session, *look, ApplyOptions{}, &error));
            QVERIFY2(error.contains(QLatin1String("has no clock")), qPrintable(error));
        }
    }

    void bugFixPlasmaRechecksForks()
    {
        // A bug-fix release of the series they were chosen for: the builds
        // in place are tried, and one that no longer loads takes them off,
        // Plasma's own back, the Plasma that lost them noted for Setup.
        RecordingSession session;
        const QString plasma = forks::runningPlasma(session);
        if (plasma.isEmpty())
            QSKIP("no Plasma here");
        const QVersionNumber series = forks::seriesOf(plasma);
        const QString plugin = paths::stylePluginRoot(6) + QLatin1String("/plasma/applets/org.kde.plasma.taskmanager.so");
        QVERIFY(QDir().mkpath(QFileInfo(plugin).path()));
        QFile file(plugin);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a plugin");
        file.close();
        State state = State::load();
        state.settings.insert(QStringLiteral("forkPlugins"), QJsonArray{plugin});
        state.settings.insert(QStringLiteral("forkSeries"), series.toString());
        // (another release of the same series, as far as check can tell)
        const QString other = series.toString() + QLatin1String(".0");
        state.settings.insert(QStringLiteral("forkPlasma"), other == plasma ? other + QLatin1String(".1") : other);

        QString what;
        QCOMPARE(forks::check(session, state, &what), forks::Checked::Removed);
        QVERIFY2(what.contains(QLatin1String("org.kde.plasma.taskmanager")), qPrintable(what));
        QVERIFY(!QFileInfo::exists(plugin));
        QVERIFY(!forks::installed(state));
        QCOMPARE(state.settings.value(QStringLiteral("forkLost")).toString(), plasma);

        // ...and the same Plasma as when chosen isn't looked at again.
        state.settings.insert(QStringLiteral("forkPlugins"), QJsonArray{plugin});
        state.settings.insert(QStringLiteral("forkPlasma"), plasma);
        QCOMPARE(forks::check(session, state), forks::Checked::Fine);
    }

    // --- setup ----------------------------------------------------------------------

    void xpTimeZones()
    {
        // XP's zones from its registry INF, in its order, each with its Linux
        // zone and its colours on XP's world map.
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        QString error;
        const auto zones = timezones::load(&error);
        QVERIFY2(zones, qPrintable(error));
        QCOMPARE(zones->size(), 75);
        QCOMPARE(zones->first().display, QStringLiteral("(GMT-12:00) International Date Line West"));
        const int central = timezones::find(*zones, "America/Chicago");
        QVERIFY(central >= 0);
        const timezones::Zone &z = zones->at(central);
        QCOMPARE(z.display, QStringLiteral("(GMT-06:00) Central Time (US & Canada)"));
        QCOMPARE(z.standard, QStringLiteral("Central Standard Time"));
        QCOMPARE(z.daylight, QStringLiteral("Central Daylight Time"));
        QCOMPARE(std::pair(z.mapSea, z.mapLand), std::pair(36, 37));
        // (Another Linux name for a zone XP has.)
        // Every zone stands for a Linux one (three by names Windows retired).
        for (const timezones::Zone &each : *zones)
            QVERIFY2(QTimeZone(each.iana).isValid(), qPrintable(each.key));
        QCOMPARE(timezones::find(*zones, "US/Central"), central);
        const auto map = timezones::map(&error);
        QVERIFY2(map, qPrintable(error));
        QCOMPARE(map->indices.size(), QSize(356, 184));
        int highest = 0;
        for (int y = 0; y < 184; ++y)
            for (int x = 0; x < 356; ++x)
                highest = qMax(highest, int(map->indices.constScanLine(y)[x]));
        QCOMPARE(highest, 89);
    }

    void dateTimeProperties()
    {
        // xplasma-edit --date-time draws its tabs offscreen at a fixed moment
        // in Central Time; the Time Zone tab's map is XP's, scrolled as XP
        // scrolls it for Central (249px, measured on XP).
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State state;
        state.media = m_realIso;
        QVERIFY(state.save());
        QString editor = QCoreApplication::applicationDirPath() + QLatin1String("/../../src/app/xplasma-edit");
        if(!QFileInfo(editor).isExecutable())editor=QCoreApplication::applicationDirPath()+QLatin1String("/xplasma-edit");
        if (!QFileInfo(editor).isExecutable())
            QSKIP("xplasma-edit isn't built here");
        const QString out = m_home.filePath(QStringLiteral("datetime"));
        QProcess run;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        run.setProcessEnvironment(env);
        run.start(editor, {QStringLiteral("--date-time"), QStringLiteral("--screenshots"), out});
        QVERIFY(run.waitForFinished(60000));
        QCOMPARE(run.exitCode(), 0);
        for (const char *tab : {"datetime", "timezone", "internettime"})
            QVERIFY2(QFileInfo::exists(out + QLatin1Char('/') + QLatin1String(tab) + QLatin1String(".png")), tab);
        const QImage zone(out + QLatin1String("/timezone.png"));
        // The map: its top-left pixel is the first navy-or-green one.
        QPoint origin(-1, -1);
        for (int y = 0; y < zone.height() && origin.x() < 0; ++y)
            for (int x = 0; x < zone.width(); ++x)
                if (const QRgb c = zone.pixel(x, y) & 0xffffff; c == 0x00007f || c == 0x007f00) {
                    origin = QPoint(x, y);
                    break;
                }
        QVERIFY(origin.x() >= 0);
        QString error;
        const auto map = timezones::map(&error);
        QVERIFY2(map, qPrintable(error));
        auto isSea = [](int i) { return (i < 48 && i % 2 == 0) || i == 78 || i == 88; };
        int wrong = 0;
        for (int y = 0; y < map->indices.height(); ++y)
            for (int x = 0; x < map->indices.width(); ++x) {
                const bool sea = isSea(map->indices.constScanLine(y)[(x + 249) % map->indices.width()]);
                wrong += (zone.pixel(origin.x() + x, origin.y() + y) & 0xffffff) != (sea ? 0x00007fu : 0x007f00u);
            }
        QCOMPARE(wrong, 0);
    }

    void importMedia()
    {
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        setup::Imported n;
        QString error;
        QVERIFY2(setup::importMedia(m_realIso, &n, {}, nullptr, &error), qPrintable(error));
        QVERIFY(n.fonts > 100);
        QCOMPARE(n.styles, 1);
        QVERIFY(n.themes >= 2);
        QCOMPARE(n.wallpapers, 31);   // XP's list: 20 in Web\\Wallpaper, 11 desktop wallpapers
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Luna/luna.msstyles")));
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Luna/Luna.theme")));
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Windows Classic.theme")));
        QVERIFY(QFileInfo::exists(paths::wallpapers() + QLatin1String("/Bliss.jpg")));
        QVERIFY(QFileInfo::exists(paths::localShare() + QLatin1String("/fonts/xp/tahoma.ttf")));
        QCOMPARE(State::load().media, m_realIso);
        // XP's names, its small tiles included, and nothing else from I386.
        for (const char *name : {"Red moon desert.jpg", "Windows XP.jpg", "Stonehenge.jpg", "Coffee Bean.bmp", "Blue Lace 16.bmp", "Zapotec.bmp"})
            QVERIFY2(QFileInfo::exists(paths::wallpapers() + QLatin1Char('/') + QLatin1String(name)), name);
        for (const char *name : {"setup_w.bmp", "table.bmp", "wpaback.jpg", "stoneh.jpg", "winnt.bmp", "bluehill.jpg"})
            QVERIFY2(!QFileInfo::exists(paths::wallpapers() + QLatin1Char('/') + QLatin1String(name)), name);
    }

    void importMediaTidiesOldWallpapers()
    {
        // An older import's 8.3 names become XP's (a saved theme naming one
        // follows); what XP didn't list goes; the user's own stays.
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        const QString dir = paths::wallpapers();
        writeFile(dir + QLatin1String("/stoneh.jpg"), "old");
        writeFile(dir + QLatin1String("/setup_w.bmp"), "old");
        writeFile(dir + QLatin1String("/mine.jpg"), "mine");
        const QString preset = paths::themes() + QLatin1String("/Saved/Old.theme");
        writeFile(preset, ("[Control Panel\\Desktop]\r\nWallpaper=" + dir + "/stoneh.jpg\r\n").toUtf8());
        QString error;
        QVERIFY2(setup::importMedia(m_realIso, nullptr, {}, nullptr, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(dir + QLatin1String("/stoneh.jpg")));
        QVERIFY(!QFileInfo::exists(dir + QLatin1String("/setup_w.bmp")));
        QVERIFY(QFileInfo::exists(dir + QLatin1String("/Stonehenge.jpg")));
        QVERIFY(QFileInfo::exists(dir + QLatin1String("/mine.jpg")));
        QFile f(preset);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains((dir + QLatin1String("/Stonehenge.jpg")).toUtf8()));
    }

    void infFields()
    {
        // XP's INF quoting: commas split fields outside quotes, "" is a quote
        // inside them, ; ends the line, %KEY% from [Strings].
        const Inf inf = Inf::parse("[Strings]\r\nName = \"Hands 1\"\r\nCursors=cursors\r\n");
        const QStringList f = inf.fields(QStringLiteral("HKLM,%NAME%,,\"\"\"%10%\\%Cursors%\\a.cur,,b.ani\"\"\" ; comment"));
        QCOMPARE(f.size(), 4);
        QCOMPARE(f.at(1), QStringLiteral("Hands 1"));
        QCOMPARE(f.at(3), QStringLiteral("\"%10%\\cursors\\a.cur,,b.ani\""));
    }

    void pointerSchemesFromTheMedia()
    {
        // XP's 19 schemes, each naming a file per role (or Windows Default's),
        // and a theme written from one with every name apps ask for.
        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State s = State::load();
        s.media = m_realIso;
        QVERIFY(s.save());
        const QList<pointers::Scheme> all = pointers::schemes();
        QCOMPARE(all.size(), 19);
        const auto dino = std::find_if(all.begin(), all.end(), [](const pointers::Scheme &x) { return x.name == QLatin1String("Dinosaur"); });
        QVERIFY(dino != all.end());
        QCOMPARE(dino->files.value(pointers::AppStarting), QStringLiteral("dinosaur.ani"));
        QVERIFY(dino->files.value(pointers::Hand).isEmpty());   // (Windows Default's)
        QVERIFY(pointers::frames(*dino, pointers::AppStarting).size() > 1);   // animated
        const QList<CursorFrame> arrow = pointers::frames(pointers::defaultScheme(), pointers::Arrow);
        QCOMPARE(arrow.size(), 1);
        QCOMPARE(arrow.first().image.size(), QSize(32, 32));
        const QString dir = m_home.filePath(QStringLiteral("pointers"));
        int size = 0;
        QString error;
        QVERIFY2(pointers::writeTheme(*dino, true, dir, &size, &error), qPrintable(error));
        QCOMPARE(size, 32);
        for (int role = 0; role < pointers::RoleCount; ++role)
            for (const QString &name : pointers::xcursorNames(role))
                QVERIFY2(QFileInfo(dir + QLatin1String("/cursors/") + name).isFile(), qPrintable(name));
        QFile index(dir + QLatin1String("/index.theme"));
        QVERIFY(index.open(QIODevice::ReadOnly) && index.readAll().contains("Name=Dinosaur (XP)"));

        // Applied: a theme per scheme, at its size, the user's own kept for reset.
        RecordingSession session;
        session.config.insert(QStringLiteral("kcminputrc|Mouse|cursorTheme"), QStringLiteral("breeze_cursors"));
        State state;
        QVERIFY2(pointers::apply(session, state, *dino, false, &error), qPrintable(error));
        const QStringList run = session.runs.last();
        QCOMPARE(run.mid(0, 3), (QStringList{QStringLiteral("plasma-apply-cursortheme"), QStringLiteral("--size"), QStringLiteral("32")}));
        QVERIFY(run.last().startsWith(QLatin1String("xplasma-pointers-")));
        QCOMPARE(session.config.value(QStringLiteral("kcminputrc|Mouse|cursorSize")), QStringLiteral("32"));
        QVERIFY(QFileInfo(paths::localShare() + QLatin1String("/icons/") + run.last() + QLatin1String("/cursors/default")).exists());
        // (and in ~/.icons, where every Xcursor library looks)
        QVERIFY(QFileInfo(paths::home() + QLatin1String("/.icons/") + run.last() + QLatin1String("/cursors/default")).exists());
        QCOMPARE(pointers::applied(state)->first.name, QStringLiteral("Dinosaur"));
        pointers::revert(session, state);
        QCOMPARE(session.runs.last().last(), QStringLiteral("breeze_cursors"));
        QVERIFY(!session.config.contains(QStringLiteral("kcminputrc|Mouse|cursorSize")));   // (it had none)
        QVERIFY(!pointers::applied(state));
        QVERIFY(QDir(paths::localShare() + QLatin1String("/icons")).entryList({QStringLiteral("xplasma-pointers-*")}, QDir::Dirs).isEmpty());
        QVERIFY(!QFileInfo::exists(paths::home() + QLatin1String("/.icons")));   // (its links gone, and it with them)
    }

    void updateCheck()
    {
        // The release file: a version and a web page; anything else refused.
        const auto r = updates::parse("{\"version\": 2, \"page\": \"https:\/\/example.org/xplasma\", \"notes\": \"Sounds.\"}");
        QVERIFY(r);
        QCOMPARE(r->version, 2);
        QCOMPARE(updates::parse("{\"version\": \"12\", \"page\": \"https:\/\/example.org\"}")->version, 12);   // (a string will do)
        QVERIFY(!updates::parse("not json"));
        QVERIFY(!updates::parse("{\"page\": \"https:\/\/example.org\"}"));
        QVERIFY(!updates::parse("{\"version\": \"0.2.0\", \"page\": \"https:\/\/example.org\"}"));   // (a release number, not a dotted version)
        QVERIFY(!updates::parse("{\"version\": 2.5, \"page\": \"https:\/\/example.org\"}"));
        QVERIFY(!updates::parse("{\"version\": 2, \"page\": \"file:\/\/\/etc/passwd\"}"));

        // Once a day; a declined version not told again, a later one is.
        State state;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(updates::due(state, now));
        updates::checked(state, now);
        QVERIFY(!updates::due(state, now.addSecs(3600)));
        QVERIFY(updates::due(state, now.addDays(1)));
        const updates::Release far{project::kVersion + 1, QStringLiteral("https://example.org"), QString()};
        QVERIFY(updates::worthTelling(state, far));
        updates::decline(state, far);
        QVERIFY(!updates::worthTelling(state, far));
        QVERIFY(updates::worthTelling(state, updates::Release{project::kVersion + 2, QStringLiteral("https://example.org"), QString()}));
        QVERIFY(!updates::worthTelling(state, updates::Release{project::kVersion, QStringLiteral("https://example.org"), QString()}));

        // Fetched (a file here, as a web address would be).
        const QString file = m_home.filePath(QStringLiteral("latest.json"));
        writeFile(file, "{\"version\": 3, \"page\": \"https:\/\/example.org/get\"}");
        QString error;
        const auto fetched = updates::fetch(QUrl::fromLocalFile(file).toString(), &error);
        QVERIFY2(fetched, qPrintable(error));
        QCOMPARE(fetched->page, QStringLiteral("https://example.org/get"));
    }

    void lookOutdated()
    {
        // A look applied by an older XPlasma (or one from before release
        // numbers) wants applying again; none applied, or this one's, doesn't.
        State state;
        QVERIFY(!xpl::lookOutdated(state));
        state.appliedKind = QStringLiteral("style");
        QVERIFY(xpl::lookOutdated(state));
        state.settings.insert(QLatin1String(kAppliedWithKey), project::kVersion - 1);
        QVERIFY(xpl::lookOutdated(state));
        state.settings.insert(QLatin1String(kAppliedWithKey), project::kVersion);
        QVERIFY(!xpl::lookOutdated(state));
    }

    void installedVersion()
    {
        // The installed XPlasma's own word: its release number; one from
        // before release numbers, or none, 0.
        const QString prefix = m_home.filePath(QStringLiteral("versioned"));
        const QString exe = prefix + QLatin1String("/bin/xplasma");
        QCOMPARE(installer::installedVersion(prefix), 0);
        for (const auto &[says, number] : {std::pair("xplasma 3", 3), std::pair("xplasma 0.1.0", 0)}) {
            writeFile(exe, QByteArray("#!/bin/sh\necho '") + says + "'\n");
            QFile(exe).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
            QCOMPARE(installer::installedVersion(prefix), number);
        }
    }

    void plasmaSeries()
    {
        // Point releases are their series; x.y.90 and up, KDE's betas, the next.
        QCOMPARE(forks::seriesOf(QStringLiteral("6.7.5")), QVersionNumber(6, 7));
        QCOMPARE(forks::seriesOf(QStringLiteral("6.7.0")), QVersionNumber(6, 7));
        QCOMPARE(forks::seriesOf(QStringLiteral("6.7.90")), QVersionNumber(6, 8));
        QCOMPARE(forks::seriesOf(QStringLiteral("6.7.91")), QVersionNumber(6, 8));
        QCOMPARE(forks::seriesOf(QStringLiteral("6.8")), QVersionNumber(6, 8));
    }

    void removeEmptyDirs()
    {
        // Emptied folders go, up to the top; a folder with anything left
        // in it stops it, and the top stays.
        const QString top = m_home.filePath(QStringLiteral(".local"));
        QVERIFY(QDir().mkpath(top + QLatin1String("/lib/qt6/plugins/styles")));
        QVERIFY(QDir().mkpath(top + QLatin1String("/lib/qt5/plugins/styles")));
        paths::removeEmptyDirs(top + QLatin1String("/lib/qt6/plugins/styles"), top);
        QVERIFY(!QFileInfo::exists(top + QLatin1String("/lib/qt6")));
        QVERIFY(QFileInfo::exists(top + QLatin1String("/lib/qt5/plugins/styles")));
        paths::removeEmptyDirs(top + QLatin1String("/lib/qt5/plugins/styles"), top);
        QVERIFY(!QFileInfo::exists(top + QLatin1String("/lib")));
        QVERIFY(QFileInfo::exists(top));
    }

    void soundSchemes()
    {
        // Every event under Plasma's names; Windows Default as XP's registry
        // has it; a sound for each of WINDOWS\Media's names.
        QSet<QString> names;
        for (const sounds::Event &e : sounds::events()) {
            QVERIFY(!e.names.isEmpty());
            for (const QString &n : e.names) {
                QVERIFY2(!names.contains(n), qPrintable(n));   // (one event per name)
                names.insert(n);
            }
        }
        const sounds::Scheme xp = sounds::defaultScheme();
        QCOMPARE(xp.sounds.value(QStringLiteral("SystemStart")), QStringLiteral("Windows XP Startup.wav"));
        QVERIFY(!xp.sounds.contains(QStringLiteral("SystemQuestion")));   // (XP's was quiet)
        for (const QString &sound : std::as_const(xp.sounds))
            QVERIFY2(sounds::mediaSounds().contains(sound), qPrintable(sound));
        QCOMPARE(sounds::Scheme::fromJson(xp.toJson()), xp);
        QVERIFY(sounds::wantedWithLook(State()));   // (a fresh desktop: yes)
        State saved;
        sounds::saveScheme(saved, sounds::Scheme{QStringLiteral("Mine"), {{QStringLiteral(".Default"), QStringLiteral("chord.wav")}}});
        QCOMPARE(sounds::savedSchemes(saved).size(), 1);
        sounds::deleteScheme(saved, QStringLiteral("Mine"));
        QVERIFY(sounds::savedSchemes(saved).isEmpty());

        // No Sounds: silence under every name (no other theme's stands in).
        const QString quiet = m_home.filePath(QStringLiteral("quiet"));
        QString error;
        QVERIFY2(sounds::writeTheme(sounds::Scheme{QStringLiteral("No Sounds"), {}}, quiet, &error), qPrintable(error));
        for (const QString &n : std::as_const(names))
            QVERIFY2(QFileInfo(quiet + QLatin1String("/stereo/") + n + QLatin1String(".wav")).size() == 44 + 2204, qPrintable(n));

        if (m_realIso.isEmpty())
            QSKIP("no XP ISO on this machine");
        State s = State::load();
        s.media = m_realIso;
        QVERIFY(s.save());
        // Windows Default's sounds from the media, under each name; the
        // log-in and lock sounds switched on; the user's theme kept for reset.
        RecordingSession session;
        session.config.insert(QStringLiteral("kdeglobals|Sounds|Theme"), QStringLiteral("oxygen"));
        State state;
        QVERIFY2(sounds::apply(session, state, xp, &error), qPrintable(error));
        const QString id = session.config.value(QStringLiteral("kdeglobals|Sounds|Theme"));
        QVERIFY(id.startsWith(QLatin1String("xplasma-sounds-")));
        const QString stereo = paths::localShare() + QLatin1String("/sounds/") + id + QLatin1String("/stereo/");
        QFile login(stereo + QLatin1String("desktop-login.wav"));
        QVERIFY(login.open(QIODevice::ReadOnly));
        const QByteArray wav = login.readAll();
        QVERIFY(wav.startsWith("RIFF") && wav.size() > 400000);   // (XP's Startup: 4.8 s)
        QVERIFY(QFileInfo(stereo + QLatin1String("dialog-question.wav")).size() < 3000);   // (quiet)
        QCOMPARE(session.config.value(QStringLiteral("plasma_workspace.notifyrc|Event/startkde|Action")), QStringLiteral("Sound"));
        QCOMPARE(session.config.value(QStringLiteral("ksmserver.notifyrc|Event/locked|Sound")), QStringLiteral("service-logout"));
        QCOMPARE(sounds::applied(state)->name, QStringLiteral("Windows Default"));
        // (and the window sounds' events, for the Start button to send)
        const QByteArray rc = readFile(paths::localShare() + QLatin1String("/knotifications6/xplasma.notifyrc"));
        QVERIFY(rc.contains("[Event/windowMinimize]") && rc.contains("Sound=window-minimized"));
        QVERIFY(QFileInfo(stereo + QLatin1String("window-minimized.wav")).size() < 3000);   // (quiet in Windows Default, as XP's)

        // Another scheme: its own theme, the last one gone.
        sounds::Scheme mine = xp;
        mine.name = QStringLiteral("Mine");
        mine.sounds.remove(QStringLiteral("SystemStart"));
        QVERIFY2(sounds::apply(session, state, mine, &error), qPrintable(error));
        QVERIFY(session.config.value(QStringLiteral("kdeglobals|Sounds|Theme")) != id);
        QVERIFY(!QFileInfo::exists(stereo));
        QVERIFY(!session.config.contains(QStringLiteral("plasma_workspace.notifyrc|Event/startkde|Action")));

        // A look brings them only while none are applied and they weren't
        // turned off; turning them off is remembered until a scheme is applied.
        QVERIFY(!sounds::wantedWithLook(state));
        sounds::turnOff(session, state);
        QVERIFY(!sounds::wantedWithLook(state));
        QVERIFY2(sounds::apply(session, state, xp, &error), qPrintable(error));
        sounds::revert(session, state);
        QVERIFY(sounds::wantedWithLook(state));
        QCOMPARE(session.config.value(QStringLiteral("kdeglobals|Sounds|Theme")), QStringLiteral("oxygen"));
        QVERIFY(!session.config.contains(QStringLiteral("ksmserver.notifyrc|Event/locked|Action")));
        QVERIFY(!sounds::applied(state));
        QVERIFY(!QFileInfo::exists(paths::localShare() + QLatin1String("/sounds")));   // (emptied: gone)
        QVERIFY(!QFileInfo::exists(paths::localShare() + QLatin1String("/knotifications6/xplasma.notifyrc")));
    }

    void wallpaperNamesFromInfs()
    {
        // XP's setup INFs, as they're written: UTF-16 or ANSI, %strings%,
        // "dest, source", comments; only the wallpaper copy lists count.
        const QString shl = QStringLiteral(
            "[DestinationDirs]\r\nWallpaper.CopyFiles = 10,Web\\Wallpaper\r\n"
            "[Wallpaper.CopyFiles]\r\n\"%MOON_JPG_NAME%\",moon.jpg\r\ndefault.jpg,bliss.jpg\r\n\r\n"
            "[OldWinntBMP.CopyFiles]\r\nwinnt.bmp,win32pro.bmp\r\n"
            "[Strings]\r\nMOON_JPG_NAME = \"Moon flower.jpg\"\r\nBLISS_JPG_NAME = \"Bliss.jpg\"\r\n");
        QByteArray wide("\xff\xfe");
        wide += QByteArray(reinterpret_cast<const char *>(shl.utf16()), shl.size() * 2);
        const QByteArray accessor = "[DeskpaperCopyFilesSys]\r\n%Coffeebn%,             coffeebn.bmp ; a tile\r\n"
                                    "[DeskpaperOldCopyFilesSys]\r\narches.bmp\r\n[Strings]\r\nCoffeebn        = \"Coffee Bean.bmp\"\r\n";
        const QHash<QString, QString> names = setup::wallpaperNames({wide, accessor});
        QCOMPARE(names.value(QStringLiteral("moon.jpg")), QStringLiteral("Moon flower.jpg"));
        QCOMPARE(names.value(QStringLiteral("bliss.jpg")), QStringLiteral("Bliss.jpg"));
        QCOMPARE(names.value(QStringLiteral("coffeebn.bmp")), QStringLiteral("Coffee Bean.bmp"));
        QCOMPARE(names.size(), 3);
    }

    void importMediaRollsBack()
    {
        // Not XP's media: nothing stays, the recorded media stays as it was.
        State s;
        s.media = QStringLiteral("/before.iso");
        QVERIFY(s.save());
        writeFile(m_home.filePath(QStringLiteral("notxp/I386/README.TXT")), "hi");
        writeFile(paths::themes() + QLatin1String("/Mine/mine.theme"), "[Theme]\n");
        QString error;
        QVERIFY(!setup::importMedia(m_home.filePath(QStringLiteral("notxp")), nullptr, {}, nullptr, &error));
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Mine/mine.theme")));
        QCOMPARE(State::load().media, QStringLiteral("/before.iso"));
    }

    void fetchPacks()
    {
        // archive.org, played by a server here: the item's metadata, then
        // its file.
        const QString pack = m_home.filePath(QStringLiteral("XP_Themes.zip"));
        QVERIFY(writeArchive(pack, QStringLiteral("zip"), {{"Royale/Royale.msstyles", "MZ-royale"}}));
        QFile packFile(pack);
        QVERIFY(packFile.open(QIODevice::ReadOnly));
        const QByteArray zip = packFile.readAll();
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QStringList requested;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const QByteArray request = socket->readAll();
                const QString path = QString::fromUtf8(request.split(' ').value(1));
                requested << path;
                QByteArray body = path.startsWith(QLatin1String("/metadata/"))
                                      ? QByteArray(R"({"files":[{"name":"readme.txt","size":"3"},{"name":"XP_Themes.zip","size":")")
                                            + QByteArray::number(zip.size()) + "\"}]}"
                                      : zip;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        qputenv("XPLASMA_ARCHIVE_URL", QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
        const setup::Download packs = setup::downloads().at(1);
        QString error;
        qint64 last = 0;
        QVERIFY2(setup::fetch(packs, {}, [&](qint64 done, qint64) { last = done; }, nullptr, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Royale/Royale.msstyles")));
        QVERIFY(QFileInfo::exists(paths::downloads() + QLatin1String("/XP_Themes.zip")));
        QCOMPARE(last, zip.size());
        QCOMPARE(requested, (QStringList{QStringLiteral("/metadata/windows-xp-official-themes"),
                                         QStringLiteral("/download/windows-xp-official-themes/XP_Themes.zip")}));
        // Downloaded once: the second time never touches the network.
        server.close();
        QVERIFY2(setup::fetch(packs, {}, {}, nullptr, &error), qPrintable(error));
        qunsetenv("XPLASMA_ARCHIVE_URL");
    }

    void preflight()
    {
        const auto checks = setup::preflight();
        QVERIFY(std::any_of(checks.begin(), checks.end(), [](const setup::Check &c) { return c.tool == QLatin1String("kwriteconfig6"); }));
    }

    // --- host programs --------------------------------------------------------

    void hostRun()
    {
        QStringList lines;
        host::Options options;
        options.onLine = [&](const QString &line) { lines << line; };
        const auto r = host::run({QStringLiteral("sh"), QStringLiteral("-c"),
                                  QStringLiteral("printf 'one\\ntwo\\r'; echo three >&2; exit 3")},
                                 options);
        QCOMPARE(r.exitCode, 3);
        QVERIFY(!r.ok());
        QCOMPARE(r.out, QByteArray("one\ntwo\r"));
        QCOMPARE(r.err, QByteArray("three\n"));
        lines.sort();
        QCOMPARE(lines, (QStringList{QStringLiteral("one"), QStringLiteral("three"), QStringLiteral("two")}));
    }

    void hostInput()
    {
        host::Options options;
        options.input = "fed through stdin";
        const auto r = host::run({QStringLiteral("cat")}, options);
        QVERIFY(r.ok());
        QCOMPARE(r.out, options.input);
    }

    void hostMissing()
    {
        const auto r = host::run({QStringLiteral("kwriteconfig6-surely-not-installed")});
        QCOMPARE(r.exitCode, 127);
        QVERIFY(r.err.contains("not found"));
        QVERIFY(host::installHint(QStringLiteral("kwriteconfig6")).contains(QLatin1String("KDE")));
    }

    void hostCancel()
    {
        std::atomic_bool cancel{false};
        host::Options options;
        options.cancel = &cancel;
        std::thread stopper([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            cancel = true;
        });
        QElapsedTimer clock;
        clock.start();
        const auto r = host::run({QStringLiteral("sleep"), QStringLiteral("30")}, options);
        stopper.join();
        QVERIFY(r.cancelled);
        QVERIFY(clock.elapsed() < 5000);

        host::Options timed;
        timed.timeoutMs = 200;
        QVERIFY(host::run({QStringLiteral("sleep"), QStringLiteral("30")}, timed).cancelled);
    }

    // --- state and adoption ---------------------------------------------------

    void stateRoundTrip()
    {
        QCOMPARE(State::load().appliedKind, QString());   // no file yet
        State s;
        s.appliedKind = QStringLiteral("style");
        s.style = QStringLiteral("/x/luna.msstyles");
        s.variant = QStringLiteral("NORMALBLUE_INI");
        s.media = QStringLiteral("/x/xp.iso");
        s.settings[QStringLiteral("titleShadows")] = true;
        QString error;
        QVERIFY2(s.save(&error), qPrintable(error));
        const State t = State::load();
        QCOMPARE(t.appliedKind, s.appliedKind);
        QCOMPARE(t.style, s.style);
        QCOMPARE(t.variant, s.variant);
        QCOMPARE(t.media, s.media);
        QCOMPARE(t.settings, s.settings);
    }

    void stateKeepsUnknownKeys()
    {
        QJsonObject json{{QStringLiteral("version"), 7}, {QStringLiteral("fromTheFuture"), QStringLiteral("keep me")}};
        writeFile(paths::statePath(), QJsonDocument(json).toJson());
        State s = State::load();
        s.media = QStringLiteral("/x/xp.iso");
        QVERIFY(s.save());
        QFile f(paths::statePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject saved = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(saved[QStringLiteral("fromTheFuture")].toString(), QStringLiteral("keep me"));
        QCOMPARE(saved[QStringLiteral("media")].toString(), QStringLiteral("/x/xp.iso"));
    }

    void stateCorrupt()
    {
        writeFile(paths::statePath(), "{ not json");
        QCOMPARE(State::load().appliedKind, QString());
    }

    void adoption()
    {
        writeFile(paths::legacyData() + QLatin1String("/assets/themes/Luna/luna.msstyles"), "MZ");
        writeFile(paths::legacyData() + QLatin1String("/schemes/user/mine.json"), "{}");
        writeFile(paths::legacyCache() + QLatin1String("/downloads/small.iso"), "x");
        writeFile(paths::legacyCache() + QLatin1String("/downloads/xp.iso"), QByteArray(4096, 'x'));
        // Something XPlasma already has isn't replaced.
        writeFile(paths::schemes() + QLatin1String("/keep.theme"), "[x]");

        const QStringList log = adoptLegacy();
        QVERIFY(QFileInfo::exists(paths::themes() + QLatin1String("/Luna/luna.msstyles")));
        // ...with a link left where it was, for what still points there.
        QVERIFY(QFileInfo(paths::legacyData() + QLatin1String("/assets")).isSymLink());
        QVERIFY(QFileInfo::exists(paths::legacyData() + QLatin1String("/assets/themes/Luna/luna.msstyles")));
        QVERIFY(QFileInfo::exists(paths::legacyData() + QLatin1String("/schemes/user/mine.json")));
        QVERIFY(QFileInfo::exists(paths::schemes() + QLatin1String("/keep.theme")));
        QVERIFY(QFileInfo::exists(paths::downloads() + QLatin1String("/xp.iso")));
        QCOMPARE(State::load().media, paths::downloads() + QLatin1String("/xp.iso"));   // the largest
        QVERIFY(!log.isEmpty());

        // Once is enough.
        QVERIFY(adoptLegacy().isEmpty());
    }
};

int main(int argc, char **argv)
{
    // (Fonts need a GUI application; an offscreen one shows nothing.)
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // (XPlasma's style draws the Plasma theme's controls)
    // (Loaded now, while HOME is the real one: the XP fonts live there.)
    QFontDatabase::families();
    TestApp test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_app.moc"
