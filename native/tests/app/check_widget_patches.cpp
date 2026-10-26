// SPDX-License-Identifier: GPL-3.0-or-later
// Apply the production patcher to pinned sources from every Plasma 6 branch.
#include "app/overrides.h"
#include "app/forks.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() != 4) {
        QTextStream(stderr) << "usage: check-widget-patches MANIFEST BLOBS OUTPUT\n";
        return 2;
    }
    QFile manifest(args[1]);
    if (!manifest.open(QIODevice::ReadOnly)) return 2;
    const QJsonObject branches = QJsonDocument::fromJson(manifest.readAll()).object().value("branches").toObject();
    if (branches.isEmpty()) return 2;
    int checked = 0, failed = 0;
    for (auto branch = branches.begin(); branch != branches.end(); ++branch) {
        const auto recipe = xpl::forks::series(branch.key());
        QHash<QString, QString> outputs;
        for (const QJsonValue &entry : branch.value().toObject().value("files").toArray()) {
            const QJsonObject spec = entry.toObject();
            const QString key = spec.value("key").toString();
            const QByteArray sha = spec.value("blob").toString().toLatin1();
            QFile source(QDir(args[2]).filePath(QString::fromLatin1(sha)));
            QString why;
            if (!source.open(QIODevice::ReadOnly)) {
                QTextStream(stderr) << branch.key() << ' ' << key << ": missing source\n";
                ++failed;
                continue;
            }
            const QByteArray bytes = source.readAll();
            const QByteArray blob = "blob " + QByteArray::number(bytes.size()) + '\0' + bytes;
            if (QCryptographicHash::hash(blob, QCryptographicHash::Sha1).toHex() != sha) {
                QTextStream(stderr) << branch.key() << ' ' << key << ": upstream blob mismatch\n";
                ++failed;
                continue;
            }
            const QJsonArray *rules = nullptr;
            if (recipe) {
                const auto own = recipe->rules.constFind(key);
                if (own != recipe->rules.cend()) rules = &own.value();
            }
            auto patched = xpl::overrides::patched(key, QString::fromUtf8(bytes), &why, rules);
            if (!patched) {
                QTextStream(stderr) << branch.key() << ' ' << key << ": " << why << '\n';
                ++failed;
                continue;
            }
            patched->replace(QStringLiteral("@LAUNCH@"), QStringLiteral("\"xplasma-edit\""));
            const QString path = QDir(args[3]).filePath(branch.key() + '/' + key);
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile output(path);
            if (!output.open(QIODevice::WriteOnly) || output.write(patched->toUtf8()) < 0) return 2;
            outputs.insert(key, *patched);
            ++checked;
        }
        // Applying every anchor is insufficient if a required XP component
        // was accidentally made optional. Assert behavior markers as well.
        const QMultiHash<QString, QString> required{
            {"taskmanager/main.qml", "xpTaskband"},
            {"taskmanager/Task.qml", "xpTaskband"},
            {"taskmanager/code/layoutmetrics.js", "return 160;"},
            {"systemtray/main.qml", "id: activeModel"},
            {"systemtray/items/AbstractItem.qml", "root.xpRevealHidden(itemId)"},
            {"notifications/global/Globals.qml", "globals.xpAdvance()"},
            {"notifications/main.qml", "Globals.xpBalloons"},
            {"panel/main.qml", "xpColours"},
            {"desktopcontainment/FolderItemDelegate.qml", "XP desktop icons have no shadow"},
            {"desktopcontainment/FolderItemDelegate.qml", "XPlasma: XP desktop icon selection:"},
        };
        for (auto item = required.begin(); item != required.end(); ++item) {
            if (!outputs.value(item.key()).contains(item.value())) {
                QTextStream(stderr) << branch.key() << ' ' << item.key() << ": missing XP behavior " << item.value() << '\n';
                ++failed;
            }
        }
        if (!outputs.value("systemtray/main.qml").contains("&& !root.xpRevealHidden(")
            && !outputs.value("systemtray/HiddenItemsView.qml").contains("&& !root.xpRevealHidden(")) {
            QTextStream(stderr) << branch.key() << ": hidden tray filter not adapted\n";
            ++failed;
        }
    }
    QTextStream(stdout) << checked << " production-patched files across " << branches.size()
                        << " Plasma branches; " << failed << " failures\n";
    return failed ? 1 : 0;
}
