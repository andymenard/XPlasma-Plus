"""Version-specific shell files must retain the installed release's integration."""
import ast,importlib.util,os,shutil,tempfile,unittest
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]

class LockScreenRecipeTests(unittest.TestCase):
 def test_trixie_uses_system_osd_without_replacing_xp_authentication_controls(self):
  tree=ast.parse((BASE/'backend.py').read_text())
  functions=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in {'copy','lock_screen'}]
  for series in ['6.3','6.6','6.7']:
   with self.subTest(series=series),tempfile.TemporaryDirectory() as tmp:
    home=Path(tmp)/'home';system=Path(tmp)/'system';state=Path(tmp)/'state'
    for p,data in [(system/'metadata.json','{}'),(system/'contents/osd/OsdItem.qml','system OSD'),
                   (system/'contents/lockscreen/LockOsd.qml','system lock OSD'),
                   (state/'templates/lockscreen/LockOsd.qml','newer OSD module'),
                   (state/'templates/lockscreen/MainBlock.qml','XP password controls'),
                   (state/'templates/sddm/assets/background.png','local art')]:
     p.parent.mkdir(parents=True,exist_ok=True);p.write_text(data)
    def path(value):return system if value=='/usr/share/plasma/shells/org.kde.plasma.desktop' else Path(value)
    ns={'Path':path,'shutil':shutil,'tempfile':tempfile,'os':os,'detect':lambda:{},'widget_series':lambda profile:series}
    exec(compile(ast.Module(body=functions,type_ignores=[]),'backend.py','exec'),ns)
    ns['lock_screen'](home,state)
    dest=home/'.local/share/plasma/shells/org.kde.plasma.desktop/contents'
    self.assertEqual((dest/'lockscreen/MainBlock.qml').read_text(),'XP password controls')
    self.assertEqual((dest/'lockscreen/LockOsd.qml').read_text(),'system lock OSD' if series=='6.3' else 'newer OSD module')
    self.assertEqual((dest/'osd').resolve(),system/'contents/osd')
    self.assertEqual((dest/'lockscreen/assets/background.png').read_text(),'local art')
    # Reapplying an installed lock screen also reconstructs the system links.
    ns['lock_screen'](home,state,upgrade=True)
    self.assertEqual((dest/'osd').resolve(),system/'contents/osd')

class CaptionVersionTests(unittest.TestCase):
 def test_only_ported_kwin_apis_are_enabled(self):
  source=BASE/'payload/caption-animation/build-local.py'
  if not source.is_file():source=BASE.parent/'effects/xp-caption-animation/build-local.py'
  spec=importlib.util.spec_from_file_location('caption_versions',source)
  builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
  for version in ['6.3.6','6.6.6','6.7.5']:
   self.assertEqual(builder.kwin_version('kwin_wayland '+version),version)
  for version in ['6.2.6','6.4.0','6.5.0','6.8.0']:
   with self.subTest(version=version),self.assertRaises(RuntimeError):builder.kwin_version('kwin_wayland '+version)

if __name__=='__main__':unittest.main()
