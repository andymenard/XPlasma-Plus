"""Fixed package recipes; never upgrade the distribution or add repositories."""
import subprocess,tempfile,re
from pathlib import Path
APPS={'notepad','calculator','paint','picture_viewer','media_player','sound_recorder','volume_control','command_prompt','task_manager','pinball','minesweeper','solitaire'}
NATIVE={'theme_manager','desktop','apps','taskbar'}
RECIPES={
 'apt':{'base':'fontconfig kde-cli-tools qt6-tools-dev-tools','native':'build-essential cmake ninja-build pkg-config libarchive-dev zlib1g-dev liblzma-dev libbz2-dev qt6-base-dev qt6-declarative-dev','gui':'python3-pyqt6','firefox':'libgtk-3-0t64 libnss3 libnspr4 libasound2t64 libdbus-glib-1-2 libxt6 libx11-xcb1 libxcomposite1 libxdamage1 libxrandr2 libgbm1','login':'sddm python3-pyside6.qtcore python3-pyside6.qtdbus qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-shapes qml6-module-qt5compat-graphicaleffects','qml':'qml6-module-qtcore qml6-module-qt-labs-folderlistmodel qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qt5compat-graphicaleffects','boot':'plymouth plymouth-themes','apps':'libgtk-3-0t64 libgtk-4-1 gsettings-desktop-schemas'},
 'dnf':{'base':'fontconfig kde-cli-tools qt6-qttools','native':'gcc-c++ cmake ninja-build pkgconf-pkg-config libarchive-devel zlib-ng-compat-devel xz-devel bzip2-devel qt6-qtbase-devel qt6-qtdeclarative-devel','gui':'python3-pyqt6','firefox':'gtk3 nss nspr alsa-lib dbus-glib libXt libX11 libXcomposite libXdamage libXrandr mesa-libgbm','login':'sddm sddm-wayland-plasma qt6-qtdeclarative qt6-qt5compat','qml':'qt6-qtdeclarative qt6-qt5compat','boot':'plymouth plymouth-plugin-script','apps':'gtk3 gtk4 gsettings-desktop-schemas'},
 'pacman':{'base':'fontconfig kde-cli-tools qt6-tools','native':'base-devel cmake ninja pkgconf libarchive zlib xz bzip2 qt6-base qt6-declarative','gui':'python-pyqt6','firefox':'gtk3 nss nspr alsa-lib dbus-glib libxt libx11 libxcomposite libxdamage libxrandr mesa','login':'sddm qt6-declarative qt6-5compat','qml':'qt6-declarative qt6-5compat','boot':'plymouth','apps':'gtk3 gtk4 gsettings-desktop-schemas'},
 'zypper':{'base':'fontconfig kde-cli-tools6 qt6-tools','native':'gcc-c++ cmake ninja pkg-config libarchive-devel zlib-devel liblzma-devel libbz2-devel qt6-base-devel qt6-declarative-devel','gui':'python3-qt6','firefox':'libgtk-3-0 mozilla-nss mozilla-nspr libasound2 libdbus-glib-1-2 libXt6 libX11-6 libXcomposite1 libXdamage1 libXrandr2 libgbm1','login':'sddm qt6-declarative-imports qt6-5compat-imports','qml':'qt6-declarative-imports qt6-5compat-imports','boot':'plymouth plymouth-scripts','apps':'libgtk-3-0 libgtk-4-1 gsettings-desktop-schemas'}
}
def packages(family,features,bootstrap=False,native_ready=False,caption_ready=False):
 if family=='xbps':return xbps_packages(features,bootstrap,native_ready,caption_ready)
 r=RECIPES[family];groups=['gui'] if bootstrap else ['base']
 if not bootstrap:
  if set(features)&NATIVE and not native_ready:groups+=['native']
  if set(features)&({'explorer','control_panel'}|APPS):groups+=['gui']
  groups += [x for x in ['firefox','login','boot','apps'] if x in features]
  if set(features)&{'lock','session','taskbar'}:groups+=['qml']
 result=set(' '.join(r[g] for g in groups).split())
 extras={
 'apt':{'paint':'python3-pyqt6.qtwebengine','media_player':'vlc vlc-plugin-skins2','sound_recorder':'python3-pyqt6.qtmultimedia libqt6multimedia6','volume_control':'pulseaudio-utils python3-pyqt6.qtmultimedia','command_prompt':'konsole','task_manager':'python3-psutil','games':'build-essential cmake ninja-build libsdl2-dev','pinball':'libsdl2-mixer-dev','solitaire':'qt6-base-dev qt6-multimedia-dev'},
 'dnf':{'paint':'python3-pyqt6-webengine','media_player':'vlc vlc-gui-skins2','sound_recorder':'qt6-qtmultimedia','volume_control':'pulseaudio-utils qt6-qtmultimedia','command_prompt':'konsole','task_manager':'python3-psutil','games':'gcc-c++ cmake ninja-build SDL2-devel','pinball':'SDL2_mixer-devel','solitaire':'qt6-qtbase-devel qt6-qtmultimedia-devel'},
 'pacman':{'paint':'python-pyqt6-webengine','media_player':'vlc vlc-gui-skins2 vlc-gui-qt vlc-plugin-ffmpeg','sound_recorder':'qt6-multimedia qt6-multimedia-ffmpeg','volume_control':'libpulse qt6-multimedia','command_prompt':'konsole','task_manager':'python-psutil','games':'base-devel cmake ninja sdl2','pinball':'sdl2_mixer','solitaire':'qt6-base qt6-multimedia'},
 'zypper':{'paint':'python3-qt6-webengine','media_player':'vlc','sound_recorder':'qt6-multimedia','volume_control':'pulseaudio-utils qt6-multimedia','command_prompt':'konsole','task_manager':'python3-psutil','games':'gcc-c++ cmake ninja libSDL2-devel','pinball':'libSDL2_mixer-devel','solitaire':'qt6-base-devel qt6-multimedia-devel'}
 }
 extras[family]['explorer']={
 'apt':'build-essential cmake ninja-build extra-cmake-modules qt6-base-dev libkf6kio-dev libkf6solid-dev kio-extras udisks2',
 'dnf':'gcc-c++ cmake ninja-build extra-cmake-modules qt6-qtbase-devel kf6-kio-devel kf6-solid-devel kio-extras udisks2',
 'pacman':'base-devel cmake ninja extra-cmake-modules qt6-base kio solid kio-extras udisks2',
 'zypper':'gcc-c++ cmake ninja extra-cmake-modules qt6-base-devel kf6-kio-devel kf6-solid-devel kio-extras6 udisks2'}[family]
 extras[family]['tray']={'apt':'plasma-pa plasma-nm powerdevil bluedevil kscreen dbus-bin qml6-module-qtcore qml6-module-qt-labs-folderlistmodel qml6-module-qtquick-controls qml6-module-qtquick-layouts','dnf':'plasma-pa plasma-nm powerdevil bluedevil kscreen dbus-tools','pacman':'plasma-pa plasma-nm powerdevil bluedevil kscreen dbus','zypper':'plasma6-pa plasma6-nm powerdevil6 bluedevil6 kscreen6 dbus-1'}[family]
 # Quick Launch and the notification service need their runtime providers,
 # including with prebuilt XP plugins and no widget build dependencies.
 extras[family]['taskbar']={'apt':'plasma-desktop plasma-widgets-addons plasma-pa','dnf':'plasma-desktop kdeplasma-addons plasma-pa','pacman':'plasma-desktop kdeplasma-addons plasma-pa','zypper':'plasma6-desktop kdeplasma6-addons plasma6-pa'}[family]
 extras[family]['control_panel']={'apt':'pulseaudio-utils network-manager','dnf':'pulseaudio-utils NetworkManager','pacman':'libpulse networkmanager','zypper':'pulseaudio-utils NetworkManager'}[family]
 extras[family]['icons']={'apt':'python3-pil','dnf':'python3-pillow','pacman':'python-pillow','zypper':'python3-Pillow'}[family]
 extras[family]['caption_animation']={
 'apt':'build-essential cmake ninja-build extra-cmake-modules kwin-dev qt6-base-dev qt6-declarative-dev libdrm-dev libwayland-dev libvulkan-dev',
 'dnf':'gcc-c++ cmake ninja-build extra-cmake-modules kwin-devel qt6-qtbase-devel qt6-qtdeclarative-devel libdrm-devel wayland-devel vulkan-headers',
 'pacman':'base-devel cmake ninja extra-cmake-modules kwin qt6-base qt6-declarative libdrm wayland vulkan-headers',
 'zypper':'gcc-c++ cmake ninja extra-cmake-modules kwin6-devel qt6-base-devel qt6-declarative-devel libdrm-devel wayland-devel vulkan-headers'}[family]
 for key,value in extras[family].items():
  if key=='caption_animation' and caption_ready:continue
  if key in features or (key=='games' and set(features)&{'pinball','minesweeper','solitaire'}):result.update(value.split())
 return sorted(result)
def xbps_packages(features,bootstrap=False,native_ready=False,caption_ready=False):
 # Void splits the Qt Python widgets and multimedia bindings into subpackages.
 if bootstrap:return ['python3-pyqt6-widgets']
 groups=['fontconfig kde-cli-tools qt6-tools']
 if set(features)&NATIVE and not native_ready:
  groups+=['base-devel cmake ninja pkg-config libarchive-devel zlib-devel liblzma-devel bzip2-devel qt6-base-devel qt6-declarative-devel']
 recipes={
  'firefox':'gtk+3 nss nspr alsa-lib dbus-glib libXt libX11 libXcomposite libXdamage libXrandr MesaLib',
  'login':'sddm python3-pyside6 python3-pyside6-dbus qt6-declarative qt6-qt5compat',
  'boot':'plymouth', 'apps':'gtk+3 gtk4 gsettings-desktop-schemas',
  'taskbar':'plasma-desktop kdeplasma-addons plasma-pa',
  'paint':'python3-pyqt6-webengine', 'media_player':'vlc',
  'sound_recorder':'python3-pyqt6-multimedia', 'volume_control':'pulseaudio-utils python3-pyqt6-multimedia',
  'command_prompt':'konsole', 'task_manager':'python3-psutil',
  'pinball':'SDL2_mixer-devel', 'solitaire':'qt6-base-devel qt6-multimedia-devel',
  'explorer':'base-devel cmake ninja extra-cmake-modules qt6-base-devel kf6-kio-devel kf6-solid-devel kio-extras udisks2',
  'control_panel':'pulseaudio-utils NetworkManager', 'icons':'python3-Pillow',
  'tray':'plasma-pa plasma-nm powerdevil bluedevil kscreen dbus qt6-declarative',
  'caption_animation':'base-devel cmake ninja extra-cmake-modules kwin-devel qt6-base-devel qt6-declarative-devel libdrm-devel wayland-devel Vulkan-Headers',
 }
 if set(features)&({'explorer','control_panel'}|APPS):groups+=['python3-pyqt6-widgets']
 if set(features)&{'lock','session','taskbar'}:groups+=['qt6-declarative qt6-qt5compat']
 if set(features)&{'pinball','minesweeper','solitaire'}:groups+=['base-devel cmake ninja SDL2-devel']
 for key,value in recipes.items():
  if key in features and not (key=='caption_animation' and caption_ready):groups.append(value)
 if 'taskbar' in features and not native_ready:
  # Build the bundled sources against Void's installed Qt/KDE headers. No
  # xbps-src checkout, repository edits, or whole-system upgrade is needed.
  groups+=['extra-cmake-modules plasma-workspace-devel libplasma-devel kwin-devel '
   'qt6-base-private-devel qt6-declarative-private-devel qt6-tools-devel qt6-shadertools-devel '
   'qt6-svg-devel qt6-wayland-devel qt6-location-devel qt6-position-devel qt6-webchannel-devel '
   'qt6-qt5compat-devel kf6-kdoctools-devel kf6-kconfig-devel kf6-kcmutils-devel '
   'kf6-kpackage-devel kf6-kio-devel kf6-knotifications-devel kf6-knotifyconfig-devel '
   'kf6-krunner-devel kf6-kglobalaccel-devel kf6-kcoreaddons-devel kf6-kguiaddons-devel '
   'kf6-kdbusaddons-devel kf6-kauth-devel kf6-ki18n-devel kf6-kiconthemes-devel '
   'kf6-kxmlgui-devel kf6-ksvg-devel kf6-kirigami-devel kirigami-addons-devel '
   'kf6-qqc2-desktop-style-devel kf6-kded-devel kf6-baloo-devel kf6-ktexteditor-devel '
   'kf6-knewstuff-devel kf6-kdesu-devel kf6-kidletime-devel kf6-solid-devel '
   'kf6-kpeople-devel kf6-kunitconversion-devel kf6-kstatusnotifieritem-devel '
   'kf6-kitemmodels-devel kf6-kwallet-devel kf6-networkmanager-qt-devel '
   'kf6-kdeclarative-devel kf6-kwayland-devel kf6-kuserfeedback-devel kf6-prison-devel '
   'kf6-kholidays-devel kf6-karchive-devel kf6-kcrash-devel kf6-kparts-devel '
   'kf6-ktextwidgets-devel kf6-kcolorscheme-devel kf6-kquickcharts-devel '
   'kf6-attica-devel kf6-kwidgetsaddons-devel kf6-kcodecs-devel kf6-sonnet-devel '
   'kf6-kaccounts-integration-devel plasma5support-devel plasma-activities-devel '
   'plasma-activities-stats-devel libaccounts-qt6-devel libksysguard-devel libkf6screen-devel '
   'kscreenlocker-devel plasma-wayland-protocols wayland-protocols libqalculate-devel '
   'phonon-devel polkit-qt6-devel kpipewire-devel libkexiv2-devel qcoro-qt6-devel '
   'layer-shell-qt-devel AppStream-qt-devel eudev-libudev-devel wayland-devel '
   'icu-devel libcanberra-devel elogind-devel NetworkManager-devel libglib-devel '
   'fontconfig-devel libxcb-devel xcb-util-devel xcb-util-cursor-devel xcb-util-image-devel '
   'libX11-devel libXft-devel libXtst-devel libXcursor-devel libXi-devel libxkbfile-devel '
   'libxkbcommon-devel libxcrypt-devel ibus-devel xf86-input-evdev-devel '
   'xf86-input-libinput-devel xorg-server-devel alsa-lib-devel libwacom-devel '
   'knighttime-devel signon-plugin-oauth2']
 return sorted(set(' '.join(groups).split()))

def apt_builddeps(run):
 # Private source-list copy, retaining URIs, suites, components and Signed-By.
 # No persistent repository changes and no untrusted repositories introduced.
 with tempfile.TemporaryDirectory(prefix='xp-apt-sources-') as temp:
  root=Path(temp);root.chmod(0o755);parts=root/'parts';parts.mkdir()
  originals=[Path('/etc/apt/sources.list'),*Path('/etc/apt/sources.list.d').glob('*.list'),*Path('/etc/apt/sources.list.d').glob('*.sources')]
  count=0
  for source in originals:
   if not source.exists():continue
   text=source.read_text()
   if source.suffix=='.sources':text=re.sub(r'^Types:.*$', 'Types: deb deb-src',text,flags=re.M)
   else:text=re.sub(r'^(\s*)deb\s',r'\1deb-src ',text,flags=re.M)
   (parts/source.name).write_text(text);count+=1
  if not count:raise RuntimeError('APT sources are missing; cannot obtain widget build dependencies.')
  options=['-o','Dir::Etc::sourcelist=/dev/null','-o','Dir::Etc::sourceparts='+str(parts)]
  run(['apt-get',*options,'update'])
  run(['apt-get',*options,'build-dep','--assume-yes','plasma-workspace','plasma-desktop'])
def install(family,features,run,bootstrap=False,native_ready=False,caption_ready=False):
 chosen=packages(family,features,bootstrap,native_ready,caption_ready)
 if family=='apt':
  # Refresh before resolving names: a newly installed beta may have an
  # empty or outdated cache. Otherwise t64 libraries get incorrectly renamed.
  run(['apt-get','update'])
  def candidate(name):
   policy=subprocess.check_output(['apt-cache','policy',name],text=True,stderr=subprocess.DEVNULL)
   return bool(re.search(r'^\s*Candidate:\s+(?!\(none\))\S+',policy,re.M))
  # Debian before the t64 transition uses the unsuffixed package names.
  for i,p in enumerate(chosen):
   if p.endswith('t64') and not candidate(p) and candidate(p[:-3]):chosen[i]=p[:-3]
  missing=[p for p in chosen if not candidate(p)]
  if missing:raise RuntimeError('No APT installation candidate for: '+', '.join(missing)+'. Check the enabled distribution repositories and the APT output above; no repositories were added.')
  run(['apt-get','install','--assume-yes',*chosen])
 elif family=='dnf':run(['dnf','install','-y',*chosen])
 elif family=='pacman':run(['pacman','-S','--needed','--noconfirm',*chosen])
 elif family=='xbps':run(['xbps-install','-Sy',*chosen])
 else:run(['zypper','--non-interactive','install',*chosen])
 if bootstrap or native_ready or 'taskbar' not in features or family=='xbps':return
 if family=='apt':apt_builddeps(run)
 elif family=='dnf':
  run(['dnf','install','-y','dnf5-command(builddep)' if 'dnf5' in subprocess.check_output(['dnf','--version'],text=True).lower() else 'dnf-plugins-core'])
  run(['dnf','builddep','-y','plasma-workspace','plasma-desktop'])
 elif family=='zypper':run(['zypper','--non-interactive','source-install','--build-deps-only','plasma6-workspace','plasma6-desktop'])
 else:
  # Arch packages contain headers and CMake config files; never perform a
  # database-only refresh (which would create a partial-upgrade environment).
  run(['pacman','-S','--needed','--noconfirm','extra-cmake-modules','kdoctools','intltool','plasma-wayland-protocols','vulkan-headers','plasma-workspace','plasma-desktop','libplasma','qt6-wayland','qt6-svg','qt6-shadertools','qt6-positioning','qt6-location','qt6-webchannel','qt6-tools','kpackage','kcmutils','kglobalacceld','kscreenlocker','libksysguard','libkscreen','breeze','breeze-icons','wayland-protocols','kuserfeedback','kaccounts-integration','xorg-server-devel','xf86-input-libinput','xf86-input-evdev','libxi','libxtst','xdotool','sdl2','kio-extras','networkmanager-qt','packagekit-qt6','scim','ibus','libcanberra','libxkbfile','libqalculate','appstream-qt','qcoro-qt6','phonon-qt6','accountsservice','polkit-qt6','libxcrypt','libxcursor'])
