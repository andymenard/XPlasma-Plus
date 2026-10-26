"""Generic Plasma feature selection and package-manager regressions."""
import json,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch,Mock
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import dependencies as deps
import system_profile as profile
FEATURES=json.loads((ROOT/'features.json').read_text())
BASE={'name':'Test Linux','family':'apt','arch':'amd64','plasma':(6,3,6),'qt':(6,8,2),'writer':'kwriteconfig6','dbus':'qdbus6','kde':True,'boot':'dracut','splash':True,'systemd':True}

class Compatibility(unittest.TestCase):
 def support(self,**changes):
  with patch.object(profile,'screens_manager',return_value=None),patch.object(profile.shutil,'which',return_value='/usr/bin/tool'),patch.object(profile.Path,'exists',return_value=False):
   return profile.capabilities({**BASE,**changes},FEATURES)
 def test_xp_taskbar_ports_cover_existing_plasma_six_branches(self):
  for minor,qt in profile.WIDGET_QT_FLOORS.items():
   with self.subTest(minor=minor):
    support=self.support(plasma=(6,minor,0),qt=qt)
    for feature in ['theme_manager','desktop','apps','taskbar']:self.assertEqual(support[feature],'')
 def test_trixie_uses_packaged_xp_ports_without_enabling_unported_dialogs(self):
  support=self.support()
  self.assertEqual(support['taskbar'],'')
  self.assertIsNone(profile.widget_target(BASE))
  self.assertEqual(profile.widget_series(BASE),'6.3')
  self.assertTrue(support['lock']);self.assertTrue(support['caption_animation'])
 def test_recipe_floors_and_prereleases(self):
  self.assertEqual(profile.widget_target({**BASE,'plasma':(6,7,91),'qt':(6,11,0)}),'6.8')
  self.assertIsNone(profile.widget_target({**BASE,'plasma':(6,8,0),'qt':(6,10,0)}))
  self.assertEqual(profile.widget_target({**BASE,'plasma':(6,5,6),'qt':(6,9,0)}),'6.5')
 def test_unknown_or_too_old_qt_cannot_replace_xp_widgets_with_stock_widgets(self):
  self.assertTrue(self.support(plasma=(6,9,0),qt=(6,12,0))['taskbar'])
  self.assertTrue(self.support(plasma=(6,4,0),qt=(6,7,0))['taskbar'])
 def test_non_systemd_desktop_remains_available(self):
  support=self.support(systemd=False,plasma=(6,7,5),qt=(6,11,2),family='xbps')
  for feature in ['desktop','taskbar','session']:self.assertEqual(support[feature],'')
  self.assertIn('systemd',support['login'])
 def test_other_plasma_major_is_not_enabled(self):
  self.assertTrue(self.support(plasma=(5,27,0))['desktop'])

class Dependencies(unittest.TestCase):
 def test_xbps_bootstrap_has_widget_binding(self):
  self.assertEqual(deps.packages('xbps',[],bootstrap=True),['python3-pyqt6-widgets'])
 def test_xbps_core_does_not_pull_widget_sdk(self):
  packages=deps.packages('xbps',{'desktop','icons'})
  self.assertIn('qt6-base-devel',packages);self.assertIn('python3-Pillow',packages)
  self.assertNotIn('plasma-workspace-devel',packages)
 def test_xbps_taskbar_and_prebuilt_paths(self):
  self.assertIn('plasma-workspace-devel',deps.packages('xbps',{'taskbar'}))
  self.assertNotIn('plasma-workspace-devel',deps.packages('xbps',{'taskbar'},native_ready=True))
  commands=[];deps.install('xbps',{'icons'},commands.append)
  self.assertEqual(commands,[['xbps-install','-Sy','fontconfig','kde-cli-tools','python3-Pillow','qt6-tools']])
 def test_apt_refresh_precedes_t64_resolution(self):
  commands=[]
  def policy(args,**kwargs):
   self.assertEqual(commands,[['apt-get','update']])
   return args[-1]+':\n  Candidate: 1.0\n'
  with patch.object(deps.subprocess,'check_output',side_effect=policy):deps.install('apt',{'firefox'},commands.append)
  self.assertIn('libgtk-3-0t64',commands[1]);self.assertEqual(commands[1][:3],['apt-get','install','--assume-yes'])
 def test_apt_unavailable_package_is_named_before_install(self):
  commands=[]
  def policy(args,**kwargs):return '  Candidate: '+('(none)' if args[-1]=='fontconfig' else '1.0')+'\n'
  with patch.object(deps.subprocess,'check_output',side_effect=policy),self.assertRaisesRegex(RuntimeError,'fontconfig'):
   deps.install('apt',[],commands.append)
  self.assertEqual(commands,[['apt-get','update']])
 def test_apt_legacy_library_names_are_resolved(self):
  commands=[]
  def policy(args,**kwargs):return '  Candidate: '+('(none)' if args[-1].endswith('t64') else '1.0')+'\n'
  with patch.object(deps.subprocess,'check_output',side_effect=policy):deps.install('apt',{'firefox'},commands.append)
  self.assertIn('libgtk-3-0',commands[1]);self.assertNotIn('libgtk-3-0t64',commands[1])

if __name__=='__main__':unittest.main()
