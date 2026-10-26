"""After a Plasma update: Setup's Rebuild, and the login notice that offers it."""
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
b=load('rebuild_backend','backend.py')
import rebuild,rebuild_notice

class Status(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
  self.home=Path(self.temp.name)/'home';self.base=Path(self.temp.name)/'bundle';(self.base/'source').mkdir(parents=True)
  self.install(['desktop','taskbar'])
 def install(self,features):
  record=self.home/'.local/share/kde-xp-suite/installation.json';record.parent.mkdir(parents=True,exist_ok=True)
  record.write_text(json.dumps({'features':features}))
 def lose(self,plasma):
  state=self.home/'.local/share/xplasma/state.json';state.parent.mkdir(parents=True,exist_ok=True)
  state.write_text(json.dumps({'settings':{'forkLost':plasma} if plasma else {}}))
 def sources(self,version):
  for project in ['desktop','workspace']:(self.base/'source'/('plasma-'+project+'-'+version+'.tar.xz')).write_text('')

 def test_series_as_xplasma_counts_them(self):
  self.assertEqual(rebuild.series('6.7.5'),'6.7');self.assertEqual(rebuild.series('6.7.91'),'6.8');self.assertEqual(rebuild.series('6'),'')

 def test_nothing_to_rebuild_while_the_builds_are_in_place(self):
  self.lose(None);self.assertIsNone(rebuild.status(self.home,self.base))

 def test_a_lost_build_this_installer_can_rebuild(self):
  self.lose('6.7.2');self.sources('6.7.5')
  self.assertEqual(rebuild.status(self.home,self.base),{'plasma':'6.7.2','series':'6.7','available':True})

 def test_a_plasma_this_installer_has_no_sources_for(self):
  self.lose('6.9.0');self.sources('6.7.5')
  self.assertEqual(rebuild.status(self.home,self.base)['available'],False)

 def test_only_a_taskbar_installation_has_builds_to_lose(self):
  self.install(['desktop']);self.lose('6.7.2')
  self.assertIsNone(rebuild.status(self.home,self.base))

 def test_the_notice_comes_once_per_plasma(self):
  self.lose('6.7.2');self.sources('6.7.5')
  with patch.object(rebuild_notice,'BASE',self.base):
   lost=rebuild_notice.pending(self.home);self.assertEqual(lost['plasma'],'6.7.2')
   rebuild_notice.told_path(self.home).write_text(json.dumps({'plasma':'6.7.2'}))
   self.assertIsNone(rebuild_notice.pending(self.home))
   # (a later Plasma that takes them off again is told about)
   self.lose('6.7.3');self.assertEqual(rebuild_notice.pending(self.home)['plasma'],'6.7.3')

 def test_the_notice_says_what_to_do(self):
  self.assertIn('Rebuild',rebuild_notice.message({'plasma':'6.7.2','series':'6.7','available':True})[1])
  self.assertIn('Download the latest',rebuild_notice.message({'plasma':'6.9.0','series':'6.9','available':False})[1])

class Rebuild(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.home=Path(self.temp.name)/'home';self.home.mkdir()
  self.bundle=Path(self.temp.name)/'bundle'
  for name in ['payload/templates','payload/tools','source']:(self.bundle/name).mkdir(parents=True)
  for project in ['desktop','workspace']:(self.bundle/'source'/('plasma-'+project+'-6.7.5.tar.xz')).write_text('')
  self.kdeglobals=self.home/'.config/kdeglobals';self.kdeglobals.parent.mkdir(parents=True)
  self.kdeglobals.write_text('[General]\nfont=Noto Sans,10\n')
  self.calls=[];self.broken=False;self.family=None;self.taskbar_ok=True
  patcher=patch.dict(os.environ,{'HOME':str(self.home)});patcher.start();self.addCleanup(patcher.stop)
  stubs={'BASE':self.bundle,'preflight':lambda selected,installed=():None,'verify':lambda:None,'root':self.root,
         'gnome':lambda:{},'run':lambda *a,**k:'','detect':lambda:{'family':self.family,'plasma':(6,7,5),'qt':(6,11,2),'systemd':True},'desktop_path':lambda home:home/'Desktop',
         'prepare_native':self.native,'basic':lambda *a,**k:None,'verify_taskbar':self.verify,'desktop_updates':lambda *a,**k:None,'caption_animation':lambda home:self.calls.append('caption'),
         'taskbar_defaults':lambda home,selected:None,'refresh':lambda home,state:None}
  for name,value in stubs.items():
   patcher=patch.object(b,name,value);patcher.start();self.addCleanup(patcher.stop)
  self.addCleanup(self.temp.cleanup)
  b.install({'desktop','taskbar','caption_animation'},'Royale');self.calls.clear();self.family='dnf'
 def root(self,action,selected=(),**options):self.calls.append(('root',action,tuple(sorted(selected)),options.get('native_ready',False)))
 def native(self,home,state,selected,theme,asset_options=None,keep_theme=False):
  self.calls.append(('native',theme,keep_theme,(asset_options or {}).get('dark_mode')))
  self.kdeglobals.write_text('[General]\nfont=Tahoma OS,8\nrebuilt=yes\n')
  if self.broken:raise RuntimeError('build failed')
 def verify(self):
  self.calls.append('verify')
  if not self.taskbar_ok:raise RuntimeError('The XP taskbar is incomplete: the bottom panel has no clock.')
 def state(self):return self.home/'.local/share/kde-xp-suite'
 def lose(self,plasma):
  path=self.home/'.local/share/xplasma/state.json';path.parent.mkdir(parents=True,exist_ok=True)
  path.write_text(json.dumps({'settings':{'forkLost':plasma,'darkMode':True}}))

 def test_install_adds_the_login_notice(self):
  entry=(self.home/'.config/autostart/kde-xp-suite-rebuild.desktop').read_text()
  self.assertIn(str(self.state()/'installer/rebuild_notice.py'),entry)
  self.assertIn('OnlyShowIn=KDE;',entry)

 def test_rebuild_builds_for_this_plasma_and_keeps_the_look(self):
  self.lose('6.7.5')
  b.rebuild()
  # Build tools for a build from source, the current theme kept, dark mode as Properties left it.
  self.assertIn(('root','dependencies',('caption_animation','desktop','taskbar'),False),self.calls)
  self.assertIn(('native','Royale',True,True),self.calls)
  self.assertIn('caption',self.calls);self.assertEqual(self.calls[-1],'verify')
  record=json.loads((self.state()/'installation.json').read_text())
  self.assertEqual(record['rebuilds'][-1]['plasma'],'6.7.5');self.assertEqual(record['features'],['caption_animation','desktop','taskbar'])
  self.assertFalse((self.state()/'upgrade').exists())

 def test_failed_rebuild_puts_the_installation_back(self):
  self.lose('6.7.5');before=self.kdeglobals.read_text();self.broken=True
  with self.assertRaises(RuntimeError):b.rebuild()
  self.assertEqual(self.kdeglobals.read_text(),before)
  self.assertNotIn('rebuilds',json.loads((self.state()/'installation.json').read_text()))

 def test_no_rebuild_without_this_plasmas_sources(self):
  self.lose('6.9.1')
  with self.assertRaisesRegex(RuntimeError,'no Plasma 6.9 sources'):b.rebuild()
  self.assertEqual(self.calls,[])

 def test_rebuild_that_leaves_no_taskbar_fails_and_puts_it_back(self):
  self.lose('6.7.5');before=self.kdeglobals.read_text();self.taskbar_ok=False
  with self.assertRaisesRegex(RuntimeError,'no clock'):b.rebuild()
  self.assertEqual(self.kdeglobals.read_text(),before)

 def test_an_installation_without_its_taskbar_is_rolled_back(self):
  b.uninstall();self.calls.clear();self.family=None;self.taskbar_ok=False
  with self.assertRaisesRegex(RuntimeError,'no clock'):b.install({'desktop','taskbar'},'Royale')
  self.assertEqual(self.kdeglobals.read_text(),'[General]\nfont=Noto Sans,10\n')
  self.assertFalse((self.state()/'installation.json').exists())
  self.assertTrue((self.state()/'incomplete.json').exists())

 def test_remove_takes_the_notice_away(self):
  b.uninstall()
  self.assertFalse((self.home/'.config/autostart/kde-xp-suite-rebuild.desktop').exists())

class TaskbarCheck(unittest.TestCase):
 def check(self,answers):
  replies=iter(answers)
  with patch.object(b,'run',lambda *a,**k:next(replies)),patch('time.sleep',lambda s:None):b.verify_taskbar(wait=8)

 def test_waits_for_the_shell_to_load_its_desktop(self):
  self.check(['','loading','complete'])

 def test_an_incomplete_taskbar_is_an_error_not_a_success(self):
  with self.assertRaisesRegex(RuntimeError,'bottom panel has no clock'):self.check(['clock']*4)
  with self.assertRaisesRegex(RuntimeError,'not finished loading'):self.check(['loading']*4)
  with self.assertRaisesRegex(RuntimeError,'not answering'):self.check(['']*4)

class Window(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  spec=importlib.util.spec_from_file_location('rebuild_gui',ROOT/'gui.py');cls.g=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.g)
  cls.app=cls.g.QtWidgets.QApplication.instance() or cls.g.QtWidgets.QApplication([])
 def window(self,lost):
  g=self.g
  with tempfile.TemporaryDirectory() as folder:
   state=Path(folder);(state/'installation.json').write_text('{"features":["desktop","taskbar"],"theme":"Royale","asset_options":{}}')
   with patch.object(g,'STATE',state),patch.object(g,'capabilities',return_value={f['id']:'' for f in g.FEATURES}),patch.object(g.plasma_update,'status',return_value=lost):
    window=g.Installer(test_mode=True);self.addCleanup(window.close)
    return window
 def test_rebuild_offered_after_a_plasma_update(self):
  window=self.window({'plasma':'6.7.5','series':'6.7','available':True})
  self.assertFalse(window.rebuild.isHidden());self.assertTrue(window.rebuild.isEnabled())
  self.assertEqual(window.rebuild.text(),'Rebuild for Plasma 6.7.5');self.assertIn('Plasma was updated to 6.7.5',window.intro.text())
 def test_a_plasma_this_installer_cannot_build_for(self):
  window=self.window({'plasma':'6.9.0','series':'6.9','available':False})
  self.assertFalse(window.rebuild.isEnabled());self.assertIn('Download the latest',window.intro.text())
 def test_no_rebuild_when_nothing_was_lost(self):
  self.assertTrue(self.window(None).rebuild.isHidden())

if __name__=='__main__':unittest.main()
