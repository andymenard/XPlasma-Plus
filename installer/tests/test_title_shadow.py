"""Exercise the Aurorae caption-shadow binding with light and dark text."""
import unittest
from pathlib import Path
try:
 from PyQt6 import QtCore,QtGui,QtQml
except ImportError:
 from PySide6 import QtCore,QtGui,QtQml

class CaptionShadow(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.app=QtGui.QGuiApplication.instance() or QtGui.QGuiApplication([])
 def test_dark_caption_disables_shadow_without_disabling_light_captions(self):
  path=Path(__file__).resolve().parents[2]/'native/src/app/overrides/title-shadow.qml'
  if not path.exists():self.skipTest('Native source tree required')
  engine=QtQml.QQmlEngine();component=QtQml.QQmlComponent(engine)
  qml='''import QtQuick
Item {
 id: root
 width: 200; height: 30
 function xpShadow(name) { return {active: "#ff000000", inactive: "#ff000000", x: 1, y: 1}; }
 QtObject { id: auroraeTheme; property string themeName: "test" }
 QtObject { id: decoration; property var client: ({active: true}) }
 Text { id: caption; objectName: "caption"; text: "Title"; color: "white" }
'''+path.read_text()+'\n}'
  component.setData(qml.encode(),QtCore.QUrl())
  self.assertTrue(component.isReady(),[e.toString() for e in component.errors()])
  root=component.create();self.assertIsNotNone(root)
  caption=root.findChild(QtCore.QObject,'caption');shadow=root.findChild(QtCore.QObject,'xplasma-caption-shadow')
  self.assertTrue(shadow.property('visible'))
  caption.setProperty('color',QtGui.QColor('#202020'));self.app.processEvents()
  self.assertFalse(shadow.property('visible'))
  caption.setProperty('color',QtGui.QColor('#ffffff'));self.app.processEvents()
  self.assertTrue(shadow.property('visible'))
  root.deleteLater();self.app.processEvents()

if __name__=='__main__':unittest.main()
