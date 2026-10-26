"""Remove gives back the desktop from before the installation."""
import importlib.util,json,os,sys,tempfile,types,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
spec=importlib.util.spec_from_file_location('uninstall_backend',ROOT/'backend.py')
b=importlib.util.module_from_spec(spec)
with patch.dict(sys.modules,{'tomlkit':types.SimpleNamespace()}):spec.loader.exec_module(b)

class Uninstall(unittest.TestCase):
 def setUp(self):
  temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup)
  self.home=Path(temp.name)/'home';self.home.mkdir();bundle=Path(temp.name)/'bundle'
  for name in ['payload/templates','payload/tools']:(bundle/name).mkdir(parents=True)
  patcher=patch.dict(os.environ,{'HOME':str(self.home)});patcher.start();self.addCleanup(patcher.stop)
  self.calls=[]
  stubs={'BASE':bundle,'preflight':lambda selected,installed=():None,'verify':lambda:None,'root':lambda *a,**k:self.calls.append((a,k)),
         'gnome':lambda:{},'run':lambda *a,**k:'','detect':lambda:{'family':None,'plasma':(6,6,4),'qt':(6,10,2),'systemd':True},'desktop_path':lambda home:home/'Desktop',
         'prepare_native':self.apply,'basic':lambda *a,**k:None,'verify_taskbar':lambda:None,'desktop_updates':lambda *a,**k:None,'taskbar_defaults':lambda *a:None}
  for name,value in stubs.items():
   patcher=patch.object(b,name,value);patcher.start();self.addCleanup(patcher.stop)
  # The desktop before XP, including a GTK theme and a look of the user's own.
  self.write('.config/kdeglobals','[General]\nColorScheme=BreezeLight\n')
  self.write('.config/plasmarc','[Theme]\nname=default\n')
  self.write('.gtkrc-2.0','gtk-theme-name="Breeze"\n')
  self.write('.local/share/color-schemes/Mine.colors','[General]\nName=Mine\n')
  self.original=self.files()
 def write(self,name,text='generated'):
  p=self.home/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text)
 def files(self):
  # (Settings files compare by content; KDE doesn't mind blank lines.)
  def text(p):return b'\n'.join(line for line in p.read_bytes().splitlines() if line.strip()) if p.parent.name=='.config' else p.read_bytes()
  return {str(p.relative_to(self.home)):text(p) for p in self.home.rglob('*') if p.is_file()}
 def apply(self,home,state,selected,theme,asset_options=None,keep_theme=False):
  # What applying a look writes: XPlasma's files, and KDE's tools' own.
  self.look('aaa')
  self.write('.config/xsettingsd/xsettingsd.conf','Net/ThemeName "XPlasma-xplasma-v2-aaa"\n')
  self.write('.config/Trolltech.conf','[Qt]\nstyle=XPlasma\n')
  self.write('.local/share/applications/firefox-esr.desktop','[Desktop Entry]\nX-XPlasma-IE6=true\n')
  self.write('.local/share/firefox-esr-ie6/profile/prefs.js','user_pref("xplasma.appearance", true);\n')
 def look(self,name):
  self.write('.config/kdeglobals','[General]\nColorScheme=xplasma-v2-'+name+'\n')
  self.write('.config/plasmarc','[Theme]\nname=xplasma-taskbar-'+name+'\n')
  self.write('.gtkrc-2.0','gtk-theme-name="XPlasma-xplasma-v2-'+name+'"\n')
  self.write('.local/share/themes/XPlasma-xplasma-v2-'+name+'/index.theme')
  self.write('.local/share/color-schemes/xplasma-v2-'+name+'.colors')
  self.write('.local/share/plasma/desktoptheme/xplasma-taskbar-'+name+'/metadata.json')
  self.write('.local/share/xplasma/start/xplasma-v2-'+name+'/start.png')
  self.write('.cache/xplasma/dark-mode/'+name+'/source.json')
  self.write('.cache/plasma_theme_xplasma-taskbar-'+name+'.kcache')
 def install(self):b.install({'desktop','taskbar'},'Luna Blue')
 def state(self):return self.home/'.local/share/kde-xp-suite'
 def outside_state(self):return {k:v for k,v in self.files().items() if not k.startswith('.local/share/kde-xp-suite/')}

 def test_remove_clears_what_an_earlier_installation_left(self):
  # Leftovers of an earlier XP installation are in the snapshot taken before
  # this one: they're still the XP desktop's, not the user's, and go.
  self.write('.local/share/sounds/xplasma-sounds-0ld/stereo/start.oga')
  self.write('.local/share/sounds/xplasma-sounds-0ld/index.theme')
  self.write('.local/share/knotifications6/xplasma.notifyrc','[Event/windowOpen]\nAction=Sound\n')
  self.write('.local/share/sounds/Mine/index.theme','[Sound Theme]\nName=Mine\n')
  self.write('.config/kdeglobals','[General]\nColorScheme=BreezeLight\n\n[KFileDialog Settings]\nRecent URLs=file:///home/u/XPlasma-Installer/\n\n[Sounds]\nTheme=xplasma-sounds-0ld\n')
  # (that earlier installation, by this installer, was removed: its record stays)
  self.write('.local/share/kde-xp-suite/removed.json','{"features":["sounds"]}')
  self.install();b.uninstall()
  left=self.outside_state()
  self.assertFalse([k for k in left if 'xplasma' in k.lower()],left.keys())
  self.assertIn('.local/share/sounds/Mine/index.theme',left)
  self.assertNotIn(b'Theme=xplasma',left['.config/kdeglobals'])
  self.assertIn(b'ColorScheme=BreezeLight',left['.config/kdeglobals'])
  # (only theme settings naming XPlasma's own: a path the user opened stays)
  self.assertIn(b'Recent URLs=file:///home/u/XPlasma-Installer/',left['.config/kdeglobals'])

 def test_remove_leaves_the_desktop_as_it_was(self):
  self.install()
  # Then Properties: another look, custom colours, a theme of the user's own.
  self.look('bbb')
  self.write('.config/xplasma-live-colors.json','{}')
  self.write('.local/share/xplasma/maintenance/theme-20261017-120000/before.json','{}')
  self.write('.local/share/xplasma/schemes/My Scheme.theme','[Theme]\n')
  self.write('.local/share/xplasma/custom-luna/abc.msstyles','style')
  self.write('.local/share/firefox-esr-ie6/profile/places.sqlite','bookmarks')
  b.uninstall()
  kept={'.local/share/xplasma/schemes/My Scheme.theme','.local/share/xplasma/custom-luna/abc.msstyles',
        '.local/share/firefox-esr-ie6/profile/places.sqlite'}
  # (The profile's defaults as the installer wrote them go; what the user
  # made there stays.)
  now=self.outside_state()
  self.assertEqual({k:v for k,v in now.items() if k not in kept},self.original)
  self.assertTrue(kept<=set(now))

 def test_remove_keeps_backups_but_not_the_runtime(self):
  self.install()
  for name in ['runtime/bin/xplasma','installer/gui.py','build/x','templates/x']:
   p=self.state()/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('x')
  b.uninstall()
  left={p.name for p in self.state().iterdir()}
  self.assertTrue({'backup','before.json','removed.json'}<=left)
  self.assertFalse(left&{'runtime','installer','build','templates'})

 def test_remove_can_delete_the_backups_too(self):
  self.install()
  record=json.loads((self.state()/'installation.json').read_text());record['features'].append('login')
  (self.state()/'installation.json').write_text(json.dumps(record))
  b.uninstall(delete_backups=True)
  self.assertFalse(self.state().exists())
  self.assertIn((('restore',),{'purge':True}),self.calls)

 def test_what_was_there_before_stays(self):
  # A generated look from an earlier XPlasma, before this installation.
  self.write('.local/share/themes/XPlasma-xplasma-v2-old/index.theme','old')
  original=self.files()
  self.install();self.look('bbb');b.uninstall()
  self.assertEqual(self.outside_state().get('.local/share/themes/XPlasma-xplasma-v2-old/index.theme'),b'old')
  self.assertEqual({k:v for k,v in self.outside_state().items() if 'firefox' not in k},original)
class DefaultBrowser(Uninstall):
 def setUp(self):
  super().setUp()
  patcher=patch.object(b,'firefox',lambda home:self.write('.local/share/applications/firefox-esr-ie6.desktop','[Desktop Entry]\n'));patcher.start();self.addCleanup(patcher.stop)
  self.write('.config/mimeapps.list','[Default Applications]\nx-scheme-handler/https=chromium.desktop;\ninode/directory=org.kde.dolphin.desktop;\n')
  self.write('.config/kdeglobals','[General]\nBrowserApplication=chromium.desktop\n')
  self.original=self.files()
 def defaults(self):
  mime=b.ini(self.home/'.config/mimeapps.list');kde=b.ini(self.home/'.config/kdeglobals')
  return mime.get('Default Applications','x-scheme-handler/https'),kde.get('General','BrowserApplication')
 def test_ie6_firefox_becomes_the_default_and_remove_restores_the_previous(self):
  b.install({'firefox','firefox_default'},'Luna Blue')
  self.assertEqual(self.defaults(),('firefox-esr-ie6.desktop;','firefox-esr-ie6.desktop'))
  mime=b.ini(self.home/'.config/mimeapps.list')
  for kind in b.WEB_TYPES:self.assertEqual(mime.get('Default Applications',kind),'firefox-esr-ie6.desktop;')
  self.assertEqual(mime.get('Default Applications','inode/directory'),'org.kde.dolphin.desktop;')
  b.uninstall()
  self.assertEqual(self.defaults(),('chromium.desktop;','chromium.desktop'))
 def test_an_upgrade_keeps_a_browser_chosen_since(self):
  b.install({'firefox','firefox_default'},'Luna Blue')
  self.write('.config/kdeglobals','[General]\nBrowserApplication=org.kde.falkon.desktop\n')
  b.install({'firefox','firefox_default'},'Luna Blue')
  self.assertEqual(self.defaults()[1],'org.kde.falkon.desktop')
 def test_the_default_needs_the_browser(self):
  with self.assertRaisesRegex(RuntimeError,'Select IE6 Replica'):b.install({'firefox_default'},'Luna Blue')
 # (The Uninstall cases run under this setUp too; they don't touch the browser.)
if __name__=='__main__':unittest.main()
