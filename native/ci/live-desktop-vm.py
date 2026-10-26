#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test Plasma 6.8 inside a disposable Arch VM with a real virtio/virgl GPU.

The outer display uses Mesa software rendering. The guest has its own kernel,
DRM device and Xorg/DRI3, which current KWin requires for nested rendering.
"""
import argparse,hashlib,json,os,shutil,subprocess,tarfile,tempfile,time,urllib.request
from pathlib import Path

IMAGE='Arch-Linux-x86_64-cloudimg-20261015.611680.qcow2'
ORIGIN='https://geo.mirror.pkgbuild.com/images/latest/'


def run(args,**kwargs):
 print('+',*map(str,args),flush=True)
 return subprocess.run(list(map(str,args)),check=True,text=True,**kwargs)


def download(url,target):
 with urllib.request.urlopen(url,timeout=120) as source,target.open('wb') as output:
  shutil.copyfileobj(source,output)


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 output=a.output.resolve();output.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='xplasma-live-vm-') as temporary:
  work=Path(temporary);base=work/IMAGE;checksum=work/(IMAGE+'.SHA256')
  download(ORIGIN+IMAGE+'.SHA256',checksum);expected=checksum.read_text().split()[0].lower()
  if len(expected)!=64 or any(c not in '0123456789abcdef' for c in expected):raise RuntimeError('Invalid official cloud-image checksum')
  download(ORIGIN+IMAGE,base)
  with base.open('rb') as stream:actual=hashlib.file_digest(stream,'sha256').hexdigest()
  if actual!=expected:raise RuntimeError('Official Arch cloud image hash mismatch')
  (output/'vm-image.json').write_text(json.dumps({'image':ORIGIN+IMAGE,'sha256':actual,'graphics':'virtio-vga-gl; outer Mesa software renderer','execution':'disposable QEMU guest'},indent=2)+'\n')
  info=json.loads(subprocess.check_output(['qemu-img','info','--output=json',str(base)],text=True))
  disk=work/'guest.qcow2';run(['qemu-img','create','-f','qcow2','-F','qcow2','-b',base,disk,str(max(info['virtual-size'],24*1024**3))])
  key=work/'vm-key';run(['ssh-keygen','-q','-t','ed25519','-N','','-f',key])
  seed=work/'seed';seed.mkdir()
  cloud={'users':[{'name':'live','groups':['wheel','video','render'],'shell':'/bin/bash','sudo':'ALL=(ALL) NOPASSWD:ALL','ssh_authorized_keys':[key.with_suffix('.pub').read_text().strip()]}],
         'ssh_pwauth':False,'runcmd':[['systemctl','enable','--now','sshd']]}
  (seed/'user-data').write_text('#cloud-config\n'+json.dumps(cloud,indent=2)+'\n')
  (seed/'meta-data').write_text('instance-id: xplasma-live-ci\nlocal-hostname: xplasma-live-vm\n')
  (seed/'network-config').write_text('version: 2\nethernets:\n  ethernet:\n    match:\n      macaddress: "52:54:00:12:34:56"\n    dhcp4: true\n')
  run(['xorriso','-as','mkisofs','-o',work/'seed.iso','-V','cidata','-r','-J','user-data','meta-data','network-config'],cwd=seed)
  codes=sorted(Path('/usr/share').glob('edk2*/**/OVMF_CODE*.fd'))
  code=next((f for f in codes if not any(s in f.name.lower() for s in ('secboot','ms.','snakeoil')) and f.with_name(f.name.replace('CODE','VARS')).exists()),None)
  if code is None:raise RuntimeError('Matching OVMF code/variable firmware is missing')
  variables=work/'OVMF_VARS.fd';shutil.copy2(code.with_name(code.name.replace('CODE','VARS')),variables)
  accelerated=os.access('/dev/kvm',os.R_OK|os.W_OK)
  command=['qemu-system-x86_64','-machine','q35,accel='+('kvm' if accelerated else 'tcg'),'-cpu','host' if accelerated else 'max','-smp','2','-m','3072',
   '-drive','if=pflash,format=raw,readonly=on,file='+str(code),'-drive','if=pflash,format=raw,file='+str(variables),
   '-drive','if=virtio,format=qcow2,file='+str(disk),'-drive','format=raw,media=cdrom,readonly=on,file='+str(work/'seed.iso'),
   '-vga','none','-device','virtio-vga-gl,xres=1280,yres=800','-display','gtk,gl=on',
   '-nic','user,model=virtio-net-pci,mac=52:54:00:12:34:56,hostfwd=tcp:127.0.0.1:2222-:22',
   '-serial','file:'+str(output/'guest-serial.log')]
  ssh=['ssh','-i',str(key),'-p','2222','-o','StrictHostKeyChecking=accept-new','-o','UserKnownHostsFile='+str(work/'known-hosts'),'-o','ConnectTimeout=5','live@127.0.0.1']
  scp=['scp','-i',str(key),'-P','2222','-o','StrictHostKeyChecking=accept-new','-o','UserKnownHostsFile='+str(work/'known-hosts')]
  with (output/'qemu.log').open('w') as log:
   machine=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
   try:
    deadline=time.monotonic()+600
    while time.monotonic()<deadline:
     if machine.poll() is not None:raise RuntimeError('QEMU exited before the guest booted')
     if subprocess.run([*ssh,'true'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0:break
     time.sleep(5)
    else:raise RuntimeError('Disposable VM did not become reachable')
    run([*ssh,'sudo pacman -Syu --needed --noconfirm plasma-workspace plasma-desktop kdeplasma-addons plasma-pa kwin xorg-server xorg-xauth xorg-xrandr mesa mesa-utils xterm imagemagick xdotool python python-pillow glib2 dbus libarchive qt6-svg qt6-5compat'],timeout=1800)
    payload=work/'runtime.tar.gz'
    with tarfile.open(payload,'w:gz') as archive:
     archive.add(a.runtime.resolve(),arcname='runtime');archive.add(Path(__file__).with_name('live-desktop.py'),arcname='live-desktop.py')
    run([*scp,payload,'live@127.0.0.1:/tmp/runtime.tar.gz'])
    setup=work/'guest-test.sh';setup.write_text('''#!/bin/bash
set -eu
mkdir -p /tmp/xp /tmp/xp-live-runtime
chmod 700 /tmp/xp-live-runtime
tar -xzf /tmp/runtime.tar.gz -C /tmp/xp
touch "$HOME/.xplasma-disposable-desktop"
sudo sh -c 'nohup /usr/lib/Xorg :99 -ac -noreset -nolisten tcp vt7 > /tmp/xplasma-xorg.log 2>&1 &'
export XDG_DATA_HOME="$HOME/.local/share" XDG_CONFIG_HOME="$HOME/.config" XDG_CACHE_HOME="$HOME/.cache"
export DISPLAY=:99 XDG_RUNTIME_DIR=/tmp/xp-live-runtime XDG_CURRENT_DESKTOP=KDE XDG_MENU_PREFIX=plasma- KDE_SESSION_VERSION=6 LC_ALL=C.UTF-8 QT_FORCE_STDERR_LOGGING=1
for attempt in $(seq 1 60); do xrandr --current >/dev/null 2>&1 && break; sleep 2; done
# Xorg's modesetting driver starts this cloud image at 640x480 even when the
# virtio device advertises a larger preferred size. Give the nested desktop
# and the screenshot/input assertions the same complete 1280x800 viewport.
connector=$(xrandr --query | awk '/ connected/{print $1; exit}')
test -n "$connector"
xrandr --newmode XPlasma1280x800 83.50 1280 1352 1480 1680 800 803 809 831 -HSync +VSync
xrandr --addmode "$connector" XPlasma1280x800
xrandr --output "$connector" --mode XPlasma1280x800
xrandr --current
glxinfo -B
python3 /tmp/xp/live-desktop.py --runtime /tmp/xp/runtime --output /tmp/live-desktop-results --prepare-session-only
kbuildsycoca6 --noincremental
cat > /tmp/xp-live-session.sh <<'SH'
#!/bin/sh
export QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software XDG_SESSION_TYPE=wayland
exec python3 /tmp/xp/live-desktop.py --runtime /tmp/xp/runtime --output /tmp/live-desktop-results --public-release-assets --nested-kwin > /tmp/xplasma-driver.log 2>&1
SH
chmod +x /tmp/xp-live-session.sh
result=0
dbus-run-session -- kwin_wayland --x11-display :99 --width 1280 --height 800 --xwayland --no-lockscreen --no-global-shortcuts --exit-with-session /tmp/xp-live-session.sh > /tmp/xplasma-kwin.log 2>&1 || result=$?
mkdir -p /tmp/live-desktop-results
cp /tmp/xplasma-xorg.log /tmp/xplasma-kwin.log /tmp/live-desktop-results/
cat /tmp/xplasma-kwin.log
printf 'Nested KWin exit status: %s\\n' "$result"
if [ -f /tmp/xplasma-driver.log ]; then
  cp /tmp/xplasma-driver.log /tmp/live-desktop-results/
  cat /tmp/xplasma-driver.log
fi
if [ "$result" != 0 ] || [ ! -f /tmp/live-desktop-results/result.json ]; then
  sudo journalctl --no-pager -b -n 100 > /tmp/live-desktop-results/guest-journal.log
  sudo coredumpctl info --no-pager > /tmp/live-desktop-results/guest-coredumps.log 2>&1 || true
  cat /tmp/live-desktop-results/guest-coredumps.log
fi
test "$result" = 0
python3 -c 'import json; assert json.load(open("/tmp/live-desktop-results/result.json"))["status"] == "passed"'
''')
    run([*scp,setup,'live@127.0.0.1:/tmp/guest-test.sh'])
    try:run([*ssh,'bash /tmp/guest-test.sh'],timeout=1800)
    finally:
     subprocess.run([*scp,'-r','live@127.0.0.1:/tmp/live-desktop-results/.',str(output)],check=False)
     subprocess.run([*scp,'live@127.0.0.1:/tmp/xplasma-xorg.log',str(output)],check=False)
   except Exception:
    for name in ['qemu.log','guest-serial.log']:
     file=output/name
     if file.exists():print('--- '+name+' ---\n'+'\n'.join(file.read_text(errors='replace').splitlines()[-80:]),flush=True)
    raise
   finally:
    machine.terminate()
    try:machine.wait(timeout=10)
    except subprocess.TimeoutExpired:machine.kill();machine.wait(timeout=5)

if __name__=='__main__':main()
