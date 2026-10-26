"""Session-local replacement for the theme/cursor systemd path units."""
import fcntl,subprocess,sys,time
from pathlib import Path

def signature(paths):
 result=[]
 for path in paths:
  try:
   stat=path.stat();result.append((stat.st_ino,stat.st_mtime_ns,stat.st_size))
  except OSError:result.append(None)
 return tuple(result)

def main():
 home=Path.home();entry=home/'.config/autostart/xp-theme-watch.desktop'
 runtime=home/'.local/share/kde-xp-suite/runtime/tools'
 cache=home/'.cache/xplasma';cache.mkdir(parents=True,exist_ok=True)
 # A second installer invocation must not create another watcher.
 with (cache/'theme-watch.lock').open('w') as lock:
  try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
  except BlockingIOError:return
  paths=[home/'.config'/name for name in ['kwinrc','kdeglobals','kcminputrc']]
  folder=home/'.local/share/plasma/plasmoids/org.kde.desktopcontainment/contents/ui'
  paths += [folder/'FolderView.qml',folder/'FolderItemDelegate.qml']
  previous=None
  while entry.exists() and runtime.is_dir():
   current=signature(paths)
   if current!=previous:
    for helper in [runtime/'keep-xp-session.py',home/'.local/share/xplasma/cursor-sync.py']:
     if helper.is_file():
      try:subprocess.run([sys.executable,str(helper)],timeout=30,check=False)
      except (OSError,subprocess.TimeoutExpired):pass
    # Helpers can rewrite the files they watch; use their resulting state.
    previous=signature(paths)
   time.sleep(2)

if __name__=='__main__':main()
