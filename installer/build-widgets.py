#!/usr/bin/python3
"""Build only the four XP widget plugins against this system's libraries."""
import hashlib,json,os,shutil,subprocess,sys,tarfile
import build_cache
from widget_sources import verified_archive
from pathlib import Path
from system_profile import detect,widget_target

def run(args):subprocess.run(list(map(str,args)),check=True,env={**os.environ,'QT_QPA_PLATFORM':'offscreen'})
def main():
 suite=Path(__file__).resolve().parent;build=Path(sys.argv[1]);jobs=sys.argv[2]
 series=widget_target(detect());cli=build/'runtime/bin/xplasma'
 if not series:raise RuntimeError('No matching XP widget build recipe for this Plasma/Qt combination.')
 info=subprocess.check_output([str(cli),'fork-info',series],text=True,env={**os.environ,'QT_QPA_PLATFORM':'offscreen'}).splitlines()
 root=build/'plasma'/series;pristine=root/'pristine';patched=root/'patched'
 pristine.mkdir(parents=True,exist_ok=True)
 sources={};targets={}
 for line in info:
  parts=line.split()
  if parts[0]=='source':
   _,project,version,expected=parts;sources[project]=version
   archive=verified_archive(suite,build/'plasma-source-cache',project,version,expected)
   if not (pristine/(project+'-'+version)).exists():
    with tarfile.open(archive) as t:t.extractall(pristine,filter='data')
  elif parts[0]=='target':targets.setdefault(parts[1],[]).append(parts[2])
 run([cli,'fork-sources',series,pristine,patched])
 out=build/'runtime/lib/xplasma/forks'/series/'plasma/applets';out.mkdir(parents=True,exist_ok=True)
 records=[]
 recipes=json.loads((build/'source/xplasma/src/app/overrides/forks.json').read_text())[series]['widgets']
 for project,names in targets.items():
  pending=[];identities={}
  for name in names:
   widget=next(w for w in recipes if w['id']==name)
   expected=build_cache.widget_identity(patched/(project+'-'+sources[project]),widget['dir'])
   identities[name]=expected
   relative='lib/xplasma/forks/'+series+'/plasma/applets/'+name+'.so'
   records.append({'path':relative,'identity':expected})
   cached=build_cache.find_component(suite,expected)
   if cached:
    shutil.copy2(cached/'runtime'/relative,out/(name+'.so'))
    print('Reusing verified widget: '+name,flush=True)
   else:pending.append(name)
  if not pending:continue
  native=root/'build'/project
  configure=['cmake','-S',patched/(project+'-'+sources[project]),'-B',native,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF']
  # Only the panel applet is built from plasma-desktop. Its unrelated X11
  # mouse KCM requires an SDK that older distribution packages don't ship.
  if project=='plasma-desktop':configure+=['-DBUILD_KCM_MOUSE_X11=OFF']
  run(configure)
  run(['cmake','--build',native,'--parallel',jobs,'--target',*pending])
  for name in pending:
   plugin=native/'bin/plasma/applets'/(name+'.so')
   symbols=subprocess.check_output(['objdump','-T',str(plugin)],text=True)
   if 'PRIVATE_API' in symbols:raise RuntimeError('Widget unexpectedly uses Qt private API: '+name)
   shutil.copy2(plugin,out/plugin.name)
   relative='lib/xplasma/forks/'+series+'/plasma/applets/'+name+'.so'
   build_cache.store_component(plugin,relative,identities[name])
 (build/'widget-components.json').write_text(json.dumps(records,indent=2)+'\n')
if __name__=='__main__':main()
