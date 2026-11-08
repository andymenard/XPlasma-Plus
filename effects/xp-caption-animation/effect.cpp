// SPDX-License-Identifier: GPL-3.0-or-later
// Independent KWin implementation of classic caption motion. Behavioral
// reference: aubymori's Windhawk classic-min-max-animations (250ms, linear,
// constant caption height; icon and text without caption buttons).
#include "caption.h"
#include <effect/offscreeneffect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <scene/imageitem.h>
#ifdef XP_KWIN_66
#include <scene/itemrenderer.h>
#endif
#include <scene/windowitem.h>
#include <scene/workspacescene.h>
#include <KConfigGroup>
#include <KConfigWatcher>
#include <KSharedConfig>
#include <QElapsedTimer>
#include <QPointer>
#include <QHash>

namespace KWin {
class XPCaptionAnimation : public CrossFadeEffect {
    Q_OBJECT
public:
    XPCaptionAnimation() {
        if(qEnvironmentVariableIsSet("XPLASMA_ANIMATION_TEST"))fprintf(stderr,"XP_CAPTION_CONSTRUCT\n");
#ifdef XP_KWIN_66
        m_caption=effects->scene()->renderer()->createImageItem(effects->scene()->overlayItem());
#else
        m_caption=std::make_unique<ImageItem>(effects->scene()->overlayItem());
#endif
        m_caption->setVisible(false);m_caption->setZ(1000);
        // The caption colours and font, read once and again when kdeglobals' [WM] changes.
        m_config=KSharedConfig::openConfig(QStringLiteral("kdeglobals"));readColours();
        m_watcher=KConfigWatcher::create(m_config);
        connect(m_watcher.data(),&KConfigWatcher::configChanged,this,[this](const KConfigGroup &group){if(group.name()==QLatin1String("WM"))readColours();});
        connect(effects,&EffectsHandler::windowAdded,this,[this](EffectWindow *w){manage(w);});
        connect(effects,&EffectsHandler::windowClosed,this,[this](EffectWindow *w){if(w==m_window)finish();m_before.remove(w);});
        connect(effects,&EffectsHandler::windowDeleted,this,[this](EffectWindow *w){if(w==m_window)finish();m_before.remove(w);});
        for(auto *w:effects->stackingOrder())manage(w);
    }
    ~XPCaptionAnimation() override {finish();}
    void reconfigure(ReconfigureFlags) override {readColours();}
    bool isActive() const override {return !m_window.isNull();}
    // Frames come from the compositor, not a timer: every refresh at the
    // output's own rate, none while nothing is drawn.
    void prePaintScreen(ScreenPrePaintData &data
#ifdef XP_KWIN_66
        ,std::chrono::milliseconds presentTime
#endif
    ) override {
        if(m_window)tick();
        CrossFadeEffect::prePaintScreen(data
#ifdef XP_KWIN_66
            ,presentTime
#endif
        );
    }
    void postPaintScreen() override {
        CrossFadeEffect::postPaintScreen();
        if(!m_window)return;
        if(effects->activeFullScreenEffect()||m_clock.elapsed()>=XP::duration)finish();
        else effects->addRepaint(RectF(m_bar));
    }
    void prePaintWindow(RenderView *view,EffectWindow *w,WindowPrePaintData &data
#ifdef XP_KWIN_66
        ,std::chrono::milliseconds presentTime
#endif
    ) override {
        if(w==m_window){data.setTransformed();data.setTranslucent();}
        CrossFadeEffect::prePaintWindow(view,w,data
#ifdef XP_KWIN_66
            ,presentTime
#endif
        );
    }
#ifdef XP_KWIN_68
    // KWin 6.8: paint calls report whether rendering succeeded.
    using PaintResult=bool;
#define XP_PAINT_SKIPPED true
#define XP_PAINT_RETURN return
#else
    using PaintResult=void;
#define XP_PAINT_SKIPPED
#define XP_PAINT_RETURN
#endif
    PaintResult paintWindow(const RenderTarget &target,const RenderViewport &viewport,EffectWindow *w,int mask,const Region &region,WindowPaintData &data) override {
        if(w==m_window){
            if(m_restoreMin)return XP_PAINT_SKIPPED; // Body becomes visible after the moving caption arrives.
            const QRectF now=w->frameGeometry();
            data.setCrossFadeProgress(0); // Hold the pre-transition snapshot, no crossfade.
            data.setXScale(m_oldGeometry.width()/qMax(qreal(1),now.width()));
            data.setYScale(m_oldGeometry.height()/qMax(qreal(1),now.height()));
            data.setXTranslation(m_oldGeometry.x()-now.x());
            data.setYTranslation(m_oldGeometry.y()-now.y());
        }
        XP_PAINT_RETURN CrossFadeEffect::paintWindow(target,viewport,w,mask,region,data);
    }
private:
    struct Before {QRectF geometry;QRectF caption;};
    QHash<EffectWindow*,Before> m_before;
    QPointer<EffectWindow> m_window;
    std::unique_ptr<ImageItem> m_caption;
    QRectF m_from,m_to,m_oldGeometry;
    QElapsedTimer m_clock;qint64 m_tick=-1;QRectF m_bar;QImage m_label;
    KSharedConfig::Ptr m_config;KConfigWatcher::Ptr m_watcher;
    bool m_minVisible=false,m_restoreMin=false;
    QString m_title;QIcon m_icon;QFont m_font;
    QColor m_left,m_right,m_text; qreal m_scale=1;
    bool eligible(EffectWindow *w) const {return w&&w->hasDecoration()&&!effects->activeFullScreenEffect();}
    QRectF captionRect(EffectWindow *w) const {
        const QRectF frame=w->frameGeometry(),inner=w->contentsRect();
        const qreal edge=qMax(qreal(0),inner.x());
        return {frame.x()+edge,frame.y()+edge,qMax(qreal(1),frame.width()-2*edge),qBound(qreal(16),inner.y()-edge,qreal(48))};
    }
    void manage(EffectWindow *w){
        if(qEnvironmentVariableIsSet("XPLASMA_ANIMATION_TEST"))fprintf(stderr,"XP_CAPTION_MANAGE decorated=%d\n",w->hasDecoration());
        connect(w,&EffectWindow::minimizedChanged,this,[this,w]{
            if(!eligible(w))return;
            QRectF task=w->iconGeometry();
            if(qEnvironmentVariableIsSet("XPLASMA_ANIMATION_TEST")&&task.isEmpty())task=QRectF(300,700,160,30);
            if(task.width()<1||task.height()<1)return;
            const QRectF bar=captionRect(w);QRectF taskBar(task.x(),task.y(),task.width(),bar.height());
            begin(w,w->isMinimized()?bar:taskBar,w->isMinimized()?taskBar:bar,w->frameGeometry(),!w->isMinimized());
        });
        connect(w,&EffectWindow::windowMaximizedStateAboutToChange,this,[this,w]{
            if(!eligible(w)||w->isMinimized())return;
            finish();m_before.insert(w,{w->frameGeometry(),captionRect(w)});redirect(w);
        });
        connect(w,&EffectWindow::windowMaximizedStateChanged,this,[this,w]{
            if(!m_before.contains(w))return;
            const Before old=m_before.take(w);const QRectF to=captionRect(w);
            if(old.geometry==QRectF(w->frameGeometry())){unredirect(w);return;}
            begin(w,old.caption,QRectF(to.x(),to.y(),to.width(),old.caption.height()),old.geometry,false,true);
        });
    }
    void begin(EffectWindow *w,QRectF from,QRectF to,QRectF old,bool restore,bool snapshotted=false){
        if(m_window)finish();
        m_minVisible=w->isMinimized();
        if(m_minVisible)w->windowItem()->refVisible(WindowItem::PAINT_DISABLED_BY_MINIMIZE);
        if(!snapshotted)redirect(w);
        m_window=w;m_oldGeometry=old;m_from=from;m_to=to;m_restoreMin=restore;
        m_title=w->caption();m_icon=w->icon();
        // External theme writers do not necessarily emit KConfig notifications.
        // Refresh once per animation, never once per painted frame.
        readColours();
        m_scale=w->screen()?w->screen()->scale():1;
        m_label=XP::captionLabel(qMax(from.width(),to.width()),from.height(),m_scale,m_title,m_icon,m_font,m_text);
        if(qEnvironmentVariableIsSet("XPLASMA_ANIMATION_TEST"))fprintf(stderr,"XP_CAPTION_START %s %g,%g -> %g,%g h=%g\n",restore?"restore":(m_minVisible?"minimize":"maximize"),from.x(),from.y(),to.x(),to.y(),from.height());
        m_caption->setVisible(true);m_clock.start();m_tick=-1;tick();effects->addRepaintFull();
    }
    void readColours(){
        m_config->reparseConfiguration();
        KConfigGroup wm(m_config,QStringLiteral("WM"));
        m_left=wm.readEntry("activeBackground",QColor(0,84,227));m_right=wm.readEntry("activeBlend",QColor(61,149,255));m_text=wm.readEntry("activeForeground",QColor(Qt::white));
        m_font=wm.readEntry("activeFont",QFont(QStringLiteral("Trebuchet MS"),10,QFont::Bold));
    }
    void tick(){
        // (Once per frame, however many outputs paint it.)
        const qint64 elapsed=qMin<qint64>(m_clock.elapsed(),XP::duration);
        if(elapsed==m_tick)return;
        m_tick=elapsed;
        m_bar=XP::interpolate(m_from,m_to,qreal(elapsed)/XP::duration);
        m_caption->setImage(XP::caption(m_bar.width(),m_bar.height(),m_scale,m_label,!m_icon.isNull(),m_left,m_right));
        m_caption->setPosition(m_bar.topLeft());m_caption->setSize(m_bar.size());
    }
    void finish(){
        if(m_window&&qEnvironmentVariableIsSet("XPLASMA_ANIMATION_TEST"))fprintf(stderr,"XP_CAPTION_END elapsed=%lld\n",m_clock.elapsed());
        m_caption->setVisible(false);m_label=QImage();
        // Clear ownership before releasing references: a closing window can
        // emit windowDeleted synchronously while its final reference drops.
        QPointer<EffectWindow> window=m_window;const bool visibleRef=m_minVisible;
        m_window=nullptr;m_minVisible=false;m_restoreMin=false;
        if(window&&visibleRef)window->windowItem()->unrefVisible(WindowItem::PAINT_DISABLED_BY_MINIMIZE);
        if(window)unredirect(window);
        effects->addRepaintFull();
    }
};
KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(XPCaptionAnimation,"metadata.json",return effects->isOpenGLCompositing();,return false;)
}
#include "effect.moc"
