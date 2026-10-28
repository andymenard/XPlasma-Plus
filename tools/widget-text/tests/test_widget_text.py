# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with QT_QPA_PLATFORM=offscreen; needs Plasma's executable data engine."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
try:
    from PySide6 import QtCore, QtGui, QtQml, QtQuick
except ImportError:
    from PyQt6 import QtCore, QtGui, QtQml, QtQuick

ROOT = Path(__file__).resolve().parents[1]
APP = QtGui.QGuiApplication.instance() or QtGui.QGuiApplication([])
# Load the repository's faces so the test does not depend on a user's install.
for font in (ROOT.parents[1]/'non-free/payload/assets/fonts').glob('xplasma-tahoma-pixel*.ttf'):
    if QtGui.QFontDatabase.addApplicationFont(str(font)) < 0:
        raise RuntimeError(f'Could not load fixture font: {font}')

class WidgetTextTest(unittest.TestCase):
    def test_packaged_helpers_match(self):
        spec = importlib.util.spec_from_file_location('widget_sync', ROOT / 'sync.py')
        sync = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(sync)
        for dest in sync.TARGETS:
            for name in ['XpText.qml', 'XpTextStyle.qml']:
                self.assertEqual((ROOT/name).read_bytes(), (dest/name).read_bytes(), str(dest/name))

    def test_live_toggle_fractional_font_and_fallback(self):
        with tempfile.TemporaryDirectory(prefix="xp widget '") as folder:
            path = Path(folder)/'appearance.json'
            def write(enabled):
                tmp = path.with_suffix('.tmp')
                tmp.write_text(json.dumps({'crispText': enabled}))
                tmp.replace(path)
            write(False)
            engine = QtQml.QQmlEngine()
            errors = []
            engine.warnings.connect(lambda xs: errors.extend(x.toString() for x in xs))
            comp = QtQml.QQmlComponent(engine)
            comp.setData(('import QtQuick\nimport "'+ROOT.as_uri()+'"\nItem {\n'
              'property alias style: style\nproperty alias label: label\n'
              'XpTextStyle { id:style; objectName:"style"; settingsDirectory: '+json.dumps(folder)+' }\n'
              'XpText { id:label; objectName:"label"; xpStyle:style; xpDpr:1.25; xpPixels:11; text:"Agjp 0123"; xpBold:true }\n'
              '}').encode(), QtCore.QUrl('file:///tmp/widget-text-test.qml'))
            self.assertFalse(comp.isError(), [e.toString() for e in comp.errors()])
            obj = comp.create()
            self.assertIsNotNone(obj, [e.toString() for e in comp.errors()])
            label = obj.findChild(QtCore.QObject, 'label')
            def wait_for(value):
                end = time.monotonic()+4
                while time.monotonic()<end:
                    APP.processEvents()
                    if label.property('xpActive') == value:
                        return
                    time.sleep(.01)
                self.fail(f'live crisp toggle did not become {value}; warnings: {errors}')
            wait_for(False)
            write(True)
            wait_for(True)
            for dpr, physical in [(1,11),(1.25,14),(1.5,17),(1.75,19),(2,22),(2.25,25),(2.5,28),(2.75,30),(3,33)]:
                label.setProperty('xpDpr',dpr)
                font = label.property('font')
                expected = f'XPlasma Tahoma Pixel {physical}' if physical != 11 else 'XPlasma Tahoma Pixel'
                self.assertEqual(font.family(),expected)
                self.assertAlmostEqual(font.pointSizeF()*96/72*dpr,physical,places=4)
                self.assertEqual(font.weight(),QtGui.QFont.Weight.Bold)
                self.assertFalse(label.property('antialiasing'))
            for dpr in (1.1, 1.3, 1.8, 2.1, 2.625, 2.8, 3.25):
                label.setProperty('xpDpr',dpr)
                self.assertFalse(label.property('xpActive'))
                self.assertEqual(label.property('font').family(),'Tahoma')
                self.assertAlmostEqual(label.property('font').pointSizeF(),11*72/96)
                self.assertTrue(label.property('antialiasing'))
            label.setProperty('xpDpr',2.75)
            label.setProperty('xpFamily','Trebuchet MS')
            self.assertFalse(label.property('xpActive'))
            self.assertEqual(label.property('font').family(),'Trebuchet MS')
            label.setProperty('xpFamily','Tahoma OS')
            self.assertTrue(label.property('xpActive'))
            write(False)
            wait_for(False)
            self.assertEqual(label.property('font').family(),'Tahoma OS')
            self.assertTrue(label.property('antialiasing'))
            self.assertFalse(errors, errors)
            obj.deleteLater()
            APP.processEvents()

if __name__=='__main__': unittest.main()
