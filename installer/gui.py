#!/usr/bin/python3
"""GUI feature picker and progress UI for the portable KDE XP Suite."""
import argparse,json,os,sys
from pathlib import Path
try:
 from PyQt6 import QtCore,QtGui,QtWidgets
except ImportError:
 from PySide6 import QtCore,QtGui,QtWidgets
BASE=Path(__file__).resolve().parent
from system_profile import detect,capabilities
import rebuild as plasma_update
FEATURES=json.loads((BASE/'features.json').read_text())
EDITION=json.loads((BASE/'edition.json').read_text()) if (BASE/'edition.json').exists() else {}
PROJECT_LABELS={'desktop':'XP Desktop (default)','firefox':'IE6 Firefox Theme (optional)','applications':'Windows XP Applications (optional)','tray':'XP System Tray Menus (optional)','shared':'Shared update support','extras':'Optional integrations'}
THEMES=['Luna Blue','Luna Silver','Luna Olive Green','Royale','Royale Noir','Zune','Embedded']
STATE=Path.home()/'.local/share/kde-xp-suite'
def feature_description(feature):
 # Packaging marks what uses Microsoft's XP media as non-free (see non-free/NOTICE.md).
 return feature['description']+("\nNon-free: uses Windows XP assets that remain Microsoft's copyright." if feature.get('non_free') else '')
class SetupStep(QtWidgets.QLabel):
 def paintEvent(self,event):
  super().paintEvent(event)
  painter=QtGui.QPainter(self);painter.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
  y=max(0,(self.height()-18)//2)
  painter.setPen(QtGui.QColor('#293f73'));painter.setBrush(QtGui.QColor('#ccd7ed'));painter.drawEllipse(QtCore.QRectF(0,y,18,18))
  active=self.property('active');complete=self.property('complete')
  gradient=QtGui.QRadialGradient(QtCore.QPointF(8,y+7),10)
  gradient.setColorAt(0,QtGui.QColor('#ffffff'));gradient.setColorAt(0.4,QtGui.QColor('#ffffff'))
  gradient.setColorAt(0.72,QtGui.QColor('#efc670' if active else ('#38bb4b' if complete else '#d6e7f6')))
  gradient.setColorAt(1,QtGui.QColor('#9a6927' if active else ('#158c32' if complete else '#7893ae')))
  painter.setPen(QtGui.QColor('#f1f5ff'));painter.setBrush(gradient);painter.drawEllipse(QtCore.QRectF(2,y+2,14,14))
class Installer(QtWidgets.QMainWindow):
 def __init__(self,test_mode=False):
  super().__init__();self.test_mode=test_mode;self.process=None;self.buffer='';self.current_action=None;self.upgrading=False;self.report_lines=[]
  self.setWindowTitle('XPlasma Plus Setup');self.resize(1024,768)
  self._tip_index=0
  self.setWindowIcon(QtGui.QIcon(str(BASE/'payload/assets/desktop-icons/computer.png')))
  host=QtWidgets.QWidget();host.setObjectName('setupBackground');self.setCentralWidget(host)
  host_layout=QtWidgets.QVBoxLayout(host);host_layout.setContentsMargins(0,0,0,0);host_layout.setSpacing(0)
  banner=QtWidgets.QLabel('XPlasma Plus');banner.setObjectName('setupBrand');banner.setFixedHeight(56);banner.setContentsMargins(24,0,0,0);host_layout.addWidget(banner)
  body=QtWidgets.QWidget();body.setObjectName('setupBody');host_layout.addWidget(body,1)
  outer=QtWidgets.QHBoxLayout(body);outer.setContentsMargins(0,0,0,0);outer.setSpacing(0)
  rail=QtWidgets.QWidget();rail.setObjectName('setupRail');rail.setFixedWidth(250);outer.addWidget(rail)
  rail_layout=QtWidgets.QVBoxLayout(rail);rail_layout.setContentsMargins(32,35,20,25);rail_layout.setSpacing(12)
  self.steps=[]
  for label in ['Collecting information','Preparing installation','Installing components','Finalizing installation','Completing setup']:
   step=SetupStep(label);step.setObjectName('setupStep');step.setWordWrap(True);rail_layout.addWidget(step);self.steps.append(step)
  rail_layout.addSpacing(16)
  self.status=QtWidgets.QLabel('Setup is ready.');self.status.setObjectName('setupNote');self.status.setWordWrap(True);rail_layout.addWidget(self.status)
  self.progress=QtWidgets.QProgressBar();self.progress.setRange(0,100);self.progress.setValue(0);self.progress.setTextVisible(False);rail_layout.addWidget(self.progress)
  self.activity=QtWidgets.QLabel('');self.activity.setObjectName('setupActivity');rail_layout.addWidget(self.activity)
  self.dots=QtCore.QTimer(self);self.dots.setInterval(350);self.dots.timeout.connect(lambda:self.activity.setText('■ '*((len(self.activity.text())//2+1)%5)))
  self.terminal_log=QtWidgets.QPlainTextEdit();self.terminal_log.setObjectName('setupTerminal');self.terminal_log.setReadOnly(True)
  self.terminal_log.setFont(QtGui.QFontDatabase.systemFont(QtGui.QFontDatabase.SystemFont.FixedFont));self.terminal_log.setLineWrapMode(QtWidgets.QPlainTextEdit.LineWrapMode.NoWrap)
  self.terminal_log.setMaximumBlockCount(2000);self.terminal_log.setMinimumHeight(100);self.terminal_log.setMaximumHeight(200)
  self.terminal_log.setPlaceholderText('Installation output will appear here.');self.terminal_log.setStyleSheet('background:#071b38;color:#ffffff;border:1px solid #92a9c8;font-size:10px;')
  rail_layout.addWidget(self.terminal_log,1)
  credit=QtWidgets.QLabel('Based on XPlasma\nby hotsocket');credit.setObjectName('setupNote');rail_layout.addWidget(credit)
  self.pages=QtWidgets.QStackedWidget();outer.addWidget(self.pages,1)
  content=QtWidgets.QWidget();content.setObjectName('setupContent');self.pages.addWidget(content)
  layout=QtWidgets.QVBoxLayout(content);layout.setContentsMargins(18,20,18,16);layout.setSpacing(7)
  dialog_title=QtWidgets.QLabel('XPlasma Plus Setup');dialog_title.setObjectName('setupDialogTitle');layout.addWidget(dialog_title)
  header=QtWidgets.QLabel('Choose your components');header.setObjectName('header');layout.addWidget(header)
  self.intro=QtWidgets.QLabel();self.intro.setWordWrap(True);layout.addWidget(self.intro)
  row=QtWidgets.QHBoxLayout();row.addWidget(QtWidgets.QLabel('Theme:'));self.theme=QtWidgets.QComboBox();self.theme.addItems(THEMES);row.addWidget(self.theme,1)
  all_button=QtWidgets.QPushButton('Select all');none_button=QtWidgets.QPushButton('Select none');self.selection_buttons=[all_button,none_button];row.addWidget(all_button);row.addWidget(none_button);layout.addLayout(row)
  self.tree=QtWidgets.QTreeWidget();self.tree.setHeaderHidden(True);self.tree.setRootIsDecorated(True);self.tree.setUniformRowHeights(False);layout.addWidget(self.tree,1)
  self.profile=detect();self.support=capabilities(self.profile,FEATURES)
  self.items={};self.groups={}
  for key,title in PROJECT_LABELS.items():
   if not any(f.get('project','desktop')==key for f in FEATURES):continue
   group=QtWidgets.QTreeWidgetItem(self.tree,[title]);group.setData(0,QtCore.Qt.ItemDataRole.UserRole,'project:'+key)
   group.setFlags(group.flags()|QtCore.Qt.ItemFlag.ItemIsUserCheckable|QtCore.Qt.ItemFlag.ItemIsAutoTristate)
   group.setToolTip(0,'Select or clear the supported features in this project.');group.setExpanded(key=='desktop');self.groups[key]=group
  installed=STATE/'installation.json';saved=json.loads(installed.read_text()) if installed.exists() else None
  # An upgrade keeps what is installed: those features stay checked.
  self.locked=set(saved['features']) if saved else set()
  for feature in FEATURES:
   item=QtWidgets.QTreeWidgetItem(self.groups[feature.get('project','desktop')],[feature['name']]);item.setData(0,QtCore.Qt.ItemDataRole.UserRole,feature['id']);item.setToolTip(0,feature_description(feature))
   item.setFlags(item.flags()|QtCore.Qt.ItemFlag.ItemIsUserCheckable)
   reason=self.support[feature['id']]
   if reason:
    item.setToolTip(0,feature_description(feature)+'\nUnavailable: '+reason);item.setFlags(item.flags() & ~QtCore.Qt.ItemFlag.ItemIsEnabled)
   checked=(feature['id'] in saved['features'] if saved else feature['default']) and not reason
   item.setCheckState(0,QtCore.Qt.CheckState.Checked if checked else QtCore.Qt.CheckState.Unchecked)
   self.items[feature['id']]=item
  self.lock_installed()
  if saved:self.theme.setCurrentText(saved['theme'])
  options=(saved or {}).get('asset_options',{})
  quality=QtWidgets.QVBoxLayout()
  self.hidpi_icons=QtWidgets.QCheckBox('Use HiDPI icons (unchecked: original XP icons)')
  self.dark_mode=QtWidgets.QCheckBox('Dark application colors (also IE6 Firefox)')
  self.dark_mode.setChecked(options.get('dark_mode',False))
  self.hires_wallpapers=QtWidgets.QCheckBox('Use high-resolution wallpapers (unchecked: originals)')
  self.hidpi_icons.setChecked(options.get('hidpi_icons',True));self.hires_wallpapers.setChecked(options.get('hires_wallpapers',True))
  self.hidpi_icons.setToolTip('Use credited XP recreations for larger icons; keeps original small toolbar/tray artwork. KDE-only replacements are included in both modes.')
  self.hires_wallpapers.setToolTip('Use bundled high-resolution XP photos without duplicate crops. Original wallpaper files remain in the installer.')
  quality.addWidget(self.hidpi_icons);quality.addWidget(self.hires_wallpapers);quality.addWidget(self.dark_mode);layout.addLayout(quality)
  self.description=QtWidgets.QLabel('Select a feature to read what it installs.');self.description.setWordWrap(True);self.description.setMinimumHeight(45);layout.addWidget(self.description)
  self.summary=QtWidgets.QLabel();self.summary.setWordWrap(True);layout.addWidget(self.summary)
  buttons=QtWidgets.QHBoxLayout();self.remove=QtWidgets.QPushButton('Remove installed features');buttons.addWidget(self.remove);buttons.addStretch()
  # After a Plasma update took the XP taskbar's builds off: build them again for it.
  self.rebuild=QtWidgets.QPushButton('Rebuild');self.rebuild.setVisible(False);buttons.addWidget(self.rebuild)
  self.install=QtWidgets.QPushButton('Install selected features');self.close_button=QtWidgets.QPushButton('Close');buttons.addWidget(self.install);buttons.addWidget(self.close_button);layout.addLayout(buttons)
  progress_page=QtWidgets.QWidget();progress_page.setObjectName('setupProgress');self.pages.addWidget(progress_page)
  progress_layout=QtWidgets.QVBoxLayout(progress_page);progress_layout.setContentsMargins(58,38,46,35)
  self.tip_title=QtWidgets.QLabel('An exciting familiar look');self.tip_title.setObjectName('tipTitle');self.tip_title.setWordWrap(True);progress_layout.addWidget(self.tip_title)
  self.tip_text=QtWidgets.QLabel();self.tip_text.setObjectName('tipText');self.tip_text.setMaximumWidth(520);self.tip_text.setWordWrap(True);progress_layout.addWidget(self.tip_text);progress_layout.addStretch()
  self.tip_timer=QtCore.QTimer(self);self.tip_timer.setInterval(14000);self.tip_timer.timeout.connect(self.rotate_tip);self.rotate_tip()
  self.credits_button=QtWidgets.QPushButton('Credits and licenses');buttons.insertWidget(1,self.credits_button);self.credits_button.clicked.connect(self.show_credits)
  footer=QtWidgets.QLabel(EDITION.get('label','XPlasma Plus')+' — based on XPlasma by hotsocket');footer.setObjectName('setupFooter');footer.setFixedHeight(30);footer.setContentsMargins(24,0,0,0);host_layout.addWidget(footer)
  self.setStyleSheet("""
   QWidget#setupBackground {background:#516bd0;}
   QLabel#setupBrand {background:#003399;color:white;font:italic bold 26px "Trebuchet MS";border-bottom:2px solid #97b2ee;}
   QWidget#setupBody {background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #516bd0,stop:0.6 #516bd0,stop:1 #7b9ce2);}
   QWidget#setupRail {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #456dcc,stop:0.7 #789be1,stop:1 #5b79d4);}
   QLabel#setupStep {color:white;font:14px "Tahoma";padding-left:25px;min-height:27px;}
   QLabel#setupStep[active="true"] {color:#f4ae32;font:bold 14px "Tahoma";}
   QLabel#setupStep[complete="true"] {color:white;font:bold 14px "Tahoma";}
   QLabel#setupNote {color:white;font:12px "Tahoma";}
   QLabel#setupActivity {color:white;}
   QWidget#setupContent {background:#d4d0c8;border:2px solid #ffffff;}
   QLabel#setupDialogTitle {background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #0a246a,stop:1 #a6caf0);color:white;font:bold 12px "Tahoma";padding:3px;}
   QLabel#header {background:white;color:black;font:bold 18px "Tahoma";padding:13px;border-bottom:1px solid #808080;}
   QLabel#tipTitle {color:white;font:bold 25px "Arial";padding-bottom:14px;}
   QLabel#tipText {color:white;font:16px "Tahoma";}
   QLabel#setupFooter {background:#003399;color:white;font:11px "Tahoma";border-top:1px solid #e49b27;}
   QTreeWidget {background:white;border:1px solid #808080;}
   QProgressBar {border:2px inset #808080;background:white;max-height:17px;min-height:17px;}
   QProgressBar::chunk {background:#31b632;width:9px;margin:1px;}
   QPushButton {min-height:24px;min-width:75px;}
  """)
  all_button.clicked.connect(lambda:self.select_all(True));none_button.clicked.connect(lambda:self.select_all(False));self.close_button.clicked.connect(self.close)
  self.tree.itemChanged.connect(self.component_changed);self.tree.currentItemChanged.connect(self.describe);self.theme.currentTextChanged.connect(self.update_summary)
  self.install.clicked.connect(lambda:self.start('install'));self.remove.clicked.connect(lambda:self.start('uninstall'));self.rebuild.clicked.connect(lambda:self.start('rebuild'))
  self.status.setText(self.profile['name']+' — Plasma '+'.'.join(map(str,self.profile['plasma']))+' — '+self.profile['arch']+'. Unavailable features are disabled.')
  self.set_step(0);self.update_summary()
 def lock_installed(self):
  installed=STATE/'installation.json'
  self.locked=set(json.loads(installed.read_text())['features']) if installed.exists() else set()
  for feature in FEATURES:
   item=self.items[feature['id']];reason=self.support[feature['id']]
   locked=feature['id'] in self.locked and not reason
   tip=feature_description(feature)+('\nUnavailable: '+reason if reason else '')+('\nInstalled: an upgrade keeps it. Remove the installation to drop it.' if locked else '')
   item.setToolTip(0,tip)
   if locked:item.setFlags(item.flags() & ~QtCore.Qt.ItemFlag.ItemIsUserCheckable)
   elif not reason:item.setFlags(item.flags()|QtCore.Qt.ItemFlag.ItemIsUserCheckable)
 def rotate_tip(self):
  tips=[('An exciting familiar look','XP-inspired window borders, a familiar Start menu and labeled task buttons bring a classic desktop to modern KDE.\n\nYour Linux applications and files stay available.'),('Choose the look you like','Use Luna, Royale, Zune or a custom XP visual style. Dark application colors can be enabled independently.\n\nChoose original or high-resolution icons and wallpapers.'),('Built on the work of others','The desktop foundation is XPlasma by hotsocket. The optional browser theme builds on RetroThemesFirefox by matthewmx86.\n\nXP icon recreations are by marchmountain. Complete credits and license notices are included.'),('A modern browser, a familiar face','Optional Firefox ESR retains modern browsing capabilities beneath the IE6-inspired toolbar.\n\nYour toolbar choices and browser profile are preserved.'),('Preparing your desktop','The installer keeps a backup for removal and recovery. Login and boot changes take effect on the next login or reboot.\n\nSetup will not restart your computer automatically.')]
  title,text=tips[self._tip_index%len(tips)];self._tip_index+=1;self.tip_title.setText(title);self.tip_text.setText(text)
 def show_credits(self):
  dialog=QtWidgets.QDialog(self);dialog.setWindowTitle('Credits and licenses');dialog.resize(760,600)
  layout=QtWidgets.QVBoxLayout(dialog);documents=QtWidgets.QComboBox();text=QtWidgets.QTextBrowser();text.setOpenExternalLinks(True)
  layout.addWidget(documents);layout.addWidget(text)
  files=[('Credits',BASE/'CREDITS.md'),('Project license',BASE/'LICENSE'),('Third-party notices',BASE/'THIRD-PARTY-NOTICES')]
  files += [(p.stem,p) for p in sorted((BASE/'LICENSES').glob('*.txt'))]
  for title,path in files:documents.addItem(title,str(path))
  def display_document():
   path=Path(documents.currentData());text.setSearchPaths([str(BASE)])
   content=path.read_text(encoding='utf-8') if path.is_file() else 'This document is missing from the installation package.'
   if path.suffix=='.md':text.setMarkdown(content)
   else:text.setPlainText(content)
  documents.currentIndexChanged.connect(display_document);display_document()
  close=QtWidgets.QPushButton('Close');close.clicked.connect(dialog.accept);layout.addWidget(close);dialog.exec()
 def set_step(self,index):
  for position,step in enumerate(self.steps):
   step.setProperty('active',position==index);step.setProperty('complete',position<index)
   step.style().unpolish(step);step.style().polish(step)
 def component_changed(self,item,column):
  name=item.data(0,QtCore.Qt.ItemDataRole.UserRole)
  checked=item.checkState(0)==QtCore.Qt.CheckState.Checked
  if name=='explorer_default' and checked:self.items['explorer'].setCheckState(0,QtCore.Qt.CheckState.Checked)
  if name=='explorer' and not checked:self.items['explorer_default'].setCheckState(0,QtCore.Qt.CheckState.Unchecked)
  if name=='firefox_default' and checked:self.items['firefox'].setCheckState(0,QtCore.Qt.CheckState.Checked)
  # (Choosing the IE6 browser makes it the default too, unless unticked.)
  if name=='firefox':self.items['firefox_default'].setCheckState(0,QtCore.Qt.CheckState.Checked if checked else QtCore.Qt.CheckState.Unchecked)
  self.update_summary()
 def selected(self):return [name for name,item in self.items.items() if item.checkState(0)==QtCore.Qt.CheckState.Checked and not self.support[name]]
 def select_all(self,checked):
  for name,item in self.items.items():
   if name not in self.locked:item.setCheckState(0,QtCore.Qt.CheckState.Checked if checked and not self.support[name] else QtCore.Qt.CheckState.Unchecked)
 def describe(self,item,previous=None):
  if item:self.description.setText(item.toolTip(0))
 def update_summary(self,*unused):
  chosen=self.selected();system=set(chosen)&{'login','boot'}
  self.summary.setText(str(len(chosen))+' features selected. '+('Login/boot changes will request authentication and take effect after login/reboot.' if system else 'No login or boot configuration will be changed.'))
  running=self.process is not None
  self.dark_mode.setEnabled(not running and bool(set(chosen)&{'apps','desktop','theme_manager'}))
  self.hidpi_icons.setEnabled(not running and bool(set(chosen)&{'icons','desktop_icons'}))
  self.hires_wallpapers.setEnabled(not running and bool(set(chosen)&{'wallpaper','theme_manager'}))
  for button in self.selection_buttons:button.setEnabled(not running)
  upgrade=(STATE/'installation.json').exists()
  self.install.setText('Upgrade installation' if upgrade else 'Install selected features')
  self.intro.setText('This computer has an installation: Upgrade installs this release over it and keeps your theme and settings. Installed features stay selected; you can add more.' if upgrade else 'Install the XP desktop, with optional IE6 Firefox. Review the features below.')
  lost=plasma_update.status(Path.home(),BASE) if upgrade else None
  self.rebuild.setVisible(bool(lost));self.rebuild.setEnabled(bool(lost and lost['available']) and not running)
  if lost:
   self.rebuild.setText('Rebuild for Plasma '+lost['plasma'])
   self.intro.setText('Plasma was updated to '+lost['plasma']+', so the XP taskbar is paused: its parts were built for the Plasma before. '
    +('Rebuild builds them for this Plasma and keeps your theme and settings.' if lost['available'] else 'This installer has no parts for Plasma '+lost['series']+'. Download the latest XPlasma Plus release to rebuild.'))
  self.install.setEnabled(bool(chosen) and not running)
  self.remove.setEnabled(not running and ((STATE/'installation.json').exists() or (STATE/'incomplete.json').exists()))
 def start(self,action):
  if self.test_mode:return
  # (Installing and rebuilding restart the desktop: open programs can lose work.)
  if action in ('install','rebuild') and self.close_programs_question().exec()!=QtWidgets.QMessageBox.StandardButton.Ok:return
  args=['-u',str(BASE/'backend.py'),action]
  if action=='uninstall':
   box=self.removal_question()
   if box.exec()!=QtWidgets.QMessageBox.StandardButton.Yes:return
   if box.checkBox().isChecked():args.append('--delete-backups')
  if action=='install':args+=['--features',','.join(self.selected()),'--theme',self.theme.currentText()]
  if action=='install':
   if self.dark_mode.isChecked():args+=['--dark-mode']
   if self.hidpi_icons.isChecked():args+=['--hidpi-icons']
   if self.hires_wallpapers.isChecked():args+=['--hires-wallpapers']
  self.upgrading=action=='install' and (STATE/'installation.json').exists()
  self.report_lines=[];self.buffer='';self.attempted_features=self.selected()
  self.set_step(1);self.pages.setCurrentIndex(1);self.tip_timer.start();self.dots.start();self.current_action=action;self.terminal_log.clear();self.progress.setRange(0,0);self.tree.setEnabled(False);self.theme.setEnabled(False);self.close_button.setEnabled(False)
  self.process=QtCore.QProcess(self);self.process.setProcessChannelMode(QtCore.QProcess.ProcessChannelMode.MergedChannels)
  self.process.readyReadStandardOutput.connect(self.read_output);self.process.finished.connect(self.finished)
  self.process.errorOccurred.connect(self.process_error);self.process.start(sys.executable,args);self.update_summary()
 def close_programs_question(self):
  box=QtWidgets.QMessageBox(QtWidgets.QMessageBox.Icon.Warning,'Close other programs',
   'Please close all other programs before continuing.\n\n'
   'Setup restarts the desktop while it installs, so save your work and close any open programs, then click Continue.',
   QtWidgets.QMessageBox.StandardButton.Ok|QtWidgets.QMessageBox.StandardButton.Cancel,self)
  box.button(QtWidgets.QMessageBox.StandardButton.Ok).setText('Continue')
  return box
 def removal_question(self):
  box=QtWidgets.QMessageBox(QtWidgets.QMessageBox.Icon.Question,'Remove XP features',
   'Restore the settings from before the installation and remove the XP looks it made, including those made in Properties since? '
   'Later edits are preserved when possible. Your saved themes, custom colours and IE6 browser profile are kept. Linux dependencies are kept.',
   QtWidgets.QMessageBox.StandardButton.Yes|QtWidgets.QMessageBox.StandardButton.No,self)
  # (Parented to the dialog: PyQt doesn't hand setCheckBox's box to Qt.)
  box.setCheckBox(QtWidgets.QCheckBox('Also delete the settings backups (they can\'t be restored again afterwards)',box))
  return box
 def append_log(self,text):
  self.report_lines.append(text)
  self.terminal_log.appendPlainText(text)
 def failure_report(self,code):
  profile={key:self.profile.get(key) for key in ['name','family','arch','plasma','qt','systemd','kde','boot']}
  output='\n'.join(self.report_lines).replace(str(Path.home()),'~')
  # Indentation keeps arbitrary tool output from closing the Markdown block.
  return '### XPlasma Plus failure report\n\nVersion: '+EDITION.get('version','unknown')+'\nAction: '+str(self.current_action)+'\nExit code: '+str(code)+'\nFeatures: '+', '.join(getattr(self,'attempted_features',[]))+'\n\nSystem:\n\n    '+json.dumps(profile,sort_keys=True)+'\n\nInstaller output (including recovery):\n\n'+'\n'.join('    '+line for line in output.splitlines())+'\n'
 def show_failure_report(self,code):
  report=self.failure_report(code);saved=''
  try:
   folder=Path.home()/'.local/state/xplasma-plus';folder.mkdir(parents=True,exist_ok=True)
   path=folder/'last-failure.log'
   # Replace atomically; never follow an existing report-file symlink.
   import tempfile
   with tempfile.NamedTemporaryFile(mode='w',dir=folder,delete=False) as file:
    file.write(report);temporary=Path(file.name)
   temporary.replace(path);saved=' A copy is saved in '+str(path)+'.'
  except OSError:pass
  dialog=QtWidgets.QDialog(self);dialog.setWindowTitle('XPlasma Plus operation failed');dialog.resize(780,540)
  layout=QtWidgets.QVBoxLayout(dialog)
  instructions=QtWidgets.QLabel('Setup did not complete. Review the report below, click Copy report, then paste it into a new or existing issue at <a href="https://github.com/andymenard/XPlasma-Plus/issues">github.com/andymenard/XPlasma-Plus/issues</a>.'+saved)
  instructions.setWordWrap(True);instructions.setOpenExternalLinks(True);layout.addWidget(instructions)
  text=QtWidgets.QPlainTextEdit();text.setObjectName('failureReport');text.setReadOnly(True);text.setPlainText(report);layout.addWidget(text)
  buttons=QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.StandardButton.Close)
  copy=buttons.addButton('Copy report',QtWidgets.QDialogButtonBox.ButtonRole.ActionRole)
  copy.clicked.connect(lambda:QtWidgets.QApplication.clipboard().setText(report))
  buttons.rejected.connect(dialog.reject);layout.addWidget(buttons);dialog.exec()
 def read_output(self):
  self.buffer+=bytes(self.process.readAllStandardOutput()).decode(errors='replace').replace('\r','\n')
  while '\n' in self.buffer:
   line,self.buffer=self.buffer.split('\n',1)
   if line.startswith('@@'):
    try:
     phase=json.loads(line[2:])['phase'];self.status.setText(phase);self.append_log(phase)
     if phase.startswith(('Checking','Creating','Installing Linux')):self.set_step(1)
     elif 'Restoring' in phase or 'recovery' in phase.lower():self.set_step(3)
     elif phase.startswith(('Finalizing','Saving')):self.set_step(3)
     else:self.set_step(2)
    except (ValueError,KeyError):self.append_log(line)
   else:self.append_log(line)
 def finished(self,code,status):
  self.read_output()
  if self.buffer:self.append_log(self.buffer);self.buffer=''
  self.tip_timer.stop();self.dots.stop();self.set_step(4 if code==0 else 0);self.pages.setCurrentIndex(0);self.process.deleteLater();self.process=None;self.progress.setRange(0,100);self.progress.setValue(100 if code==0 else 0)
  self.tree.setEnabled(True);self.theme.setEnabled(True);self.close_button.setEnabled(True);self.lock_installed();self.update_summary()
  if code==0:
   message='Rebuild complete. The XP taskbar is back, with your theme and settings. No logout or reboot was performed.' if self.current_action=='rebuild' else ('Upgrade complete. Your theme and settings were kept. Reopen applications to load the new version. No logout or reboot was performed.' if self.upgrading else 'Installation complete. Reopen applications to load the themes. No logout or reboot was performed.') if self.current_action=='install' else 'Managed XP features removed. Reopen applications to refresh them.'
   if self.current_action in ('install','rebuild'):message+=' Please log out and back in for all changes to take effect.'
   if self.current_action=='install' and set(self.selected())&{'codex_colors','clippy'}:message+=' Restart Codex; type /pet if Clippy is hidden.'
   self.status.setText(message);QtWidgets.QMessageBox.information(self,'Complete',message)
  else:
   self.status.setText('The operation failed. See the installation output for the error and recovery result.')
   self.show_failure_report(code)
 def process_error(self,error):
  if error==QtCore.QProcess.ProcessError.FailedToStart:
   self.append_log('Could not start installer: '+self.process.errorString());self.finished(1,None)
 def closeEvent(self,event):
  if self.process is not None:
   QtWidgets.QMessageBox.information(self,'Installation running','Please wait for the current installation or recovery to finish.');event.ignore()
  else:event.accept()
def main():
 p=argparse.ArgumentParser();p.add_argument('--preview');p.add_argument('--preview-stage',choices=['options','installing','complete'],default='options');p.add_argument('--test-mode',action='store_true');p.add_argument('--rebuild',action='store_true',help='Open on Rebuild (from the Plasma update notice).');a=p.parse_args()
 app=QtWidgets.QApplication(sys.argv[:1]);app.setApplicationName('XPlasma Plus')
 app.setStyle('Windows')
 for name in ['tahoma.ttf','tahomabd.ttf','trebuc.ttf','trebucbd.ttf','trebucit.ttf','trebucbi.ttf','arial.ttf','arialbd.ttf','ariali.ttf','arialbi.ttf']:
  QtGui.QFontDatabase.addApplicationFont(str(BASE/'payload/assets/fonts'/name))
 app.setFont(QtGui.QFont('Tahoma',9))
 palette=app.palette()
 for role,color in [(QtGui.QPalette.ColorRole.Window,'#ece9d8'),(QtGui.QPalette.ColorRole.Button,'#ece9d8'),
  (QtGui.QPalette.ColorRole.Base,'#ffffff'),(QtGui.QPalette.ColorRole.Text,'#000000'),
  (QtGui.QPalette.ColorRole.WindowText,'#000000'),(QtGui.QPalette.ColorRole.ButtonText,'#000000'),
  (QtGui.QPalette.ColorRole.Highlight,'#316ac5'),(QtGui.QPalette.ColorRole.HighlightedText,'#ffffff'),
  (QtGui.QPalette.ColorRole.Light,'#ffffff'),(QtGui.QPalette.ColorRole.Dark,'#aca899')]:palette.setColor(role,QtGui.QColor(color))
 app.setPalette(palette)
 window=Installer(a.test_mode or bool(a.preview));window.show()
 if a.rebuild and window.rebuild.isVisible():window.rebuild.setDefault(True);window.rebuild.setFocus()
 if a.preview_stage=='installing':window.pages.setCurrentIndex(1);window.set_step(2);window.status.setText('Installing desktop components');window.progress.setValue(55)
 if a.preview_stage=='complete':window.pages.setCurrentIndex(1);window.set_step(4);window.tip_title.setText('Welcome');window.tip_text.setText('Setup is complete. Your XP-inspired desktop is ready.');window.progress.setValue(100)
 if a.preview:
  def save():window.grab().save(a.preview);app.quit()
  QtCore.QTimer.singleShot(250,save)
 return app.exec()
if __name__=='__main__':sys.exit(main())
