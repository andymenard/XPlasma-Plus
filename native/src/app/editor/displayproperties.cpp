// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor/displayproperties.h"

#include "app/editor/abouttab.h"
#include "app/editor/wallpapercatalogue.h"
#include "app/editor/advanced.h"
#include "app/editor/extras.h"
#include "app/editor/customluna.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "app/editor/jobs.h"
#include "app/editor/setupdialog.h"
#include "app/editor/soundstab.h"
#include "app/paths.h"
#include "app/darkmode.h"
#include "app/crisptext.h"
#include "app/session.h"
#include "app/state.h"
#include "app/startmenu.h"
#include "app/themeimport.h"
#include "core/themefile.h"
#include "core/schema.h"

#include <QApplication>
#include "app/editor/cachepreferences.h"
#include "app/maintenance.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QProcess>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace xpl::editor {

namespace {

// Theme combo rows that aren't themes.
constexpr const char *kBrowse = "browse";
constexpr const char *kSetup = "setup";

// XP's Position drop-down, as Plasma FillModes.
const std::pair<const char *, int> kPositions[] = {{"Center", 6}, {"Tile", 3}, {"Stretch", 0}, {"Fill", 2}};

// The desktop the session shows now (Plasma's first image or colour
// wallpaper), for the Desktop tab to open on.
Wallpaper currentDesktop()
{
    Wallpaper w{QString(), 2, QColor(58, 110, 165)};
    QFile f(paths::config() + QLatin1String("/plasma-org.kde.plasma.desktop-appletsrc"));
    if (!f.open(QIODevice::ReadOnly))
        return w;
    QString group;
    for (const QString &line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1Char('['))) {
            group = line.trimmed();
            continue;
        }
        const bool image = group.startsWith(QLatin1String("[Containments]")) && group.endsWith(QLatin1String("[Wallpaper][org.kde.image][General]"));
        const bool colour = group.startsWith(QLatin1String("[Containments]")) && group.endsWith(QLatin1String("[Wallpaper][org.kde.color][General]"));
        if (!image && !colour)
            continue;
        const QString key = line.section(QLatin1Char('='), 0, 0), value = line.section(QLatin1Char('='), 1).trimmed();
        if (image && key == QLatin1String("Image") && w.image.isEmpty())
            w.image = value.startsWith(QLatin1String("file:")) ? QUrl(value).toLocalFile() : value;
        else if (image && key == QLatin1String("FillMode"))
            w.fill = value.toInt();
        else if (key == QLatin1String("Color"))
            w.colour = QColor(value.startsWith(QLatin1Char('#')) ? value : QStringLiteral("#3a6ea5"));
    }
    if (!QFileInfo(w.image).isFile())
        w.image.clear();
    return w;
}

// What a look draws from: the style and variant, or the Classic scheme's
// contents (which Advanced edits).
QString lookKey(const Look &look)
{
    if (look.classic()) {
        ThemeFile file;
        look.system.writeTo(file);
        return QStringLiteral("classic:") + QString::fromLatin1(QCryptographicHash::hash(file.toBytes(), QCryptographicHash::Sha1).toHex());
    }
    return QFileInfo(look.stylePath).canonicalFilePath() + QLatin1Char(':') + look.variant.toUpper();
}

bool sameDesktop(const Wallpaper &a, const Wallpaper &b)
{
    return a.image == b.image && a.fill == b.fill && a.colour == b.colour;
}

bool shiftHeld()
{
    return QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier);
}

QLabel *sampleBox(QSize size)
{
    auto *label = new QLabel;
    label->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    label->setFixedSize(size + QSize(6, 6));
    label->setAlignment(Qt::AlignCenter);
    return label;
}

// Built-in Start logos in place of XP's flag: the editor's own copy
// written out where the Start button's picture is read from (the session
// redraws it from this file on every Apply, so it can't stay a resource).
QString embeddedStartLogo(const QString &name)
{
    const QString file = paths::data() + QLatin1String("/logos/") + name;
    QFile resource(QStringLiteral(":/logos/") + name);
    if (!resource.open(QIODevice::ReadOnly))
        return file;
    const QByteArray bytes = resource.readAll();
    QFile existing(file);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == bytes)
        return file;
    existing.close();
    QDir().mkpath(QFileInfo(file).path());
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly) && out.write(bytes) >= 0)
        out.commit();
    return file;
}

} // namespace

DisplayProperties::DisplayProperties(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("XPlasma Plus Properties"));
    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget;
    m_tabs->addTab(themesTab(), QStringLiteral("Themes"));
    m_tabs->addTab(desktopTab(), QStringLiteral("Desktop"));
    m_tabs->addTab(appearanceTab(), QStringLiteral("Appearance"));
    m_tabs->addTab(customLunaTab(), QStringLiteral("Custom Colors"));
    m_tabs->addTab(pointersTab(), QStringLiteral("Pointers"));
    auto *loginPage = new QWidget;
    auto *loginLayout = new QVBoxLayout(loginPage);
    auto *loginHelp = new QLabel(QStringLiteral("Adjust the SDDM Welcome login screen for all accounts.\nChanges take effect the next time the login screen starts.\nThe Plasma session lock screen has separate settings."));
    loginHelp->setWordWrap(true);
    loginLayout->addWidget(loginHelp);
    auto *loginScale = new QSpinBox;
    loginScale->setRange(50, 300);
    loginScale->setSingleStep(25);
    loginScale->setSuffix(QStringLiteral("%"));
    QSettings loginConfig(QStringLiteral("/etc/sddm.conf.d/90-xp-welcome.conf"), QSettings::IniFormat);
    const QString greeterEnvironment = loginConfig.value(QStringLiteral("General/GreeterEnvironment")).toStringList().join(QLatin1Char(','));
    int initialScale = 100;
    for (const QString &entry : greeterEnvironment.split(QLatin1Char(','))) {
        if (entry.startsWith(QLatin1String("QT_SCALE_FACTOR="))) {
            bool valid = false;
            const double factor = entry.mid(16).toDouble(&valid);
            if (valid) initialScale = qRound(factor * 100);
        }
    }
    loginScale->setValue(initialScale);
    loginLayout->addWidget(new QLabel(QStringLiteral("Login screen scale (100% uses the system default):")));
    loginLayout->addWidget(loginScale);
    auto *loginKeyboard = new QCheckBox(QStringLiteral("Enable onscreen keyboard"));
    loginKeyboard->setChecked(loginConfig.value(QStringLiteral("General/InputMethod")).toString() == QLatin1String("qtvirtualkeyboard"));
    loginLayout->addWidget(loginKeyboard);
    auto *loginSave = new QPushButton(QStringLiteral("Apply Login Screen Settings…"));
    auto *loginStatus = new QLabel(QStringLiteral("Administrator authentication is required."));
    loginStatus->setWordWrap(true);
    loginLayout->addWidget(loginSave);
    loginLayout->addWidget(loginStatus);
    loginLayout->addStretch();
    connect(loginSave, &QPushButton::clicked, this, [this, loginScale, loginKeyboard, loginSave, loginStatus] {
        const QString helper = QDir::homePath() + QStringLiteral("/.local/share/kde-xp-suite/installer/login_settings.py");
        if (!QFileInfo::exists(helper)) {
            loginStatus->setText(QStringLiteral("Login settings helper is missing. Reinstall XPlasma Plus."));
            return;
        }
        auto *process = new QProcess(this);
        loginSave->setEnabled(false);
        loginStatus->setText(QStringLiteral("Waiting for administrator authentication…"));
        connect(process, &QProcess::errorOccurred, this, [loginSave, loginStatus, process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                loginSave->setEnabled(true);
                loginStatus->setText(QStringLiteral("Could not start authentication: ") + process->errorString());
                process->deleteLater();
            }
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [loginSave, loginStatus, process](int code, QProcess::ExitStatus status) {
            loginSave->setEnabled(true);
            loginStatus->setText(code == 0 && status == QProcess::NormalExit
                ? QStringLiteral("Saved. The next login screen will use these settings.")
                : QStringLiteral("Settings were not saved. ") + QString::fromUtf8(process->readAllStandardError()).trimmed());
            process->deleteLater();
        });
        process->start(QStringLiteral("pkexec"), {QStringLiteral("/usr/bin/python3"), helper,
                       QStringLiteral("--scale"), QString::number(loginScale->value()),
                       QStringLiteral("--keyboard"), loginKeyboard->isChecked() ? QStringLiteral("on") : QStringLiteral("off")});
    });
    m_tabs->addTab(loginPage, QStringLiteral("Login Screen"));
    m_sounds = new SoundsTab;
    m_tabs->addTab(m_sounds, QStringLiteral("Sounds"));
    connect(m_sounds, &SoundsTab::changed, this, &DisplayProperties::updateApply);
    auto *about = new AboutTab;
    m_tabs->addTab(about, QStringLiteral("About"));
    // The dialog's width: its widest tab's but About's (whose paragraphs
    // wrap to it), and room for every tab's name.
    int pageWidth = 0;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->widget(i) != about)
            pageWidth = qMax(pageWidth, m_tabs->widget(i)->sizeHint().width());
    const int frame = m_tabs->sizeHint().width() - m_tabs->currentWidget()->sizeHint().width();
    pageWidth = qMax(pageWidth, m_tabs->tabBar()->sizeHint().width() + 4 - frame);
    for (int i = 0; i < m_tabs->count(); ++i)
        m_tabs->widget(i)->setMinimumWidth(pageWidth);
    about->fitWidth(pageWidth);
    root->addWidget(m_tabs, 1);
    // A property sheet: its size is its contents', not the user's to change.
    root->setSizeConstraint(QLayout::SetFixedSize);

    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(QStringLiteral("Cancel"));
    m_apply = new QPushButton(QStringLiteral("Apply"));
    m_apply->setToolTip(QStringLiteral("Hold Shift to rebuild and apply everything, changed or not."));
    ok->setDefault(true);
    for (QPushButton *b : {ok, cancel, m_apply})
        row->addWidget(b);
    root->addLayout(row);
    connect(ok, &QPushButton::clicked, this, [this] {
        if (apply())
            accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, &DisplayProperties::apply);
    // (Shift, wherever the focus is in the dialog.)
    qApp->installEventFilter(this);

    loadLibrary();
    // Open on what's applied now.
    const State state = State::load();
    m_loading = true;
    m_appliedThemeCache = state.settings.value(QStringLiteral("themeCacheEnabled")).toBool();
    m_themeCache->setChecked(m_appliedThemeCache);
    m_appliedDark = state.settings.value(QStringLiteral("darkMode")).toBool();
    m_darkMode->setChecked(m_appliedDark);
    m_appliedCrisp = state.settings.value(QStringLiteral("crispText")).toBool();
    m_crispText->setChecked(m_appliedCrisp);
    m_appliedHiDpiStart = state.settings.value(QLatin1String(kHiDpiStartKey)).toBool();
    m_hidpiStart->setChecked(m_appliedHiDpiStart);
    if (state.appliedKind == QLatin1String("classic")) {
        if (const auto s = schemes::load(state.scheme))
            m_scheme = *s;
    }
    fillStyles();
    if (const int i = state.appliedKind == QLatin1String("style") ? styleFor(state.style) : -1; i >= 0) {
        const StyleVariant *v = m_styles.at(i).variant(state.variant);
        selectStyle(i, v ? v->colour : QString());
        fillSizes();
        if (v)
            m_size->setCurrentIndex(m_size->findData(v->size));
    } else {
        if (state.appliedKind == QLatin1String("classic"))
            m_style->setCurrentIndex(m_styleRows.size());
        fillColours();
    }
    fillSizes();
    const Wallpaper desktop = currentDesktop();
    fillWallpapers();
    for (int i = 0; i < m_wallpapers->count(); ++i)
        if (m_wallpapers->item(i)->data(Qt::UserRole).toString() == desktop.image)
            m_wallpapers->setCurrentRow(i);
    if (m_wallpapers->currentRow() < 0 && !desktop.image.isEmpty()) {
        auto *item = new QListWidgetItem(QFileInfo(desktop.image).completeBaseName());
        item->setData(Qt::UserRole, desktop.image);
        m_wallpapers->addItem(item);
        m_wallpapers->setCurrentItem(item);
    }
    if (m_wallpapers->currentRow() < 0)
        m_wallpapers->setCurrentRow(0);
    for (int i = 0; i < m_position->count(); ++i)
        if (m_position->itemData(i).toInt() == desktop.fill.value_or(2))
            m_position->setCurrentIndex(i);
    m_desktopColour->setColour(desktop.colour);
    m_themeFile = state.themeFile;
    for (int i = 0; i < m_themes->count(); ++i)
        if (m_themes->itemData(i).toString() == state.themeFile)
            m_themes->setCurrentIndex(i);
    if(m_themes->findData(state.themeFile)<0 && !state.style.isEmpty()){
        for(const auto &theme:m_library.themes)if(theme.path==state.themeFile && QFileInfo(theme.path).dir().dirName()==QLatin1String("XP Presets")){
            const int xp=m_themes->findText(QStringLiteral("Windows XP"));if(xp>=0)m_themes->setCurrentIndex(xp);
        }
    }
    restoreCustomLuna();
    m_appliedLiveStart=state.settings.value(QLatin1String(kLiveStartColorsKey)).toObject();
    if(m_appliedLiveStart.value(QStringLiteral("enabled")).toBool() && !m_appliedLiveStart.contains(QStringLiteral("startMenuText")))
        m_appliedLiveStart.insert(QStringLiteral("startMenuText"),m_appliedLiveStart.value(QStringLiteral("menuText")).toInt());
    bool linked=state.settings.value(QStringLiteral("linkTitleToTaskbar")).toBool(true);
    if(!state.settings.contains(QStringLiteral("linkTitleToTaskbar"))) {
        if(m_appliedLiveStart.contains(QStringLiteral("linkTitleToTaskbar")))linked=m_appliedLiveStart.value(QStringLiteral("linkTitleToTaskbar")).toBool();
        else if(m_appliedLiveStart.contains(QStringLiteral("title")))linked=m_appliedLiveStart.value(QStringLiteral("title"))==m_appliedLiveStart.value(QStringLiteral("menu"));
        else if(m_customLunaEnabled->isChecked())linked=m_lunaColours[1]->colour()==m_lunaColours[2]->colour();
    }
    m_linkTitleColor->setChecked(linked);m_appliedTitleLink=linked;
    const auto rememberedColors=m_appliedLiveStart.isEmpty()?(m_customLunaEnabled->isChecked()?QJsonObject():state.settings.value(QStringLiteral("retiredColorProfile")).toObject()):m_appliedLiveStart;
    m_liveStartEnabled->setChecked(false);
    if(rememberedColors.value("enabled").toBool()) {
        m_lunaColours[0]->setColour(QColor(rememberedColors.value(QStringLiteral("start")).toString()));
        m_lunaColours[1]->setColour(QColor(rememberedColors.value(QStringLiteral("menu")).toString()));
        m_customTextModes[0]->setCurrentIndex(rememberedColors.value(QStringLiteral("startText")).toInt());
        m_customTextModes[1]->setCurrentIndex(rememberedColors.value(QStringLiteral("menuText")).toInt());
        m_startMenuTextMode->setCurrentIndex(rememberedColors.value(QStringLiteral("startMenuText")).toInt(rememberedColors.value(QStringLiteral("menuText")).toInt()));
        if(rememberedColors.contains(QStringLiteral("title")))m_lunaColours[2]->setColour(QColor(rememberedColors.value(QStringLiteral("title")).toString()));
        if(rememberedColors.contains(QStringLiteral("buttons")))m_lunaColours[3]->setColour(QColor(rememberedColors.value(QStringLiteral("buttons")).toString()));
        if(rememberedColors.contains(QStringLiteral("titleText")))m_customTextModes[2]->setCurrentIndex(rememberedColors.value(QStringLiteral("titleText")).toInt());
        if(rememberedColors.contains(QStringLiteral("buttonsText")))m_customTextModes[3]->setCurrentIndex(rememberedColors.value(QStringLiteral("buttonsText")).toInt());
    }
    syncTitleColorLink();
    connect(this,&QDialog::rejected,this,[this]{
        m_liveStartTimer->stop();
        if(m_livePreviewTouched){LiveSession session;applyLiveStartColors(session,m_appliedLiveStart);}
    });
    m_loading = false;
    // What's applied: the look XPlasma recorded, if the dialog could show it,
    // and the desktop as the dialog shows it.
    QString applied;
    if (state.appliedKind == QLatin1String("style"))
        applied = QFileInfo(state.style).canonicalFilePath() + QLatin1Char(':') + state.variant.toUpper();
    else if (state.appliedKind == QLatin1String("classic"))
        if (const auto s = schemes::load(state.scheme))
            applied = lookKey(Look::fromScheme(*s));
    // (made by an older XPlasma: not what this one would make, so Apply
    // stays ready until it's rebuilt)
    m_outdated = lookOutdated(state);
    m_appliedLook = applied == lookKey(look()) && !m_outdated ? applied : QString();
    m_appliedDesktop = wallpaper();
    m_appliedThemeFile = state.themeFile;
    // Pointers: what XPlasma applied, or "(None)" (Plasma's own).
    m_pointerSchemes = pointers::schemes();
    m_appliedPointers = pointers::applied(state);
    if (m_appliedPointers) {
        m_pointers = m_appliedPointers->first;
        m_pointerShadow = m_appliedPointers->second;
    }
    fillPointerSchemes();
    showPointers();
    m_appliedStartExtras = m_startExtras = StartButtonExtras::fromJson(state.settings.value(QLatin1String(kStartButtonKey)).toObject());
    showStartLogos();
    // First start -- nothing of XPlasma's on the desktop yet: the "Windows
    // XP" theme chosen, as if picked (Luna, Bliss...), with XP's own
    // pointers, and Apply ready -- so Apply alone gives XP's desktop.
    if (state.appliedKind.isEmpty() && !m_appliedPointers) {
        if (const int xp = m_themes->findText(QStringLiteral("Windows XP")); xp >= 0) {
            m_themes->setCurrentIndex(xp);
            themePicked(xp);
        }
        if (!m_pointerSchemes.isEmpty()) {
            m_pointers = pointers::defaultScheme();
            m_pointerShadow = true;
            fillPointerSchemes();
            showPointers();
        }
    }
    updateApply();
    if (!m_previewer.styleAvailable())
        m_appearanceSample->setToolTip(QStringLiteral("XPlasma's style plugin isn't installed: the samples can't be drawn"));
    renderThemes();
    renderDesktop();
    renderAppearance();
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_tabs->tabText(index) == QLatin1String("Custom Colors")) syncCustomColors();
        renderThemes();renderAppearance();
    });
    if (!m_customLunaEnabled->isChecked()) syncCustomColors();
}

// --- tabs ----------------------------------------------------------------------------------

QWidget *DisplayProperties::themesTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *blurb = new QLabel(QStringLiteral("A theme is a background plus a set of sounds, icons, and other elements to help you "
                                            "personalize your computer with one click."));
    blurb->setWordWrap(true);
    layout->addWidget(blurb);
    layout->addWidget(new QLabel(QStringLiteral("Theme:")));
    auto *row = new QHBoxLayout;
    m_themes = new QComboBox;
    m_themes->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_themes->setMinimumContentsLength(1);
    row->addWidget(m_themes, 1);
    auto *saveAsButton = new QPushButton(QStringLiteral("Save As..."));
    m_delete = new QPushButton(QStringLiteral("Delete"));
    row->addWidget(saveAsButton);
    row->addWidget(m_delete);
    layout->addLayout(row);
    auto *installButton = new QPushButton(QStringLiteral("Install Theme..."));
    installButton->setObjectName(QStringLiteral("installCustomTheme"));
    auto *installMenu = new QMenu(installButton);
    installMenu->addAction(QStringLiteral("Theme file or archive..."), this, [this] { installTheme(false); });
    installMenu->addAction(QStringLiteral("Extracted theme folder..."), this, [this] { installTheme(true); });
    installButton->setMenu(installMenu);
    layout->addWidget(installButton, 0, Qt::AlignLeft);
    layout->addWidget(new QLabel(QStringLiteral("Sample:")));
    m_themeSample = sampleBox(preview::kThemesSize);
    layout->addWidget(m_themeSample, 0, Qt::AlignHCenter);
    layout->addStretch(1);
    connect(m_themes, &QComboBox::activated, this, &DisplayProperties::themePicked);
    connect(saveAsButton, &QPushButton::clicked, this, &DisplayProperties::saveAs);
    connect(m_delete, &QPushButton::clicked, this, &DisplayProperties::deleteTheme);
    return page;
}

QWidget *DisplayProperties::desktopTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    m_desktopSample = sampleBox(QSize(200, 170));
    m_desktopSample->setFrameStyle(QFrame::NoFrame);
    layout->addWidget(m_desktopSample, 0, Qt::AlignHCenter);
    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(QStringLiteral("Background:")), 0, 0);
    m_wallpapers = new QListWidget;
    m_wallpapers->setFixedHeight(122);
    grid->addWidget(m_wallpapers, 1, 0);
    auto *column = new QVBoxLayout;
    auto *browse = new QPushButton(QStringLiteral("Browse..."));
    column->addWidget(browse);
    column->addSpacing(8);
    column->addWidget(new QLabel(QStringLiteral("Position:")));
    m_position = new QComboBox;
    for (const auto &[name, fill] : kPositions)
        m_position->addItem(QLatin1String(name), fill);
    column->addWidget(m_position);
    column->addSpacing(8);
    column->addWidget(new QLabel(QStringLiteral("Color:")));
    m_desktopColour = new ColourButton;
    column->addWidget(m_desktopColour);
    column->addStretch(1);
    grid->addLayout(column, 1, 1);
    grid->setColumnStretch(0, 1);
    layout->addLayout(grid);
    layout->addStretch(1);
    connect(m_wallpapers, &QListWidget::currentRowChanged, this, &DisplayProperties::desktopChanged);
    connect(m_position, &QComboBox::activated, this, &DisplayProperties::desktopChanged);
    connect(m_desktopColour, &ColourButton::picked, this, &DisplayProperties::desktopChanged);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Browse"), paths::wallpapers(),
                                                          QStringLiteral("Pictures (*.jpg *.jpeg *.png *.bmp *.gif)"));
        if (file.isEmpty())
            return;
        auto *item = new QListWidgetItem(QFileInfo(file).completeBaseName());
        item->setData(Qt::UserRole, file);
        m_wallpapers->addItem(item);
        m_wallpapers->setCurrentItem(item);
    });
    return page;
}

QWidget *DisplayProperties::customLunaTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    m_customLunaSample = sampleBox(preview::kThemesSize);
    layout->addWidget(m_customLunaSample, 0, Qt::AlignHCenter);
    m_customBaseLabel = new QLabel;
    m_customBaseLabel->setWordWrap(true); layout->addWidget(m_customBaseLabel);
    m_liveStartEnabled = new QCheckBox(QStringLiteral("Live theme colors (experimental)"));
    m_liveStartEnabled->setObjectName(QStringLiteral("liveStartColors"));
    m_liveStartEnabled->hide(); // Retired: colors are baked into the generated theme.
    m_liveStartTimer=new QTimer(this);m_liveStartTimer->setSingleShot(true);m_liveStartTimer->setInterval(60);
    connect(m_liveStartTimer,&QTimer::timeout,this,[this]{
        QString error;LiveSession session;
        if(applyLiveStartColors(session,liveStartColors(),&error)) {
            m_livePreviewTouched=true;
            m_customLunaStatus->setText(QStringLiteral("Theme colors update live. Apply saves; Cancel restores. Close stays red."));
        } else m_customLunaStatus->setText(error);
        renderThemes();renderAppearance();
    });
    m_customLunaEnabled = new QCheckBox(QStringLiteral("Use custom theme colors"));
    layout->addWidget(m_customLunaEnabled);
    auto *testingNote = new QLabel(QStringLiteral("Luna is the only theme extensively tested with Custom Colors. Other themes may work, but support is experimental."));
    testingNote->setObjectName(QStringLiteral("customColorsTestingNote"));
    testingNote->setWordWrap(true);
    layout->addWidget(testingNote);
    auto *grid = new QGridLayout;
    const char *labels[] = {"Start button:", "Taskbar / Start menu:", "Titlebars and borders:", "Titlebar buttons (Close stays red):"};
    const char *defaults[] = {"#3c9b35", "#245edb", "#245edb", "#245edb"};
    for (int i = 0; i < 4; ++i) {
        m_lunaColours[i] = new ColourButton;
        m_lunaColours[i]->setObjectName(QStringLiteral("customColor%1").arg(i));
        m_lunaColours[i]->setColour(QColor(QLatin1String(defaults[i])));
        auto *label = m_customColorLabels[i] = new QLabel(QLatin1String(labels[i]));
        label->setBuddy(m_lunaColours[i]);
        grid->addWidget(label, i, 0); grid->addWidget(m_lunaColours[i], i, 1);
        connect(m_lunaColours[i], &ColourButton::picked, this, [this] {
            if(m_liveStartEnabled->isChecked()){liveStartColorsChanged();return;}
            { const QSignalBlocker block(m_customLunaEnabled); m_customLunaEnabled->setChecked(true); } scheduleCustomColors();
        });
    }
    for(int i=0;i<4;++i) {
        auto *mode=m_customTextModes[i]=new QComboBox;
        mode->setObjectName(QStringLiteral("customTextMode%1").arg(i));
        mode->addItems({QStringLiteral("Auto"),QStringLiteral("White"),QStringLiteral("Black")});
        mode->setToolTip(i==3 ? QStringLiteral("Titlebar button symbols. Close keeps its red background.") : QStringLiteral("Text color for this part of the theme."));
        mode->setAccessibleName(QString::fromLatin1(labels[i])+QStringLiteral(" text color"));
        grid->addWidget(mode,i,2);
        connect(mode,&QComboBox::activated,this,[this]{if(m_liveStartEnabled->isChecked()){liveStartColorsChanged();return;}{ const QSignalBlocker block(m_customLunaEnabled); m_customLunaEnabled->setChecked(true); } scheduleCustomColors();});
    }
    m_startMenuTextMode=new QComboBox;
    m_startMenuTextMode->setObjectName(QStringLiteral("startMenuTextMode"));
    m_startMenuTextMode->addItems({QStringLiteral("Auto"),QStringLiteral("White"),QStringLiteral("Black")});
    m_startMenuTextMode->setAccessibleName(QStringLiteral("Start menu text color"));
    m_startMenuTextMode->setToolTip(QStringLiteral("Start menu and flyout text, independent of taskbar text. Applied with the generated theme."));
    m_startMenuTextMode->setEnabled(true);
    grid->addWidget(new QLabel(QStringLiteral("Start menu text:")),4,0);
    grid->addWidget(m_startMenuTextMode,4,2);
    connect(m_startMenuTextMode,&QComboBox::activated,this,[this]{liveStartColorsChanged();});
    grid->setColumnStretch(0, 1); layout->addLayout(grid);
    m_linkTitleColor=new QCheckBox(QStringLiteral("Use taskbar color for titlebars"));
    m_linkTitleColor->setObjectName(QStringLiteral("linkTitleToTaskbar"));
    m_linkTitleColor->setChecked(true);layout->addWidget(m_linkTitleColor);
    m_lunaColours[2]->setEnabled(false);
    connect(m_linkTitleColor,&QCheckBox::toggled,this,[this]{
        if(m_loading){m_lunaColours[2]->setEnabled(!m_linkTitleColor->isChecked());return;}
        syncTitleColorLink();
        if(m_liveStartEnabled->isChecked())liveStartColorsChanged();
        else if(m_customLunaEnabled->isChecked())scheduleCustomColors();
        else updateApply();
    });
    auto *reset = new QPushButton(QStringLiteral("Reset to original theme"));
    layout->addWidget(reset);
    m_customLunaStatus = new QLabel(QStringLiteral("Choose colors, then Apply. Start menu and highlights follow the taskbar color."));
    m_customLunaStatus->setWordWrap(true); layout->addWidget(m_customLunaStatus);
    layout->addStretch();
    connect(m_liveStartEnabled,&QCheckBox::toggled,this,[this](bool enabled){
        for(int i=0;i<4;++i) {
            if(enabled){m_beforeLiveColors[i]=m_lunaColours[i]->colour();m_beforeLiveTextModes[i]=m_customTextModes[i]->currentIndex();}
            else if(m_beforeLiveColors[i].isValid()){m_lunaColours[i]->setColour(m_beforeLiveColors[i]);m_customTextModes[i]->setCurrentIndex(m_beforeLiveTextModes[i]);}
        }
        m_customLunaEnabled->setEnabled(!enabled);
        m_startMenuTextMode->setEnabled(true);
        m_customColorLabels[1]->setText(QStringLiteral("Taskbar / Start menu:"));
        m_customTextModes[1]->setToolTip(enabled ? QStringLiteral("Taskbar text only. Start menu text has its own selector below.") : QStringLiteral("Taskbar and Start menu text."));
        m_customLunaSample->setToolTip(enabled ? QStringLiteral("Preview and desktop follow the selected live colors.") : QString());
        for(int i=0;i<4;++i){m_lunaColours[i]->setEnabled(true);m_customTextModes[i]->setEnabled(true);m_lunaColours[i]->livePreview=enabled;}
        syncTitleColorLink();
        if(!m_loading)liveStartColorsChanged();
    });
    connect(m_customLunaEnabled, &QCheckBox::toggled, this, [this] { scheduleCustomColors(); });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_liveStartEnabled->setChecked(false);
        m_startMenuTextMode->setCurrentIndex(0);
        m_linkTitleColor->setChecked(true);
        const char *defaults[] = {"#3c9b35", "#245edb", "#245edb", "#245edb"};
        for (int i = 0; i < 4; ++i) m_lunaColours[i]->setColour(QColor(QLatin1String(defaults[i])));
        { const QSignalBlocker block(m_customLunaEnabled); m_customLunaEnabled->setChecked(false); } scheduleCustomColors();
    });
    return page;
}

void DisplayProperties::syncTitleColorLink()
{
    m_lunaColours[2]->setEnabled(!m_linkTitleColor->isChecked());
    if(m_linkTitleColor->isChecked())m_lunaColours[2]->setColour(m_lunaColours[1]->colour());
}

QJsonObject DisplayProperties::liveStartColors() const
{
    return {}; // Runtime tinting is retired; previews use the generated style.
}

void DisplayProperties::liveStartColorsChanged()
{
    if(m_loading)return;
    syncTitleColorLink();
    { const QSignalBlocker block(m_customLunaEnabled); m_customLunaEnabled->setChecked(true); }
    scheduleCustomColors();
    updateApply();
}

void DisplayProperties::syncCustomColors()
{
    if (m_loading || m_customLunaEnabled->isChecked()) return;
    if(m_liveStartEnabled->isChecked()){
        m_customBaseLabel->setText(QStringLiteral("Base: ")+look().name+QStringLiteral(". Choose another base on Appearance."));
        return; // Switching tabs must not replace live draft colors with base defaults.
    }
    const Look base = look();
    std::array<QColor,4> colours{base.system.colour(u"ButtonFace"), base.system.colour(u"ButtonFace"),
                               base.system.colour(u"ActiveTitle"), base.system.colour(u"ButtonFace")};
    if (base.renderer) {
        const PartRef refs[] = {{QStringLiteral("button"), 1, 1, QStringLiteral("start")},
            {QStringLiteral("taskbar"), schema::partId(u"taskbar", u"backgroundbottom"), 0, {}},
            {QStringLiteral("window"), schema::partId(u"window", u"caption"), 1, {}},
            {QStringLiteral("window"), schema::partId(u"window", u"maxbutton"), 1, {}}};
        for (int i = 0; i < 4; ++i) {
            const auto image = base.renderer->background(refs[i], QSize(100,30));
            qint64 red=0,green=0,blue=0,count=0;
            for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x) {
                const QRgb c=image.pixel(x,y);
                if (!qAlpha(c) || c==qRgb(255,0,255)) continue;
                red+=qRed(c);green+=qGreen(c);blue+=qBlue(c);++count;
            }
            if (count) colours[i]=QColor(red/count,green/count,blue/count);
        }
    }
    for (int i=0;i<4;++i) m_lunaColours[i]->setColour(colours[i]);
    syncTitleColorLink();
    m_customBaseLabel->setText(QStringLiteral("Base: ") + base.name + QStringLiteral(". Choose another base on Appearance."));
    for(auto *mode:m_customTextModes) mode->setCurrentIndex(0);
    m_customLunaSource.clear(); m_customLunaVariant.clear();
}

void DisplayProperties::restoreCustomLuna(const QString &path)
{
    if (m_scheme.shellColors && path.isEmpty()) {
        m_customClassic = m_scheme;
        m_startMenuTextMode->setCurrentIndex(m_scheme.shellMenuTextMode<0 ? (m_scheme.shellTextModes[1]<0 ? m_scheme.shellTextMode : m_scheme.shellTextModes[1]) : m_scheme.shellMenuTextMode);
        for(int i=0;i<4;++i) m_customTextModes[i]->setCurrentIndex(m_scheme.shellTextModes[i]<0 ? m_scheme.shellTextMode : m_scheme.shellTextModes[i]);
        for (int i = 0; i < 4; ++i) m_lunaColours[i]->setColour((*m_scheme.shellColors)[i]);
        m_customLunaSource = QStringLiteral("classic:") + m_scheme.shellBaseName;
        m_customLunaPath = QStringLiteral("classic");
        m_customLunaEnabled->setChecked(true);
        return;
    }
    const QString applied = path.isEmpty() ? State::load().style : path;
    const auto obj = colorMetadata(applied);
    if (!obj.isEmpty()) {
        const auto colours = obj.value(QStringLiteral("colours")).toArray();
        if (colours.size() == 4) {
            m_startMenuTextMode->setCurrentIndex(obj.value("startMenuText").toInt(obj.value("textMode").toInt()));
            const auto modes=obj.value(QStringLiteral("textModes")).toArray();
            for(int i=0;i<4;++i) m_customTextModes[i]->setCurrentIndex(qBound(0,modes.size()==4 ? modes[i].toInt() : obj.value(QStringLiteral("textMode")).toInt(),2));
            m_customLunaSource = obj.value(QStringLiteral("source")).toString();
            m_customLunaVariant = obj.value(QStringLiteral("variant")).toString(QStringLiteral("NORMALBLUE_INI"));
            for (int i = 0; i < 4; ++i) m_lunaColours[i]->setColour(QColor(colours[i].toString()));
            m_customLunaPath = applied;
            m_customLunaEnabled->setChecked(true);
        }
    }
}

void DisplayProperties::scheduleCustomColors()
{
    if(m_loading || m_customColorsPending)return;
    m_customColorsPending=true;
    QTimer::singleShot(0,this,[this]{if(m_customColorsPending)customLunaChanged();});
}

// Unsaved custom colors are pruned once another look is applied; this
// window's, if it has some, are built again (the same file) when needed.
bool DisplayProperties::customLunaPruned() const
{
    return m_customLunaEnabled->isChecked() && !m_customClassic && !m_customLunaPath.isEmpty()
        && !QFileInfo::exists(m_customLunaPath);
}

void DisplayProperties::customLunaChanged()
{
    m_customColorsPending=false;
    if (m_loading) return;
    syncTitleColorLink();
    if (m_customLunaSource.isEmpty()) {
        const int index = styleIndex();
        if (index >= 0) {
            const auto &style = m_styles[index];
            m_customLunaSource = style.path;
            m_customLunaVariant = style.ini(colourKey(), m_size->currentData().toString());
        } else m_customLunaSource = QStringLiteral("classic:") + m_scheme.name;
    }
    if (m_customLunaEnabled->isChecked()) {
        m_customBaseLabel->setText(QStringLiteral("Base: ") + (m_customLunaSource.startsWith(QLatin1String("classic:"))
            ? m_customLunaSource.mid(8) : Library::style(m_customLunaSource).label) + QStringLiteral(". Choose another base on Appearance."));
        QString error;
        std::array<QColor, 4> colours;
        std::array<int,4> textModes;for(int i=0;i<4;++i) textModes[i]=m_customTextModes[i]->currentIndex();
        for (int i = 0; i < 4; ++i) colours[i] = m_lunaColours[i]->colour();
        if (m_customLunaSource.startsWith(QLatin1String("classic:"))) {
            ClassicScheme base = schemes::load(m_customLunaSource.mid(8)).value_or(m_scheme);
            base.shellBaseName = m_customLunaSource.mid(8); base.shellColors = colours; base.shellTextModes = textModes; base.shellMenuTextMode=m_startMenuTextMode->currentIndex();
            QByteArray paletteKey;
            for (const auto &c : colours) paletteKey += c.name().toUtf8();
            for(int mode : textModes) paletteKey += QByteArray::number(mode);
            paletteKey+=QByteArray::number(m_startMenuTextMode->currentIndex());
            base.name = QStringLiteral("Custom ") + base.shellBaseName + QLatin1Char(' ') + QString::fromLatin1(QCryptographicHash::hash(paletteKey, QCryptographicHash::Sha256).toHex().left(10));
            base.setColour(u"ActiveTitle", colours[2]); base.setColour(u"GradientActiveTitle", colours[2].lighter(140));
            base.setColour(u"InactiveTitle", colours[2].darker(130)); base.setColour(u"GradientInactiveTitle", colours[2].lighter(110));
            base.setColour(u"Hilight", colours[1]);
            const auto textFor=[&](int part) {return textModes[part]==1 ? QColor(Qt::white) : textModes[part]==2 ? QColor(Qt::black) : colours[part].lightnessF()<.5 ? QColor(Qt::white) : QColor(Qt::black);};
            base.setColour(u"HilightText",textFor(1));base.setColour(u"TitleText",textFor(2));base.setColour(u"InactiveTitleText",textFor(2));
            m_customClassic = base; m_customLunaPath = QStringLiteral("classic");
        } else {
            m_customClassic.reset();
            const auto source=m_customLunaSource, variant=m_customLunaVariant;
            const int menuMode=m_startMenuTextMode->currentIndex();
            QString generated;
            runResponsiveTask(this,[&](QString *e){generated=customLuna(source,colours,paths::data()+QStringLiteral("/custom-luna"),e,variant,0,textModes,menuMode);return !generated.isEmpty();},&error);
            m_customLunaPath=generated;
            if(!generated.isEmpty()) {
                const QString baseSource=colorMetadata(generated).value(QStringLiteral("source")).toString(source);
                m_customBaseLabel->setText(QStringLiteral("Base: ")+Library::style(baseSource).label+QStringLiteral(". Choose another base on Appearance."));
            }
        }
        m_customLunaStatus->setText(m_customLunaPath.isEmpty() ? error : QStringLiteral("Start menu and highlights follow the taskbar color. Apply to use these colors."));
    } else {
        m_customLunaPath.clear(); m_customClassic.reset();
        if (m_customLunaSource.startsWith(QLatin1String("classic:"))) {
            if (auto base = schemes::load(m_customLunaSource.mid(8))) m_scheme = *base;
            m_loading = true; m_style->setCurrentIndex(m_styleRows.size()); fillColours(); fillSizes(); m_loading = false;
        }
        // Restore the original selected skin, color scheme and font size.
        for (int i = 0; i < m_styles.size(); ++i)
            if (QFileInfo(m_styles[i].path).canonicalFilePath() == QFileInfo(m_customLunaSource).canonicalFilePath()) {
                m_loading = true;
                const auto *v = m_styles[i].variant(m_customLunaVariant);
                selectStyle(i, v ? v->colour : QString()); fillSizes();
                if (v) m_size->setCurrentIndex(m_size->findData(v->size));
                m_loading = false;
                break;
            }
        m_customLunaStatus->setText(QStringLiteral("Original theme colors restored. Apply to update your desktop."));
        syncCustomColors();
    }
    m_themeFile.clear();
    renderThemes(); renderAppearance(); updateApply();
}

QWidget *DisplayProperties::appearanceTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    m_appearanceSample = sampleBox(preview::kAppearanceSize);
    layout->addWidget(m_appearanceSample, 0, Qt::AlignHCenter);
    auto *grid = new QGridLayout;
    m_style = new QComboBox; m_style->setObjectName(QStringLiteral("appearanceStyle"));
    m_colour = new QComboBox; m_colour->setObjectName(QStringLiteral("appearanceColor"));
    m_size = new QComboBox; m_size->setObjectName(QStringLiteral("appearanceSize"));
    m_advanced = new QPushButton(QStringLiteral("Advanced"));
    // (Where XP had Effects...: XPlasma's own, the Start button's label and logo.)
    m_extras = new QPushButton(QStringLiteral("Extras..."));
    m_tux = new QCheckBox(QStringLiteral("&Tux Start logo"));
    m_tux->setToolTip(QStringLiteral("Tux the penguin on the Start button, in place of the Windows flag"));
    grid->addWidget(new QLabel(QStringLiteral("Windows and buttons:")), 0, 0);
    grid->addWidget(m_style, 1, 0);
    grid->addWidget(new QLabel(QStringLiteral("Color scheme:")), 2, 0);
    grid->addWidget(m_colour, 3, 0);
    grid->addWidget(new QLabel(QStringLiteral("Font size:")), 4, 0);
    grid->addWidget(m_size, 5, 0);
    grid->addWidget(m_tux, 1, 2);
    m_kdeLogo = new QCheckBox(QStringLiteral("&KDE Start logo"));
    m_kdeLogo->setToolTip(QStringLiteral("The classic KDE K and gear on the Start button"));
    grid->addWidget(m_kdeLogo, 2, 2);
    grid->addWidget(m_extras, 3, 2);
    grid->addWidget(m_advanced, 5, 2);
    grid->setColumnStretch(0, 5);
    grid->setColumnMinimumWidth(1, 40);
    layout->addLayout(grid);
    m_darkMode = new QCheckBox(QStringLiteral("&Dark application colors"));
    m_darkMode->setToolTip(QStringLiteral("Dark backgrounds and light text for applications and IE6 Firefox; keeps the selected theme\'s titlebars and taskbar"));
    layout->addWidget(m_darkMode);
    m_crispText = new QCheckBox(QStringLiteral("Crisp XP text (experimental)"));
    m_crispText->setToolTip(QStringLiteral("Experimental: XP-rendered pixel Tahoma for XPlasma widgets and IE6 Firefox, with a legacy pixel-font fallback. Aligns glyph sizes to physical pixels at fractional scaling; preserves explicit document and code font choices."));
    layout->addWidget(m_crispText);
    connect(m_crispText,&QCheckBox::toggled,this,[this]{updateApply();});
    m_hidpiStart = new QCheckBox(QStringLiteral("Sharp Start button on &HiDPI screens"));
    m_hidpiStart->setToolTip(QStringLiteral("Draws the Start button's label and logo at the screen's own resolution when the display is scaled; XP's button stays its own pixels, made bigger."));
    layout->addWidget(m_hidpiStart);
    m_themeCache = new QCheckBox(QStringLiteral("Cache themes for faster switching"));
    m_themeCache->setObjectName(QStringLiteral("themeCacheEnabled"));
    m_themeCache->setToolTip(QStringLiteral("Keep generated artwork from previously applied themes to save time when switching. Uses more disk space, within the cache limits. When off, unused artwork is removed; the current theme, saved themes and recovery data are kept."));
    layout->addWidget(m_themeCache);
    connect(m_themeCache, &QCheckBox::clicked, this, [this] { m_themeCacheChosen = true; updateApply(); });
    connect(m_hidpiStart,&QCheckBox::toggled,this,[this]{updateApply();});
    connect(m_darkMode, &QCheckBox::toggled, this, [this] { renderAppearance(); updateApply(); });
    layout->addStretch(1);
    connect(m_style, &QComboBox::activated, this, [this] {
        fillColours();
        fillSizes();
        appearanceChanged();
    });
    connect(m_colour, &QComboBox::activated, this, [this] {
        fillSizes();
        appearanceChanged();
    });
    connect(m_size, &QComboBox::activated, this, &DisplayProperties::appearanceChanged);
    connect(m_advanced, &QPushButton::clicked, this, &DisplayProperties::openAdvanced);
    connect(m_extras, &QPushButton::clicked, this, &DisplayProperties::openExtras);
    connect(m_tux, &QCheckBox::toggled, this, [this](bool on) {
        // (Off: XP's flag back -- unless Extras had chosen something else.)
        if (on) {
            m_startExtras.logoFile = embeddedStartLogo(QStringLiteral("tux.png"));
            m_startExtras.logoText.clear();
        } else {
            m_startExtras.logoFile.clear();
        }
        showStartLogos();
        renderThemes();
        updateApply();
    });
    connect(m_kdeLogo, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            m_startExtras.logoFile = embeddedStartLogo(QStringLiteral("kde-crystal.png"));
            m_startExtras.logoText.clear();
        } else {
            m_startExtras.logoFile.clear();
        }
        showStartLogos();
        renderThemes();
        updateApply();
    });
    return page;
}

QWidget *DisplayProperties::pointersTab()
{
    // XP's Mouse Properties > Pointers, the parts that mean something here.
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *top = new QHBoxLayout;
    auto *schemeBox = new QGroupBox(QStringLiteral("Scheme"));
    auto *schemeRow = new QVBoxLayout(schemeBox);
    m_pointerScheme = new QComboBox;
    schemeRow->addWidget(m_pointerScheme);
    top->addWidget(schemeBox, 1);
    m_pointerPreview = new QLabel;
    m_pointerPreview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_pointerPreview->setFixedSize(56, 56);
    m_pointerPreview->setAlignment(Qt::AlignCenter);
    m_pointerPreview->setAutoFillBackground(true);
    m_pointerPreview->setBackgroundRole(QPalette::Base);
    top->addWidget(m_pointerPreview, 0, Qt::AlignBottom);
    layout->addLayout(top);
    layout->addWidget(new QLabel(QStringLiteral("Customize:")));
    m_pointerList = new QListWidget;
    m_pointerList->setIconSize(QSize(32, 32));
    layout->addWidget(m_pointerList, 1);
    auto *row = new QHBoxLayout;
    m_pointerShadowBox = new QCheckBox(QStringLiteral("Enable pointer shadow"));
    m_pointerDefault = new QPushButton(QStringLiteral("Use Default"));
    m_pointerBrowse = new QPushButton(QStringLiteral("Browse..."));
    row->addWidget(m_pointerShadowBox);
    row->addStretch(1);
    row->addWidget(m_pointerDefault);
    row->addWidget(m_pointerBrowse);
    layout->addLayout(row);
    m_pointerTimer = new QTimer(this);
    m_pointerTimer->setSingleShot(true);
    connect(m_pointerTimer, &QTimer::timeout, this, [this] {
        ++m_previewFrame;
        previewPointer();
    });
    connect(m_pointerScheme, &QComboBox::activated, this, [this](int index) {
        const QString name = m_pointerScheme->itemData(index).toString();
        m_pointers.reset();
        for (const pointers::Scheme &s : std::as_const(m_pointerSchemes))
            if (s.name == name)
                m_pointers = s;
        showPointers();
        pointersChanged();
    });
    connect(m_pointerList, &QListWidget::currentRowChanged, this, [this] {
        m_previewFrame = 0;
        previewPointer();
    });
    connect(m_pointerShadowBox, &QCheckBox::toggled, this, [this](bool on) {
        m_pointerShadow = on;
        showPointers();
        pointersChanged();
    });
    connect(m_pointerDefault, &QPushButton::clicked, this, [this] {
        const int role = m_pointerList->currentRow();
        if (!m_pointers || role < 0)
            return;
        m_pointers->files[role].clear();
        showPointers();
        pointersChanged();
    });
    connect(m_pointerBrowse, &QPushButton::clicked, this, [this] {
        const int role = m_pointerList->currentRow();
        if (!m_pointers || role < 0)
            return;
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Browse"), paths::home(),
                                                          QStringLiteral("Cursors (*.cur *.ani)"));
        if (file.isEmpty())
            return;
        m_pointers->files[role] = file;
        showPointers();
        pointersChanged();
    });
    return page;
}

void DisplayProperties::fillPointerSchemes()
{
    m_pointerScheme->clear();
    m_pointerScheme->addItem(QStringLiteral("(None)"), QString());
    for (const pointers::Scheme &s : std::as_const(m_pointerSchemes))
        m_pointerScheme->addItem(s.name, s.name);
    m_pointerScheme->setCurrentIndex(qMax(0, m_pointerScheme->findData(m_pointers ? m_pointers->name : QString())));
    m_pointerScheme->setEnabled(!m_pointerSchemes.isEmpty());
    if (m_pointerSchemes.isEmpty())
        m_pointerScheme->setToolTip(QStringLiteral("XP's pointers come from its install media: set it up under Themes > More themes."));
}

QList<CursorFrame> DisplayProperties::pointerFrames(int role)
{
    const pointers::Scheme scheme = m_pointers.value_or(pointers::defaultScheme());
    const QString file = scheme.files.value(role);
    const QString key = (file.isEmpty() ? QStringLiteral("default:%1").arg(role) : file) + (m_pointerShadow ? QStringLiteral("+shadow") : QString());
    auto it = m_cursorCache.find(key);
    if (it == m_cursorCache.end()) {
        QList<CursorFrame> frames = pointers::frames(scheme, role);
        if (m_pointerShadow)
            frames = pointers::withShadow(frames);
        it = m_cursorCache.insert(key, frames);
    }
    return *it;
}

void DisplayProperties::showPointers()
{
    // "(None)" shows Windows Default's, unchosen: Plasma's own pointers stay.
    const int current = qMax(0, m_pointerList->currentRow());
    m_pointerList->clear();
    m_pointerShadowBox->setChecked(m_pointerShadow);
    if (m_pointerSchemes.isEmpty()) {
        m_pointerList->setEnabled(false);
        m_pointerShadowBox->setEnabled(false);
        m_pointerDefault->setEnabled(false);
        m_pointerBrowse->setEnabled(false);
        return;
    }
    for (int role = 0; role < pointers::RoleCount; ++role) {
        const QList<CursorFrame> frames = pointerFrames(role);
        auto *item = new QListWidgetItem(pointers::roleName(role));
        if (!frames.isEmpty())
            item->setIcon(QIcon(QPixmap::fromImage(frames.first().image)));
        m_pointerList->addItem(item);
    }
    m_pointerList->setEnabled(m_pointers.has_value());
    m_pointerShadowBox->setEnabled(m_pointers.has_value());
    m_pointerDefault->setEnabled(m_pointers.has_value());
    m_pointerBrowse->setEnabled(m_pointers.has_value());
    m_pointerList->setCurrentRow(current);
    m_previewFrame = 0;
    previewPointer();
}

void DisplayProperties::previewPointer()
{
    // The chosen pointer, animated as it would be.
    m_pointerTimer->stop();
    const int role = m_pointerList->currentRow();
    const QList<CursorFrame> frames = role >= 0 ? pointerFrames(role) : QList<CursorFrame>();
    if (frames.isEmpty()) {
        m_pointerPreview->clear();
        return;
    }
    const CursorFrame &f = frames.at(m_previewFrame % frames.size());
    m_pointerPreview->setPixmap(QPixmap::fromImage(f.image));
    if (frames.size() > 1)
        m_pointerTimer->start(qMax(16, f.delayMs));
}

void DisplayProperties::pointersChanged()
{
    if (m_loading)
        return;
    updateApply();
}

bool DisplayProperties::pointersDiffer() const
{
    if (!m_pointers)
        return m_appliedPointers.has_value();
    if(!m_appliedPointers || !(m_appliedPointers->first == *m_pointers) || m_appliedPointers->second != m_pointerShadow)return true;
    // Still honor a genuine desktop override without rebuilding for every preset.
    const QString current=QSettings(paths::config()+QStringLiteral("/kcminputrc"),QSettings::IniFormat).value(QStringLiteral("Mouse/cursorTheme")).toString();
    return !current.isEmpty() && current!=pointers::themeId(*m_pointers,m_pointerShadow);
}

// --- the library ---------------------------------------------------------------------------

void DisplayProperties::loadLibrary()
{
    m_library = Library::scan();
    m_styles.clear();
    for (const StyleInfo &s : std::as_const(m_library.styles))
        if (s.readable())
            m_styles.append(s);
    const QString applied = State::load().style;
    if (applied.startsWith(paths::data() + QStringLiteral("/custom-luna/"))) {
        auto custom = Library::style(applied);
        if (custom.readable()) { custom.label = QStringLiteral("Custom Colors"); m_styles.append(custom); }
    }
    fillThemes();
}

void DisplayProperties::fillThemes()
{
    m_themes->clear();
    bool defaults=false, separated=false;
    for (const ThemeBundle &t : m_library.themeChoices()) {
        if(!t.styleMissing.isEmpty())continue;
        if(Library::stockTheme(t))defaults=true;
        else if(defaults && !separated){m_themes->insertSeparator(m_themes->count());separated=true;}
        m_themes->addItem(t.label,t.path);
    }
    m_themes->insertSeparator(m_themes->count());
    m_themes->addItem(QStringLiteral("More themes..."), QLatin1String(kSetup));
    m_themes->addItem(QStringLiteral("Browse..."), QLatin1String(kBrowse));
}

void DisplayProperties::fillStyles()
{
    // XP Media Center's Royale and Microsoft's later colourings of it ship as
    // separate styles: listed as Royale's colour schemes, as XP lists Luna's.
    static const QStringList royale{QStringLiteral("royale.msstyles"), QStringLiteral("royale noir.msstyles"),
                                    QStringLiteral("zune.msstyles"), QStringLiteral("embedded.msstyles")};
    m_styleRows.clear();
    QList<std::tuple<int, int, StyleOption>> family;   // rank in the family, m_styles entry, colour
    for (int i = 0; i < m_styles.size(); ++i) {
        const StyleInfo &s = m_styles.at(i);
        const int rank = royale.indexOf(QFileInfo(s.path).fileName().toLower());
        if (rank < 0) {
            StyleRow row{s.label, {}};
            for (const StyleOption &c : s.colours)
                row.colours.append({i, c});
            m_styleRows.append(row);
            continue;
        }
        if (family.isEmpty())
            m_styleRows.append({QStringLiteral("Royale"), {}});   // (placed where the first one was)
        for (StyleOption c : s.colours) {
            // A style's only scheme, "Default", is the style: "Zune Style" is Zune.
            if (s.colours.size() == 1 && c.label.compare(QLatin1String("Default"), Qt::CaseInsensitive) == 0)
                c.label = QString(s.label).remove(QRegularExpression(QStringLiteral("\\s+style$"), QRegularExpression::CaseInsensitiveOption));
            family.append({rank, i, c});
        }
    }
    if (!family.isEmpty()) {
        std::stable_sort(family.begin(), family.end(), [](const auto &a, const auto &b) { return std::get<0>(a) < std::get<0>(b); });
        auto &row = *std::find_if(m_styleRows.begin(), m_styleRows.end(), [](const StyleRow &r) { return r.colours.isEmpty(); });
        for (const auto &[rank, i, c] : std::as_const(family))   // (Royale Noir's file holds Royale too: once)
            if (std::none_of(row.colours.cbegin(), row.colours.cend(), [&](const auto &have) { return have.second.label == c.label; }))
                row.colours.append({i, c});
    }
    m_style->clear();
    for (const StyleRow &row : std::as_const(m_styleRows))
        m_style->addItem(row.label, m_styles.at(row.colours.value(0).first).path);
    m_style->addItem(QStringLiteral("Windows Classic style"));
}

bool DisplayProperties::classicChosen() const
{
    return m_style->currentIndex() >= m_styleRows.size();
}

int DisplayProperties::styleIndex() const
{
    if (classicChosen() || m_style->currentIndex() < 0)
        return -1;
    const QString data = m_colour->currentData().toString();
    const int colon = int(data.indexOf(QLatin1Char(':')));
    return colon > 0 ? data.left(colon).toInt() : -1;
}

QString DisplayProperties::colourKey() const
{
    const QString data = m_colour->currentData().toString();
    return data.mid(data.indexOf(QLatin1Char(':')) + 1);
}

int DisplayProperties::styleFor(const QString &path) const
{
    const QString wanted = QFileInfo(path).canonicalFilePath();
    for (int i = 0; i < m_styles.size(); ++i)
        if (m_styles.at(i).path == path || (!wanted.isEmpty() && QFileInfo(m_styles.at(i).path).canonicalFilePath() == wanted))
            return i;
    return -1;
}

// The row holding m_styles[index], and its colour: by key, else (a colour
// listed once for the family) by name, else the style's first.
void DisplayProperties::selectStyle(int index, const QString &colour)
{
    QString label;
    for (const StyleOption &c : m_styles.value(index).colours)
        if (c.key == colour)
            label = c.label;
    int row = -1;
    for (int r = 0; r < m_styleRows.size() && row < 0; ++r)
        for (const auto &entry : m_styleRows.at(r).colours)
            if (entry.first == index)
                row = r;
    m_style->setCurrentIndex(row);
    fillColours();
    int at = m_colour->findData(QStringLiteral("%1:%2").arg(index).arg(colour));
    if (at < 0 && !label.isEmpty())
        for (int k = 0; k < m_colour->count() && at < 0; ++k)
            if (m_colour->itemText(k) == label)
                at = k;
    for (int k = 0; k < m_colour->count() && at < 0; ++k)
        if (m_colour->itemData(k).toString().startsWith(QString::number(index) + QLatin1Char(':')))
            at = k;
    if (at >= 0)
        m_colour->setCurrentIndex(at);
}

void DisplayProperties::fillColours()
{
    const QString previous = m_colour->currentData().toString();
    m_colour->clear();
    const int row = m_style->currentIndex();
    if (row >= 0 && row < m_styleRows.size()) {
        for (const auto &[index, c] : m_styleRows.at(row).colours)
            m_colour->addItem(c.label, QStringLiteral("%1:%2").arg(index).arg(c.key));
    } else {
        // Classic: the stock and saved schemes (a size is the Font size's).
        const QString base = schemes::split(m_scheme.name).first;
        for (const QString &name : schemes::bases())
            m_colour->addItem(name, name);
        if (m_colour->findData(base) < 0)
            m_colour->insertItem(0, base, base);
        m_colour->setCurrentIndex(m_colour->findData(base));
        return;
    }
    const int keep = m_colour->findData(previous);
    m_colour->setCurrentIndex(keep >= 0 ? keep : 0);
}

void DisplayProperties::fillSizes()
{
    const QString previous = m_size->currentData().toString();
    m_size->clear();
    if (const int i = styleIndex(); i >= 0) {
        for (const StyleOption &s : m_styles.at(i).sizesFor(colourKey()))
            m_size->addItem(s.label, s.key);
    } else {
        const QString base = m_colour->currentData().toString();
        for (const FontSize &s : fontSizes())
            if (schemes::sizeAvailable(base, QLatin1String(s.key)))
                m_size->addItem(QLatin1String(s.label), QLatin1String(s.key));
        const int keep = m_size->findData(schemes::split(m_scheme.name).second);
        m_size->setCurrentIndex(keep >= 0 ? keep : 0);
        return;
    }
    const int keep = m_size->findData(previous.isEmpty() ? QStringLiteral("NORMAL") : previous);
    m_size->setCurrentIndex(keep >= 0 ? keep : 0);
}

void DisplayProperties::fillWallpapers()
{
    m_wallpapers->clear();
    auto *none = new QListWidgetItem(QStringLiteral("(None)"));
    m_wallpapers->addItem(none);
    QFileInfoList files = QDir(paths::wallpapers()).entryInfoList({QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
                                                                    QStringLiteral("*.bmp"), QStringLiteral("*.gif")},
                                                                   QDir::Files, QDir::Name | QDir::IgnoreCase);
    sortWallpapers(files);
    for (const QFileInfo &f : std::as_const(files)) {
        auto *item = new QListWidgetItem(f.completeBaseName());
        item->setData(Qt::UserRole, f.absoluteFilePath());
        m_wallpapers->addItem(item);
    }
}

// --- choices ----------------------------------------------------------------------------------

Look DisplayProperties::look() const
{
    if (m_customLunaEnabled->isChecked() && m_customClassic) return Look::fromScheme(*m_customClassic);
    if (m_customLunaEnabled->isChecked() && !m_customLunaPath.isEmpty())
        if (auto custom = Look::fromStyle(m_customLunaPath, colorMetadata(m_customLunaPath).value(QStringLiteral("variant")).toString(m_customLunaVariant))) return *custom;

    if (const int i = styleIndex(); i >= 0) {
        const StyleInfo &s = m_styles.at(i);
        const QString ini = s.ini(colourKey(), m_size->currentData().toString());
        if (auto look = Look::fromStyle(s.path, ini))
            return *look;
    }
    return Look::fromScheme(m_scheme);
}

Wallpaper DisplayProperties::wallpaper() const
{
    Wallpaper w;
    if (QListWidgetItem *item = m_wallpapers->currentItem())
        w.image = item->data(Qt::UserRole).toString();
    w.fill = m_position->currentData().toInt();
    w.colour = m_desktopColour->colour();
    return w;
}

void DisplayProperties::installTheme(bool folder)
{
    const QString source = folder
        ? QFileDialog::getExistingDirectory(this, QStringLiteral("Install XP Theme Folder"), paths::home())
        : QFileDialog::getOpenFileName(this, QStringLiteral("Install XP Theme"), paths::home(),
             QStringLiteral("XP themes and archives (*.theme *.msstyles *.zip *.7z *.rar *.cab *.tar *.gz);;All files (*)"));
    if (source.isEmpty()) return;
    themeimport::Result installed;
    QString error;
    const bool ok = runJob(this, QStringLiteral("Install Theme"), [&](const JobContext &c, QString *e) {
        return themeimport::install(source, &installed, e, c.say, c.cancel);
    }, &error);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Install Theme"), error);
        return;
    }
    m_loading = true;
    loadLibrary();
    fillStyles();
    fillColours();
    fillSizes();
    fillWallpapers();
    m_loading = false;
    const int choice = m_themes->findData(installed.theme);
    if (choice >= 0) m_themes->setCurrentIndex(choice);
    showBundle(Library::bundle(installed.theme));
    updateApply();
}

void DisplayProperties::themePicked(int index)
{
    const QString data = m_themes->itemData(index).toString();
    if (data == QLatin1String(kSetup)) {
        SetupDialog dialog(this);
        dialog.exec();
        if (dialog.changedLibrary()) {
            loadLibrary();
            fillStyles();
            fillColours();
            fillSizes();
            fillWallpapers();
        }
        return;
    }
    if (data == QLatin1String(kBrowse)) {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Open Theme"), paths::themes(), QStringLiteral("Themes (*.theme)"));
        if (file.isEmpty())
            return;
        const ThemeBundle bundle = Library::bundle(file);
        if (!bundle.styleMissing.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Open Theme"), QStringLiteral("%1 names a visual style that isn't here (%2).")
                                                                         .arg(QFileInfo(file).fileName(), bundle.styleMissing));
            return;
        }
        m_themes->insertItem(0, bundle.label, bundle.path);
        m_themes->setCurrentIndex(0);
        showBundle(bundle);
        return;
    }
    for (const ThemeBundle &b : std::as_const(m_library.themes))
        if (b.path == data)
            showBundle(b);
}

void DisplayProperties::showBundle(const ThemeBundle &b)
{
    m_customColorsPending=false;
    const QSignalBlocker customBlock(m_customLunaEnabled);
    m_customLunaEnabled->setChecked(false);
    m_customLunaPath.clear(); m_customLunaSource.clear(); m_customLunaVariant.clear(); m_customClassic.reset();
    m_loading = true;
    if (!b.style.isEmpty() && styleFor(b.style) < 0) {
        auto custom = Library::style(b.style);
        if (custom.readable()) { custom.label = QStringLiteral("Custom Colors"); m_styles.append(custom); fillStyles(); }
    }
    m_themeFile = b.path;
    const auto file = ThemeFile::load(b.path);
    const ClassicScheme colours = file ? ClassicScheme::fromThemeFile(*file) : ClassicScheme();
    if (b.classic()) {
        m_scheme = colours;
        m_scheme.name = b.label;
        m_style->setCurrentIndex(m_styleRows.size());
        fillColours();
        fillSizes();
    } else {
        const int i = styleFor(b.style);
        const StyleInfo s = m_styles.value(i);
        const StyleVariant *v = s.variant(b.variant.isEmpty() ? s.defaultVariant : b.variant);
        if (i >= 0)
            selectStyle(i, v ? v->colour : QString());
        else
            fillColours();
        fillSizes();
        if (v)
            m_size->setCurrentIndex(m_size->findData(v->size));
    }
    // A complete preset also selects its pointer scheme. Unspecified bundled
    // Luna/Classic presets use XP's Windows Default rather than a desktop override.
    const QJsonObject installedFeatures = State::load().settings.value(QStringLiteral("installerFeatures")).toObject();
    const bool presetPointers = !installedFeatures.contains(QStringLiteral("pointers")) || installedFeatures.value(QStringLiteral("pointers")).toBool();
    if (presetPointers && !m_pointerSchemes.isEmpty()) {
        m_pointers = pointers::defaultScheme();
        const QString name = file ? file->value(u"Control Panel\\Cursors", u"SchemeName") : QString();
        for (const auto &scheme : std::as_const(m_pointerSchemes))
            if (!name.isEmpty() && scheme.name.compare(name, Qt::CaseInsensitive) == 0)
                m_pointers = scheme;
        m_pointerShadow = true;
        // Keep the applied cursor baseline: color-only profiles can reuse it.
        fillPointerSchemes();
        showPointers();
    }
    // The desktop the theme brings.
    int row = 0;
    for (int i = 0; i < m_wallpapers->count(); ++i)
        if (!b.wallpaper.isEmpty() && QFileInfo(m_wallpapers->item(i)->data(Qt::UserRole).toString()) == QFileInfo(b.wallpaper))
            row = i;
    if (row == 0 && !b.wallpaper.isEmpty()) {
        auto *item = new QListWidgetItem(QFileInfo(b.wallpaper).completeBaseName());
        item->setData(Qt::UserRole, b.wallpaper);
        m_wallpapers->addItem(item);
        row = m_wallpapers->count() - 1;
    }
    m_wallpapers->setCurrentRow(row);
    for (int i = 0; i < m_position->count(); ++i)
        if (m_position->itemData(i).toInt() == b.fill.value_or(0))
            m_position->setCurrentIndex(i);
    m_desktopColour->setColour(colours.colour(u"Background"));
    if (!b.style.isEmpty()) restoreCustomLuna(b.style);
    const auto live=file ? QJsonDocument::fromJson(file->value(u"XPlasma",u"LiveColors").toUtf8()).object() : QJsonObject();
    m_liveStartTimer->stop();
    m_liveStartEnabled->setChecked(false);
    if(live.value("enabled").toBool()){
        const char *keys[]={"start","menu","title","buttons"};
        const char *textKeys[]={"startText","menuText","titleText","buttonsText"};
        for(int i=0;i<4;++i){const QColor color(live.value(QLatin1String(keys[i])).toString());if(color.isValid())m_lunaColours[i]->setColour(color);m_customTextModes[i]->setCurrentIndex(qBound(0,live.value(QLatin1String(textKeys[i])).toInt(),2));}
        m_startMenuTextMode->setCurrentIndex(qBound(0,live.value("startMenuText").toInt(live.value("menuText").toInt()),2));
        m_linkTitleColor->setChecked(live.value("linkTitleToTaskbar").toBool(true));syncTitleColorLink();
    }
    m_loading = false;
    if(live.value("enabled").toBool()) {
        m_customLunaEnabled->setChecked(true);
        // Loading a baked profile restores controls, not a second tint pass.
        if(live.value("rendering").toString()!=QLatin1String("static"))customLunaChanged();
        else if(!b.classic())m_customLunaPath=b.style;
        else {m_customClassic=colours;m_customLunaPath=QStringLiteral("classic");}
        m_themeFile=b.path;
    }
    m_delete->setEnabled(presets::owned(b.path) || themeimport::removable(b.path));
    renderThemes();
    renderDesktop();
    renderAppearance();
    updateApply();
}

void DisplayProperties::appearanceChanged()
{
    m_customColorsPending=false;
    if (!m_loading) {
        const QSignalBlocker block(m_customLunaEnabled);
        m_customLunaEnabled->setChecked(false);
        m_customLunaPath.clear(); m_customLunaSource.clear(); m_customLunaVariant.clear(); m_customClassic.reset();
    }
    if (m_loading)
        return;
    if (classicChosen()) {
        // A Classic scheme picked by name and size.
        const QString base = m_colour->currentData().toString();
        const QString sizeKey = m_size->currentData().toString();
        QString name = base;
        for (const FontSize &s : fontSizes())
            if (sizeKey == QLatin1String(s.key))
                name += QLatin1String(s.suffix);
        if (const auto s = schemes::load(name))
            m_scheme = *s;
    }
    m_advanced->setEnabled(classicChosen());
    m_themeFile.clear();
    renderAppearance();
    renderThemes();
    updateApply();
}

void DisplayProperties::desktopChanged()
{
    if (m_loading)
        return;
    m_themeFile.clear();
    renderDesktop();
    renderThemes();
    updateApply();
}

void DisplayProperties::openExtras()
{
    ExtrasDialog dialog(look(), m_startExtras, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_startExtras = dialog.extras();
    showStartLogos();
    renderThemes();
    updateApply();
}

// Synchronize the mutually exclusive logo checkboxes with Extras.
void DisplayProperties::showStartLogos()
{
    const QSignalBlocker tuxBlocker(m_tux), kdeBlocker(m_kdeLogo);
    const QString selected = m_startExtras.logoFile.isEmpty() ? QString() : QFileInfo(m_startExtras.logoFile).absoluteFilePath();
    // Absolute paths remain distinct even before an embedded asset is extracted.
    m_tux->setChecked(!selected.isEmpty() && selected == QFileInfo(paths::data() + QLatin1String("/logos/tux.png")).absoluteFilePath());
    m_kdeLogo->setChecked(!selected.isEmpty() && selected == QFileInfo(paths::data() + QLatin1String("/logos/kde-crystal.png")).absoluteFilePath());
}

void DisplayProperties::openAdvanced()
{
    AdvancedDialog dialog(m_scheme, &m_previewer, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_scheme = dialog.scheme();
    m_themeFile.clear();
    m_loading = true;
    fillColours();
    fillSizes();
    m_loading = false;
    renderAppearance();
    renderThemes();
    updateApply();
}

void DisplayProperties::saveAs()
{
    if(m_customColorsPending || customLunaPruned())customLunaChanged();
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Save Theme"), QStringLiteral("Save this theme as:"), QLineEdit::Normal,
                                               m_themes->currentText(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    presets::Preset p;
    p.name = name;
    if(m_customLunaEnabled->isChecked()){
        p.liveColors={{"enabled",true},{"rendering","static"},{"startMenuText",m_startMenuTextMode->currentIndex()},{"linkTitleToTaskbar",m_linkTitleColor->isChecked()}};
        const char *keys[]={"start","menu","title","buttons"};const char *text[]={"startText","menuText","titleText","buttonsText"};
        for(int i=0;i<4;++i){p.liveColors.insert(keys[i],m_lunaColours[i]->colour().name());p.liveColors.insert(text[i],m_customTextModes[i]->currentIndex());}
    }
    const Look l = look();
    if (!l.classic()) {
        p.style = l.stylePath;
        const StyleInfo s = Library::style(l.stylePath);
        if (const StyleVariant *v = s.variant(l.variant)) {
            for (const StyleOption &c : s.colours)
                if (c.key == v->colour)
                    p.colorStyle = c.name;
            for (const StyleOption &z : s.sizes)
                if (z.key == v->size)
                    p.size = z.name;
        }
    } else {
        p.scheme = l.system;
    }
    const Wallpaper w = wallpaper();
    p.desktop = presets::Desktop{w.image, w.fill.value_or(2), w.colour};
    if (m_themeFile.endsWith(QLatin1String(".theme"), Qt::CaseInsensitive))
        p.base = m_themeFile;
    QString path, error;
    if (!presets::save(p, &path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Save Theme"), error);
        return;
    }
    m_library = Library::scan();
    fillThemes();
    m_themes->setCurrentIndex(m_themes->findData(path));
    m_themeFile = path;
    m_delete->setEnabled(true);
    updateApply();
}

void DisplayProperties::deleteTheme()
{
    const QString path = m_themes->currentData().toString();
    const bool imported = themeimport::removable(path);
    if (!presets::owned(path) && !imported) return;
    const QString question = QStringLiteral("Delete the theme \"%1\" from the library?").arg(m_themes->currentText())
        + (imported ? QStringLiteral("\n\nShared artwork is retained so other presets and the current desktop keep working.") : QString());
    if (QMessageBox::question(this, QStringLiteral("Delete Theme"), question) != QMessageBox::Yes) return;
    QString error;
    const bool removed = imported ? themeimport::remove(path, &error) : QFile::remove(path);
    if (!removed) {
        QMessageBox::warning(this, QStringLiteral("Delete Theme"), error.isEmpty() ? QStringLiteral("Could not delete the theme file.") : error);
        return;
    }
    State state = State::load();
    if (state.themeFile == path) {
        state.themeFile.clear();
        m_appliedThemeFile.clear();
    }
    // (Its custom colors go too, unless they're what the desktop wears.)
    pruneGeneratedLooks(state);
    if (!state.save(&error)) QMessageBox::warning(this, QStringLiteral("Delete Theme"), error);
    const QSignalBlocker blockThemeSelection(m_themes);
    m_loading = true;
    loadLibrary();
    m_loading = false;
    // Retain the currently displayed appearance until the user chooses another theme.
    m_themes->setCurrentIndex(-1);
    m_themeFile.clear();
    m_delete->setEnabled(false);
    updateApply();
}

bool DisplayProperties::apply()
{
    if(m_customColorsPending || customLunaPruned())customLunaChanged();
    if (m_customLunaEnabled->isChecked() && m_customLunaPath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Custom Colors"), m_customLunaStatus->text());
        return false;
    }
    const Look l = look();
    const Wallpaper desktop = wallpaper();
    // Shift: everything, as if nothing were applied.
    const bool everything = shiftHeld();
    const bool lookChanged = everything || lookKey(l) != m_appliedLook;
    const bool desktopChanged = everything || !sameDesktop(desktop, m_appliedDesktop);
    const bool pointersChanged = pointersDiffer() || (everything && m_pointers.has_value());
    const bool startChanged = m_startExtras != m_appliedStartExtras;
    const bool crispChanged = m_crispText->isChecked() != m_appliedCrisp;
    const bool hidpiChanged = m_hidpiStart->isChecked() != m_appliedHiDpiStart;
    const bool darkChanged = m_darkMode->isChecked() != m_appliedDark;
    const bool cacheChanged = m_themeCache->isChecked() != m_appliedThemeCache
        || (m_themeCacheChosen && !State::load().settings.contains(QStringLiteral("themeCacheEnabled")));
    const bool linkChanged=m_linkTitleColor->isChecked()!=m_appliedTitleLink;
    const bool liveChanged=liveStartColors()!=m_appliedLiveStart;
    if (!lookChanged && !desktopChanged && !pointersChanged && !startChanged && !darkChanged && !crispChanged && !hidpiChanged && !liveChanged && !linkChanged && !cacheChanged && m_themeFile == m_appliedThemeFile)
        return m_sounds->apply();

    QString error;
    bool ok = true;
    if (lookChanged || cacheChanged) {
        const auto choice = cachepreferences::choose(this, m_themeCache->isChecked(), m_themeCacheChosen, &error);
        if (!choice) {
            if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Theme caching"), error);
            return false;
        }
        m_themeCache->setChecked(*choice);
        m_appliedThemeCache = *choice;
        m_themeCacheChosen = false;
        if (cacheChanged && !lookChanged) { LiveSession session; maintenance::run(session); }
    }
    if (startChanged || hidpiChanged) {
        // (Kept first: a look applied now draws its Start button with them.)
        State state = State::load();
        if (m_startExtras.isDefault())
            state.settings.remove(QLatin1String(kStartButtonKey));
        else
            state.settings.insert(QLatin1String(kStartButtonKey), m_startExtras.toJson());
        state.settings.insert(QLatin1String(kHiDpiStartKey), m_hidpiStart->isChecked());
        state.save();
    }
    if (lookChanged) {
        ApplyOptions o;
        o.allowColorOnly=!everything && !startChanged && !hidpiChanged && !m_outdated;
        o.darkMode = m_darkMode->isChecked();
        if (desktopChanged)
            o.wallpaper = desktop;
        o.themeFile = m_themeFile;
        const QJsonObject features = State::load().settings.value(QStringLiteral("installerFeatures")).toObject();
        auto enabled = [&](const char *name) { return !features.contains(QLatin1String(name)) || features.value(QLatin1String(name)).toBool(); };
        o.properties = enabled("theme_manager"); o.colours = enabled("desktop"); o.decoration = enabled("desktop"); o.fonts = enabled("fonts");
        o.widgets = enabled("apps"); o.taskbar = enabled("taskbar"); o.icons = enabled("icons"); o.sounds = enabled("sounds");
        if (!enabled("wallpaper")) o.wallpaper.reset();

        ok = runJob(this, QStringLiteral("Applying %1").arg(l.name), [&](const JobContext &c, QString *e) {
            ApplyOptions options = o;
            options.progress = c.say;
            LiveSession session;
            return applyLook(session, l, options, e);
        }, &error);
    } else if (desktopChanged || m_themeFile != m_appliedThemeFile) {
        if (desktopChanged) {
            LiveSession session;
            applyWallpaper(session, desktop);
        }
        // (The same look, now from this theme: only the record changes.)
        State state = State::load();
        state.themeFile = m_themeFile;
        state.save();
    }
    if ((startChanged || hidpiChanged) && !lookChanged) {
        ok = runJob(this, QStringLiteral("Applying the Start button"), [&](const JobContext &, QString *e) {
            LiveSession session;
            return applyStartButton(session, l, e);
        }, &error);
    }
    if(ok && (liveChanged || ((lookChanged || darkChanged) && m_liveStartEnabled->isChecked()))) {
        m_liveStartTimer->stop();const auto colors=liveStartColors();
        ok=runResponsiveTask(this,[colors](QString *error){LiveSession session;return applyLiveStartColors(session,colors,error);},&error);
        if(ok) {
            State state=State::load();
            if(liveStartColors().isEmpty()){
                if(state.settings.contains(QLatin1String(kLiveStartColorsKey)))state.settings.insert(QStringLiteral("retiredColorProfile"),state.settings.value(QLatin1String(kLiveStartColorsKey)));
                state.settings.remove(QLatin1String(kLiveStartColorsKey));
            }
            else state.settings.insert(QLatin1String(kLiveStartColorsKey),liveStartColors());
            ok=state.save(&error);
            if(ok)m_appliedLiveStart=liveStartColors();
        }
    }
    if(ok && linkChanged) {
        State state=State::load();state.settings.insert(QStringLiteral("linkTitleToTaskbar"),m_linkTitleColor->isChecked());
        ok=state.save(&error);if(ok)m_appliedTitleLink=m_linkTitleColor->isChecked();
    }
    if (ok && darkChanged && !lookChanged) {
        ok = runJob(this, QStringLiteral("Applying application colors"), [&](const JobContext &, QString *e) { LiveSession session; return darkmode::apply(session, l, m_darkMode->isChecked(), e); }, &error);
    }
    if (ok && crispChanged) {
        LiveSession session;ok = crisptext::apply(session,m_crispText->isChecked(),&error);
        if (ok) m_appliedCrisp=m_crispText->isChecked();
    }
    if (ok) m_appliedDark = m_darkMode->isChecked();
    if (ok && startChanged)
        m_appliedStartExtras = m_startExtras;
    if (ok && hidpiChanged)
        m_appliedHiDpiStart = m_hidpiStart->isChecked();
    if (ok) {
        if (lookChanged) {
            m_appliedLook = lookKey(l);
            m_outdated = false;
        }
        m_appliedDesktop = desktop;
        m_appliedThemeFile = m_themeFile;
    }
    if (ok && pointersChanged) {
        LiveSession session;
        State state = State::load();
        if (m_pointers)
            ok = pointers::apply(session, state, *m_pointers, m_pointerShadow, &error);
        else
            pointers::revert(session, state);
        if (ok) {
            state.save();
            m_appliedPointers.reset();
            if (m_pointers)
                m_appliedPointers = std::pair(*m_pointers, m_pointerShadow);
        }
    }
    if (!ok)
        QMessageBox::warning(this, QStringLiteral("Apply"), error);
    // (after the look, which may have put Windows Default on itself)
    if (ok)
        ok = m_sounds->apply();
    updateApply();
    return ok;
}

void DisplayProperties::updateApply()
{
    m_apply->setToolTip(m_outdated ? QStringLiteral("XPlasma has been updated since this look was applied. Apply rebuilds it with this "
                                                    "version.\nHold Shift to rebuild and apply everything, changed or not.")
                                   : QStringLiteral("Hold Shift to rebuild and apply everything, changed or not."));
    m_apply->setEnabled(m_themeCache->isChecked() != m_appliedThemeCache
                        || (m_themeCacheChosen && !State::load().settings.contains(QStringLiteral("themeCacheEnabled"))) || shiftHeld() || lookKey(look()) != m_appliedLook || !sameDesktop(wallpaper(), m_appliedDesktop)
                        || m_themeFile != m_appliedThemeFile || pointersDiffer() || m_startExtras != m_appliedStartExtras
                        || m_linkTitleColor->isChecked()!=m_appliedTitleLink || liveStartColors()!=m_appliedLiveStart || m_sounds->pending() || m_darkMode->isChecked() != m_appliedDark || m_crispText->isChecked() != m_appliedCrisp
                        || m_hidpiStart->isChecked() != m_appliedHiDpiStart);
}

bool DisplayProperties::eventFilter(QObject *watched, QEvent *event)
{
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Shift && isActiveWindow())
        QTimer::singleShot(0, this, &DisplayProperties::updateApply);
    return QDialog::eventFilter(watched, event);
}

// --- samples -----------------------------------------------------------------------------------

void DisplayProperties::renderThemes()
{
    if (m_loading)
        return;
    if(!m_themeSample->isVisibleTo(m_tabs) && !m_customLunaSample->isVisibleTo(m_tabs))return;
    const auto sample = QPixmap::fromImage(m_previewer.theme(look(), wallpaper(), preview::kThemesSize, m_startExtras, liveStartColors()));
    m_themeSample->setPixmap(sample);
    m_customLunaSample->setPixmap(sample);
}

void DisplayProperties::renderDesktop()
{
    if (m_loading)
        return;
    m_desktopSample->setPixmap(QPixmap::fromImage(m_previewer.desktop(wallpaper(), QSize(200, 170))));
}

void DisplayProperties::renderAppearance()
{
    if (m_loading)
        return;
    m_advanced->setEnabled(classicChosen());
    if(!m_appearanceSample->isVisibleTo(m_tabs))return;
    const auto applications = darkmode::applicationLook(look(), m_darkMode->isChecked());
    const Look l = applications.value_or(look());
    m_appearanceSample->setPixmap(QPixmap::fromImage(m_previewer.appearance(l, preview::kAppearanceSize, l.system.colour(u"Background"),liveStartColors())));
}

void DisplayProperties::showTab(const QString &name)
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabText(i).remove(QLatin1Char('&')).compare(name, Qt::CaseInsensitive) == 0)
            m_tabs->setCurrentIndex(i);
}

bool DisplayProperties::saveScreenshots(const QString &dir)
{
    QDir().mkpath(dir);
    setAttribute(Qt::WA_DontShowOnScreen);
    show();
    bool ok = true;
    m_sounds->showForScreenshot();
    const char *const names[] = {"themes", "desktop", "appearance", "custom-luna", "pointers", "login-screen", "sounds", "about"};
    for (int i = 0; i < m_tabs->count(); ++i) {
        m_tabs->setCurrentIndex(i);
        QApplication::processEvents();
        ok &= grab().save(QDir(dir).filePath(QLatin1String(names[i]) + QLatin1String(".png")));
    }
    AdvancedDialog advanced(m_scheme, &m_previewer, this);
    advanced.setAttribute(Qt::WA_DontShowOnScreen);
    advanced.show();
    QApplication::processEvents();
    ok &= advanced.grab().save(QDir(dir).filePath(QStringLiteral("advanced.png")));
    ExtrasDialog extras(look(), m_startExtras, this);
    extras.setAttribute(Qt::WA_DontShowOnScreen);
    extras.show();
    QApplication::processEvents();
    ok &= extras.grab().save(QDir(dir).filePath(QStringLiteral("extras.png")));
    return ok;
}

} // namespace xpl::editor


