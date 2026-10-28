// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFont>
#include <QFontDatabase>
#include <QStringList>
#include <QtMath>
namespace xpl {
inline QString osUiFamily(const QString &family) {
 return family.compare(QStringLiteral("Tahoma"),Qt::CaseInsensitive)==0?QStringLiteral("Tahoma OS"):family;
}
inline QFont osUiFont(QFont font) {font.setFamily(osUiFamily(font.family()));return font;}

// Pixel designs are opt-in at quarter-step display scales, 100--300%.
// Intermediate scales must retain a vector face rather than stretch pixels.
inline bool crispStandardScale(qreal dpr) {
 return qIsFinite(dpr) && dpr >= 1.0 && dpr <= 3.0
     && qAbs(dpr * 4.0 - qRound(dpr * 4.0)) < 0.0001;
}
inline QFont crispUiFont(QFont font,qreal dpr=1.0,qreal logicalDpi=96.0) {
 const QString requested=font.family();
 const bool supported=requested.compare(QStringLiteral("Tahoma"),Qt::CaseInsensitive)==0
     || requested==QStringLiteral("Tahoma OS") || requested==QStringLiteral("MS Sans Serif")
     || requested==QStringLiteral("MS Sans Serif Bold") || requested.startsWith(QStringLiteral("XPlasma Tahoma Pixel"));
 if(!supported)return font; // Preserve explicit document, code and other UI fonts.
 if(requested==QStringLiteral("MS Sans Serif Bold"))font.setBold(true);
 const qreal pixels=font.pixelSize()>0?font.pixelSize():font.pointSizeF()*logicalDpi/72.0;
 static const QStringList pixelFamilies=[] {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return QFontDatabase::families();
#else
  return QFontDatabase().families();
#endif
 }();
 const int physical=qMax(1,qRound(qMax(1,qRound(pixels))*dpr));
 const QString family=physical==11 ? QStringLiteral("XPlasma Tahoma Pixel")
     : QStringLiteral("XPlasma Tahoma Pixel %1").arg(physical);
 if(crispStandardScale(dpr) && !font.italic() && pixelFamilies.contains(family)) {
  font.setFamily(family);
  font.setPointSizeF(physical/dpr*72.0/logicalDpi);
  font.setStyleStrategy(QFont::NoAntialias);
  font.setKerning(false);
 } else {
  // Preserve the requested size and weight. No pixel rounding between targets.
  font.setFamily(QStringLiteral("Tahoma"));
  font.setStyleStrategy(QFont::PreferAntialias);
 }
 font.setHintingPreference(QFont::PreferFullHinting);
 return font;
}
}
