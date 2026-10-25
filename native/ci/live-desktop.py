#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real Plasma shell in a disposable X11 session, never a user's home.

Uses original geometric bitmap fixtures, not Windows artwork. This checks runtime
integration and interactions, not pixel fidelity or a full distribution login.
"""
import argparse,ast,base64,hashlib,json,os,re,shutil,struct,subprocess,tarfile,time,urllib.request
from pathlib import Path


def run(args,**kwargs):
 print('+',*map(str,args),flush=True)
 return subprocess.run(list(map(str,args)),check=True,text=True,**kwargs)


def public_assets(home):
 # Reuse immutable data already distributed in the project's public release;
 # never include extracted Windows resources in source or test artifacts.
 url='https://github.com/andymenard/XPlasma-Plus/releases/download/v1.0.1/xplasma-plus-1.0.1-installer-generic.tar.gz'
 expected='d7409746b2f95dcd5cb105b3bba47b062def3b70e9a79172b0ba2b153b2738f8'
 archive=home/'public-release.tar.gz';digest=hashlib.sha256()
 with urllib.request.urlopen(url,timeout=120) as response,archive.open('wb') as target:
  while chunk:=response.read(1024*1024):digest.update(chunk);target.write(chunk)
 if digest.hexdigest()!=expected:raise RuntimeError('Public test-data archive hash mismatch')
 with tarfile.open(archive,'r|gz') as source:
  for member in source:
   parts=Path(member.name).parts
   if not member.isfile() or 'payload' not in parts:continue
   relative=parts[parts.index('payload')+1:]
   if relative[:2]!=('assets','xplasma') and relative[:2]!=('assets','fonts'):continue
   tail=relative[2:]
   if not tail or any(p in {'.','..'} for p in tail):raise RuntimeError('Unsafe release test-data path')
   root=home/('.local/share/xplasma' if relative[1]=='xplasma' else '.local/share/fonts/xp')
   destination=root.joinpath(*tail);destination.parent.mkdir(parents=True,exist_ok=True)
   with source.extractfile(member) as content,destination.open('wb') as target:shutil.copyfileobj(content,target)
 archive.unlink();run(['fc-cache','-f'])


def stock_layout(home):
 # Exercise transformation of an existing stock desktop. Arch 6.8's first-login
 # layout script crashes in KConfigGroup before any XP code is applied; seed
 # equivalent ordinary stock applets instead of invoking that unrelated script.
 reply=subprocess.check_output(['gdbus','call','--session','--dest','org.kde.ActivityManager','--object-path','/ActivityManager/Activities','--method','org.kde.ActivityManager.Activities.CurrentActivity'],text=True,timeout=30)
 activity=ast.literal_eval(reply)[0]
 if not activity:raise RuntimeError('Activity manager did not supply a current activity')
 config=home/'.config/plasma-org.kde.plasma.desktop-appletsrc';config.parent.mkdir(parents=True,exist_ok=True)
 text='[Containments][1]\nactivityId='+activity+'\nformfactor=0\nlastScreen=0\nlocation=0\nplugin=org.kde.plasma.folder\nwallpaperplugin=org.kde.image\n\n'
 text+='[Containments][2]\nformfactor=2\nlastScreen=0\nlocation=4\nplugin=org.kde.panel\n\n[Containments][2][General]\nAppletOrder=3;4;5;6\n\n'
 for number,plugin in [(3,'org.kde.plasma.kickoff'),(4,'org.kde.plasma.icontasks'),(5,'org.kde.plasma.systemtray'),(6,'org.kde.plasma.digitalclock')]:
  text+='[Containments][2][Applets]['+str(number)+']\nplugin='+plugin+'\n\n'
 config.write_text(text)


def fixture_explorer(path):
 # Resource-only PE containing original geometric test bitmaps. The production
 # media parser and classic generators consume the same format as real media.
 images={143:(16,16),176:(16,16),172:(8,8),173:(8,8),7010:(72,24),7011:(72,24)}
 resources=bytearray(24+16+8*len(images))
 struct.pack_into('<HII',resources,14,1,2,0x80000000|24)
 struct.pack_into('<H',resources,24+14,len(images))
 for index,(key,(width,height)) in enumerate(images.items()):
  language=len(resources);resources.extend(bytes(24))
  entry=len(resources);resources.extend(bytes(16))
  struct.pack_into('<II',resources,24+16+8*index,key,0x80000000|language)
  struct.pack_into('<HII',resources,language+14,1,1033,entry)
  stride=(width*3+3)&~3
  pixels=b''.join(b''.join(bytes((40+(x%3)*60,80+(y%3)*50,180)) for x in range(width))+bytes(stride-width*3) for y in range(height))
  dib=struct.pack('<IiiHHIIiiII',40,width,height,1,24,0,len(pixels),0,0,0,0)+pixels
  struct.pack_into('<II',resources,entry,0x1000+len(resources),len(dib))
  resources.extend(dib)
 headers=bytearray(512);headers[:2]=b'MZ';struct.pack_into('<I',headers,0x3c,0x80)
 headers[0x80:0x84]=b'PE\0\0';struct.pack_into('<H',headers,0x86,1);struct.pack_into('<H',headers,0x94,224)
 optional=0x98;struct.pack_into('<H',headers,optional,0x10b);struct.pack_into('<I',headers,optional+92,16)
 struct.pack_into('<II',headers,optional+96+16,0x1000,len(resources))
 section=optional+224;headers[section:section+8]=b'.rsrc\0\0\0'
 struct.pack_into('<IIII',headers,section+8,len(resources),0x1000,len(resources),512)
 path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(headers+resources)


def script(js):
 reply=subprocess.check_output(['gdbus','call','--session','--dest','org.kde.plasmashell','--object-path','/PlasmaShell','--method','org.kde.PlasmaShell.evaluateScript',js],text=True,timeout=70)
 return ast.literal_eval(reply)[0].strip()


def wait_shell():
 deadline=time.monotonic()+60
 while time.monotonic()<deadline:
  try:
   if int(script('print(desktops().length)'))>0:return
  except (subprocess.SubprocessError,ValueError):pass
  time.sleep(1)
 raise RuntimeError('Plasma did not load a desktop')


def snapshot(output,name):
 run(['import','-window','root',output/(name+'.png')])


def layout(output,name):
 data=json.loads(script('print(JSON.stringify(panels().map(function(p){return {id:p.id,height:p.height,location:p.location,widgets:p.widgets().map(function(w){return {id:w.id,type:w.type}})}})))'))
 (output/(name+'.json')).write_text(json.dumps(data,indent=2)+'\n')
 return {w['type'] for p in data for w in p['widgets']}


def click_widget(widget):
 js='print(JSON.stringify((function(){for (const p of panels()){for(const w of p.widgets()){if(w.type==='+json.dumps(widget)+'){const g=w.geometry;return {x:g.x+Math.min(40,g.width/2),y:800-p.height+g.y+g.height/2,width:g.width,height:g.height};}}}return null;})()))'
 geometry=json.loads(script(js))
 if not geometry or geometry['width']<=0 or geometry['height']<=0:raise RuntimeError('Widget has no visible representation: '+widget)
 run(['xdotool','mousemove',str(round(geometry['x'])),str(round(geometry['y'])),'click','1'])


def changed(output,before,after,box):
 from PIL import Image,ImageChops
 a=Image.open(output/(before+'.png')).convert('RGB').crop(box)
 b=Image.open(output/(after+'.png')).convert('RGB').crop(box)
 if not ImageChops.difference(a,b).getbbox():raise RuntimeError(after+' did not change the expected desktop region')


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--public-release-assets',action='store_true');a=p.parse_args()
 home=Path.home();output=a.output.resolve();output.mkdir(parents=True,exist_ok=True)
 if not (home/'.xplasma-disposable-desktop').is_file():raise RuntimeError('Refusing a home not explicitly marked disposable')
 cli=a.runtime.resolve()/'bin/xplasma'
 if a.public_release_assets:public_assets(home)
 else:fixture_explorer(home/'.local/share/xplasma/extracted/explorer.exe')
 look=str(home/'.local/share/xplasma/assets/themes/Luna/luna.msstyles') if a.public_release_assets else 'classic:Windows Standard'
 # Preserve restarted shells' logs: production intentionally detaches them.
 wrappers=home/'bin';wrappers.mkdir(exist_ok=True);real=shutil.which('plasmashell')
 import shlex
 wrapper=wrappers/'plasmashell';wrapper.write_text('#!/bin/sh\nif [ "${1:-}" = --version ]; then exec '+shlex.quote(real)+' "$@"; fi\nexec '+shlex.quote(real)+' "$@" >> '+shlex.quote(str(output/'plasmashell.log'))+' 2>&1\n');wrapper.chmod(0o755)
 os.environ['PATH']=str(wrappers)+':'+os.environ['PATH']
 processes=[]
 try:
  with (output/'session.log').open('w') as log:
   processes.append(subprocess.Popen(['openbox'],stdout=log,stderr=log))
   stock_layout(home)
   processes.append(subprocess.Popen(['plasmashell'],stdout=log,stderr=log))
   try:wait_shell()
   except RuntimeError:
    if shutil.which('gdb'):
     with (output/'startup-backtrace.log').open('w') as trace:
      subprocess.run(['timeout','30','gdb','--batch','-ex','run','-ex','thread apply all bt','--args',real,'--no-respawn'],stdout=trace,stderr=subprocess.STDOUT,check=False)
    raise
   snapshot(output,'before')
   # Real desktop application path: this checks preflight, plugin selection,
   # patches, generated themes, layout changes and shell restarts together.
   args=[cli,'apply',look,'--no-decoration','--no-fonts','--no-widgets','--no-icons','--no-sounds','--no-wallpaper']
   for attempt in (1,2):
    with (output/('apply-'+str(attempt)+'.log')).open('w') as apply_log:run(args,stdout=apply_log,stderr=subprocess.STDOUT,timeout=240)
    wait_shell();time.sleep(5)
    found=layout(output,'applied-'+str(attempt))
    required={'fyi.hotsocket.xplasma.start','fyi.hotsocket.xplasma.clock','org.kde.plasma.taskmanager','org.kde.plasma.systemtray'}
    if not required<=found:raise RuntimeError('Required XP widgets missing: '+str(required-found))
   # Make the task manager render a real managed application window.
   processes.append(subprocess.Popen(['xterm','-title','XPlasmaLiveTest','-e','sleep','240'],stdout=log,stderr=log))
   time.sleep(3);snapshot(output,'applied')
   window=subprocess.check_output(['xdotool','search','--onlyvisible','--name','^XPlasmaLiveTest$'],text=True).splitlines()[-1]
   run(['xdotool','windowminimize',window]);time.sleep(1)
   click_widget('org.kde.plasma.taskmanager');time.sleep(2)
   active=subprocess.check_output(['xdotool','getactivewindow'],text=True).strip()
   if active!=window:raise RuntimeError('XP task button did not activate the application window')
   click_widget('fyi.hotsocket.xplasma.start');time.sleep(3);snapshot(output,'start-menu')
   changed(output,'applied','start-menu',(0,200,500,760))
   screenshot=(output/'start-menu.png').read_bytes()
   if len(screenshot)<500000:print('XPLASMA_SCREENSHOT_BASE64 '+base64.b64encode(screenshot).decode(),flush=True)
   run(['xdotool','key','Escape']);time.sleep(1)
   snapshot(output,'clock-before')
   click_widget('fyi.hotsocket.xplasma.clock');time.sleep(2);snapshot(output,'clock-calendar')
   changed(output,'clock-before','clock-calendar',(780,100,1280,760))
   run(['xdotool','key','Escape']);time.sleep(1);snapshot(output,'notification-before')
   run(['gdbus','call','--session','--dest','org.freedesktop.Notifications','--object-path','/org/freedesktop/Notifications','--method','org.freedesktop.Notifications.Notify','XPlasmaLiveTest','0','dialog-information','XPlasma live desktop test','Notification delivered by the real Plasma service','[]','{}','10000'])
   time.sleep(3);snapshot(output,'notification')
   changed(output,'notification-before','notification',(0,100,1280,760))
   # Inspect the replacement plugins actually mapped by the running shell.
   for proc in Path('/proc').iterdir():
    if proc.name.isdigit():
     try:
      if (proc/'comm').read_text().strip()=='plasmashell':
       maps=(proc/'maps').read_text();(output/'plasmashell-maps.txt').write_text(maps)
     except (OSError,ProcessLookupError):pass
   state=json.loads((home/'.local/share/xplasma/state.json').read_text())
   maps=(output/'plasmashell-maps.txt').read_text()
   for plugin in state.get('settings',{}).get('forkPlugins',[]):
    if plugin not in maps:raise RuntimeError('Required replacement plugin was not loaded: '+plugin)
   if a.public_release_assets:
    with (output/'switch-classic.log').open('w') as switch_log:
     run([cli,'apply','classic:Windows Standard',*args[3:]],stdout=switch_log,stderr=subprocess.STDOUT,timeout=240)
    wait_shell();time.sleep(3);snapshot(output,'switched-classic')
   with (output/'reset.log').open('w') as reset_log:run([cli,'reset'],stdout=reset_log,stderr=subprocess.STDOUT,timeout=240)
   wait_shell();time.sleep(5)
   if any(t.startswith('fyi.hotsocket.xplasma.') for t in layout(output,'reset')):raise RuntimeError('XP applets remained after reset')
   snapshot(output,'reset')
  logs=(output/'plasmashell.log').read_text()
  bad=[line for line in logs.splitlines() if re.search(r'(TypeError|ReferenceError|is not a type|Error loading QML|module .* is not installed|Binding loop)',line) and not line.startswith('file:///usr/share/plasma/') and re.search(r'(xplasma|Xp[A-Z]|org.kde.plasma.(taskmanager|systemtray|notifications)|org.kde.panel)',line,re.I)]
  if bad:raise RuntimeError('XP widget runtime errors:\n'+'\n'.join(bad))
  (output/'result.json').write_text(json.dumps({'status':'passed','coverage':['real Plasma shell','apply','repeat apply','XP widget layout','task button activation','Start popup','clock calendar popup','notification popup','reset'],'assets':'public v1.0.1 Luna/Classic' if a.public_release_assets else 'original geometric fixtures','limits':['Xvfb and Openbox; not a complete KWin/login/hardware certification','Desktop runtime apply/reset; not the full installer lifecycle']},indent=2)+'\n')
 except Exception:
  for logfile in sorted(output.glob('*.log')):
   print('--- '+logfile.name+' ---',flush=True)
   print('\n'.join(logfile.read_text(errors='replace').splitlines()[-100:]),flush=True)
  raise
 finally:
  for process in reversed(processes):
   if process.poll() is None:process.terminate()

if __name__=='__main__':main()
