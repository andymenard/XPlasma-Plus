"""Load the real logout QML with the older greeter's context and check signals."""
import shutil,tempfile,unittest
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]

class LogoutDialogTests(unittest.TestCase):
 def test_older_greeter_context_and_power_actions(self):
  from PyQt6 import QtCore,QtGui,QtQuick,QtTest
  app=QtGui.QGuiApplication.instance() or QtGui.QGuiApplication([])
  source=BASE/'payload/templates/session/contents/logout'
  if not source.exists():source=BASE.parent/'themes/xp-session/contents/logout'
  with tempfile.TemporaryDirectory() as tmp:
   folder=Path(tmp);shutil.copytree(source,folder/'logout')
   assets=folder/'logout/assets';assets.mkdir(exist_ok=True)
   # Synthetic art keeps the regression independent of private XP raster files.
   for name in ['20100.png','20141.png','20140.png','20150.png','14356.png']:
    image=QtGui.QImage(320,240,QtGui.QImage.Format.Format_ARGB32);image.fill(QtGui.QColor('blue'));image.save(str(assets/name))
   wrapper=folder/'Test.qml'
   wrapper.write_text('''import QtQuick
import org.kde.plasma.private.sessions
Item {
 width: 1280; height: 800
 property var screenGeometry: Qt.rect(0,0,1280,800)
 property int sdtype: ShutdownType.ShutdownTypeNone
 property bool maysd: true
 property bool canLogout: true
 property bool softwareUpdatePending: false
 property bool rebootToFirmwareSetup: false
 property bool rebootToBootLoaderMenu: false
 property var spdMethods: ({SuspendState:true,HibernateState:true})
 function i18nd(domain,text) { return text; }
 function i18ndc(domain,context,text) { return text; }
 function i18ndp(domain,one,many,n) { return n===1?one:many; }
 Loader { id: loader; source: "logout/Logout.qml" }
}''')
   view=QtQuick.QQuickView();warnings=[]
   view.engine().warnings.connect(lambda errors:warnings.extend(error.toString() for error in errors))
   view.rootContext().setContextProperty('ShutdownType',{'ShutdownTypeNone':0,'ShutdownTypeReboot':1,'ShutdownTypeHalt':2,'ShutdownTypeDefault':-1})
   view.setSource(QtCore.QUrl.fromLocalFile(str(wrapper)));view.show();app.processEvents()
   self.assertNotEqual(view.status(),QtQuick.QQuickView.Status.Error,view.errors())
   root=view.rootObject().findChild(QtCore.QObject,'root')
   self.assertIsNotNone(root,'Logout QML did not load with the Plasma 6.3 context')
   self.assertFalse(root.property('powerMode'))
   view.rootObject().setProperty('sdtype',2);app.processEvents()
   self.assertTrue(root.property('powerMode'))
   for button,signal in [('cancelButton','cancelRequested()'),('logoutButton','logoutRequested()'),('shutdownButton','haltRequested()'),('rebootButton','rebootRequested()'),('suspendButton','suspendRequested(int)')]:
    with self.subTest(button=button):
     method=root.metaObject().method(root.metaObject().indexOfSignal(signal))
     spy=QtTest.QSignalSpy(root,method)
     item=root.findChild(QtCore.QObject,button);self.assertIsNotNone(item)
     QtCore.QMetaObject.invokeMethod(item,'clicked')
     self.assertEqual(len(spy),1)
   self.assertEqual(warnings,[])
   view.close();view.deleteLater();app.processEvents()

if __name__=='__main__':unittest.main()
