import json
import tarfile
import unittest
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


class IconPayloadTests(unittest.TestCase):
    @unittest.skipUnless((ROOT / 'payload/tools/icons.tar.gz').exists(), 'Requires complete installer payload')
    def test_audited_inventory_and_mask_safe_controls_are_bundled(self):
        with tarfile.open(ROOT / 'payload/tools/icons.tar.gz') as archive:
            prefix = 'xplasma-xp-extended/'
            report = json.load(archive.extractfile(prefix + 'AUDIT.json'))
            self.assertTrue(report['passed'])
            self.assertGreaterEqual(report['mapped_names'], 7390)
            aliases = json.load(archive.extractfile(prefix + 'ALIAS-PROVENANCE.json'))
            self.assertEqual(aliases['go-next-symbolic']['reason'], 'control-glyph')
            self.assertEqual(aliases['help-feedback']['role'], 'dialog-information')
            name = prefix + '32x32/actions/go-next-symbolic'
            if name + '.png' in archive.getnames():
                image = Image.open(archive.extractfile(name + '.png')).convert('RGBA')
                alpha = lambda x, y: image.getpixel((x, y))[3]
            else:
                # The audited layer now supplies control glyphs as vectors.
                # Render the shipped SVG and keep the original mask checks.
                from PyQt6 import QtCore, QtGui, QtSvg, QtWidgets
                app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
                renderer = QtSvg.QSvgRenderer(archive.extractfile(name + '.svg').read())
                self.assertTrue(renderer.isValid())
                image = QtGui.QImage(32, 32, QtGui.QImage.Format.Format_ARGB32)
                image.fill(QtCore.Qt.GlobalColor.transparent)
                painter = QtGui.QPainter(image);renderer.render(painter);painter.end()
                alpha = lambda x, y: image.pixelColor(x, y).alpha()
            self.assertEqual(alpha(16, 4), 0)
            self.assertGreater(alpha(16, 16), 0)
            self.assertTrue(archive.getmember(prefix + 'COPYING-KDE-ICONS').isfile())


if __name__ == '__main__':
    unittest.main()
