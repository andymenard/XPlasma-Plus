#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build locally for the installed KWin ABI, reusing a matching build only."""
import argparse,hashlib,json,os,platform,re,shutil,subprocess,tempfile,sys
from pathlib import Path

SOURCE=Path(__file__).resolve().parent
PACKAGES=['build-essential','cmake','ninja-build','extra-cmake-modules','kwin-dev','qt6-declarative-dev','libdrm-dev','libwayland-dev','libvulkan-dev']
# KWin series this source builds for; system_profile.CAPTION_KWIN_SERIES and
# CMakeLists.txt must agree (tests check).
SUPPORTED_KWIN=('6.6','6.7','6.8')
SOURCES=['CMakeLists.txt','effect.cpp','caption.h','metadata.json','test-caption.cpp','demo.cpp']

def run(args,**kwargs):
    print('+ '+' '.join(map(str,args)),flush=True)
    return subprocess.run(list(map(str,args)),check=True,**kwargs)

def kwin_version(text):
    match=re.search(r'(?:kwin(?:_wayland|_x11)?|KWin version:)\s+(6\.\d+\.\d+)',text,re.I)
    if not match:raise RuntimeError('Cannot identify the installed KWin version.')
    version=match.group(1)
    if version.rsplit('.',1)[0] not in SUPPORTED_KWIN:raise RuntimeError('This source supports KWin '+', '.join(SUPPORTED_KWIN)+'; KWin '+version+' needs a source update.')
    return version

def fingerprint(kwin,qt,packages,arch=None):
    hashes={name:hashlib.sha256((SOURCE/name).read_bytes()).hexdigest() for name in SOURCES}
    return {'kwin':kwin,'qt':qt,'arch':arch or platform.machine(),'packages':packages,'source':hashes}

def cached(record,expected,plugin):
    return record.get('abi')==expected and plugin.is_file() and record.get('sha256')==hashlib.sha256(plugin.read_bytes()).hexdigest()

def atomic(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent,delete=False) as tmp:
        tmp.write(data);tmp.flush();os.fsync(tmp.fileno());name=tmp.name
    os.chmod(name,0o644);os.replace(name,path)

def running_effect_session():
    dbus=shutil.which('qdbus6') or shutil.which('qdbus-qt6') or '/usr/lib/qt6/bin/qdbus'
    try:
        effects=subprocess.run([dbus,'org.kde.KWin','/Effects','org.kde.kwin.Effects.loadedEffects'],check=True,capture_output=True,text=True).stdout.split()
        if 'xp-caption-animation' not in effects:return None
        pid=subprocess.run([dbus,'org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.GetConnectionUnixProcessID','org.kde.KWin'],check=True,capture_output=True,text=True).stdout.strip()
        return int(pid)
    except (OSError,ValueError,subprocess.CalledProcessError):return None

def activate(plugin,record,version):
    if record.get('sha256')!=hashlib.sha256(plugin.read_bytes()).hexdigest():raise RuntimeError('The local module changed after it was built.')
    dbus=shutil.which('qdbus6') or shutil.which('qdbus-qt6') or '/usr/lib/qt6/bin/qdbus'
    writer=shutil.which('kwriteconfig6')
    if not writer or not shutil.which(dbus):raise RuntimeError('Plasma 6 D-Bus and configuration tools are required.')
    live=run([dbus,'org.kde.KWin','/KWin','supportInformation'],capture_output=True,text=True).stdout
    if kwin_version(live)!=version:raise RuntimeError('KWin was upgraded during this session. Log out/in before loading the new build.')
    loaded=run([dbus,'org.kde.KWin','/Effects','org.kde.kwin.Effects.loadedEffects'],capture_output=True,text=True).stdout.split()
    if 'xp-caption-animation' in loaded:
        # Replacing a file does not replace the module already mapped into KWin.
        if record.get('just_built') or (record.get('replaced_session') is not None and record['replaced_session']==running_effect_session()):raise RuntimeError('New build installed. Log out/in to replace the already loaded animation safely.')
    else:
        result=run([dbus,'org.kde.KWin','/Effects','org.kde.kwin.Effects.loadEffect','xp-caption-animation'],capture_output=True,text=True)
        if result.stdout.strip()!='true':raise RuntimeError('Build installed. Log out/in to refresh plugin discovery, then run this launcher again.')
    backup=Path.home()/'.local/share/xplasma/maintenance/caption-local-build'
    backup.mkdir(parents=True,exist_ok=True)
    config=Path.home()/'.config/kwinrc'
    if config.exists() and not (backup/'kwinrc.before').exists():shutil.copy2(config,backup/'kwinrc.before')
    for effect,value in [('xp-caption-animation','true'),('maximize','false'),('squash','false'),('magiclamp','false')]:
        run([writer,'--file','kwinrc','--group','Plugins','--key',effect+'Enabled',value])
        if value=='false':subprocess.run([dbus,'org.kde.KWin','/Effects','org.kde.kwin.Effects.unloadEffect',effect],check=False,capture_output=True)
    print('XP caption animation enabled.')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--install-deps',action='store_true');parser.add_argument('--activate',action='store_true');parser.add_argument('--force',action='store_true');parser.add_argument('--interactive',action='store_true');args=parser.parse_args()
    if os.getuid()==0:raise RuntimeError('Run as your desktop user. Only dependency installation uses sudo.')
    if args.install_deps:
        if not shutil.which('apt-get'):raise RuntimeError('Automatic dependency installation currently supports Kubuntu/Ubuntu. Install the matching KWin SDK manually on other systems.')
        missing=subprocess.run(['dpkg-query','-W','-f=${db:Status-Abbrev}\n',*PACKAGES],capture_output=True,text=True)
        if missing.returncode or any(not line.startswith('ii ') for line in missing.stdout.splitlines()):
            run(['sudo','apt-get','update']);run(['sudo','apt-get','install','--no-install-recommends','-y',*PACKAGES])
    kwin=shutil.which('kwin_wayland') or shutil.which('kwin_x11')
    qmake=shutil.which('qmake6') or shutil.which('qmake-qt6')
    if not kwin or not qmake or not shutil.which('cmake') or not shutil.which('ninja'):
        raise RuntimeError('Build dependencies are missing. Run again with --install-deps on Kubuntu.')
    version=kwin_version(run([kwin,'--version'],capture_output=True,text=True).stdout)
    qt=run([qmake,'-query','QT_VERSION'],capture_output=True,text=True).stdout.strip()
    packages=''
    if shutil.which('dpkg-query'):
        packages=run(['dpkg-query','-W','-f=${Package}=${Version}\n','kwin*','libkwin*','libqt6*','libkf6*','extra-cmake-modules'],capture_output=True,text=True).stdout
    expected=fingerprint(version,qt,packages)
    home=Path.home();root=home/'.local/share/kwin/xplasma/caption-build';root.mkdir(parents=True,exist_ok=True)
    plugin=home/'.local/lib/qt6/plugins/kwin/effects/plugins/xp-caption-animation.so'
    stamp=root/'installed.json';record=json.loads(stamp.read_text()) if stamp.exists() else {}
    built=False
    if args.force or not cached(record,expected,plugin):
        with tempfile.TemporaryDirectory(prefix='build-',dir=root) as folder:
            build=Path(folder)
            run(['cmake','-S',SOURCE,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release'])
            abi=json.loads((build/'build-abi.json').read_text())
            if abi['kwin']!=version or abi['qt']!=qt:raise RuntimeError('Development libraries do not match installed KWin/Qt. No plugin was replaced.')
            run(['cmake','--build',build,'--parallel',str(min(4,os.cpu_count() or 1))])
            env={**os.environ,'QT_QPA_PLATFORM':'offscreen'}
            run(['ctest','--test-dir',build,'--output-on-failure'],env=env)
            binary=build/'lib/kwin/effects/plugins/xp-caption-animation.so'
            linkage=run(['ldd','-r',binary],capture_output=True,text=True)
            if 'not found' in linkage.stdout or 'undefined symbol:' in linkage.stdout+linkage.stderr:raise RuntimeError('The compiled plugin has unresolved libraries/symbols; not installed.')
            if plugin.exists():shutil.copy2(plugin,root/'previous-plugin.so')
            replaced_session=running_effect_session()
            atomic(plugin,binary.read_bytes())
        record={'abi':expected,'sha256':hashlib.sha256(plugin.read_bytes()).hexdigest(),'replaced_session':replaced_session}
        atomic(stamp,(json.dumps(record,indent=2)+'\n').encode());built=True
        print('Matching native animation built and installed.')
    else:print('The installed animation already matches this KWin, Qt and source; no rebuild needed.')
    # A separate file preserves the existing XPlasma environment setup.
    environment=home/'.config/plasma-workspace/env/xplasma-caption.sh'
    atomic(environment,b'export QT_PLUGIN_PATH="$HOME/.local/lib/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"\n')
    if args.activate:activate(plugin,{**record,'just_built':built},version)

if __name__=='__main__':
    try:main()
    except (RuntimeError,subprocess.CalledProcessError,OSError,ValueError) as error:
        raise SystemExit('Caption animation: '+str(error))

    finally:
        if '--interactive' in sys.argv:
            try:input('\nPress Enter to close…')
            except EOFError:pass
