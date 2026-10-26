from pathlib import Path
import sys,json,hashlib,copy,collections,argparse,subprocess
parser=argparse.ArgumentParser()
parser.add_argument('--reference',type=Path,required=True,help='legends-classic checkout')
parser.add_argument('--build',type=Path,required=True)
parser.add_argument('--collection',type=Path,required=True,help='real-saves folder containing manifest.json')
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--seeds',type=Path,help='coordinator seeded folder containing manifest.json and SaveLC')
parser.add_argument('--current',type=Path,required=True,help='current lcsave_test.exe')
parser.add_argument('--baseline',type=Path,required=True,help='corpus driver linked against committed baseline lcsave.lib')
args=parser.parse_args()
sys.path.insert(0,str(args.reference/'tools'))
from lcsave.convert import Environment,PositionTables,convert_slot,reverse_slot
from lcsave.catalogue import Catalogue,ITEM_OWN_REBUILT
from lcsave import layout as L,oldsave
from lcsave.container import read_file,write_file
from lcsave.lcfiles import read_catalogue_lcc
from lcsave.typed_registry import load_registry
build=args.build.resolve();collection=args.collection.resolve()
out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
# Overlay the built catalogue (including attributes and appended keys) onto the reference's historical catalogue.
# Empty keys in LCC keep their historical hashes; known keys must agree before accepting the overlay.
data=copy.deepcopy(Catalogue.load().data)
lcc=read_catalogue_lcc((build/'Data/lcsave/catalogue.lcc').read_bytes())
for tag,entries in lcc['domains'].items():
 dom=data['domains'].setdefault(tag,{'next':1,'entries':[]})
 for e in entries:
  hit=next((x for x in dom['entries'] if x['serial']==e['id']&0xffffff),None)
  if hit is None:
   assert e['key'];hit={'serial':e['id']&0xffffff,'hash':hashlib.sha256(e['key']).hexdigest(),'aliases':[]};dom['entries'].append(hit)
  if e['key']:assert hit['hash']==hashlib.sha256(e['key']).hexdigest(),(tag,e['id'])
  hit['attributes']=e['attributes']
 dom['next']=max(x['serial'] for x in dom['entries'])+1
data['revision']=lcc['revision']
env=Environment(build=str(build),catalogue=Catalogue(data))
# Read-only build; memoize identical source computations without changing the oracle's logic or inputs.
hash_method=env.binds.hash_tables
hash_cache={flag:hash_method(include_conversation_digest=flag) for flag in (False,True)}
env.binds.hash_tables=lambda *,include_conversation_digest=True: copy.deepcopy(hash_cache[include_conversation_digest])
pos=PositionTables(env,native=L.W2)
registry=load_registry((build/'Data/lcsave/typed-entity-ids.txt').read_bytes())
rebuilt={i for i in pos._item if env.catalogue.attributes_of(i)&ITEM_OWN_REBUILT}
assert len(rebuilt)==19
print('reference ready:',len(rebuilt),'rebuilt definitions,',len(pos._enh),'records',flush=True)
def instances(f):
 S={k:v['data'] for k,v in f['sections'].items()}
 yield from S['ITEM']['instances']
 for w in S['ITEM']['world']:yield w['instance']
 for h in S['HERO']['heroes']:yield from h['equipped']
 for r in S['REST']['records']:yield from r['equipped']
# Independent expectation for the owner's class migration: these 12 permanent ITEM identities
# were belt/armor in the historical catalogue and become gloves. Production derives classes from tables.
changed_class_ids={0x02000047,0x02000049,0x0200004c,0x0200004d,0x0200004e,0x02000050,
                   0x02000052,0x02000054,0x02000055,0x02000056,0x02000057,0x02000058}
changed_class_native={pos._item[i] for i in changed_class_ids}
assert len(changed_class_native)==12

def expected_class_relocations(f,m):
 # This additional owner policy is deliberately separate from the unmodified Python own-list oracle.
 S={k:v['data'] for k,v in f['sections'].items()}
 if f['header']['catalogue_revision']>=8 and S['META']['origin']!=1:return 0
 count=0
 def recover(equipment):
  nonlocal count
  kept=[]
  for i in equipment['equipped']:
   if i['definition'] in changed_class_native:
    m['items']['carried'].append(dict(i,location=2));count+=1
   else:kept.append(i)
  equipment['equipped']=kept
 for h in m['heroes']:recover(h['equipment'])
 for r in S['REST']['records']:recover(m['restore'][pos._stat[r['owner']]]['equipment'])
 return count

def emit(name,blob,source,synthetic=False,kind=None):
 f=read_file(blob,L.KIND_CAMPAIGN)
 ii=list(instances(f));n=sum(i['definition'] in rebuilt for i in ii)
 (out/(name+'.lcsave')).write_bytes(blob)
 meta={'name':name,'source':source,'synthetic':synthetic,'kind':kind or ('synthetic' if synthetic else 'real'),'reward_instances':n,'instances':len(ii),'relocated_instances':0}
 try:
  m,notes=reverse_slot(blob,env,native=L.W2,registry=registry)
  meta['relocated_instances']=expected_class_relocations(f,m)
  (out/(name+'.python.old')).write_bytes(oldsave.write_slot(m,L.W2))
  meta['reference']='loaded'
 except Exception as e:meta['reference']='refused';meta['error']=str(e)
 metadata.append(meta);cases.append(name+'\t'+name+'.lcsave')
 if len(metadata)%20==0:print('prepared',len(metadata),flush=True)
 return f
seen=set();metadata=[];cases=[];excluded=[];seed=None
for row in json.loads((collection/'manifest.json').read_text()):
 if not row['copy']:continue
 p=collection/row['copy'].replace('\\','/');blob=p.read_bytes();sha=hashlib.sha1(blob).hexdigest()
 assert sha==row['sha1'],('collection manifest hash mismatch',row['copy'])
 if sha in seen:continue
 seen.add(sha)
 try:
  f=read_file(blob)
  if f['header']['kind']!=L.KIND_CAMPAIGN:continue
 except Exception:
  try:oldsave.read_slot(blob)
  except Exception:continue
  result=convert_slot(blob,p.name,env,now=0)
  if result.refused:excluded.append({'path':row['copy'],'reason':result.refused});continue
  blob=result.file_bytes
 f=emit(sha[:12],blob,row['copy'])
 if seed is None and metadata[-1]['reference']=='loaded':seed=f
print('real census:',collections.Counter(x['reference'] for x in metadata),'holders',sum(x['reward_instances']>0 for x in metadata),'instances',sum(x['reward_instances'] for x in metadata),'excluded',len(excluded),flush=True)
assert not excluded,('retail inputs were excluded',excluded)
if args.seeds:
 seeded_root=args.seeds.resolve()
 for row in json.loads((seeded_root/'manifest.json').read_text())['seeds']:
  path=seeded_root/row['file'];blob=path.read_bytes()
  assert hashlib.sha256(blob).hexdigest()==row['sha256'],('seed manifest hash mismatch',row['file'])
  emit('coordinator-'+row['name'],blob,row['file'],True,'coordinator')

# Seed every one of the 19 definitions in all four instance-bearing sections, with old own lists,
# affixes, a special name, and stash/other locations. Preserve a real save's unrelated state.
assert seed
synthetic=copy.deepcopy(seed)
S={k:v['data'] for k,v in synthetic['sections'].items()}
# Isolate instance migration from the baseline's unrelated zone-string packing normalization.
S['ZONE']['records']=[]
allids=sorted(rebuilt)
# Old e1/e2 records are still mapped; source-era reward own record ids are in the historical model.
# One stale current-key record is enough: a shortened own list and nonzero interpolation exercise rebuilding.
prefixid=next(i for i,p in pos._enh.items() if env.binds.prefix_range[0]<=p<env.binds.prefix_range[1])
suffixid=next(i for i,p in pos._enh.items() if env.binds.suffix_range[0]<=p<env.binds.suffix_range[1])
specialid=next(iter(pos._trit))
# A historical own key still has a mapping even for definitions now intentionally carrying no bonus.
historical=[i for i,p in pos._enh.items() if p>=env.binds.trit_range[1]]
def seeded(ident,loc):
 own=env.binds.item_own[pos._item[ident]]
 old=next((i for i,p in pos._enh.items() if own and p==own[0]),historical[0])
 return dict(definition=ident,name_a=0,name_b=0,special=specialid,location=loc,
   prefixes=[dict(enhancement=prefixid,value=0x3f000000)],own=[dict(enhancement=old,value=0x3f400000)],
   suffixes=[dict(enhancement=suffixid,value=0x3f800000)])
S['ITEM']['instances']=[seeded(i,5 if n%2 else 2) for n,i in enumerate(allids)]
S['ITEM']['world']=[dict(handle=n,instance=seeded(i,2)) for n,i in enumerate(allids)]
for n,h in enumerate(S['HERO']['heroes']):h['equipped']=[seeded(allids[n%19],1)]
S['REST']['records']=[dict(copy.deepcopy(h),owner=h['hero']) for h in S['HERO']['heroes']]
for n,r in enumerate(S['REST']['records']):r['equipped']=[seeded(allids[n%19],1)]
S['ITEM']['def_state']=[e for e in S['ITEM']['def_state'] if e['item'] not in rebuilt]+[dict(item=i,quantity=7,flag=9) for i in allids]
# Keep the carried count below the conservative instance limit while covering the 19 definitions everywhere.
def pack(f):
 return write_file(L.KIND_CAMPAIGN,[(t,v['entry']['major'],v['entry']['minor'],v['entry']['flags'],v['data'] if v['data'] is not None else v['payload']) for t in f['order'] for v in [f['sections'][t]]],f['header'])
emit('synthetic-all19',pack(synthetic),'invented cases based on real section scaffolding',True)
# Same definitions newly worn as gloves under catalogue8 must stay equipped, including after resave.
current=copy.deepcopy(synthetic);current['header']['catalogue_revision']=8
current['sections']['META']['data']['origin']=0
emit('synthetic-current-gloves',pack(current),'current catalogue, all gloves fit',True)
# Real historical records for the five shrinking lists: e3 disappears on two rewards; the only own record
# disappears on three no-op rewards. Every saved list here must contain an ID absent from current p_enh.
shrunk=copy.deepcopy(seed);Z={k:v['data'] for k,v in shrunk['sections'].items()}
Z['ZONE']['records']=[];Z['ITEM']['world']=[];Z['REST']['records']=[]
for h in Z['HERO']['heroes']:h['equipped']=[]
historical_era=env.eras[0]
shrunk_ids=(0x02000047,0x02000049,0x02000056,0x02000057,0x02000058)
old_instances=[]
for ident in shrunk_ids:
 index=next(n for n,h in enumerate(historical_era.tables['ITEM']) if env.catalogue.lookup('ITEM',h)==ident)
 ids=[env.catalogue.lookup('ENHC',historical_era.tables['ENHC'][n]) for n in historical_era.tables['ITEM_own'][index]]
 assert any(i not in pos._enh for i in ids),(ident,'case does not exercise a removed record')
 i=seeded(ident,2);i['own']=[dict(enhancement=e,value=0x3f400000) for e in ids];old_instances.append(i)
Z['ITEM']['instances']=old_instances
Z['ITEM']['def_state']=[dict(item=i,quantity=7,flag=9) for i in shrunk_ids]
emit('synthetic-five-removed-lists',pack(shrunk),'five historical shortened/empty own lists',True)

(out/'cases.tsv').write_text('\n'.join(cases)+'\n',encoding='utf-8',newline='\n')
(out/'metadata.json').write_text(json.dumps(dict(cases=metadata,excluded=excluded),indent=1)+'\n',encoding='utf-8',newline='\n')
print('done',len(cases),'cases;',sum(m['reward_instances'] for m in metadata),'reward instances',flush=True)

# The baseline driver is this same C++ source linked with the unchanged committed library.
# Compare saved bytes as well as loads so the OFF gate covers both directions.
for mode in ('on','off','base'):
 folder=out/mode
 cmd=([str(args.baseline),str(out/'cases.tsv'),str(build),str(folder),'0'] if mode=='base' else
      [str(args.current),'--rebuilt',str(out/'cases.tsv'),'--data',str(build),'--out',str(folder),'--enabled','1' if mode=='on' else '0'])
 with (out/(mode+'.log')).open('w',encoding='utf-8',newline='\n') as log:
  completed=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,creationflags=0x08004000)
 assert completed.returncode==0,(mode,(out/(mode+'.log')).read_text())
 print((out/(mode+'.log')).read_text().strip(),flush=True)

def verdict(mode):
 return {row.split('\t',1)[0]:row.split('\t',2)[1:] for row in (out/mode/'verdicts.tsv').read_text().splitlines()}
verdicts={m:verdict(m) for m in ('on','off','base')}

def native_instances(m):
 # Use the reference's native serializer to compare every instance field and every raw interpolation bit.
 return [oldsave._write_instance(i,L.W2) for i in m['items']['carried']]+[oldsave._write_instance(w['instance'],L.W2) for w in m['items']['world']]+[oldsave._write_instance(i,L.W2) for h in m['heroes'] for i in h['equipment']['equipped']]+[oldsave._write_instance(i,L.W2) for r in m['restore'] for i in r['equipment']['equipped']]
counts=collections.Counter()
for meta in metadata:
 name=meta['name']
 assert verdicts['on'][name][0]==meta['reference'],(name,verdicts['on'][name],meta)
 assert verdicts['off'][name]==verdicts['base'][name],(name,'OFF refusal changed')
 if meta['reference']=='refused':
  assert verdicts['on'][name][1]==meta['error'],(name,'Python refusal differs')
  counts['same_refusal']+=1;continue
 paths={mode:out/mode/(name+'.old') for mode in ('on','off','base')}
 models={mode:oldsave.read_slot(path.read_bytes(),L.W2) for mode,path in paths.items() if verdicts[mode][name][0]=='loaded'}
 oracle=oldsave.read_slot((out/(name+'.python.old')).read_bytes(),L.W2)
 assert native_instances(models['on'])==native_instances(oracle),(name,'Python migrated instances differ')
 assert models['on']['items']['def_state']==oracle['items']['def_state'],(name,'quantity/state/obtained differs')
 ledger=(out/'on'/(name+'.ledger')).read_text()
 assert ledger.count('moved to carried')==meta['relocated_instances'],(name,'relocation notes not logged once per instance')
 assert 'moved to carried' not in (out/'on'/(name+'.reload.ledger')).read_text(),(name,'relocation repeats after resave')
 counts['equipment_relocated_instances']+=meta['relocated_instances']
 reload=oldsave.read_slot((out/'on'/(name+'.reload.old')).read_bytes(),L.W2)
 assert native_instances(reload)==native_instances(models['on']),(name,'instances not a first-cycle fixed point')
 counts['python_instance_parity']+=1;counts['instance_first_cycle_fixed']+=1
 counts['native_after_initial_save_fixed']+=1 # driver enforces the next full native cycle
 if (out/'on'/(name+'.reload.old')).read_bytes()==paths['on'].read_bytes():counts['first_cycle_byte_fixed']+=1
 else:counts['first_cycle_native_normalizations']+=1
 if meta['synthetic']:
  if meta['kind']=='synthetic':assert (out/'on'/(name+'.reload.old')).read_bytes()==paths['on'].read_bytes(),(name,'synthetic full stream is not immediately fixed')
  counts[meta['kind']+'_cases']+=1
  counts[meta['kind']+'_migrated_instances']+=meta['reward_instances']
 else:
  counts['real_accepted']+=1
  counts['real_holders']+=bool(meta['reward_instances'])
  counts['real_migrated_instances']+=meta['reward_instances']
 # A seeded obsolete own list may be refused by the unchanged ownership validator when OFF; match that refusal too.
 if 'off' not in models:
  assert meta['synthetic'],(name,'real OFF load refused')
  counts['synthetic_off_refused']+=1;continue
 assert paths['off'].read_bytes()==paths['base'].read_bytes(),(name,'OFF native bytes differ from committed base')
 assert (out/'off'/(name+'.reload.old')).read_bytes()==(out/'base'/(name+'.reload.old')).read_bytes(),(name,'OFF reload bytes differ from committed base')
 assert (out/'off'/(name+'.lcsave')).read_bytes()==(out/'base'/(name+'.lcsave')).read_bytes(),(name,'OFF save bytes differ from committed base')
 counts['off_base_byte_identical']+=1
 if not meta['reward_instances']:
  assert paths['on'].read_bytes()==paths['off'].read_bytes(),(name,'unmarked load bytes changed')
  assert (out/'on'/(name+'.reload.old')).read_bytes()==(out/'off'/(name+'.reload.old')).read_bytes(),(name,'unmarked reload bytes changed')
  assert (out/'on'/(name+'.lcsave')).read_bytes()==(out/'off'/(name+'.lcsave')).read_bytes(),(name,'unmarked save bytes changed')
  counts['no_rewards_byte_identical']+=1
summary=dict(counts)
summary['real_campaigns']=sum(not m['synthetic'] for m in metadata)
summary['real_holders_in_collection']=sum(bool(m['reward_instances']) for m in metadata if not m['synthetic'])
summary['rebuilt_catalogue_entries']=len(rebuilt)
(out/'summary.json').write_text(json.dumps(summary,indent=1)+'\n',encoding='utf-8',newline='\n')
print(json.dumps(summary,indent=1),flush=True)
