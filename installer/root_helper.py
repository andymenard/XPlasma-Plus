#!/usr/bin/python3
"""Fixed-scope system changes for the portable KDE XP Suite (invoked by pkexec)."""
import argparse,configparser,json,os,pwd,shutil,subprocess,sys
from pathlib import Path
BASE=Path(__file__).resolve().parent
from system_profile import detect,widget_target
from dependencies import install
STATE='kde-xp-suite'
PATHS={
 'login':['/etc/sddm.conf.d/90-xp-welcome.conf','/usr/share/sddm/themes/xp-welcome','/usr/share/icons/xp-windows-default',
  '/usr/local/libexec/xp-logon-status','/etc/xdg/autostart/xp-logon-status.desktop','/etc/tmpfiles.d/xp-logonui.conf'],
 'boot':['/etc/plymouth/plymouthd.conf','/usr/share/plymouth/themes/xp-boot']
}
def run(args):
 print('Running: '+' '.join(map(str,args)),flush=True)
 subprocess.run(args,check=True,env={**os.environ,'DEBIAN_FRONTEND':'noninteractive'})
def copy(src,dest):
 dest=Path(dest);dest.parent.mkdir(parents=True,exist_ok=True)
 if src.is_symlink():dest.symlink_to(os.readlink(src))
 elif src.is_dir():shutil.copytree(src,dest,symlinks=True,dirs_exist_ok=True)
 else:shutil.copy2(src,dest)
def remove(path):
 if path.is_symlink() or path.is_file():path.unlink()
 elif path.is_dir():shutil.rmtree(path)
def context():
 if os.geteuid()!=0:raise RuntimeError('System changes require authentication.')
 uid=int(os.environ.get('PKEXEC_UID',os.environ.get('SUDO_UID','0')))
 if uid<=0:raise RuntimeError('Launch from your desktop user account.')
 return Path(pwd.getpwuid(uid).pw_dir),Path('/var/lib')/STATE/str(uid)
# The screens are system-wide, so one account's installation manages them. Its
# record is root-only; screens.json beside it tells every account which one.
def managing_account(store):
 return next((int(p.parent.name) for p in sorted(store.parent.glob('*/system.json')) if p.parent!=store and p.parent.name.isdigit()),None)
def account_name(uid):
 try:return pwd.getpwuid(uid).pw_name
 except KeyError:return 'user '+str(uid)
def publish(store,features):
 marker=store.parent/'screens.json'
 marker.write_text(json.dumps({'uid':int(store.name),'user':account_name(int(store.name)),'features':sorted(features)},indent=2)+'\n');marker.chmod(0o644)
def withdraw(store):
 marker=store.parent/'screens.json'
 try:
  if json.loads(marker.read_text()).get('uid')==int(store.name):marker.unlink()
 except (OSError,ValueError):pass
def check_screens(store,features):
 # Before anything is installed: installations from before screens.json
 # existed get it now, so Setup shows the screens as unavailable from here on.
 other=managing_account(store)
 if other is None or not set(features)&set(PATHS):return
 manager=store.parent/str(other)
 if not (store.parent/'screens.json').exists():publish(manager,json.loads((manager/'system.json').read_text()).get('features',[]))
 raise RuntimeError('The XPlasma Plus installation of the '+account_name(other)+' account manages the system login and boot screens, which already apply to every account. Clear XP Welcome login screen and XP boot splash to install the other features here.')
# Per-session program counts for the Welcome screen's "N programs running." status.
LOGIN_STATUS=[('xp-logon-status',PATHS['login'][3],0o755),('xp-logon-status.desktop',PATHS['login'][4],0o644),('xp-logonui.conf',PATHS['login'][5],0o644)]
def greeter_environment():
 # The greeter reads those counts with XMLHttpRequest; keep the distribution's own variables (e.g. layer-shell).
 base=''
 for p in [*sorted(Path('/usr/lib/sddm/sddm.conf.d').glob('*.conf')),Path('/etc/sddm.conf')]:
  if p.is_file():
   for line in p.read_text(errors='replace').splitlines():
    if line.strip().startswith('GreeterEnvironment='):base=line.split('=',1)[1].strip()
 values=[v for v in base.split(',') if v.strip() and not v.strip().startswith('QML_XHR_ALLOW_FILE_READ=')]
 return ','.join(values+['QML_XHR_ALLOW_FILE_READ=1'])
def deps(features,native_ready=False,caption_ready=False):
 profile=detect()
 if not profile['family']:raise RuntimeError('No supported system package manager detected.')
 # Packaged-QML XP ports need the core engine, but no compiled widget SDK.
 if 'taskbar' in features and not widget_target(profile):features=(set(features)-{'taskbar'})|{'theme_manager'}
 install(profile['family'],features,run,native_ready=native_ready,caption_ready=caption_ready)
def rebuild():
 engine=detect()['boot']
 if engine=='update-initramfs':run([engine,'-u','-k',os.uname().release])
 elif engine=='dracut':run([engine,'--force','--kver',os.uname().release])
 else:raise RuntimeError('No supported initramfs builder detected.')
def restore(store):
 file=store/'system.json'
 if not file.exists():return
 record=json.loads(file.read_text())
 allowed={p for values in PATHS.values() for p in values}
 if any(item['path'] not in allowed for item in record['paths']):raise RuntimeError('Invalid system backup paths.')
 if 'boot' in record['features']:
  theme='/usr/share/plymouth/themes/xp-boot/xp-boot.plymouth'
  if record.get('alternative') and shutil.which('update-alternatives'):
   run(['update-alternatives','--set','default.plymouth',record['alternative']])
  if record.get('alternative') or record.get('alternative_auto'):subprocess.run(['update-alternatives','--remove','default.plymouth',theme],check=False)
  if record.get('alternative_auto'):run(['update-alternatives','--auto','default.plymouth'])
 for item in record['paths']:
  p=Path(item['path']);remove(p)
  if item['existed']:copy(store/item['backup'],p)
 if record.get('changed_display_manager'):
  run(['systemctl','disable','sddm.service'])
  if record.get('previous_display_manager'):run(['systemctl','enable',record['previous_display_manager']])
  run(['systemctl','daemon-reload'])
 if 'boot' in record['features']:rebuild()
 file.rename(store/'system-restored.json')
 withdraw(store)
def put_back(store,entries):
 for item in entries:
  p=Path(item['path']);remove(p)
  if item['existed']:copy(store/item['backup'],p)
def screens(home,store,features,upgrade=False):
 features=sorted(set(features)&set(PATHS))
 if not features:return
 file=store/'system.json'
 if file.exists() and not upgrade:raise RuntimeError('System screens are already managed; remove the previous installation first.')
 other=managing_account(store)
 if other is not None:raise RuntimeError('The XPlasma Plus installation of the '+account_name(other)+' account manages the system XP screens.')
 store.mkdir(parents=True,exist_ok=True);store.chmod(0o700)
 # An upgrade keeps the backups from before XP (what Remove restores) and
 # saves the screens it replaces, which a failed upgrade puts back.
 record=json.loads(file.read_text()) if file.exists() else {'features':[],'paths':[]}
 if any(item['path'] not in {p for values in PATHS.values() for p in values} for item in record['paths']):raise RuntimeError('Invalid system backup paths.')
 managed=set(record['features']);added=[name for name in features if name not in managed]
 replaced=[]
 if managed:
  upgrading=store/'upgrade-saved'
  if upgrading.exists():shutil.rmtree(upgrading)
  for item in record['paths']:
   p=Path(item['path']);existed=p.exists() or p.is_symlink();saved='upgrade-saved/'+str(len(replaced))
   if existed:copy(p,store/saved)
   replaced.append({'path':item['path'],'existed':existed,'backup':saved})
 originals=[]
 for name in added:
  for entry in PATHS[name]:
   p=Path(entry);existed=p.exists() or p.is_symlink();saved='saved/'+str(len(record['paths']))
   if existed:copy(p,store/saved)
   item={'path':entry,'existed':existed,'backup':saved};record['paths'].append(item);originals.append(item)
 if 'boot' in added and detect()['family']=='apt':
  query=subprocess.run(['update-alternatives','--query','default.plymouth'],text=True,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL).stdout
  record['alternative']=next((line[7:] for line in query.splitlines() if line.startswith('Value: ')),None)
  record['alternative_auto']='Status: auto' in query
 dm=Path('/etc/systemd/system/display-manager.service')
 previous=dm.resolve().name if dm.is_symlink() else None
 if 'login' in added:
  record['previous_display_manager']=previous
  record['changed_display_manager']=previous!='sddm.service'
 record['features']=sorted(managed|set(added))
 file.write_text(json.dumps(record,indent=2)+'\n')
 publish(store,record['features'])
 source=home/'.local/share'/STATE/'templates'
 try:
  if 'login' in features:
   for src,dest in [('sddm',PATHS['login'][1]),('login-cursor',PATHS['login'][2])]:
    remove(Path(dest));copy(source/src,dest)
   p=Path(PATHS['login'][0]);p.parent.mkdir(parents=True,exist_ok=True)
   p.write_text('[Theme]\nCurrent=xp-welcome\nCursorTheme=xp-windows-default\nCursorSize=32\n\n[General]\nGreeterEnvironment='+greeter_environment()+'\n\n[X11]\nEnableHiDPI=true\n\n[Wayland]\nEnableHiDPI=true\n')
   for name,dest,mode in LOGIN_STATUS:
    dest=Path(dest);remove(dest);copy(source/'login-status'/name,dest);dest.chmod(mode)
   run(['systemd-tmpfiles','--create',PATHS['login'][5]])
   if shutil.which('restorecon'):subprocess.run(['restorecon','-RF',*PATHS['login'],'/run/xp-logonui'],check=False)
   if 'login' in added and record['changed_display_manager']:
    if previous:run(['systemctl','disable',previous])
    run(['systemctl','enable','sddm.service']);run(['systemctl','daemon-reload'])
  if 'boot' in features:
   remove(Path(PATHS['boot'][1]));copy(source/'boot',PATHS['boot'][1])
   c=configparser.RawConfigParser();c.optionxform=str;c.read(PATHS['boot'][0])
   if not c.has_section('Daemon'):c.add_section('Daemon')
   c.set('Daemon','Theme','xp-boot');p=Path(PATHS['boot'][0]);p.parent.mkdir(parents=True,exist_ok=True)
   with p.open('w') as f:c.write(f)
   theme='/usr/share/plymouth/themes/xp-boot/xp-boot.plymouth'
   if 'boot' in added and detect()['family']=='apt' and record.get('alternative'):
    run(['update-alternatives','--install','/usr/share/plymouth/themes/default.plymouth','default.plymouth',theme,'50'])
    run(['update-alternatives','--set','default.plymouth',theme])
   rebuild()
 except Exception:
  if not managed:restore(store);raise
  # A failed upgrade: the screens it replaced come back, and those it added
  # go back to the system's own.
  put_back(store,replaced);put_back(store,originals)
  record['paths']=[item for item in record['paths'] if item not in originals];record['features']=sorted(managed)
  if 'login' in added and record.get('changed_display_manager'):
   run(['systemctl','disable','sddm.service'])
   if previous:run(['systemctl','enable',previous])
   run(['systemctl','daemon-reload'])
  file.write_text(json.dumps(record,indent=2)+'\n')
  if 'boot' in features:rebuild()
  raise
 print('System screens prepared. No logout or reboot was performed.',flush=True)
def main():
 p=argparse.ArgumentParser();p.add_argument('action',choices=['dependencies','screens','restore','bootstrap']);p.add_argument('--features',default='');p.add_argument('--native-ready',action='store_true');p.add_argument('--caption-ready',action='store_true');p.add_argument('--upgrade',action='store_true');p.add_argument('--purge',action='store_true');a=p.parse_args()
 home,store=context()
 if not detect()['family']:raise RuntimeError('No supported package manager detected.')
 features=[x for x in a.features.split(',') if x]
 known={x['id'] for x in json.loads((BASE/'features.json').read_text())}
 if not set(features)<=known:raise RuntimeError('Unknown installer feature.')
 if a.action=='dependencies':check_screens(store,features);deps(features,a.native_ready,a.caption_ready)
 elif a.action=='screens':screens(home,store,features,a.upgrade)
 elif a.action=='restore':
  restore(store)
  # (Remove's "delete the backups too": the system screens' backups as well.)
  if a.purge and not (store/'system.json').exists():shutil.rmtree(store,ignore_errors=True)
 else:install(detect()['family'],[],run,bootstrap=True)
if __name__=='__main__':
 try:main()
 except Exception as e:print('ERROR: '+str(e),file=sys.stderr);sys.exit(1)
