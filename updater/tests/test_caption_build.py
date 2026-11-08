import importlib.util,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('caption_build',Path(__file__).resolve().parents[2]/'effects/xp-caption-animation/build-local.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
class CaptionBuild(unittest.TestCase):
 def test_versions_and_unsupported_apis(self):
  self.assertEqual(builder.kwin_version('kwin_wayland 6.6.6'),'6.6.6')
  self.assertEqual(builder.kwin_version('KWin version: 6.7.5'),'6.7.5')
  self.assertEqual(builder.kwin_version('kwin_wayland 6.8.2'),'6.8.2')
  with self.assertRaises(RuntimeError):builder.kwin_version('kwin_wayland 6.9.0')
 def test_cache_requires_matching_source_sdk_and_binary(self):
  with tempfile.TemporaryDirectory() as folder:
   plugin=Path(folder)/'plugin';plugin.write_bytes(b'local module')
   old=builder.fingerprint('6.6.6','6.10.2','libraries=1','x86_64')
   record={'abi':old,'sha256':builder.hashlib.sha256(plugin.read_bytes()).hexdigest()}
   self.assertTrue(builder.cached(record,old,plugin))
   for change in [{'kwin':'6.6.7'},{'qt':'6.10.3'},{'arch':'aarch64'},{'packages':'libraries=2'},{'source':{}}]:
    self.assertFalse(builder.cached(record,{**old,**change},plugin))
   plugin.write_bytes(b'altered');self.assertFalse(builder.cached(record,old,plugin))
 def test_failure_before_install_leaves_previous_plugin(self):
  # A failed SDK configure exits before any replacement call.
  with patch.object(builder.os,'getuid',return_value=1000),patch.object(builder.shutil,'which',return_value=None),patch('sys.argv',['build-local.py']):
   with self.assertRaises(RuntimeError):builder.main()
if __name__=='__main__':unittest.main()
