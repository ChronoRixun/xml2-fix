"""Whole-DLL W2 startup/selection gate in the owned union host; no game launch.
Usage: python scripts/check_w2_startup.py <DLL/map dir> <output dir> [CASE]
Uses only workspace copies, and engine DLLs already copied by the OFF gate.
"""
from pathlib import Path
import hashlib, json, os, re, shutil, struct, subprocess, sys
sys.dont_write_bytecode=True
import uuid
from prove_w2_step3_off import symbols


ROOT=Path(__file__).resolve().parent.parent
DLL=Path(sys.argv[1]).resolve();OUT=Path(sys.argv[2]).resolve()
assert OUT.is_relative_to(ROOT)
OUT.mkdir(parents=True,exist_ok=True)
HOST=ROOT/'native/union/Release/union_host.exe'
RETAIL=Path(os.environ.get('XML2_FIX_TEST_EXE','D:/tools/re-projects/bin/XMen2.exe'))
ENGINE=ROOT/'build/off-proof67-final/engine';DATA=ROOT/'build/off-proof67-final/baseline/Data'
LC='[Save]\nFormat=lc\n';W2=LC+'[Limits]\nEntityCapacity=1\nSaveLayout=w2\n'
EXPECTED_VALUES=(0x10802,0x10000400,0x4000400,0xc80400)
EXPECTED_STATUS='layout W2; defs 1023/1024; enh 4096/4096; att 4095/4096; inst 1024/1024; world 1023/1024; obj 1024/1024; talents 256/256; zones 8/8'
cases={
 'on':(W2,'W2',{}),'explicit':(W2+'ItemEnhancements=4096\n','W2',{}),
 'descriptions':(W2+'[Game]\nObjectiveDescriptions=1\n','W2',{}),
 'off':(LC+'[Limits]\nEntityCapacity=1\n','E10A2',{}),'retail':(LC,'retail',{}),
 'native_ignore':('[Limits]\nSaveLayout=w2\n','native',{}),
 'missing_entity':(LC+'[Limits]\nSaveLayout=w2\n','refuse',{}),
 'conflicting_enh':(W2+'ItemEnhancements=512\n','refuse',{}),
 'unknown_layout':(LC+'[Limits]\nEntityCapacity=1\nSaveLayout=other\n','refuse',{}),
 'guard_objective':(W2,'refuse',{'UNION_MUTATE_VA':'489800'}),
 'guard_talent':(W2,'refuse',{'UNION_MUTATE_VA':'4c0375'}),
 'guard_item':(W2,'refuse',{'UNION_MUTATE_VA':'480430'}),
 'allocation':(W2,'refuse',{'XML2_W2_STARTUP_FAULT':'allocation'}),
 'protect':(W2,'refuse',{'XML2_W2_STARTUP_FAULT':'protect','UNION_READONLY_VA':'488000'}),
 'status':(W2,'W2',{'UNION_SEED_W2_USAGE':'1'}),
}
if len(sys.argv)>3 and sys.argv[3] not in cases:raise SystemExit('unknown W2 startup case: '+sys.argv[3])
results=[];outputs={}
def symbol(table,prefix,contains=''):
 values=[a-0x10000000 for a,n in table if n.startswith(prefix) and contains in n]
 assert len(values)==1,(prefix,values)
 return hex(values[0])
def invoke(name,ini,expected,extra,source=DLL/'dinput.dll',mapping=DLL/'dinput.map',peer=None):
 folder=OUT/(name+'-'+uuid.uuid4().hex);folder.mkdir()
 shutil.copytree(DATA,folder/'Data',dirs_exist_ok=True);shutil.copy2(source,folder/'dinput.dll')
 (folder/'xml2-fix.ini').write_text(ini,encoding='utf-8',newline='\n')
 table=symbols(mapping);env=os.environ.copy()
 for key in ('XML2_SAVE_STARTUP_FAULT','XML2_W2_STARTUP_FAULT','XML2_W2_SNAPSHOT_DIR','UNION_ALLOW_REFUSAL','UNION_MUTATE_VA','UNION_STATUS_RVA','UNION_PEER_WIRE','UNION_DESCRIBE_RVA','UNION_SEED_W2_USAGE','UNION_READONLY_VA','UNION_PIPE_RVA','UNION_SAVE_BACKING_RVA','UNION_SAVE_CLAIM_RVA'):
  env.pop(key,None)
 env.update(extra);env['UNION_PRIVATE_DESKTOP']='1'
 if expected=='refuse':env.update(UNION_ALLOW_REFUSAL='1');env.setdefault('XML2_W2_SNAPSHOT_DIR',str(folder))
 if extra.get('UNION_SEED_W2_USAGE'):
  env['UNION_STATUS_RVA']=symbol(table,'?status@native_save_layout@@')
  env['UNION_PIPE_RVA']=symbol(table,'?pipe@save_format@@')
  env['UNION_SAVE_BACKING_RVA']=symbol(table,'?save_backing@','entity_capacity@@')
  env['UNION_SAVE_CLAIM_RVA']=symbol(table,'?claim@','entity_capacity@@')
 if peer:
  env['UNION_PEER_WIRE']=str(peer);env['UNION_DESCRIBE_RVA']=symbol(table,'?describe@record@gameplay_capabilities@@')
 cap=symbol(table,'?capabilities@','3Vrecord@gameplay_capabilities@@A')
 run=subprocess.run([str(HOST),str(folder/'dinput.dll'),str(RETAIL),str(ENGINE),str(folder/'writes.tsv'),cap,str(folder/'wire.bin'),'108'],cwd=folder,env=env,capture_output=True,creationflags=0x08004000,timeout=120)
 host=(run.stdout+run.stderr).decode(errors='replace');(folder/'host.log').write_text(host,encoding='utf-8')
 log=(folder/'xml2-fix.log').read_text(errors='replace')
 assert run.returncode==0,(name,run.returncode,host[-800:])
 if expected=='refuse':
  assert '(1114)' in host and 'startup: refused:' in log and 'NativeSaveLayout unverified' in log,(name,'missing fail-closed refusal')
  before=(folder/'before-union.bin').read_bytes();after=(folder/'after-union.bin').read_bytes()
  assert len(before)==0x700000 and before==after,(name,'refusal changed image bytes')
  assert 'native layout W2 (' not in log and 'native save layout: layout W2;' not in log,(name,'published refused W2')
  if name in ('allocation','protect'):assert (folder/'w2-fault-hit.txt').exists(),(name,'fault not exercised')
  if name=='protect':assert 'union readonly page retained' in host,(name,'read-only protection not retained')
 else:
  assert 'cannot load' not in host,(name,host)
  if expected!='native':assert 'native layout '+expected+' (' in log,(name,'wrong native layout')
  entries=[(int(a),int(b),tuple(map(int,values.split(',')))) for a,b,values in re.findall(r'online: rule (\d+) .*? revision (\d+) \{([\d,]+)\}',log)]
  layout=[r for r in entries if r[0]==47]
  if expected=='W2':
   assert layout==[(47,1,EXPECTED_VALUES)],(name,layout)
   if name=='status':assert 'union status: '+EXPECTED_STATUS in host and 'union pipe: '+EXPECTED_STATUS in host,(name,host)
   assert (34,1,(4096,0,0,0)) in entries,(name,'implicit enhancement rule34 absent')
   assert 'pool stays at 375 records' not in log and '375 item enhancements (no [Limits]' not in log,(name,'legacy parser misreports W2 item capacity')
   wire=(folder/'wire.bin').read_bytes();canonical=struct.pack('<4sHH',b'GCAP',4,len(entries))+b''.join(struct.pack('<HH4I',i,r,*v) for i,r,v in sorted(entries))
   assert len(wire)==108 and wire[:4]==b'GCAP' and wire[12:44]==hashlib.sha256(canonical).digest(),(name,'canonical agreement digest')
   assert 'native save layout: layout W2; defs 0/1024; enh 0/4096; att 0/4096; inst 0/1024; world 0/1024; obj 0/1024; talents 0/256; zones 0/8' in log,(name,'initial status line')
  else:assert not layout and 'native save layout: layout W2;' not in log,(name,'OFF registered W2')
 return folder,host
for name,(ini,expected,extra) in cases.items():
 if len(sys.argv)>3 and name!=sys.argv[3]:continue
 try:
  if expected=='refuse':folder,host=invoke(name,ini,expected,extra,DLL/'save-startup-faults.dll',DLL/'save-startup-faults.map')
  else:folder,host=invoke(name,ini,expected,extra)
  outputs[name]=folder;results.append({'case':name,'pass':True,'folder':str(folder)});print('PASS',name,flush=True)
 except (AssertionError,OSError) as error:
  results.append({'case':name,'pass':False,'error':str(error)});print('FAIL',name,str(error),flush=True)
if len(sys.argv)==3 and all(k in outputs for k in ('on','off','explicit')):
 comparisons=[('same_w2',W2,'W2',DLL/'dinput.dll',DLL/'dinput.map',outputs['explicit']/'wire.bin','result=0'),
  ('w2_to_e10a2',W2,'W2',DLL/'dinput.dll',DLL/'dinput.map',outputs['off']/'wire.bin','47 NativeSaveLayout'),
  ('e10a2_to_w2',cases['off'][0],'E10A2',DLL/'dinput.dll',DLL/'dinput.map',outputs['on']/'wire.bin','47 NativeSaveLayout'),
  ('wave1_to_w2',cases['off'][0],'E10A2',ROOT/'build/step8-baseline/dinput.dll',ROOT/'build/step8-baseline/dinput.map',outputs['on']/'wire.bin','47 a rule this PC does not have')]
 for name,ini,expected,source,mapping,peer,want in comparisons:
  try:
   _,host=invoke(name,ini,expected,{},source,mapping,peer);assert want in host,(name,host)
   if name!='same_w2':assert 'result=0' not in host,(name,'mismatched peer admitted')
   results.append({'case':name,'pass':True});print('PASS',name,flush=True)
  except (AssertionError,OSError) as error:
   results.append({'case':name,'pass':False,'error':str(error)});print('FAIL',name,str(error),flush=True)
if len(sys.argv)==3:
 try:
  invoke('missing_evidence',W2,'refuse',{'XML2_W2_STARTUP_FAULT':'allocation','XML2_W2_SNAPSHOT_DIR':str(OUT/'uncreated-parent'/'no-snapshots')},DLL/'save-startup-faults.dll',DLL/'save-startup-faults.map')
  results.append({'case':'missing_evidence','pass':False,'error':'missing current snapshots accepted'})
  print('FAIL missing_evidence',flush=True)
 except FileNotFoundError as error:
  accepted=Path(error.filename).name in ('before-union.bin','after-union.bin')
  results.append({'case':'missing_evidence','pass':accepted})
  print(('PASS' if accepted else 'FAIL'),'missing_evidence',flush=True)
(OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print('W2 startup:',len(results),'cases,',sum(not row['pass'] for row in results),'failures',flush=True)
raise SystemExit(any(not row['pass'] for row in results))
