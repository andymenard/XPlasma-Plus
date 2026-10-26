#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the production XP patches against pinned upstream Plasma branches."""
import argparse,hashlib,json,os,subprocess,tempfile,urllib.request
from pathlib import Path

def blob_sha(data):
 return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()

def fetch_sources(manifest,cache):
 cache.mkdir(parents=True,exist_ok=True)
 for series,branch in manifest['branches'].items():
  for spec in branch['files']:
   target=cache/spec['blob']
   if target.exists():
    if blob_sha(target.read_bytes())!=spec['blob']:raise RuntimeError('Cached source was modified: '+str(target))
    continue
   commit=branch['projects'][spec['project']]['commit']
   url='https://raw.githubusercontent.com/KDE/'+spec['project']+'/'+commit+'/'+spec['path']
   with urllib.request.urlopen(url,timeout=60) as response:data=response.read()
   if blob_sha(data)!=spec['blob']:raise RuntimeError('Upstream source hash mismatch: '+series+' '+spec['path'])
   with tempfile.NamedTemporaryFile(dir=cache,delete=False) as output:
    output.write(data);temporary=Path(output.name)
   temporary.replace(target)

def installed_sources(manifest,series,data,cache):
 """Exercise downstream packaged QML separately from the pinned upstream matrix."""
 if series not in {'6.0','6.1','6.2','6.3','6.4'}:
  raise ValueError('Installed QML checks require a packaged-widget Plasma branch (6.0–6.4)')
 branch=manifest['branches'][series]
 cache.mkdir(parents=True,exist_ok=True)
 packages={'taskmanager':'org.kde.plasma.taskmanager','panel':'org.kde.panel',
           'desktopcontainment':'org.kde.desktopcontainment','notifications':'org.kde.plasma.notifications',
           'systemtray':'org.kde.plasma.private.systemtray' if series in {'6.0','6.1','6.2','6.3'} else 'org.kde.plasma.systemtray'}
 for spec in branch['files']:
  widget,relative=spec['key'].split('/',1)
  if series=='6.0' and relative=='code/layoutmetrics.js':relative='code/layout.js'
  source=data/'plasma/plasmoids'/packages[widget]/'contents/ui'/relative
  content=source.read_bytes();spec['blob']=blob_sha(content)
  (cache/spec['blob']).write_bytes(content)
  print('Packaged source:',source,spec['blob'],flush=True)
 return {'branches':{series:branch}}

def lint_qml(tool,file,modern):
 if modern:
  subprocess.run([str(tool),'--ignore-settings','--max-warnings','-1',str(file)],check=True,stdout=subprocess.DEVNULL)
  return
 # Qt 6.6/6.7 lack --max-warnings. Their structured diagnostics distinguish
 # parser failures (including warning-level syntax messages) from analysis
 # warnings about the intentionally isolated file's missing sibling imports.
 result=subprocess.run([str(tool),'--ignore-settings','--json','-',str(file)],capture_output=True,text=True)
 report=json.loads(result.stdout)
 files=report.get('files',[])
 if result.returncode not in (0,255) or len(files)!=1 or Path(files[0]['filename']).resolve()!=file.resolve():
  raise RuntimeError('Unexpected qmllint result: '+result.stderr+' '+result.stdout)
 errors=[message for message in files[0].get('warnings',[]) if message.get('id')=='syntax' or message.get('type') not in {'debug','info','warning'}]
 if errors or (result.returncode and not files[0].get('warnings')):
  raise RuntimeError('QML parse failed: '+str(file)+' '+json.dumps(errors))

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--checker',type=Path,required=True)
 p.add_argument('--cache',type=Path,default=Path.home()/'.cache/xplasma-plus/widget-sources')
 parser=p.add_mutually_exclusive_group(required=True)
 parser.add_argument('--qmlformat',type=Path)
 parser.add_argument('--qmllint',type=Path)
 p.add_argument('--output',type=Path)
 p.add_argument('--installed-series',choices=['6.0','6.1','6.2','6.3','6.4'])
 p.add_argument('--system-data',type=Path,default=Path('/usr/share'))
 a=p.parse_args();manifest=Path(__file__).with_name('plasma-widget-sources.json')
 with tempfile.TemporaryDirectory(prefix='xp-widget-patches-') as temp:
  if a.installed_series:
   cache=Path(temp)/'blobs'
   installed=installed_sources(json.loads(manifest.read_text()),a.installed_series,a.system_data,cache)
   manifest=Path(temp)/'installed.json';manifest.write_text(json.dumps(installed))
  else:
   cache=a.cache;fetch_sources(json.loads(manifest.read_text()),cache)
  output=a.output or Path(temp)
  subprocess.run([str(a.checker),str(manifest),str(cache),str(output)],check=True)
  modern=bool(a.qmllint and '--max-warnings' in subprocess.check_output([str(a.qmllint),'--help'],text=True))
  count=0
  for file in sorted(output.rglob('*.qml')):
   # qmllint verifies the input without rewriting it. Older qmlformat
   # versions corrupt comma declarations in for loops during write-out.
   if a.qmllint:lint_qml(a.qmllint,file,modern)
   else:subprocess.run([str(a.qmlformat),'--ignore-settings',str(file)],check=True,stdout=subprocess.DEVNULL)
   count+=1
  print('Parsed',count,'patched QML files without syntax errors.',flush=True)

if __name__=='__main__':main()
