import ast,json,re,tempfile,unittest
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]

class TaskbarDefaultsTests(unittest.TestCase):
 def script(self,home,selected=(),repair=False):
  tree=ast.parse((BASE/'backend.py').read_text());function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='taskbar_defaults')
  commands=[];ns={'Path':Path,'json':json,'kdbus':lambda:'qdbus','run':lambda args:commands.append(args)}
  exec(compile(ast.Module(body=[function],type_ignores=[]),'backend.py','exec'),ns)
  ns['taskbar_defaults'](home,set(selected),repair=repair)
  return commands[-1][-1]
 def entries(self,home):
  entries=home/'.local/share/applications';entries.mkdir(parents=True)
  for name in ['systemsettings.desktop','org.kde.dolphin.desktop','firefox.desktop','firefox-esr-ie6.desktop']:(entries/name).write_text('[Desktop Entry]\n')
  return entries
 def test_exactly_three_launchers_with_optional_ie6_browser(self):
  with tempfile.TemporaryDirectory() as tmp:
   home=Path(tmp);entries=self.entries(home)
   for selected,browser in [({'firefox'},'firefox-esr-ie6.desktop'),(set(),'firefox.desktop')]:
    script=self.script(home,selected);urls=json.loads(re.search(r'writeConfig\("launcherUrls",(\[.*?\])\)',script).group(1))
    self.assertEqual(urls,[(entries/name).as_uri() for name in ['systemsettings.desktop','org.kde.dolphin.desktop',browser]])
 def test_position_and_upgrade_migration_preserve_edited_pins(self):
  # Execute the production script. Match Plasma's inert Widget.index API:
  # only addWidget(name,x,y,...) can insert in the live layout.
  from PyQt6.QtCore import QCoreApplication
  from PyQt6.QtQml import QJSEngine
  app=QCoreApplication.instance() or QCoreApplication([])
  with tempfile.TemporaryDirectory() as tmp:
   home=Path(tmp);entries=self.entries(home)
   defaults=[(entries/name).as_uri() for name in ['systemsettings.desktop','org.kde.dolphin.desktop','firefox.desktop']]
   for repair,edited,correct in [(False,False,False),(True,False,False),(True,True,False),(True,False,True)]:
    with self.subTest(repair=repair,edited=edited,correct=correct):
     setup='''
var made=0,removed=0,cleared=0,restored=false;
var defaults=DEFAULTS;
var start={type:"fyi.hotsocket.xplasma.start",index:-1,geometry:{x:4,y:0,width:95,height:30}};
var clock={type:"fyi.hotsocket.xplasma.clock",geometry:{x:900}};
var quick={type:"org.kde.plasma.quicklaunch",geometry:{x:CORRECT?108:970},
 currentConfigGroup:[],readConfig:function(k){return k==="launcherUrls"?(EDITED?["custom.desktop"]:defaults):"kept";},
 remove:function(){removed++;}};
Object.defineProperty(quick,"configKeys",{get:function(){return this.currentConfigGroup.length?["launcherUrls","customSetting"]:[];}});
Object.defineProperty(quick,"configGroups",{get:function(){return this.currentConfigGroup.length?[]:["General"];}});
var task={type:"org.kde.plasma.taskmanager",writeConfig:function(){cleared++;},reloadConfig:function(){}};
var p={widgets:function(){return [start,quick,task,clock];},
 addWidget:function(name,x,y,w,h){
  if(!(x>start.geometry.x+start.geometry.width/2&&x<start.geometry.x+start.geometry.width&&y===15))throw Error("Bad insertion point");
  made++;return {type:"org.kde.plasma.quicklaunch",writeConfig:function(k,v){if(k==="customSetting"&&v==="kept")restored=true;},reloadConfig:function(){}};
 }};
function panels(){return [p];}
'''.replace('DEFAULTS',json.dumps(defaults)).replace('EDITED',str(edited).lower()).replace('CORRECT',str(correct).lower())
     engine=QJSEngine();result=engine.evaluate(setup+self.script(home,repair=repair))
     self.assertFalse(result.isError(),result.toString())
     migrate=not repair or (not edited and not correct)
     self.assertEqual(engine.evaluate('made').toInt(),int(migrate))
     self.assertEqual(engine.evaluate('removed').toInt(),int(migrate))
     self.assertEqual(engine.evaluate('cleared').toInt(),0 if repair else 1)
     self.assertEqual(engine.evaluate('restored').toBool(),repair and migrate)
if __name__=='__main__':unittest.main()
