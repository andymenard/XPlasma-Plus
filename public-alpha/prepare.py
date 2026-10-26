#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export the alpha source and prune a separate local test installer, leaving the private suite intact."""
import argparse,json,shutil,subprocess,hashlib,tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PROFILE=json.loads((ROOT/'public-alpha/edition.json').read_text())
KEEP={'docs','native','browser','installer','themes','effects','tools','public-alpha','.github'}
EXCLUDED={'applications','tray','extras'}
PRIVATE_RELEASE_TESTS={'updater/tests/test_release_024.py','updater/tests/test_sounds_setup.py','updater/tests/test_window_outlines.py'}
def configure(base):
 features=json.loads((ROOT/'installer/features.json').read_text())
 features=[f for f in features if f.get('project') not in EXCLUDED and f['id']!='updates']
 (base/'features.json').write_text(json.dumps(features,indent=2)+'\n')
 (base/'edition.json').write_text(json.dumps(PROFILE,indent=2)+'\n')
def source(destination):
 if destination.resolve()==ROOT.resolve():raise RuntimeError('Public source destination must differ from the source checkout')
 subprocess.run(['python3',str(ROOT/'tools/sync-legal.py'),'--check'],check=True)
 destination.mkdir(parents=True,exist_ok=True)
 for name in PRIVATE_RELEASE_TESTS:
  (destination/name).unlink(missing_ok=True)
 paths=subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=ROOT).decode().split('\0')
 # Retire native source files removed by a merge; stale embedded resources
 # would otherwise produce a different source identity in public builds.
 if (destination/'.git').exists():
  tracked=subprocess.check_output(['git','ls-files','-z','--','native'],cwd=destination).decode().split('\0')
  for name in filter(None,tracked):
   if not (ROOT/name).is_file():(destination/name).unlink(missing_ok=True)

 for name in sorted(set(paths)-{''}):
  p=Path(name)
  if p.parts[0] not in KEEP:continue
  if p.parts[0]=='.github' and name not in {'.github/workflows/prebuilt-kubuntu.yml','.github/workflows/plasma-widget-compatibility.yml'}:continue
  if name in PRIVATE_RELEASE_TESTS:continue
  if name.startswith('tools/dark-theme/tests/') or '__pycache__' in p.parts:continue
  included_os_font = name in {'installer/assets/fonts/tahoma-os/tahoma-os.ttf','installer/assets/fonts/tahoma-os/tahoma-os-bold.ttf'}
  if p.suffix.lower() in {'.exe','.dll','.ttf','.msstyles','.vlt'} and not included_os_font:continue
  if p.suffix=='.pem' and name!='updater/release-key.pem':continue
  source=ROOT/p
  if source.is_symlink():raise RuntimeError('Source links must be reviewed before public export: '+name)
  if source.is_file():
   target=destination/p;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
 configure(destination/'installer')
 shutil.copy2(ROOT/'CREDITS.md',destination/'CREDITS.md')
 (destination/'LICENSE').write_text((ROOT/'native/LICENSE').read_text())
 (destination/'README.md').write_text((ROOT/'public-alpha/README.md').read_text())
 (destination/'installer/README.md').write_text((ROOT/'public-alpha/INSTALLER.md').read_text())
 (destination/'ASSET-DISTRIBUTION.md').write_text((ROOT/'public-alpha/ASSET-DISTRIBUTION.md').read_text())
 (destination/'.gitignore').write_text('build/\ndist/\n__pycache__/\n*.pyc\nprivate-assets/\n*.pem\n!updater/release-key.pem\n*signing-key*\n')
def bundle(suite,destination):
 subprocess.run(['python3',str(ROOT/'tools/sync-legal.py'),'--check'],check=True)
 if destination.exists():
  if not (destination/'LOCAL-ASSET-BUNDLE').is_file():raise RuntimeError('Destination is not an owned local alpha bundle')
  shutil.rmtree(destination)
 shutil.copytree(suite,destination,symlinks=True,ignore=shutil.ignore_patterns('__pycache__'))
 for name in ['accessories','shell','tray','codex','updater']:
  folder=destination/'payload'/name
  if folder.exists():shutil.rmtree(folder)
 configure(destination)
 for name in ['LICENSE','THIRD-PARTY-NOTICES']:
  shutil.copy2(ROOT/'installer'/name,destination/name)
 shutil.copytree(ROOT/'native/LICENSES',destination/'LICENSES',dirs_exist_ok=True)
 shutil.copytree(ROOT/'installer/assets/fonts/tahoma-os',destination/'payload/assets/fonts/tahoma-os',dirs_exist_ok=True)
 shutil.copy2(ROOT/'CREDITS.md',destination/'CREDITS.md')
 (destination/'README.md').write_text((ROOT/'public-alpha/README.md').read_text())
 (destination/'ASSET-DISTRIBUTION.md').write_text((ROOT/'public-alpha/ASSET-DISTRIBUTION.md').read_text())
 (destination/'PROJECTS.md').write_text('XPlasma Plus: Desktop (default) and Firefox (optional). OTA updates and other projects are excluded.\n')
 # Excluded projects' integration tests are not part of this edition.
 for name in ['test_accessory_integration.py','test_shell_integration.py','test_updater_integration.py']:
  test=destination/'tests'/name
  if test.exists():test.unlink()
 test=destination/'tests/test_installer.py'
 if test.exists():
  test.write_text(test.read_text().replace(' def test_codex_changes_do_not_replace_other_config('," @unittest.skipUnless('codex_colors' in b.KNOWN,'Codex integration is excluded from this edition')\n def test_codex_changes_do_not_replace_other_config("))
 # This is a local test bundle containing user-supplied assets, not a public upload.
 (destination/'LOCAL-ASSET-BUNDLE').write_text('Contains supplied proprietary XP/stock-photo assets. Do not upload without reviewing their separate rights.\n')
 inventory={}
 for p in sorted(destination.rglob('*')):
  if p.name=='SHA256SUMS.json' or '__pycache__' in p.parts:continue
  if p.is_symlink():
   if not p.resolve().is_relative_to(destination.resolve()):raise RuntimeError('External bundle link: '+str(p))
   inventory[str(p.relative_to(destination))]={'kind':'link','target':str(p.readlink())}
  elif p.is_file():inventory[str(p.relative_to(destination))]=hashlib.sha256(p.read_bytes()).hexdigest()
 (destination/'SHA256SUMS.json').write_text(json.dumps(inventory,indent=2)+'\n')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path);p.add_argument('--suite',type=Path);p.add_argument('--bundle',type=Path);a=p.parse_args()
 if a.source:source(a.source.resolve())
 if a.bundle:
  if not a.suite:p.error('--bundle requires --suite')
  bundle(a.suite.resolve(),a.bundle.resolve())
