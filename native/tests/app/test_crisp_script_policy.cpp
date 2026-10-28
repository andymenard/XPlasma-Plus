// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QJSEngine>
#include <QFile>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <cmath>
static void check(bool b,const QString &m){if(!b)qFatal("%s",qPrintable(m));}
static QString read(const QString &f){QFile file(f);check(file.open(QIODevice::ReadOnly),f);return QString::fromUtf8(file.readAll());}
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);const QString root=QStringLiteral(XPLASMA_REPOSITORY_ROOT);
 const QString cfg=read(root+"/browser/ie6/runtime/ie6-ui.cfg");
 const QString firefox=cfg.mid(cfg.indexOf(" function setDarkMode("),cfg.indexOf(" function initializeMenuBar(")-cfg.indexOf(" function setDarkMode("));
 const QString qml=read(root+"/tools/widget-text/XpTextStyle.qml");
 const QString methods=qml.mid(qml.indexOf("    function supports("),qml.indexOf("    function read()")-qml.indexOf("    function supports("));
 QJSEngine f,q;
 check(!f.evaluate("var Components={interfaces:{nsIBaseWindow:{}}};var overrideScale='-1';var Services={prefs:{getCharPref:()=>overrideScale}};var enabled=true;function readAppearance(){return {crispText:enabled,darkMode:false};}"+firefox+R"(
 function select(dpr, requested="-1", nativeScale=dpr) {overrideScale=requested;let attributes={},properties={};let win={docShell:{treeOwner:{QueryInterface:()=>({devicePixelsPerDesktopPixel:nativeScale})}},devicePixelRatio:dpr,document:{documentElement:{setAttribute:(k,v)=>attributes[k]=v,style:{setProperty:(k,v)=>properties[k]=v}},querySelector:()=>null}};setDarkMode(win);return {attributes,properties};}
 )").isError(),"Firefox syntax");
 QJsonArray families;families.append("Tahoma");families.append("Tahoma OS");families.append("XPlasma Tahoma Pixel");
 for(int n:{14,17,19,22,25,28,30,33})families.append(QString("XPlasma Tahoma Pixel %1").arg(n));
 const QString pre="var crispEnabled=true;var families="+QString::fromUtf8(QJsonDocument(families).toJson(QJsonDocument::Compact))+";var Qt={font:x=>x};var Font={PreferFullHinting:1,PreferDefaultHinting:0};";
 check(!q.evaluate(pre+methods).isError(),"QML policy syntax");
 for(double dpr:{0.75,1.,1.1,1.25,1.3,1.5,1.6,1.75,1.8,2.,2.1,2.25,2.4,2.5,2.625,2.75,2.8,3.,3.25}){
  const bool pixel=dpr>=1&&dpr<=3&&std::abs(dpr*4-std::round(dpr*4))<0.0001;
  const int size=int(std::floor(11*dpr+.5));
  const QString expected=pixel?(size==11?"XPlasma Tahoma Pixel":QString("XPlasma Tahoma Pixel %1").arg(size)):"Tahoma";
  const auto ff=f.evaluate(QString("select(%1)").arg(dpr));
  check(!ff.isError(),"Firefox policy execution");
  check(ff.property("attributes").property("xplasma-crisp-pixels").toString()==(pixel?"true":"false"),"Firefox pixel selection");
  check(ff.property("properties").property("--xplasma-crisp-family").toString().contains(expected),"Firefox family");
  const double ffSize=ff.property("properties").property("--xplasma-crisp-size").toString().remove("px").toDouble();
  check(std::abs(ffSize-(pixel?size/dpr:11))<0.00001,"Firefox preserves fallback size");
  const auto family=q.evaluate(QString("family('Tahoma',11,%1)").arg(dpr));
  check(family.toString()==expected,"QML family at "+QString::number(dpr));
  const auto font=q.evaluate(QString("font('Tahoma',8,%1)").arg(dpr));
  check(font.property("family").toString()==expected,"QML input font");
  check(std::abs(font.property("pointSize").toNumber()-(pixel?size/dpr*72/96:8))<0.00001,"QML fallback size");
 }
 // Firefox exposes 275% as 60/22 after layout-unit quantization.
 for(const QString &expression:{QString("select(60/22, '2.75')"),QString("select(60/22, '-1', 2.75)"),QString("select(60/22, '-1', 1)")}) {
  const auto value=f.evaluate(expression);
  check(value.property("attributes").property("xplasma-crisp-pixels").toString()=="true","Quantized 275% detection");
  check(value.property("properties").property("--xplasma-crisp-family").toString().contains("Pixel 30"),"275% must use the 30px face");
 }
 for(const QString &expression:{QString("select(60/22, '2.74')"),QString("select(60/22, '-1', 2.74)")})
  check(f.evaluate(expression).property("attributes").property("xplasma-crisp-pixels").toString()=="false","Explicit intermediate scales must remain vectors");
 check(q.evaluate("family('monospace',11,2.75)").toString()=="monospace","Preserve code fonts");
 check(q.evaluate("family('Tahoma',13,1.25)").toString()=="Tahoma","Unavailable QML design uses vectors");
 q.evaluate("crispEnabled=false");check(q.evaluate("family('Tahoma',11,2.75)").toString()=="Tahoma OS","Toggle restores UI family");
 f.evaluate("enabled=false");check(f.evaluate("select(2.75).attributes['xplasma-crisp']").toString()=="false","Firefox toggle");
 QTextStream(stdout)<<"PASS: Firefox and QML selection agree at 19 scales, including 275%, 300%, intermediate sizes and crisp toggle.\n";
}
