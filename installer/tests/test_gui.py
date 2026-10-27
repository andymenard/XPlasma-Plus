import importlib.util,sys,tempfile,unittest
from unittest.mock import Mock,patch
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(BASE))
spec=importlib.util.spec_from_file_location('suite_gui',BASE/'gui.py');g=importlib.util.module_from_spec(spec);sys.modules['suite_gui']=g;spec.loader.exec_module(g)
class GuiTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.app=g.QtWidgets.QApplication.instance() or g.QtWidgets.QApplication([])
 def setUp(self):
  self.mock=patch.object(g,'capabilities',return_value={f['id']:'' for f in g.FEATURES});self.mock.start()
  self.window=g.Installer(test_mode=True)
 def tearDown(self):self.mock.stop();self.window.close();self.window.deleteLater();self.app.processEvents()
 def test_all_components_are_selectable(self):
  self.window.select_all(True);self.assertEqual(set(self.window.selected()),{f['id'] for f in g.FEATURES})
  self.window.select_all(False);self.assertEqual(self.window.selected(),[]);self.assertFalse(self.window.install.isEnabled())
 def test_unsupported_feature_is_unchecked_and_cannot_select_all(self):
  self.mock.stop()
  with patch.object(g,'capabilities',return_value={f['id']:('Unsupported series' if f['id']=='taskbar' else '') for f in g.FEATURES}):window=g.Installer(test_mode=True)
  window.select_all(True);self.assertNotIn('taskbar',window.selected());self.assertIn('Unsupported series',window.items['taskbar'].toolTip(0));window.close()
 def test_unavailable_feature_shows_its_reason_when_selected(self):
  self.mock.stop()
  with patch.object(g,'capabilities',return_value={f['id']:('No splash option' if f['id']=='boot' else '') for f in g.FEATURES}):window=g.Installer(test_mode=True)
  item=window.items['boot'];flags=item.flags()
  self.assertTrue(flags&g.QtCore.Qt.ItemFlag.ItemIsEnabled);self.assertFalse(flags&g.QtCore.Qt.ItemFlag.ItemIsUserCheckable)
  self.assertIn('(unavailable)',item.text(0))
  window.tree.setCurrentItem(item);self.assertIn('Unavailable: No splash option',window.description.text())
  # Ticking its group leaves it out.
  item.parent().setCheckState(0,g.QtCore.Qt.CheckState.Checked);self.assertNotIn('boot',window.selected())
  self.assertIsNone(item.data(0,g.QtCore.Qt.ItemDataRole.CheckStateRole));window.close()
 def test_browser_can_be_selected_without_desktop(self):
  self.window.select_all(False);self.window.items['firefox'].setCheckState(0,g.QtCore.Qt.CheckState.Checked)
  self.assertEqual(set(self.window.selected()),{'firefox','firefox_default'});self.assertIn('No login or boot',self.window.summary.text())
 def test_theme_variants_and_system_change_notice(self):
  self.assertEqual(self.window.theme.count(),len(g.THEMES));self.assertGreaterEqual(self.window.theme.findText('Zune'),0)
  self.window.select_all(False);self.window.items['boot'].setCheckState(0,g.QtCore.Qt.CheckState.Checked)
  self.assertIn('authentication',self.window.summary.text())
 def test_installed_features_are_kept_by_an_upgrade(self):
  with tempfile.TemporaryDirectory() as folder:
   state=Path(folder);(state/'installation.json').write_text('{"features":["desktop","taskbar"],"theme":"Royale","asset_options":{}}')
   with patch.object(g,'STATE',state):
    window=g.Installer(test_mode=True)
    self.assertEqual(window.install.text(),'Upgrade installation');self.assertTrue(window.install.isEnabled())
    self.assertEqual(window.theme.currentText(),'Royale')
    window.select_all(False);self.assertEqual(set(window.selected()),{'desktop','taskbar'})
    self.assertFalse(window.items['taskbar'].flags()&g.QtCore.Qt.ItemFlag.ItemIsUserCheckable)
    self.assertIn('an upgrade keeps it',window.items['taskbar'].toolTip(0))
    # Removed: everything is selectable again.
    (state/'installation.json').unlink();window.lock_installed();window.update_summary()
    self.assertEqual(window.install.text(),'Install selected features')
    window.select_all(False);self.assertEqual(window.selected(),[])
    window.close()
 def test_removal_offers_to_delete_backups_unchecked(self):
  box=self.window.removal_question()
  self.assertIn('settings backups',box.checkBox().text());self.assertFalse(box.checkBox().isChecked())
  self.assertIn('IE6 browser profile are kept',box.text())
 def test_choosing_ie6_firefox_makes_it_the_default_too(self):
  self.window.select_all(False);self.window.items['firefox'].setCheckState(0,g.QtCore.Qt.CheckState.Checked)
  self.assertEqual(set(self.window.selected()),{'firefox','firefox_default'})
  self.window.items['firefox_default'].setCheckState(0,g.QtCore.Qt.CheckState.Unchecked);self.assertEqual(self.window.selected(),['firefox'])
  self.window.items['firefox'].setCheckState(0,g.QtCore.Qt.CheckState.Unchecked);self.assertEqual(self.window.selected(),[])
 def test_install_asks_to_close_other_programs_first(self):
  box=self.window.close_programs_question()
  self.assertIn('Please close all other programs',box.text())
  self.assertEqual(box.button(g.QtWidgets.QMessageBox.StandardButton.Ok).text(),'Continue')
  self.assertIsNotNone(box.button(g.QtWidgets.QMessageBox.StandardButton.Cancel))
  # Cancel: nothing starts. (Remove has its own question instead.)
  self.window.test_mode=False
  asked=[]
  def question():
   asked.append(True);answer=g.QtWidgets.QMessageBox(self.window)
   answer.exec=lambda:g.QtWidgets.QMessageBox.StandardButton.Cancel;return answer
  with patch.object(self.window,'close_programs_question',question),patch.object(g.QtCore,'QProcess') as process:
   self.window.start('install');self.window.start('rebuild')
   self.assertEqual(len(asked),2);process.assert_not_called();self.assertIsNone(self.window.process)
   with patch.object(self.window,'removal_question') as removal:
    removal.return_value.exec.return_value=g.QtWidgets.QMessageBox.StandardButton.No
    self.window.start('uninstall')
   self.assertEqual(len(asked),2)
  self.window.test_mode=True
 def test_finished_install_asks_to_log_out_and_back_in(self):
  for action,upgrading,expected in [('install',False,True),('install',True,True),('rebuild',False,True),('uninstall',False,False)]:
   # (A finished backend: its output read, the process handed back.)
   self.window.current_action=action;self.window.upgrading=upgrading;self.window.process=Mock()
   with patch.object(self.window,'read_output'),patch.object(g.QtWidgets.QMessageBox,'information') as shown:self.window.finished(0,None)
   self.assertIsNone(self.window.process)
   message=shown.call_args.args[2]
   self.assertEqual('Please log out and back in for all changes to take effect.' in message,expected,message)
 def test_preview_mode_cannot_execute_installation(self):
  self.window.start('install');self.assertIsNone(self.window.process)
 def test_live_output_is_visible_beside_progress_page(self):
  self.window.pages.setCurrentIndex(1);self.window.show();self.app.processEvents()
  self.window.append_log('Building component 1 of 4')
  self.assertTrue(self.window.terminal_log.isVisible())
  self.assertFalse(self.window.pages.isAncestorOf(self.window.terminal_log))
  self.assertIn('Building component 1 of 4',self.window.terminal_log.toPlainText())
  self.assertTrue(self.window.terminal_log.isReadOnly())

 def test_credits_and_all_licenses_are_readable(self):
  def inspect(dialog):
   selector=dialog.findChild(g.QtWidgets.QComboBox)
   browser=dialog.findChild(g.QtWidgets.QTextBrowser)
   self.assertGreaterEqual(selector.count(),11)
   for i in range(selector.count()):
    selector.setCurrentIndex(i)
    self.assertTrue(browser.toPlainText().strip())
    self.assertNotIn('missing from',browser.toPlainText())
   selector.setCurrentIndex(0)
   self.assertIn('Credits and licenses',browser.toPlainText())
   self.assertNotIn('posting credits',browser.toPlainText())
   return 0
  with patch.object(g.QtWidgets.QDialog,'exec',inspect):self.window.show_credits()
 def test_live_log_does_not_need_details_button(self):
  self.window.append_log('Installation failed: recovery information')
  self.assertIn('recovery information',self.window.terminal_log.toPlainText())
  self.assertFalse(any(b.text() in ['Show details','Hide details'] for b in self.window.findChildren(g.QtWidgets.QPushButton)))

if __name__=='__main__':unittest.main()
