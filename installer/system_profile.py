"""Read-only platform detection and per-feature compatibility decisions."""
import json,os,platform,re,shutil,subprocess
from pathlib import Path

def output(args):
 try:return subprocess.check_output(args,text=True,stderr=subprocess.DEVNULL,timeout=8).strip()
 except (OSError,subprocess.SubprocessError):return ''
def numbers(value):
 match=re.search(r'(\d+)\.(\d+)(?:\.(\d+))?',value)
 return tuple(map(int,match.groups(default='0'))) if match else (0,0,0)
# Source API floors, not the Qt used by a particular prebuilt artifact.
WIDGET_QT_FLOORS={0:(6,6,0),1:(6,6,0),2:(6,7,0),3:(6,7,0),4:(6,8,0),5:(6,9,0),6:(6,10,0),7:(6,10,0),8:(6,11,0)}
def widget_series(profile):
 """Known XP widget port; unknown future layouts remain blocked."""
 major,minor,patch=profile['plasma']
 if patch>=90:minor+=1
 floor=WIDGET_QT_FLOORS.get(minor)
 return '6.'+str(minor) if major==6 and floor and profile['qt']>=floor else None
def widget_target(profile):
 """Only 6.5+ need compiled replacements; older ports patch packaged QML."""
 series=widget_series(profile)
 return series if series and int(series.split('.')[1])>=5 else None
def executable(*names):
 return next((str(Path(x)) if '/' in x and Path(x).is_file() else shutil.which(x) for x in names if (Path(x).is_file() if '/' in x else shutil.which(x))),None)
def detect():
 release={}
 for line in Path('/etc/os-release').read_text().splitlines():
  if '=' in line:
   key,value=line.split('=',1);release[key]=value.strip('"')
 ids=(release.get('ID','')+' '+release.get('ID_LIKE','')).split()
 family=next((f for f,names in [('xbps',{'void'}),('apt',{'ubuntu','debian'}),('dnf',{'fedora','rhel','centos'}),('pacman',{'arch','manjaro'}),('zypper',{'opensuse','opensuse-tumbleweed','opensuse-leap','suse'})] if set(ids)&names),None)
 manager={'xbps':'xbps-install','apt':'apt-get','dnf':'dnf','pacman':'pacman','zypper':'zypper'}.get(family)
 if not manager or not shutil.which(manager):family=None
 qttool=executable('qtpaths6','qtpaths-qt6','/usr/lib/qt6/bin/qtpaths','qmake6','qmake-qt6')
 qt=output([qttool,'--qt-version']) if qttool and 'qtpaths' in qttool else output([qttool,'-query','QT_VERSION']) if qttool else ''
 if not numbers(qt)[0]:
  if family in {'dnf','zypper'}:qt=output(['rpm','-q','--qf','%{VERSION}','qt6-qtbase' if family=='dnf' else 'libQt6Core6'])
  elif family=='apt':
   qt=output(['dpkg-query','-W','-f=${Version}','libqt6core6t64']) or output(['dpkg-query','-W','-f=${Version}','libqt6core6'])
  elif family=='xbps':qt=output(['xbps-query','-p','pkgver','qt6-core'])
  elif family=='pacman':qt=output(['pacman','-Q','qt6-base'])
 plasma=''
 if family in {'dnf','zypper'}:plasma=output(['rpm','-q','--qf','%{VERSION}','plasma-workspace' if family=='dnf' else 'plasma6-workspace'])
 elif family=='apt':plasma=output(['dpkg-query','-W','-f=${Version}','plasma-workspace']).split(':')[-1]
 elif family=='xbps':plasma=output(['xbps-query','-p','pkgver','plasma-workspace'])
 elif family=='pacman':plasma=output(['pacman','-Q','plasma-workspace'])
 if not numbers(plasma)[0]:plasma=output(['plasmashell','--version'])
 arch={'x86_64':'amd64','amd64':'amd64','aarch64':'arm64','arm64':'arm64'}.get(platform.machine(),platform.machine())
 boot=next((x for x in ['update-initramfs','dracut'] if shutil.which(x)),None)
 return {'name':release.get('PRETTY_NAME','Linux'),'family':family,'arch':arch,'plasma':numbers(plasma),'qt':numbers(qt),
  'writer':executable('kwriteconfig6'),'dbus':executable('qdbus6','qdbus-qt6','/usr/lib/qt6/bin/qdbus'),
  'kde':'KDE' in os.environ.get('XDG_CURRENT_DESKTOP','').upper(),'boot':boot,
  'splash':bool(set(Path('/proc/cmdline').read_text().split())&{'splash','rhgb'}),
  'systemd':Path('/run/systemd/system').is_dir()}
# The login and boot screens belong to the whole system, so one account's
# installation manages them; root_helper.py publishes which one here, since
# its per-account records are readable by root only.
SCREENS_MARKER=Path('/var/lib/kde-xp-suite/screens.json')
def screens_manager(marker=None):
 """The other account whose installation manages the system screens, or None."""
 try:record=json.loads((marker or SCREENS_MARKER).read_text());uid=int(record['uid'])
 except (OSError,ValueError,KeyError,TypeError):return None
 if uid==os.getuid():return None
 return str(record.get('user') or 'user '+str(uid))
def capabilities(profile,features):
 result={f['id']:'' for f in features}
 def disable(names,reason):
  for name in names:result[name]=reason
 if profile['plasma'][0]!=6 or not profile['writer']:
  disable([n for n in result if n not in {'firefox','firefox_default','codex_colors','clippy','desktop_icons'}],'Requires Plasma 6 and kwriteconfig6.')
 if not profile['kde']:disable(['wallpaper','taskbar','behavior','session','lock','tray'],'Run inside a KDE Plasma session.')
 if not shutil.which('plasma-apply-cursortheme'):disable(['pointers'],'Plasma cursor-theme utility is missing.')
 if not shutil.which('fc-cache'):disable(['fonts'],'Fontconfig is missing.')
 if not profile['dbus']:disable(['wallpaper','taskbar','behavior'],'Qt 6 qdbus is missing; install the distribution Qt tools first.')
 if profile['qt']<(6,6,0):disable(['theme_manager','desktop','apps','taskbar'],'XPlasma requires Qt 6.6 or newer; Qt development tools must be installed for detection.')
 if profile['plasma']<(6,6,0):disable(['tray'],'XP tray panels require Plasma 6.6 or newer.')
 if not widget_series(profile):disable(['taskbar'],'This Plasma/Qt combination has no verified XP widget port. The XP taskbar cannot be replaced with stock widgets; update the installer when its port is available.')
 if widget_target(profile) not in {'6.6','6.7','6.8'}:disable(['session','lock','caption_animation'],'This optional shell integration needs a matching Plasma recipe. Other XP desktop features remain available.')
 if not profile['family']:
  disable(['pinball','minesweeper','solitaire'],'Game build dependencies require a supported package-manager adapter.')
 if not profile['family']:disable(['theme_manager','desktop','apps','taskbar','login','boot','caption_animation'],'Automatic dependency installation supports APT, DNF, pacman, Zypper and XBPS families.')
 if profile['arch'] not in {'amd64','arm64'}:disable(['firefox','firefox_default'],'Bundled Firefox ESR supports Intel/AMD 64-bit and ARM64.')
 if not profile['systemd']:disable(['login'],'Automatic display-manager switching requires systemd. Other desktop features use XDG autostart.')
 if not profile['boot'] or not profile['splash']:disable(['boot'],'Requires an existing splash-enabled Plymouth setup using dracut or update-initramfs. Configure that first; the installer does not alter bootloader arguments.')
 # (Unless it's the XP lock screen's own, which an upgrade replaces.)
 try:installed=set(json.loads((Path.home()/'.local/share/kde-xp-suite/installation.json').read_text()).get('features',[]))
 except (OSError,ValueError):installed=set()
 if 'lock' not in installed and (Path.home()/'.local/share/plasma/shells/org.kde.plasma.desktop').exists():disable(['lock'],'A custom user Plasma shell is already installed.')
 manager=screens_manager()
 if manager:disable(['login','boot'],'The XPlasma Plus installation of the '+manager+' account manages the system login and boot screens, which already apply to every account. Remove it there to manage them from this account.')
 return result
