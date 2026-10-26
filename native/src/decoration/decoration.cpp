// SPDX-License-Identifier: GPL-3.0-or-later
#include "decoration/decoration.h"

#include "core/schema.h"
#include "core/themefile.h"

#include <KDecoration3/DecoratedWindow>
#include <KPluginFactory>

#include <QFileInfo>
#include <QtMath>
#include <QFileSystemWatcher>
#include <QFontMetricsF>
#include <QIcon>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

using KDecoration3::DecorationButtonType;

namespace xpd {
namespace {

int part(const char *name)
{
    return xpl::schema::partId(u"window", QString::fromLatin1(name));
}

struct Parts
{
    int caption = part("caption"), maxCaption = part("maxcaption");
    int left = part("frameleft"), right = part("frameright"), bottom = part("framebottom");
    int close = part("closebutton"), max = part("maxbutton"), restore = part("restorebutton");
    int min = part("minbutton"), help = part("helpbutton");
};
const Parts &parts()
{
    static const Parts p;
    return p;
}

QString rcPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/xplasmarc");
}

std::pair<QString, QString> configuredTheme(const QString &rc)
{
    QSettings settings(rc, QSettings::IniFormat);
    return {settings.value(QStringLiteral("Theme/Path")).toString(), settings.value(QStringLiteral("Theme/Variant")).toString()};
}

} // namespace

// --- the look ------------------------------------------------------------------

Look &Look::instance()
{
    static Look look;
    return look;
}

Look::Look()
    : m_rc(rcPath())
{
    m_parts.setMaxCost(8 * 1024 * 1024);   // bytes, shared by every window
    m_loaded = configuredTheme(m_rc);
    load();
    // xplasmarc is replaced atomically on every apply: watch its folder, and
    // the file again each time it comes back; fold bursts together.
    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(QFileInfo(m_rc).absolutePath());
    if (QFileInfo::exists(m_rc))
        m_watcher->addPath(m_rc);
    m_reload = new QTimer(this);
    m_reload->setSingleShot(true);
    m_reload->setInterval(150);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, m_reload, qOverload<>(&QTimer::start));
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, m_reload, qOverload<>(&QTimer::start));
    connect(m_reload, &QTimer::timeout, this, &Look::reloadIfChanged);
}

void Look::reloadIfChanged()
{
    if (QFileInfo::exists(m_rc) && !m_watcher->files().contains(m_rc))
        m_watcher->addPath(m_rc);
    const auto now = configuredTheme(m_rc);
    if (now == m_loaded)
        return;
    m_loaded = now;
    load();
    Q_EMIT changed();
}

void Look::load()
{
    m_parts.clear();
    m_renderer.reset();
    m_system = xpl::ClassicScheme();
    const auto &[path, variant] = m_loaded;
    if (path.endsWith(QLatin1String(".theme"), Qt::CaseInsensitive)) {
        // Windows Classic: a colour scheme and its metrics.
        if (const auto file = xpl::ThemeFile::load(path))
            m_system = xpl::ClassicScheme::fromThemeFile(*file);
    } else if (!path.isEmpty()) {
        if (auto theme = xpl::Theme::load(path, variant)) {
            m_system = xpl::ClassicScheme::fromSysMetrics(*theme);
            m_renderer = std::make_unique<xpl::Renderer>(theme);
        }
    }
    // (As XPlasma's Aurorae themes have it: SM_CXSIZEFRAME frames,
    // SM_CYCAPTION captions; Classic's buttons SM_CXSIZE - 2 by SM_CYSIZE - 4,
    // a style's square, SM_CYSIZE - 4.)
    const xpl::NonClientMetrics &m = m_system.metrics;
    m_frame = m.borderWidth + 3;
    m_caption = m.captionHeight + 1;
    m_button = classic() ? QSize(qMax(8, m.captionWidth - 2), qMax(8, m.captionHeight - 4))
                         : QSize(qMax(8, m.captionHeight - 4), qMax(8, m.captionHeight - 4));
    const xpl::LogFont &lf = m.captionFont;
    m_font = QFont(lf.face);
    m_font.setPointSize(qMax(1, lf.points()));
    m_font.setBold(lf.bold());
    m_font.setItalic(lf.italic);
}

QImage Look::part(int part, int state, QSize size, qreal scale) const
{
    if (!m_renderer || size.isEmpty())
        return QImage();
    const Key key{part, state, size.width(), size.height(), qRound(scale * 100)};
    if (const QImage *cached = m_parts.object(key))
        return *cached;
    // XP's art is drawn at its own 96-DPI size and the whole part scaled to
    // the screen, as the Qt style does its scroll bars: rendered at the
    // screen's DPI instead, TrueSize glyphs keep their pixel size and come
    // out a fraction of their buttons on a HiDPI screen.
    QImage image = m_renderer->background({QStringLiteral("window"), part, state, {}}, size, 96);
    if (!qFuzzyCompare(scale, 1.0))
        image = image.scaled((QSizeF(size) * scale).toSize(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    image.setDevicePixelRatio(scale);
    const qint64 bytes = image.sizeInBytes();
    if (bytes <= m_parts.maxCost())
        m_parts.insert(key, new QImage(image), int(bytes));
    return image;
}

QImage Look::classicPiece(Piece piece, bool active, int length, qreal scale) const
{
    if (length <= 0)
        return QImage();
    const Key key{int(piece), active ? 1 : 2, length, 0, qRound(scale * 100)};
    if (const QImage *cached = m_parts.object(key))
        return *cached;
    const xpl::ClassicScheme &s = m_system;
    const QPalette pal = s.shellPalette(2);
    const QColor face = pal.color(QPalette::Button);
    const QColor from = s.colour(active ? u"ActiveTitle" : u"InactiveTitle");
    const QColor to = s.colour(active ? u"GradientActiveTitle" : u"GradientInactiveTitle");
    const int F = m_frame, C = m_caption, bar = s.metrics.captionHeight;
    QImage logical;
    if (piece == Piece::Maximized) {
        logical = QImage(length, C, QImage::Format_ARGB32_Premultiplied);
        logical.fill(face);
        QPainter p(&logical);
        xpl::classic::gradientFill(&p, QRect(0, 0, length, bar), from, to);
    } else {
        // A window just tall enough to cut from, so the bevel's corners and
        // joins are where XP has them: its caption, one row of side, its
        // bottom. (Sides are the same all the way down.)
        const int w = piece == Piece::Left || piece == Piece::Right ? 64 : length;
        QImage mini(w, F + C + 1 + F, QImage::Format_ARGB32_Premultiplied);
        mini.fill(face);
        {
            QPainter p(&mini);
            xpl::classic::raisedEdge(&p, mini.rect(), pal);
            // XP's caption gradient runs from past the icon to short of the
            // buttons, the start colour before it and the end colour after.
            const int Lc = F + 2 + 16 + 2;
            const int Rc = F + 2 + 3 * m_button.width() + 2 + 2;
            p.fillRect(QRect(F, F, Lc - F, bar), from);
            xpl::classic::gradientFill(&p, QRect(Lc, F, qMax(0, w - Lc - Rc), bar), from, to);
            p.fillRect(QRect(w - Rc, F, Rc - F, bar), to);
        }
        switch (piece) {
        case Piece::Top:
            logical = mini.copy(0, 0, w, F + C);
            break;
        case Piece::Bottom:
            logical = mini.copy(0, F + C + 1, w, F);
            break;
        default:
            logical = mini.copy(piece == Piece::Left ? 0 : w - F, F + C, F, 1).scaled(F, length);
            break;
        }
    }
    QImage image = qFuzzyCompare(scale, 1.0)
                       ? logical
                       : logical.scaled((QSizeF(logical.size()) * scale).toSize(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    image.setDevicePixelRatio(scale);
    m_parts.insert(key, new QImage(image), int(qMin<qint64>(image.sizeInBytes(), INT_MAX)));
    return image;
}

bool Look::hasPart(int part, int state) const
{
    return m_renderer && m_renderer->hasPart({QStringLiteral("window"), part, state, {}});
}

std::optional<QString> Look::captionProperty(bool active, QStringView name) const
{
    if (!m_renderer)
        return std::nullopt;
    return m_renderer->property({QStringLiteral("window"), parts().caption, active ? 1 : 2, {}}, name);
}

// --- the decoration ----------------------------------------------------------------

Decoration::Decoration(QObject *parent, const QVariantList &args)
    : KDecoration3::Decoration(parent, args)
{
}

bool Decoration::maximized() const
{
    return window()->isMaximized();
}

bool Decoration::init()
{
    // The window menu (the icon) on the left; help, minimize, maximize and
    // close on the right, as XP lays them out.
    for (const DecorationButtonType type : {DecorationButtonType::Menu, DecorationButtonType::Close, DecorationButtonType::Maximize,
                                            DecorationButtonType::Minimize, DecorationButtonType::ContextHelp})
        m_buttons.append(new Button(type, this, this));

    const auto *w = window();
    const auto layoutAndPaint = [this] {
        relayout();
        update();
    };
    connect(w, &KDecoration3::DecoratedWindow::activeChanged, this, [this] { update(); });
    connect(w, &KDecoration3::DecoratedWindow::captionChanged, this, [this] { update(); });
    connect(w, &KDecoration3::DecoratedWindow::iconChanged, this, [this] { update(); });
    connect(w, &KDecoration3::DecoratedWindow::sizeChanged, this, layoutAndPaint);
    connect(w, &KDecoration3::DecoratedWindow::maximizedChanged, this, layoutAndPaint);
    connect(w, &KDecoration3::DecoratedWindow::minimizeableChanged, this, layoutAndPaint);
    connect(w, &KDecoration3::DecoratedWindow::maximizeableChanged, this, layoutAndPaint);
    connect(w, &KDecoration3::DecoratedWindow::providesContextHelpChanged, this, layoutAndPaint);
    connect(w, &KDecoration3::DecoratedWindow::scaleChanged, this, layoutAndPaint);
    connect(&Look::instance(), &Look::changed, this, layoutAndPaint);
    relayout();
    return true;
}

void Decoration::relayout()
{
    const Look &look = Look::instance();
    const bool max = maximized();
    const qreal F = look.frame(), C = look.caption();
    const qreal W = window()->width() + (max ? 0 : 2 * F);
    setBorders(max ? QMarginsF(0, C, 0, 0) : QMarginsF(F, F + C, F, F));
    const qreal edge = max ? 0 : F;   // where the frame ends and the caption begins
    setTitleBar(QRectF(edge, edge, W - 2 * edge, C));
    setOpaque(look.classic());

    const QSizeF b = look.button();
    const qreal top = edge + 2;
    // From the right, XP's slots: 2px in from the frame, 2px apart
    // (Classic: minimize and maximize touch, close 2px off).
    int slot = 0;
    auto place = [&](KDecoration3::DecorationButton *button) {
        const qreal gaps = look.classic() ? (slot > 0 ? 2 : 0) : slot * 2;
        button->setGeometry(QRectF(W - edge - 2 - b.width() - slot * b.width() - gaps, top, b.width(), b.height()));
        ++slot;
    };
    // Minimize and maximize together, or neither (XP shows the one a window
    // can't do disabled beside the one it can).
    const bool sizeBox = window()->isMinimizeable() || window()->isMaximizeable();
    qreal leftmost = W - edge;
    for (KDecoration3::DecorationButton *button : std::as_const(m_buttons)) {
        if (!button)
            continue;
        switch (button->type()) {
        case DecorationButtonType::Menu:
            button->setGeometry(QRectF(edge + 2, edge + (C - 16) / 2, 16, 16));
            continue;
        case DecorationButtonType::Minimize:
        case DecorationButtonType::Maximize:
            button->setVisible(sizeBox);
            break;
        case DecorationButtonType::ContextHelp:
            button->setVisible(window()->providesContextHelp());
            break;
        default:
            break;
        }
        if (!button->isVisible())
            continue;
        place(button);
        leftmost = button->geometry().left();
    }
    const qreal labelLeft = edge + 2 + 16 + (look.classic() ? 2 : 4);
    m_label = QRectF(labelLeft, edge, qMax<qreal>(0, leftmost - 2 - labelLeft), C);
}

void Decoration::paint(QPainter *painter, const QRectF &repaintArea)
{
    const bool active = window()->isActive();
    if (Look::instance().classic())
        paintClassic(painter, active);
    else
        paintStyle(painter, active);
    paintCaption(painter, active);
    for (KDecoration3::DecorationButton *button : std::as_const(m_buttons))
        if (button && button->isVisible() && button->geometry().intersects(repaintArea))
            button->paint(painter, repaintArea);
}

// The frame around a window that isn't maximized: the caption across the
// whole width (its own top edge the frame's), the sides down to the bottom
// frame, and the bottom. Sized up to whole pixels: at a fractional scale a
// window is a fraction of a pixel bigger, and every pixel of the decoration
// has to be painted or it shows what was there before.
void Decoration::drawEdges(QPainter *painter, bool active, const std::function<QImage(int, int, int)> &piece)
{
    Q_UNUSED(active)
    const Look &look = Look::instance();
    const qreal W = size().width(), H = size().height();
    const int F = look.frame(), top = F + look.caption();
    const int w = qCeil(W), side = qCeil(H - top - F);
    painter->drawImage(QPointF(0, 0), piece(0, w, top));
    if (side > 0) {
        painter->drawImage(QPointF(0, top), piece(1, F, side));
        painter->drawImage(QPointF(W - F, top), piece(2, F, side));
    }
    painter->drawImage(QPointF(0, H - F), piece(3, w, F));
}

void Decoration::paintStyle(QPainter *painter, bool active)
{
    const Look &look = Look::instance();
    const Parts &p = parts();
    const qreal scale = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const int state = active ? 1 : 2;   // CS_ACTIVE / FS_ACTIVE, CS_INACTIVE / FS_INACTIVE
    const int w = qCeil(size().width());
    const int F = look.frame(), C = look.caption();
    if (maximized()) {
        // XP's frame is off the screen; what shows is the caption below it
        // (MaxCaption, where the style has one).
        if (look.hasPart(p.maxCaption, state)) {
            painter->drawImage(QPointF(0, 0), look.part(p.maxCaption, state, QSize(w, C), scale));
        } else {
            painter->save();
            painter->setClipRect(QRectF(0, 0, w, C));
            painter->drawImage(QPointF(0, -F), look.part(p.caption, state, QSize(w, F + C), scale));
            painter->restore();
        }
        return;
    }
    // Each part at the window's size, by XP's own stretching and tiling.
    const int ids[] = {p.caption, p.left, p.right, p.bottom};
    drawEdges(painter, active, [&](int which, int width, int height) { return look.part(ids[which], state, QSize(width, height), scale); });
}

void Decoration::paintClassic(QPainter *painter, bool active)
{
    using Piece = Look::Piece;
    const Look &look = Look::instance();
    const qreal scale = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    if (maximized()) {
        painter->drawImage(QPointF(0, 0), look.classicPiece(Piece::Maximized, active, qCeil(size().width()), scale));
        return;
    }
    const Piece pieces[] = {Piece::Top, Piece::Left, Piece::Right, Piece::Bottom};
    drawEdges(painter, active, [&](int which, int width, int height) {
        return look.classicPiece(pieces[which], active, which == 1 || which == 2 ? height : width, scale);
    });
}

void Decoration::paintCaption(QPainter *painter, bool active)
{
    const QString title = window()->caption();
    if (title.isEmpty() || m_label.width() <= 0)
        return;
    const Look &look = Look::instance();
    painter->save();
    painter->setFont(look.captionFont());
    const QString text = QFontMetricsF(look.captionFont()).elidedText(title, Qt::ElideRight, m_label.width());
    const int flags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine;
    const QColor textColor = look.system().colour(active ? u"TitleText" : u"InactiveTitleText");
    const qreal luminance = 0.2126 * textColor.redF() + 0.7152 * textColor.greenF() + 0.0722 * textColor.blueF();
    // The caption part's shadow under the text, where the style has one.
    if (luminance > 0.5 && look.captionProperty(active, u"textshadowtype").value_or(QString()).compare(QLatin1String("single"), Qt::CaseInsensitive) == 0) {
        QPointF offset(1, 1);
        const QStringList xy = look.captionProperty(active, u"textshadowoffset").value_or(QString()).split(QLatin1Char(','));
        if (xy.size() == 2)
            offset = QPointF(xy[0].trimmed().toInt(), xy[1].trimmed().toInt());
        QRgb shadow = qRgb(128, 128, 128);
        xpl::parseColour(look.captionProperty(active, u"textshadowcolor").value_or(QString()), &shadow);
        painter->setPen(QColor(shadow));
        painter->drawText(m_label.translated(offset), flags, text);
    }
    painter->setPen(textColor);
    painter->drawText(m_label, flags, text);
    painter->restore();
}

// --- the buttons -------------------------------------------------------------------

Button::Button(DecorationButtonType type, Decoration *decoration, QObject *parent)
    : KDecoration3::DecorationButton(type, decoration, parent)
{
}

Button::Button(QObject *parent, const QVariantList &args)
    : KDecoration3::DecorationButton(args.value(0).value<DecorationButtonType>(),
                                     args.value(1).value<KDecoration3::Decoration *>(), parent)
{
}

void Button::paint(QPainter *painter, const QRectF &)
{
    const auto *deco = static_cast<const Decoration *>(decoration());
    const QRectF r = geometry();
    if (type() == DecorationButtonType::Menu) {
        deco->window()->icon().paint(painter, r.toRect());
        return;
    }
    const Look &look = Look::instance();
    const bool active = deco->window()->isActive();
    const bool max = deco->maximized();
    if (look.classic()) {
        using xpl::classic::CaptionGlyph;
        const CaptionGlyph glyph = type() == DecorationButtonType::Close      ? CaptionGlyph::Close
                                   : type() == DecorationButtonType::Minimize ? CaptionGlyph::Minimize
                                   : max                                      ? CaptionGlyph::Restore
                                                                              : CaptionGlyph::Maximize;
        if (type() == DecorationButtonType::ContextHelp) {
            const QPalette pal = look.system().shellPalette(3);
            xpl::classic::pushEdge(painter, r.toRect(), pal, isPressed());
            painter->setPen(pal.color(isEnabled() ? QPalette::ButtonText : QPalette::Dark));
            painter->drawText(r, Qt::AlignCenter, QStringLiteral("?"));
            return;
        }
        xpl::classic::captionButton(painter, r.toRect(), glyph, look.system().shellPalette(glyph == CaptionGlyph::Close ? 4 : 3),
                                    isPressed(), !isEnabled());
        return;
    }
    const Parts &p = parts();
    const int wp = type() == DecorationButtonType::Close      ? p.close
                   : type() == DecorationButtonType::Minimize ? p.min
                   : type() == DecorationButtonType::Maximize ? (max ? p.restore : p.max)
                                                              : p.help;
    // Normal, hot, pushed, disabled; an inactive window's four frames after.
    int state = !isEnabled() ? 4 : isPressed() ? 3 : isHovered() ? 2 : 1;
    if (!active)
        state += 4;
    const qreal scale = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    painter->drawImage(r.topLeft(), look.part(wp, state, r.size().toSize(), scale));
}

} // namespace xpd

K_PLUGIN_FACTORY_WITH_JSON(XPlasmaDecorationFactory, "decoration.json", registerPlugin<xpd::Decoration>(); registerPlugin<xpd::Button>();)

#include "decoration.moc"
