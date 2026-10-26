#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Assemble reviewed installer variants from a synced, complete base bundle."""
import argparse,hashlib,importlib.util,json,os,shutil,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'installer'))
import build_cache
spec=importlib.util.spec_from_file_location('release_package',ROOT/'public-alpha/package.py')
packager=importlib.util.module_from_spec(spec);spec.loader.exec_module(packager)
TARGETS=('generic','kubuntu-26.04-amd64','kubuntu-26.04-arm64','fedora-44-amd64','fedora-44-arm64')

def sha(path):
 with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def inventory(folder):
 result={}
 for path in sorted(folder.rglob('*')):
  relative=path.relative_to(folder)
  if path.name=='SHA256SUMS.json' or '__pycache__' in relative.parts or path.suffix=='.pyc':continue
  if not path.resolve().is_relative_to(folder.resolve()):raise RuntimeError('External bundle link: '+str(relative))
  if path.is_symlink():result[str(relative)]={'kind':'link','target':os.readlink(path)}
  elif path.is_file():result[str(relative)]=sha(path)
 (folder/'SHA256SUMS.json').write_text(json.dumps(result,indent=2)+'\n')
 return len(result)

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--base',type=Path,required=True);p.add_argument('--prebuilt-root',type=Path,required=True)
 p.add_argument('--staging',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--source-commit',required=True);p.add_argument('--build-commit',required=True)
 p.add_argument('--build-run',type=int,required=True);p.add_argument('--only',choices=TARGETS)
 a=p.parse_args();a.base=a.base.resolve();a.staging=a.staging.resolve();a.output=a.output.resolve()
 if a.base==a.staging or a.staging.is_relative_to(a.base):raise RuntimeError('Staging must be outside the base bundle')
 edition=json.loads((a.base/'edition.json').read_text());version=edition['version']
 source_id=build_cache.source_id(a.base);a.staging.mkdir(parents=True,exist_ok=True)
 release_notes=ROOT/('public-alpha/RELEASE-NOTES-'+version+'.md')
 if not release_notes.is_file():raise RuntimeError('Release notes are missing')
 provenance=[]
 for target in ([a.only] if a.only else TARGETS):
  suite=a.staging/target
  if suite.exists():
   if not (suite/'RELEASE-STAGING.json').is_file():raise RuntimeError('Refusing to replace an unowned staging directory')
   shutil.rmtree(suite)
  shutil.copytree(a.base,suite,symlinks=True,ignore=shutil.ignore_patterns('__pycache__','*.pyc','prebuilt'))
  (suite/'RELEASE-STAGING.json').write_text(json.dumps({'version':version,'variant':target})+'\n')
  entries=[]
  if target!='generic':
   cache=a.prebuilt_root/target
   if not cache.is_dir():raise RuntimeError('Missing prebuilt input: '+target)
   shutil.copytree(cache,suite/'prebuilt',symlinks=True)
   complete=0
   for manifest in sorted((suite/'prebuilt').rglob('manifest.json')):
    data=json.loads(manifest.read_text());identity=data['identity']
    if identity.get('schema')==1:
     if identity.get('source')!=source_id or not build_cache.validate(manifest.parent,identity):raise RuntimeError('Stale or invalid complete runtime: '+str(manifest))
     expected_arch=target.rsplit('-',1)[1]
     expected_distribution='ubuntu' if target.startswith('kubuntu-') else 'fedora'
     if identity['arch']!=expected_arch or identity['distribution']!=expected_distribution:raise RuntimeError('Wrong runtime target: '+target)
     complete+=1
    elif not build_cache.validate_component(manifest.parent,identity):raise RuntimeError('Invalid component cache: '+str(manifest))
    entries.append(identity)
   if complete!=1:raise RuntimeError('Expected exactly one freshly built complete runtime: '+target)
  record={'release':version,'variant':target,'source_commit':a.source_commit,'native_source_sha256':source_id,
          'ci_run':a.build_run,'ci_build_commit':a.build_commit,'prebuilt':entries,'ota_included':False}
  (suite/'BUILD-PROVENANCE.json').write_text(json.dumps(record,indent=2)+'\n');provenance.append(record)
  (suite/'VARIANT.txt').write_text('XPlasma Plus '+edition['label']+' — '+target+'\nMatching verified binaries are used when available; other supported environments build locally.\n')
  shutil.copy2(release_notes,suite/'RELEASE-NOTES.md')
  shutil.copy2(ROOT/'public-alpha/README.md',suite/'README.md')
  for name in ['PRERELEASE-NOTES.txt','THIRD-PARTY-ACCESSORIES.md','RELEASE-NOTES-0.2.6.md']:(suite/name).unlink(missing_ok=True)
  (suite/'VALIDATION.md').write_text('Desktop runtime acceptance passed across Plasma 6.0–6.8. Release build identities and every packaged resource are verified. See RELEASE-VALIDATION.json beside the archives for package-level results. Full login/hardware and system-wide installer lifecycle certification are not claimed. Void remains experimental.\n')
  inventory(suite);packager.package(suite,a.output,target)
 archives=sorted(a.output.glob('xplasma-plus-'+version+'-installer-*.tar.gz'))
 (a.output/'SHA256SUMS').write_text(''.join(sha(path)+'  '+path.name+'\n' for path in archives))
 (a.output/'BUILD-PROVENANCE.json').write_text(json.dumps(provenance,indent=2)+'\n')
 shutil.copy2(release_notes,a.output/'RELEASE-NOTES.md');shutil.copy2(ROOT/'CREDITS.md',a.output/'CREDITS.md')
 print('Prepared',len(archives),'verified release archives in',a.output,flush=True)

if __name__=='__main__':main()
