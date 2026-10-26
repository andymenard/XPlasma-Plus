import importlib.util,sys,tempfile,types,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
import theme_watch
spec=importlib.util.spec_from_file_location('watch_backend',ROOT/'backend.py');b=importlib.util.module_from_spec(spec)
with patch.dict(sys.modules,{'tomlkit':types.SimpleNamespace()}):spec.loader.exec_module(b)

class Watcher(unittest.TestCase):
 def test_non_systemd_does_not_invoke_systemctl_even_when_unchecked(self):
  with patch.object(b,'detect',return_value={'systemd':False}),patch.object(b.subprocess,'run') as run:
   self.assertEqual(b.run(['systemctl','--user','is-enabled','anything'],capture=True,check=False),'')
   self.assertEqual(b.run(['systemctl','--user','daemon-reload']),1)
   run.assert_not_called()
 def test_autostart_uses_quoted_paths_and_is_tracked_for_removal(self):
  with tempfile.TemporaryDirectory(prefix='xp home ') as folder:
   home=Path(folder)/'home';state=home/'.local/share/kde-xp-suite'
   with patch.object(b,'detect',return_value={'systemd':False}),patch.object(b.subprocess,'Popen') as start:
    b.session_watch(home,state)
   entry=home/'.config/autostart/xp-theme-watch.desktop'
   self.assertIn('OnlyShowIn=KDE;',entry.read_text());self.assertIn('"'+str(home),entry.read_text())
   self.assertTrue((home/'.local/share/xplasma/theme-watch.py').is_file())
   self.assertIn(str(entry.relative_to(home)),b.USER_PATHS);start.assert_called_once()
 def test_watcher_runs_helpers_once_then_on_external_changes_and_stops_on_removal(self):
  with tempfile.TemporaryDirectory() as folder:
   home=Path(folder);entry=home/'.config/autostart/xp-theme-watch.desktop';entry.parent.mkdir(parents=True);entry.touch()
   tools=home/'.local/share/kde-xp-suite/runtime/tools';tools.mkdir(parents=True);(tools/'keep-xp-session.py').touch()
   config=home/'.config/kdeglobals';config.write_text('first')
   ticks=[]
   def tick(_):
    ticks.append(1)
    if len(ticks)==1:config.write_text('second')
    elif len(ticks)==3:entry.unlink()
   with patch.object(theme_watch.Path,'home',return_value=home),patch.object(theme_watch.time,'sleep',side_effect=tick),patch.object(theme_watch.subprocess,'run') as run:
    theme_watch.main()
   self.assertEqual(run.call_count,2);self.assertEqual(len(ticks),3)

if __name__=='__main__':unittest.main()
