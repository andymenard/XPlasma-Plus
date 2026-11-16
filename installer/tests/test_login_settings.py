import configparser
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from login_settings import update


class LoginSettingsTests(unittest.TestCase):
    original = ('[Theme]\nCurrent=xp-welcome\nCursorSize=32\n'
                '[General]\nGreeterEnvironment=QML_XHR_ALLOW_FILE_READ=1,QT_SCALE_FACTOR=2,QT_IM_MODULE=qtvirtualkeyboard\n'
                '[X11]\nEnableHiDPI=true\n')

    def test_scale_and_keyboard_preserve_other_settings(self):
        config = configparser.RawConfigParser()
        config.read_string(update(self.original, 150, False))
        self.assertEqual(config.get('General', 'InputMethod'), 'compose')
        self.assertEqual(config.get('General', 'GreeterEnvironment'),
                         'QML_XHR_ALLOW_FILE_READ=1,QT_SCALE_FACTOR=1.5,QT_IM_MODULE=compose')
        self.assertEqual(config.get('Theme', 'CursorSize'), '32')
        self.assertEqual(config.get('X11', 'EnableHiDPI'), 'true')

    def test_keyboard_can_be_enabled(self):
        config = configparser.RawConfigParser()
        config.read_string(update(self.original, 100, True))
        self.assertEqual(config.get('General', 'InputMethod'), 'qtvirtualkeyboard')
        self.assertIn('QT_IM_MODULE=qtvirtualkeyboard', config.get('General', 'GreeterEnvironment'))

    def test_invalid_scale_and_unmanaged_theme_are_rejected(self):
        for scale in [0, 49, 301]:
            with self.subTest(scale=scale), self.assertRaises(ValueError):
                update(self.original, scale, False)
        with self.assertRaises(ValueError):
            update('[Theme]\nCurrent=breeze\n', 100, False)


if __name__ == '__main__':
    unittest.main()
