"""Snapshots tolerate live files disappearing without hiding backup failures."""
import hashlib,importlib.util,json,sys,tempfile,types,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
spec=importlib.util.spec_from_file_location('snapshot_backend',ROOT/'backend.py')
b=importlib.util.module_from_spec(spec)
with patch.dict(sys.modules,{'tomlkit':types.SimpleNamespace()}):spec.loader.exec_module(b)

class SnapshotRace(unittest.TestCase):
 def setUp(self):
  temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup)
  self.home=Path(temp.name)/'home';self.home.mkdir()
  self.state=Path(temp.name)/'state'
  self.file=self.home/'profile/cache2/entries/cache';self.file.parent.mkdir(parents=True)
  self.file.write_text('old')
  for name,value in {'snapshot_retention':lambda *a:None,'gnome':lambda:{},'run':lambda *a,**k:''}.items():
   stub=patch.object(b,name,value);stub.start();self.addCleanup(stub.stop)
 def test_disappearing_file_during_inventory(self):
  read=Path.read_bytes
  def vanish(path):
   if path==self.file:path.unlink()
   return read(path)
  with patch.object(Path,'read_bytes',vanish):
   self.assertEqual(b.inventory(self.home,['profile']),{})
 def test_disappearing_file_during_save(self):
  original=b.copy
  def vanish(source,dest):
   if source==self.file:source.unlink()
   return original(source,dest)
  with patch.object(b,'copy',vanish):b.Snapshot(self.home,self.state,['profile']).save()
  self.assertEqual(json.loads((self.state/'before.json').read_text()),{})
  self.assertFalse(list(self.state.rglob('.xp-copy-*')))
 def test_manifest_matches_saved_bytes_after_live_file_changes(self):
  original=b.copy
  def change(source,dest):
   if source==self.file:source.write_text('new')
   return original(source,dest)
  with patch.object(b,'copy',change):b.Snapshot(self.home,self.state,['profile']).save()
  data=json.loads((self.state/'before.json').read_text())
  self.assertEqual(data['profile/cache2/entries/cache']['value'],hashlib.sha256(b'new').hexdigest())
  self.assertEqual((self.state/'backup/profile/cache2/entries/cache').read_text(),'new')
 def test_permission_errors_are_fatal(self):
  with patch.object(b,'copy',side_effect=PermissionError('denied')):
   with self.assertRaises(PermissionError):b.Snapshot(self.home,self.state,['profile']).save()
  self.assertFalse((self.state/'before.json').exists())
 def test_destination_errors_are_fatal(self):
  with patch.object(b,'copy',side_effect=FileNotFoundError('destination missing')):
   with self.assertRaises(FileNotFoundError):b.Snapshot(self.home,self.state,['profile']).save()
 def test_broken_symlinks_are_saved(self):
  self.file.unlink();self.file.symlink_to('missing-target')
  b.Snapshot(self.home,self.state,['profile']).save()
  self.assertTrue((self.state/'backup/profile/cache2/entries/cache').is_symlink())
  self.assertEqual(json.loads((self.state/'before.json').read_text())['profile/cache2/entries/cache'],{'kind':'link','value':'missing-target'})

if __name__=='__main__':unittest.main()
