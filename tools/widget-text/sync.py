#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[2]
TARGETS=[ROOT/'native/src/app'/name/'contents/ui' for name in ['start-menu','clock']]
TRAY = ROOT/'apps/tray'
if TRAY.is_dir():
 TARGETS += [TRAY/'common']+[p/'contents/ui' for p in TRAY.iterdir() if (p/'metadata.json').is_file()]
def sync():
 for dest in TARGETS:
  for name in ['XpText.qml','XpTextStyle.qml']:shutil.copy2(Path(__file__).with_name(name),dest/name)
 # The desktop folder label embeds this same policy through its patch template.
 shutil.copy2(Path(__file__).with_name('XpTextStyle.qml'),ROOT/'native/src/app/overrides/XpTextStyle.qml')
if __name__=='__main__':sync()
