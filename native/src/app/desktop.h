// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Whether this is a desktop XPlasma can dress: a KDE Plasma session, a Plasma
// from the oldest XPlasma is tested on, the programs applying a look runs.
// Setup checks it before installing, xplasma and xplasma-edit before
// changing anything; a Plasma newer than XPlasma's newest build of Plasma's
// widgets is let through (it may well take them), but marked, so a failure
// there asks for a bug report, with diagnostics() to paste in it.
#include "app/session.h"

#include <QList>
#include <QString>

namespace xpl::desktop {

// XP widget ports cover the packaged-QML Plasma 6.0+ layouts; compiled
// replacements and their library ABI are checked separately before apply.
inline constexpr const char *kOldestPlasma = "6.0";
// The newest Plasma series XPlasma has builds of Plasma's widgets for
// (overrides/forks.json): "6.7".
QString newestKnownPlasma();

struct Requirement
{
    QString what;      // "KDE Plasma session"
    QString found;     // what's here: "Plasma 6.7.5", "GNOME", the program's path
    bool met = false;
};

struct Report
{
    QList<Requirement> requirements;
    QString plasma;      // the running Plasma's version, if it has one
    bool newer = false;  // newer than newestKnownPlasma()
    bool ok() const;
    // The unmet requirements, a line each, for a message.
    QString problems() const;
};

// This session, as it is.
Report check(Session &session);

// What a bug report needs: XPlasma's version and builds, Plasma, KDE
// Frameworks, Qt, the distribution, the session, the kernel -- and, if
// given, what failed.
QString diagnostics(Session &session, const QString &failure = QString());

} // namespace xpl::desktop
