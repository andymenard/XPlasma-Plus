// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/crispfont.h"
#include <QGuiApplication>
#include <QFontDatabase>
#include <QFontInfo>
#include <QImage>
#include <QPainter>
#include <QDebug>
#include <QTextStream>
#include <QDir>
#include <cmath>
#include <cstdlib>

static void require(bool result, const char *message) {
 if(!result) {qCritical()<<message;std::exit(1);}
}
int main(int argc,char **argv) {
 QGuiApplication app(argc,argv);
 const QDir fonts(QStringLiteral(XPLASMA_REPOSITORY_ROOT "/non-free/payload/assets/fonts"));
 for(const QString &file:fonts.entryList({"xplasma-tahoma-pixel*.ttf"}))
  require(QFontDatabase::addApplicationFont(fonts.filePath(file))>=0,"Cannot load pixel font");
 QImage sheet(1450,1200,QImage::Format_RGB32);sheet.fill(Qt::white);
 QPainter preview(&sheet);int row=0;
 int tested=0;
 for(int quarter=4;quarter<=12;quarter++) {
  const qreal dpr=quarter/4.0;
  const int size=qRound(11*dpr);
  for(bool bold:{false,true}) {
   QFont original("Tahoma",8);original.setBold(bold);
   const QFont selected=xpl::crispUiFont(original,dpr);
   const QString expected=size==11?"XPlasma Tahoma Pixel":QString("XPlasma Tahoma Pixel %1").arg(size);
   require(selected.family()==expected,"Wrong pixel face at standard scale");
   require(selected.bold()==bold,"Weight was changed");
   require(qAbs(selected.pointSizeF()*96/72*dpr-size)<0.00001,"Wrong physical size");
   QImage image(1100,90,QImage::Format_RGB32);image.fill(Qt::white);
   image.setDotsPerMeterX(qRound(96/0.0254));image.setDotsPerMeterY(qRound(96/0.0254));
   image.setDevicePixelRatio(dpr);
   {QPainter painter(&image);painter.setPen(Qt::black);painter.setFont(selected);
    painter.drawText(QPointF(0,52/dpr),"XPlasma — File Edit View 0123456789 AaBb");}
   int ink=0,gray=0;
   for(int y=0;y<image.height();y++)for(int x=0;x<image.width();x++) {
    const QRgb c=image.pixel(x,y);if(c!=qRgb(255,255,255)) {ink++;if(c!=qRgb(0,0,0))gray++;}
   }
   require(ink>10,"No glyphs were rendered");require(gray==0,"Gray edges in a pixel target");
   // Compose actual device pixels without rescaling the rendered samples.
   image.setDevicePixelRatio(1);
   preview.setFont(QFont("sans-serif",9));preview.setPen(Qt::black);
   preview.drawText(5,row*60+29,QString("%1% %2 / %3px").arg(dpr*100).arg(bold?"bold":"regular").arg(size));
   preview.drawImage(210,row*60,image);
   row++;tested++;
  }
 }
 for(double dpr:{0.75,1.01,1.1,1.2,1.3,1.4,1.6,1.8,2.1,2.6,2.74,2.8,2.99,3.1}) {
  QFont original("Tahoma",8);original.setBold(true);
  const QFont f=xpl::crispUiFont(original,dpr);
  require(f.family()=="Tahoma","Intermediate scale stretched a pixel face");
  require(f.pointSizeF()==original.pointSizeF() && f.bold(),"Fallback changed size or weight");tested++;
 }
 QFont larger("Tahoma",10);require(xpl::crispUiFont(larger,1.0).family()=="Tahoma","Missing design must use vectors");
 QFont italic("Tahoma",8);italic.setItalic(true);require(xpl::crispUiFont(italic,1.5).italic(),"Italic was lost");
 require(xpl::crispUiFont(italic,1.5).family()=="Tahoma","Italic must use real vector face");
 QFont mono("monospace",13);require(xpl::crispUiFont(mono,1.5)==mono,"Document/code font changed");
 preview.end();if(argc>1)require(sheet.save(QString::fromLocal8Bit(argv[1])),"Cannot save preview");
 QTextStream(stdout)<<"PASS: "<<tested<<" scale/weight cases; physical pixels, vector fallback, missing design, italic and code-font preservation\n";
}
