// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/crisptext.h"
#include "app/darkmode.h"
#include "app/paths.h"
#include "app/state.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
namespace xpl::crisptext {
bool apply(Session &session,bool enabled,QString *error) {
 const QString file=paths::config()+QStringLiteral("/fontconfig/xplasma-crisp-text.conf");
 if(enabled) {
  QDir().mkpath(QFileInfo(file).absolutePath());QSaveFile target(file);
  QByteArray xml=R"(<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd"><fontconfig><match target="font"><test name="family" compare="eq"><string>MS Sans Serif</string></test><edit name="antialias" mode="assign"><bool>false</bool></edit><edit name="hinting" mode="assign"><bool>true</bool></edit><edit name="hintstyle" mode="assign"><const>hintfull</const></edit><edit name="rgba" mode="assign"><const>none</const></edit></match><match target="font"><test name="family" compare="eq"><string>MS Sans Serif Bold</string></test><edit name="antialias" mode="assign"><bool>false</bool></edit><edit name="hinting" mode="assign"><bool>true</bool></edit><edit name="hintstyle" mode="assign"><const>hintfull</const></edit><edit name="rgba" mode="assign"><const>none</const></edit></match><match target="font"><test name="family" compare="eq"><string>XPlasma Tahoma Pixel</string></test><edit name="antialias" mode="assign"><bool>false</bool></edit><edit name="hinting" mode="assign"><bool>true</bool></edit><edit name="autohint" mode="assign"><bool>false</bool></edit><edit name="hintstyle" mode="assign"><const>hintfull</const></edit><edit name="rgba" mode="assign"><const>none</const></edit></match></fontconfig>)";
  // Optical pixel faces need the same hard-edge rules in GTK and Firefox.
  const QByteArray rule=xml.mid(xml.indexOf("<match target=\"font\"><test name=\"family\" compare=\"eq\"><string>XPlasma Tahoma Pixel"));
  const QByteArray optical=rule.left(rule.indexOf("</match>")+8);
  for(int size : {14,17,19,22,25,28,30,33}) {
   QByteArray match=optical;QByteArray family("XPlasma Tahoma Pixel ");family+=QByteArray::number(size);family+="</string>";
   match.replace("XPlasma Tahoma Pixel</string>",family);match+="</fontconfig>";
   xml.replace("</fontconfig>",match);
  }
  if(!target.open(QIODevice::WriteOnly)||target.write(xml)!=xml.size()||!target.commit()){if(error)*error=target.errorString();return false;}
 } else if(QFile::exists(file)&&!QFile::remove(file)){if(error)*error=QStringLiteral("Could not remove the crisp-font rule");return false;}
 // Load after KDE's general font settings, which otherwise override smoothing.
 const QString userFile=paths::config()+QStringLiteral("/fontconfig/fonts.conf");
 QFile user(userFile);QString settings;
 if(user.open(QIODevice::ReadOnly))settings=QString::fromUtf8(user.readAll());
 const QString include=QStringLiteral("<include ignore_missing=\"yes\" prefix=\"xdg\">fontconfig/xplasma-crisp-text.conf</include>");
 settings.remove(include);
 if(enabled) {
  if(settings.trimmed().isEmpty())settings=QStringLiteral("<?xml version='1.0'?><fontconfig></fontconfig>");
  const int end=settings.lastIndexOf(QStringLiteral("</fontconfig>"));
  if(end<0){if(error)*error=QStringLiteral("Existing fontconfig file cannot be extended safely");return false;}
  settings.insert(end,include+QStringLiteral("\n"));
 }
 if(!settings.isEmpty()) {
  QSaveFile output(userFile);const QByteArray bytes=settings.toUtf8();
  if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit()){if(error)*error=output.errorString();return false;}
 }
 QFile::remove(paths::config()+QStringLiteral("/fontconfig/conf.d/98-xplasma-crisp.conf"));
 if(!session.writeConfig(QStringLiteral("xplasmarc"),{QStringLiteral("Text")},QStringLiteral("Crisp"),enabled?QStringLiteral("true"):QStringLiteral("false")))return false;
 const QString shared=paths::data()+QStringLiteral("/active/xplasmarc");
 if(QFile::exists(shared))session.writeConfig(shared,{QStringLiteral("Text")},QStringLiteral("Crisp"),enabled?QStringLiteral("true"):QStringLiteral("false"));
 State state=State::load();state.settings.insert(QStringLiteral("crispText"),enabled);if(!state.save(error))return false;
 return darkmode::publish(state.settings.value(QStringLiteral("darkMode")).toBool(),error);
}
}
