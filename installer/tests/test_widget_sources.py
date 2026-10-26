import hashlib,io,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from widget_sources import verified_archive

class WidgetSources(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
  self.root=Path(self.temp.name);self.data=b'verified upstream source archive'
  self.sha=hashlib.sha256(self.data).hexdigest()
 def obtain(self,**kwargs):
  return verified_archive(self.root/'suite',self.root/'cache','plasma-desktop','6.5.6',kwargs.get('sha',self.sha))
 def test_verified_bundled_source_is_reused_without_a_download(self):
  source=self.root/'suite/source/plasma-desktop-6.5.6.tar.xz';source.parent.mkdir(parents=True);source.write_bytes(self.data)
  with patch('widget_sources.urllib.request.urlopen') as request:self.assertEqual(self.obtain(),source)
  request.assert_not_called()
 def test_missing_source_is_downloaded_from_kde_and_verified(self):
  with patch('widget_sources.urllib.request.urlopen',return_value=io.BytesIO(self.data)) as request:
   source=self.obtain()
  self.assertEqual(source.read_bytes(),self.data)
  self.assertEqual(request.call_args.args[0],'https://download.kde.org/stable/plasma/6.5.6/plasma-desktop-6.5.6.tar.xz')
  with patch('widget_sources.urllib.request.urlopen') as request:self.assertEqual(self.obtain(),source)
  request.assert_not_called()
 def test_corrupt_download_is_not_kept_or_used(self):
  with patch('widget_sources.urllib.request.urlopen',return_value=io.BytesIO(b'corrupt')),self.assertRaisesRegex(RuntimeError,'checksum mismatch'):self.obtain()
  self.assertEqual(list((self.root/'cache').iterdir()),[])
 def test_corrupt_bundled_source_does_not_silently_fall_back(self):
  source=self.root/'suite/source/plasma-desktop-6.5.6.tar.xz';source.parent.mkdir(parents=True);source.write_bytes(b'changed')
  with patch('widget_sources.urllib.request.urlopen') as request,self.assertRaisesRegex(RuntimeError,'checksum mismatch'):self.obtain()
  request.assert_not_called()
 def test_recipe_paths_and_hashes_are_constrained(self):
  for project,version,sha in [('other','6.5.6',self.sha),('plasma-desktop','../6.5',self.sha),('plasma-desktop','6.5.6','unverified')]:
   with self.assertRaises(ValueError):verified_archive(self.root,self.root,project,version,sha)

if __name__=='__main__':unittest.main()
