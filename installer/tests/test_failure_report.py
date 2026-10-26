import importlib.util,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
spec=importlib.util.spec_from_file_location('failure_gui',ROOT/'gui.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)

class FailureReport(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.app=g.QtWidgets.QApplication.instance() or g.QtWidgets.QApplication([])
 def test_report_shows_apt_error_and_recovery_and_can_be_copied(self):
  with tempfile.TemporaryDirectory() as folder,patch.object(g,'STATE',Path(folder)/'installation'),patch.object(g.Path,'home',return_value=Path(folder)):
   window=g.Installer(test_mode=True)
   try:
    window.current_action='install';window.attempted_features=['desktop']
    window.append_log('E: Unable to locate package example')
    window.append_log('Restored '+folder+'/.config/kdeglobals')
    # An error near the beginning must survive the visible log's 2000-line cap.
    for i in range(2100):window.append_log('Recovery step '+str(i))
    def inspect(dialog):
     text=dialog.findChild(g.QtWidgets.QPlainTextEdit,'failureReport').toPlainText()
     self.assertIn('E: Unable to locate package example',text)
     self.assertIn('Restored ~/.config/kdeglobals',text);self.assertNotIn(folder,text)
     self.assertIn('Recovery step 2099',text);self.assertIn('Exit code: 1',text)
     copy=next(b for b in dialog.findChildren(g.QtWidgets.QPushButton) if b.text()=='Copy report')
     copy.click();self.assertEqual(self.app.clipboard().text(),text)
     return 0
    with patch.object(g.QtWidgets.QDialog,'exec',inspect):window.show_failure_report(1)
    saved=Path(folder)/'.local/state/xplasma-plus/last-failure.log'
    self.assertIn('Unable to locate package',saved.read_text())
    self.assertEqual(saved.stat().st_mode&0o777,0o600)
   finally:window.close();window.deleteLater();self.app.processEvents()

if __name__=='__main__':unittest.main()
