"""Obtain pinned upstream Plasma sources without changing system repositories."""
import hashlib,re,tempfile,urllib.request
from pathlib import Path

def verified_archive(suite,cache,project,version,expected):
 if project not in {'plasma-workspace','plasma-desktop'} or not re.fullmatch(r'6\.\d+\.\d+',version):
  raise ValueError('Invalid Plasma source recipe')
 if not re.fullmatch(r'[0-9a-f]{64}',expected):raise ValueError('Plasma source recipe needs a SHA-256 digest')
 name=project+'-'+version+'.tar.xz';bundled=Path(suite)/'source'/name
 archive=bundled if bundled.is_file() else Path(cache)/name
 if not archive.is_file():
  archive.parent.mkdir(parents=True,exist_ok=True)
  channel='unstable' if int(version.split('.')[-1])>=90 else 'stable'
  url='https://download.kde.org/'+channel+'/plasma/'+version+'/'+name
  temporary=None
  try:
   digest=hashlib.sha256()
   with urllib.request.urlopen(url,timeout=60) as source,tempfile.NamedTemporaryFile(dir=archive.parent,delete=False) as output:
    temporary=Path(output.name)
    while chunk:=source.read(1024*1024):digest.update(chunk);output.write(chunk)
   if digest.hexdigest()!=expected:raise RuntimeError('Plasma source checksum mismatch: '+name)
   temporary.replace(archive)
  finally:
   if temporary and temporary.exists():temporary.unlink()
 if hashlib.sha256(archive.read_bytes()).hexdigest()!=expected:raise RuntimeError('Plasma source checksum mismatch: '+name)
 return archive
