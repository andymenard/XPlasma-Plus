"""Installing over an installation upgrades it in place."""
import importlib.util,json,os,sys,tempfile,types,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
def load(name,file):
 spec=importlib.util.spec_from_file_location(name,ROOT/file)
 module=importlib.util.module_from_spec(spec)
 with patch.dict(sys.modules,{'tomlkit':types.SimpleNamespace()}):spec.loader.exec_module(module)
 return module
b=load('upgrade_backend','backend.py')
r=load('upgrade_root_helper','root_helper.py')

class Upgrade(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.home=Path(self.temp.name)/'home';self.home.mkdir()
  self.bundle=Path(self.temp.name)/'bundle'
  for name in ['payload/templates','payload/tools']:(self.bundle/name).mkdir(parents=True)
  self.version('1.0.0-alpha.7')
  self.kdeglobals=self.home/'.config/kdeglobals';self.kdeglobals.parent.mkdir(parents=True)
  self.kdeglobals.write_text('[General]\nfont=Noto Sans,10\n')
  self.calls=[];self.release='v1';self.broken=False
  for target,value in {'HOME':str(self.home)}.items():
   patcher=patch.dict(os.environ,{target:value});patcher.start();self.addCleanup(patcher.stop)
  stubs={'BASE':self.bundle,'preflight':lambda selected,installed=():None,'verify':lambda:None,'root':self.root,
         'gnome':lambda:{},'run':lambda *a,**k:'','detect':lambda:{'family':None,'plasma':(6,6,4),'qt':(6,10,2),'systemd':True},'desktop_path':lambda home:home/'Desktop',
         'prepare_native':self.native,'basic':self.basic,'verify_taskbar':lambda:None,'desktop_updates':lambda *a,**k:None,
         'taskbar_defaults':lambda home,selected:self.calls.append('taskbar_defaults')}
  for name,value in stubs.items():
   patcher=patch.object(b,name,value);patcher.start();self.addCleanup(patcher.stop)
  self.addCleanup(self.temp.cleanup)
 def version(self,version):(self.bundle/'edition.json').write_text(json.dumps({'version':version}))
 def root(self,action,selected=(),**options):self.calls.append(('root',action,options.get('upgrade',False)))
 def native(self,home,state,selected,theme,asset_options=None,keep_theme=False):
  self.calls.append(('native',keep_theme,(asset_options or {}).get('dark_mode')))
 def basic(self,home,selected,native=False,asset_options=None):
  # Each release writes its own settings, and this one a file of its own.
  self.kdeglobals.write_text('[General]\nfont=Tahoma OS,8\nrelease='+self.release+'\n')
  (home/'.local/share/xplasma').mkdir(parents=True,exist_ok=True)
  (home/'.local/share/xplasma'/(self.release+'.txt')).write_text(self.release)
  if self.broken:raise RuntimeError('step failed')
 def state(self):return self.home/'.local/share/kde-xp-suite'
 def record(self):return json.loads((self.state()/'installation.json').read_text())

 def install_v1(self):
  b.install({'desktop','taskbar'},'Luna Blue')
  self.assertIn('release=v1',self.kdeglobals.read_text())

 def test_upgrade_keeps_the_original_backup_and_records_the_release(self):
  self.install_v1()
  self.release='v2';self.version('1.0.0-alpha.8');self.calls.clear()
  b.install({'desktop','taskbar','sounds'},'Luna Blue')
  self.assertIn('release=v2',self.kdeglobals.read_text())
  # Remove still restores what was there before XP, not v1's desktop.
  self.assertEqual((self.state()/'backup/.config/kdeglobals').read_text(),'[General]\nfont=Noto Sans,10\n')
  record=self.record()
  self.assertEqual(record['features'],['desktop','sounds','taskbar'])
  self.assertEqual(record['edition'],{'version':'1.0.0-alpha.8'})
  self.assertEqual(record['installed_edition'],{'version':'1.0.0-alpha.7'})
  self.assertEqual(record['upgrades'],[{'from':{'version':'1.0.0-alpha.7'},'to':{'version':'1.0.0-alpha.8'}}])
  # The current theme is reapplied, and the Quick Launch isn't seeded again.
  self.assertIn(('native',True,None),self.calls)
  self.assertNotIn('taskbar_defaults',self.calls)
  self.assertFalse((self.state()/'upgrade').exists())
  self.assertEqual(json.loads((self.state()/'installer/edition.json').read_text()),{'version':'1.0.0-alpha.8'})
  b.uninstall()
  self.assertEqual(self.kdeglobals.read_text(),'[General]\nfont=Noto Sans,10\n')
  self.assertFalse((self.home/'.local/share/xplasma/v1.txt').exists())
  self.assertFalse((self.home/'.local/share/xplasma/v2.txt').exists())

 def test_failed_upgrade_returns_to_the_installed_release(self):
  self.install_v1()
  before=self.record()
  self.release='v2';self.broken=True;self.version('1.0.0-alpha.8')
  with self.assertRaises(RuntimeError):b.install({'desktop','taskbar'},'Luna Blue')
  self.assertIn('release=v1',self.kdeglobals.read_text())
  self.assertTrue((self.home/'.local/share/xplasma/v1.txt').exists())
  self.assertFalse((self.home/'.local/share/xplasma/v2.txt').exists())
  # Still installed, as it was: nothing marks it incomplete.
  self.assertEqual(self.record(),before)
  self.assertFalse((self.state()/'incomplete.json').exists())
  b.uninstall()
  self.assertEqual(self.kdeglobals.read_text(),'[General]\nfont=Noto Sans,10\n')

 def test_upgrade_keeps_dark_mode_set_in_properties(self):
  self.install_v1()
  settings=self.home/'.local/share/xplasma/state.json'
  settings.write_text(json.dumps({'settings':{'darkMode':True}}))
  self.release='v2';self.calls.clear()
  b.install({'desktop','taskbar'},'Luna Blue',{'dark_mode':False})
  self.assertIn(('native',True,True),self.calls)
  self.assertTrue(self.record()['asset_options']['dark_mode'])

 def test_upgrade_cannot_drop_installed_features(self):
  self.install_v1()
  with self.assertRaisesRegex(RuntimeError,'Remove installed features first to drop: taskbar'):b.install({'desktop'},'Luna Blue')
  self.assertIn('release=v1',self.kdeglobals.read_text())

 def test_new_tracked_paths_join_the_original_backup(self):
  self.install_v1()
  state=self.state()
  tracked=json.loads((state/'tracked.json').read_text())
  (self.home/'.config/newrc').write_text('mine\n')
  snapshot=b.Snapshot(self.home,state,tracked+['.config/newrc'])
  snapshot.extend(tracked+['.config/newrc'])
  before=json.loads((state/'before.json').read_text())
  self.assertIn('.config/newrc',before)
  self.assertEqual((state/'backup/.config/newrc').read_text(),'mine\n')
  # (Paths it already had keep their original state.)
  self.assertEqual((state/'backup/.config/kdeglobals').read_text(),'[General]\nfont=Noto Sans,10\n')

 def test_failed_upgrade_takes_back_screens_it_added(self):
  self.install_v1()
  self.release='v2';self.broken=True;self.calls.clear()
  with self.assertRaises(RuntimeError):b.install({'desktop','taskbar','login'},'Luna Blue')
  self.assertIn(('root','restore',False),self.calls)
  self.assertEqual(self.record()['features'],['desktop','taskbar'])

 def test_upgrade_from_a_release_with_the_retired_updater(self):
  self.install_v1()
  record=self.record();record['features']+=['updates'];(self.state()/'installation.json').write_text(json.dumps(record))
  self.release='v2'
  b.install({'desktop','taskbar'},'Luna Blue')
  self.assertEqual(self.record()['features'],['desktop','taskbar'])

 def test_newly_tracked_xp_settings_are_not_kept_as_originals(self):
  self.install_v1()
  state=self.state()
  tracked=[p for p in json.loads((state/'tracked.json').read_text()) if p!='.gtkrc-2.0']
  (state/'tracked.json').write_text(json.dumps(tracked))
  (self.home/'.gtkrc-2.0').write_text('gtk-theme-name="XPlasma-xplasma-v2-aaa"\n')
  self.release='v2';b.install({'desktop','taskbar'},'Luna Blue')
  self.assertNotIn('.gtkrc-2.0',json.loads((state/'before.json').read_text()))
  b.uninstall()
  self.assertFalse((self.home/'.gtkrc-2.0').exists())

 def test_system_screens_upgrade_through_the_helper(self):
  self.install_v1()
  self.release='v2';self.calls.clear()
  record=self.record();record['features']+=['login'];(self.state()/'installation.json').write_text(json.dumps(record))
  b.install({'desktop','taskbar','login'},'Luna Blue')
  self.assertIn(('root','screens',True),self.calls)

class LockScreenCapability(unittest.TestCase):
 def test_own_lock_screen_shell_stays_upgradable(self):
  import system_profile
  profile={'plasma':(6,7,5),'writer':'kwriteconfig6','kde':True,'dbus':'qdbus6','qt':(6,11,0),'family':'apt','arch':'amd64','systemd':True,'boot':None,'splash':False}
  with tempfile.TemporaryDirectory() as home,patch.dict(os.environ,{'HOME':home}):
   (Path(home)/'.local/share/plasma/shells/org.kde.plasma.desktop').mkdir(parents=True)
   self.assertTrue(system_profile.capabilities(profile,b.FEATURES)['lock'])
   state=Path(home)/'.local/share/kde-xp-suite';state.mkdir(parents=True)
   (state/'installation.json').write_text(json.dumps({'features':['lock']}))
   self.assertEqual(system_profile.capabilities(profile,b.FEATURES)['lock'],'')

class SystemScreens(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
  base=Path(self.temp.name);self.store=base/'var/kde-xp-suite/1000';self.home=base/'home'
  self.system=base/'system';self.config=self.system/'sddm.conf.d/90-xp-welcome.conf';self.theme=self.system/'themes/xp-welcome'
  templates=self.home/'.local/share/kde-xp-suite/templates'
  for name in ['sddm','login-cursor','login-status']:(templates/name).mkdir(parents=True)
  for name in ['xp-logon-status','xp-logon-status.desktop','xp-logonui.conf']:(templates/'login-status'/name).write_text(name)
  self.templates=templates
  login=[str(self.config),str(self.theme),str(self.system/'icons'),str(self.system/'status'),str(self.system/'status.desktop'),str(self.system/'tmpfiles.conf')]
  self.theme.mkdir(parents=True);(self.theme/'Main.qml').write_text('stock')
  for name,value in {'PATHS':{'login':login,'boot':[]},'LOGIN_STATUS':[('xp-logon-status',login[3],0o755),('xp-logon-status.desktop',login[4],0o644),('xp-logonui.conf',login[5],0o644)],
                     'run':lambda args:None,'greeter_environment':lambda:'QML_XHR_ALLOW_FILE_READ=1','detect':lambda:{'family':None,'plasma':(6,6,4),'qt':(6,10,2),'systemd':True}}.items():
   patcher=patch.object(r,name,value);patcher.start();self.addCleanup(patcher.stop)
  # (No SELinux relabelling of test paths.)
  patcher=patch.object(r.shutil,'which',lambda name:None);patcher.start();self.addCleanup(patcher.stop)
 def release(self,text):(self.templates/'sddm/Main.qml').write_text(text)

 def test_upgrade_keeps_system_originals_and_replaces_the_screens(self):
  self.release('v1');r.screens(self.home,self.store,['login'])
  with self.assertRaisesRegex(RuntimeError,'already managed'):r.screens(self.home,self.store,['login'])
  self.release('v2');r.screens(self.home,self.store,['login'],upgrade=True)
  self.assertEqual((self.theme/'Main.qml').read_text(),'v2')
  record=json.loads((self.store/'system.json').read_text())
  self.assertEqual(record['features'],['login'])
  self.assertEqual(len(record['paths']),6)
  r.restore(self.store)
  self.assertEqual((self.theme/'Main.qml').read_text(),'stock')

 def test_failed_screen_upgrade_puts_the_previous_release_back(self):
  self.release('v1');r.screens(self.home,self.store,['login'])
  self.release('v2')
  def fail(args):raise RuntimeError('tmpfiles failed')
  with patch.object(r,'run',fail),self.assertRaises(RuntimeError):r.screens(self.home,self.store,['login'],upgrade=True)
  self.assertEqual((self.theme/'Main.qml').read_text(),'v1')
  r.restore(self.store)
  self.assertEqual((self.theme/'Main.qml').read_text(),'stock')
if __name__=='__main__':unittest.main()
