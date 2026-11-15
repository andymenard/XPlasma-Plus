#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Smoke-test a built effect in an isolated virtual KWin, never the live session."""
import argparse,os,shlex,shutil,signal,subprocess,sys,tempfile
from pathlib import Path


def probe():
    from PyQt6.QtWidgets import QApplication,QMainWindow,QLabel
    from PyQt6.QtCore import QTimer
    app=QApplication([])
    window=QMainWindow();window.setWindowTitle('XP caption integration probe')
    window.setCentralWidget(QLabel('Isolated caption animation regression'))
    window.resize(640,400);window.show()
    def restore():
        # A taskbar requests unminimize through KWin; Wayland clients cannot
        # unminimize themselves. Use KWin's scripting API for the same action.
        script=Path.home()/'restore.js'
        script.write_text('workspace.windowList().forEach(function(w){w.minimized=false;});')
        subprocess.run(['qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',str(script),'xp-caption-restore-test'],check=True)
        subprocess.run(['qdbus6','org.kde.KWin','/Scripting','org.kde.kwin.Scripting.start'],check=True)
    for delay,action in [(1000,window.showMaximized),(1700,window.showNormal),
                         (2400,window.showMinimized),(3100,restore),
                         (3900,window.showMaximized),(3970,window.showNormal),
                         (4600,window.showMinimized),(4670,window.close),(5200,app.quit)]:
        QTimer.singleShot(delay,action)
    return app.exec()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plugin',nargs='?',type=Path)
    parser.add_argument('--probe',action='store_true')
    args=parser.parse_args()
    if args.probe:return probe()
    if not args.plugin or not args.plugin.is_file():parser.error('Provide the compiled xp-caption-animation.so')
    with tempfile.TemporaryDirectory(prefix='xp-caption-test-') as tmp:
        root=Path(tmp);home=root/'home';config=home/'.config';config.mkdir(parents=True)
        runtime=root/'run';runtime.mkdir(mode=0o700)
        plugins=root/'plugins';dest=plugins/'kwin/effects/plugins';dest.mkdir(parents=True)
        shutil.copy2(args.plugin,dest/'xp-caption-animation.so')
        (config/'kwinrc').write_text('[Plugins]\nxp-caption-animationEnabled=true\nmaximizeEnabled=false\nsquashEnabled=false\nmagiclampEnabled=false\n')
        session=root/'session';session.write_text('#!/bin/sh\nexport QT_QPA_PLATFORM=wayland\nexec '+shlex.join([sys.executable,str(Path(__file__).resolve()),'--probe'])+'\n');session.chmod(0o755)
        env={**os.environ,'HOME':str(home),'XDG_CONFIG_HOME':str(config),
             'XDG_DATA_HOME':str(home/'.local/share'),'XDG_CACHE_HOME':str(home/'.cache'),
             'XDG_RUNTIME_DIR':str(runtime),'QT_PLUGIN_PATH':str(plugins),
             'QT_QPA_PLATFORM':'offscreen','LIBGL_ALWAYS_SOFTWARE':'1',
             'XPLASMA_ANIMATION_TEST':'1','KDE_FULL_SESSION':'true'}
        for key in ['DISPLAY','WAYLAND_DISPLAY','SESSION_MANAGER']:env.pop(key,None)
        command=['dbus-run-session','--','kwin_wayland','--virtual',
                               '--no-lockscreen','--no-global-shortcuts','--no-kactivities',
                               '--width','1280','--height','800','--exit-with-session',str(session)]
        with (root/'kwin.log').open('w+') as output:
            process=subprocess.Popen(command,env=env,stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
            try:
                code=process.wait(timeout=25)
            except subprocess.TimeoutExpired:
                code=-1
            finally:
                # Portal helpers can outlive KWin and keep captured pipes open.
                try:os.killpg(process.pid,signal.SIGTERM)
                except ProcessLookupError:pass
                process.wait(timeout=5)
            output.seek(0);log=output.read()
        markers=[line for line in log.splitlines() if line.startswith('XP_CAPTION_')]
        print('\n'.join(markers))
        required=['XP_CAPTION_CONSTRUCT','XP_CAPTION_MANAGE decorated=1',
                  'XP_CAPTION_START maximize','XP_CAPTION_START minimize','XP_CAPTION_START restore']
        if code or any(token not in log for token in required) or log.count('XP_CAPTION_START ')<7 or log.count('XP_CAPTION_END ')<7:
            print(log,file=sys.stderr)
            raise SystemExit('Virtual KWin caption regression failed')
        print('Virtual KWin: maximize, restore, minimize, taskbar restore, interruption and close passed.')
    return 0


if __name__=='__main__':sys.exit(main())
