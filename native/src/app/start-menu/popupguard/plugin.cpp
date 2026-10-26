// SPDX-License-Identifier: GPL-3.0-or-later
#include <QQmlExtensionPlugin>
#include <qqml.h>
#include <QWindow>
#include <QQuickItem>
#include <QPointer>
#include <QDebug>
#include <QMetaProperty>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
class ResizeGuard : public QObject {
    Q_OBJECT
    Q_PROPERTY(QWindow* window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool bordersWritable READ bordersWritable NOTIFY windowChanged)
    QPointer<QWindow> m_window;
    QList<QPointer<QObject>> m_filters;
    QList<QPair<QPointer<QQuickItem>, qreal>> m_backgrounds;
public:
    using QObject::QObject;
    ~ResizeGuard() override {restore();}
    QWindow* window() const {return m_window;}
    bool bordersWritable() const {
        if (!m_window) return false;
        const int index = m_window->metaObject()->indexOfProperty("borders");
        return index >= 0 && m_window->metaObject()->property(index).isWritable();
    }
    void restore(){
        if(m_window)for(auto filter:m_filters)if(filter)m_window->installEventFilter(filter);
        m_filters.clear();
        for(const auto &entry:m_backgrounds)if(entry.first)entry.first->setOpacity(entry.second);
        m_backgrounds.clear();
    }
    void setWindow(QWindow *window){
        if(window==m_window)return;
        restore();m_window=window;
        if(window)for(auto *child:window->children()){
            if(QByteArray(child->metaObject()->className())=="PlasmaQuick::WindowResizeHandler"){
                window->removeEventFilter(child);m_filters.append(child);
            }
        }
        if(window)for(auto *item:window->findChildren<QQuickItem*>()){
            if(QByteArray(item->metaObject()->className())=="PlasmaQuick::DialogBackground"){
                m_backgrounds.append(qMakePair(QPointer<QQuickItem>(item),item->opacity()));
                item->setOpacity(0);
            }
        }
        if(window && m_backgrounds.isEmpty())qWarning()<<"XP Start menu: no Plasma background layer found";
        if(window && m_filters.isEmpty())qWarning()<<"XP Start menu: no Plasma resize handler found";
        Q_EMIT windowChanged();
    }
Q_SIGNALS:
    void windowChanged();
};
// KRunner's stable D-Bus entry point predates the workspace.dbus QML
// module (absent in Plasma 6.0). Expose only this fixed action to the menu.
class SessionActions : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void showRunner() {
        auto *call = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.krunner"), QStringLiteral("/App"),
                QStringLiteral("org.kde.krunner.App"), QStringLiteral("display"))), this);
        connect(call, &QDBusPendingCallWatcher::finished, this, [](QDBusPendingCallWatcher *watcher) {
            const QDBusPendingReply<> reply = *watcher;
            if (reply.isError())
                qWarning() << "XP Start menu: KRunner call failed:" << reply.error().message();
            watcher->deleteLater();
        });
    }
};
class Plugin : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
public:
    void registerTypes(const char *uri) override {
        qmlRegisterType<ResizeGuard>(uri,1,0,"ResizeGuard");
        qmlRegisterType<SessionActions>(uri,1,0,"SessionActions");
    }
};
#include "plugin.moc"

