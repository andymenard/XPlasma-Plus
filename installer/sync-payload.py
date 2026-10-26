#!/usr/bin/env python3
"""Refresh a complete private installer payload from current monorepo sources.

Preserves separately supplied Windows artwork and Linux runtime archives.
Run the existing release packager afterward to regenerate checksums/archive.
"""
import argparse
import json
import re
import shutil
import subprocess
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def collect_wallpapers(source, saved_wallpapers):
    # Keep the uncropped source, never substitute a widescreen/portrait crop.
    photos=[]
    for photo in sorted(source.rglob('*')):
        if not photo.is_file() or photo.is_symlink():continue
        relative=photo.relative_to(source)
        if relative.parts[0] in {'User Account Pictures','Scanner & Camera Wizard'}:continue
        if photo.suffix.lower() not in {'.jpg','.jpeg','.png','.bmp'}:continue
        if re.search(r' \((?:\d+\s*[x×]\s*\d+|Alt|Alternative|Crop(?:ped)?|1080p|4K)\)$',photo.stem,re.I):continue
        # Party Mode distributes several resolutions of the same picture.
        resolution=re.search(r'_(\d{3,4})$',photo.stem)
        if resolution:
            family=photo.stem[:resolution.start()]
            siblings=list(photo.parent.glob(family+'_*'+photo.suffix))
            siblings=[p for p in siblings if re.fullmatch(re.escape(family)+r'_\d{3,4}',p.stem)]
            if siblings and photo!=max(siblings,key=lambda p:int(p.stem.rsplit('_',1)[1])):continue
        clean=re.sub(r' \((?:Unedited Version|Getty Images Rescan)\)$','',photo.stem,flags=re.I)+photo.suffix
        name=' — '.join((*relative.parts[:-1],clean))
        shutil.copy2(photo,saved_wallpapers/name)
        photos.append({'file':name,'source':str(relative),'group':'stock' if len(relative.parts)==1 else relative.parts[0]})
    (saved_wallpapers/'WALLPAPERS.json').write_text(json.dumps(photos,indent=2)+'\n')


def normalize_session_sprites(folder, names=('20150.png', '14356.png')):
    # XP draws these bitmaps with TransparentBlt's exact RGB(255,0,255) key.
    # PNG/QML needs an alpha channel instead; preserve all other pixels.
    from PIL import Image
    for name in names:
        path=Path(folder)/name
        if not path.is_file():continue
        image=Image.open(path).convert('RGBA')
        pixels=list(image.getdata())
        fixed=[(r,g,b,0) if (r,g,b)==(255,0,255) else (r,g,b,a) for r,g,b,a in pixels]
        if name=='14356.png' and image.size==(32,192):
            # shell32's bitmap ends short: the Log Off hot frame's bottom edge holds
            # two wrapped pixels and black fill, so hovering appears to shift the icon.
            # Rebuild that outline row from the Switch User hot frame plus the two
            # surviving end pixels.
            last,hot=191*32,95*32
            if fixed[last+4][:3]==(0,0,0) and fixed[last+4][3]:
                row=fixed[hot:hot+28]+fixed[last:last+2]+fixed[hot+30:hot+32]
                fixed[last:last+32]=row
        if fixed!=pixels:
            image.putdata(fixed);image.save(path)


def sync(suite, icons=None, wallpapers=None):
    subprocess.run(['python3',str(ROOT/'tools/sync-legal.py'),'--check'],check=True)
    suite = Path(suite).resolve()
    if not (suite / 'payload/assets').is_dir() or not (suite / 'source').is_dir():
        raise ValueError('Expected a complete installer with asset and source inputs')
    if suite == ROOT or suite.is_relative_to(ROOT / 'native'):
        raise ValueError('Installer destination overlaps source')
    for source in (ROOT / 'installer').iterdir():
        if source.is_file():
            shutil.copy2(source, suite / source.name)
    shutil.copy2(ROOT/'CREDITS.md',suite/'CREDITS.md')
    shutil.copy2(ROOT/'native/LICENSE',suite/'LICENSE')
    shutil.copy2(ROOT/'native/THIRD-PARTY-NOTICES',suite/'THIRD-PARTY-NOTICES')
    shutil.copytree(ROOT/'native/LICENSES',suite/'LICENSES',dirs_exist_ok=True)
    shutil.copytree(ROOT/'installer/assets/fonts/tahoma-os',suite/'payload/assets/fonts/tahoma-os',dirs_exist_ok=True)
    prebuilts=ROOT/'private-assets/prebuilt'
    if prebuilts.is_dir():
        shutil.copytree(prebuilts,suite/'prebuilt',dirs_exist_ok=True,symlinks=True)
    tests = ROOT / 'installer/tests'
    if tests.is_dir():
        # Test modules removed from the source must not survive in an updated
        # installer (for example, older unsupported-version expectations).
        for old in (suite / 'tests').glob('test_*.py'):
            if not (tests / old.name).is_file():old.unlink()
        shutil.copytree(tests, suite / 'tests', dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('__pycache__'))
    # Always rebuild the native-source input: it embeds desktop patch rules.
    files = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others',
                                     '--exclude-standard', '--', 'native'], cwd=ROOT)
    archive = suite / 'source/xplasma-source.tar.gz'
    temporary = archive.with_suffix('.new')
    with tarfile.open(temporary, 'w:gz') as target:
        for relative in sorted(set(files.decode().split('\0')) - {''}):
            source = ROOT / relative
            if source.exists():
                target.add(source, arcname='xplasma/' + str(Path(relative).relative_to('native')), recursive=False)
    temporary.replace(archive)
    # Source-controlled layouts are authoritative; never retain stale bundle QML.
    shutil.copytree(ROOT/'themes/xp-session/contents/logout',suite/'payload/templates/session/contents/logout',dirs_exist_ok=True)
    shutil.copytree(ROOT/'themes/xp-boot',suite/'payload/templates/boot',dirs_exist_ok=True)
    shutil.copytree(ROOT/'themes/xp-welcome',suite/'payload/templates/welcome-splash',dirs_exist_ok=True)
    shutil.copytree(ROOT/'effects/xp-caption-animation',suite/'payload/caption-animation',dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__'))
    # Original optional font stays in the local asset payload, outside public source.
    welcome_fonts = suite/'payload/templates/welcome-splash/contents/splash/fonts'
    welcome_fonts.mkdir(parents=True, exist_ok=True)
    arial = suite/'payload/assets/fonts/arialbi.ttf'
    if arial.is_file():
        shutil.copy2(arial, welcome_fonts/'arialbi.ttf')
    for name in ['MainBlock.qml','AccountPicture.qml']:
        shutil.copy2(ROOT/'themes/xp-lockscreen'/name,suite/'payload/templates/lockscreen'/name)
    user_pictures=ROOT/'private-assets/user-pictures'
    if user_pictures.is_dir():
        shutil.copytree(user_pictures,suite/'payload/assets/user-pictures',dirs_exist_ok=True)
    crisp_fonts=ROOT/'private-assets/crisp-fonts'
    if crisp_fonts.is_dir():
        shutil.copytree(crisp_fonts,suite/'payload/assets/fonts',dirs_exist_ok=True)
        shutil.copy2(ROOT/'tools/crisp-text/CREDITS.md',suite/'payload/assets/fonts/CRISP-FONT-CREDITS.md')
    browser = ROOT / 'browser/ie6'
    subprocess.run(['python3', str(browser/'build-icon-wrappers.py'), '--check'], check=True)
    payload = suite / 'payload/firefox'
    for source, destination in [(browser / 'chrome', payload / 'profile/chrome'),
                                (browser / 'runtime-overrides', payload / 'runtime-overrides'),
                                (ROOT / 'apps/tray', suite / 'payload/tray')]:
        if not source.exists():
            continue
        shutil.copytree(source, destination, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('__pycache__'))
    for name in ['user.js', 'initial-prefs.js']:
        shutil.copy2(browser / name, payload / 'profile' / name)
    shutil.copy2(browser / 'runtime/ie6-ui.cfg', payload / 'runtime-overrides/ie6-ui.cfg')
    shutil.copy2(browser / 'install-icon.py', payload / 'install-icon.py')
    if (ROOT / 'projects/README.md').is_file():
        shutil.copy2(ROOT / 'projects/README.md', suite / 'PROJECTS.md')
    # Preserve the audited icon build as a private package input, so future
    # installer rebuilds cannot revert to the earlier partial layer.
    saved_icons = ROOT / 'private-assets/icons.tar.gz'
    if icons:
        icons = Path(icons).resolve()
        audit = json.loads((icons / 'AUDIT.json').read_text())
        if not audit.get('passed'):
            raise ValueError('Icon layer must pass the inventory/image/Qt audit')
        saved_icons.parent.mkdir(parents=True, exist_ok=True)
        temporary = saved_icons.with_suffix('.new')
        with tarfile.open(temporary, 'w:gz') as target:
            target.add(icons, arcname='xplasma-xp-extended')
        temporary.replace(saved_icons)
    if not saved_icons.exists():
        saved_icons = ROOT / 'non-free/payload/tools/icons.tar.gz'
    if saved_icons.exists():
        shutil.copy2(saved_icons, suite / 'payload/tools/icons.tar.gz')
    shutil.copy2(ROOT / 'tools/icon-expansion/install.py', suite / 'payload/tools/xp-icons.py')
    shutil.copy2(ROOT / 'tools/cursor-sync/sync.py', suite / 'payload/tools/cursor-sync.py')
    shutil.copy2(ROOT / 'tools/desktop-behavior/xp-behavior.py', suite / 'payload/tools/xp-behavior.py')
    for name in ['xp-cursor-sync.path', 'xp-cursor-sync.service']:
        shutil.copy2(ROOT / 'tools/cursor-sync' / name, suite / 'payload/tools' / name)
    # Supplied photos are a separate private input; never overwrite originals.
    saved_wallpapers = ROOT / 'private-assets/hires-wallpapers'
    if wallpapers:
        source = Path(wallpapers).resolve()
        if not (source / 'Bliss.jpg').is_file():
            raise ValueError('High-resolution wallpaper input must include Bliss.jpg')
        if saved_wallpapers.exists():
            shutil.rmtree(saved_wallpapers)
        saved_wallpapers.mkdir(parents=True)
        collect_wallpapers(source, saved_wallpapers)
        if (source / 'Notes.txt').is_file():
            shutil.copy2(source / 'Notes.txt', saved_wallpapers / 'SOURCE-NOTES.txt')
    if saved_wallpapers.exists():
        destination = suite / 'payload/assets/hires-wallpapers'
        if destination.exists():shutil.rmtree(destination)
        shutil.copytree(saved_wallpapers, destination)
    custom=suite/'payload/assets/xplasma/assets/themes/XPlasma';custom.mkdir(parents=True,exist_ok=True)
    for old in ['XP Midnight.theme','XP Midnight.msstyles','Zune Dark.theme','Zune Dark.msstyles']:
        if (custom/old).exists():(custom/old).unlink()
    shutil.copy2(ROOT/'themes/README.md',custom/'README.md')
    # Microsoft artwork stays outside Git, but must accompany its QML users.
    session_assets = ROOT / 'private-assets/session/contents/logout/assets'
    session = suite / 'payload/templates/session/contents/logout'
    if session_assets.is_dir():
        shutil.copytree(session_assets, session / 'assets', dirs_exist_ok=True)
    normalize_session_sprites(session/'assets')
    # Logon/lock screen bitmaps (password box, scroll arrows) share that key.
    sddm=suite/'payload/templates/sddm/assets'
    normalize_session_sprites(sddm,sorted(p.name for p in sddm.glob('*.png')))
    for qml in session.glob('*.qml'):
        for asset in re.findall(r'"(assets/[^"\n]+)"', qml.read_text()):
            if not (session / asset).is_file():
                raise RuntimeError('Missing session dialog artwork: ' + asset)
    expected = json.loads((ROOT / 'native/src/app/overrides/patches.json').read_text())
    with tarfile.open(archive) as bundle:
        actual = json.load(bundle.extractfile('xplasma/src/app/overrides/patches.json'))
    if actual != expected:
        raise RuntimeError('Native desktop rules are stale in installer source')
    print('Updated native source, desktop rules, tray, Firefox and validated session artwork.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite', type=Path)
    parser.add_argument('--icons', type=Path, help='Audited expanded icon layer')
    parser.add_argument('--wallpapers', type=Path, help='Supplied high-resolution XP wallpaper directory')
    args = parser.parse_args()
    sync(args.suite, args.icons, args.wallpapers)
