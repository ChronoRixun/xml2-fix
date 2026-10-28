"""Generate the shared retail style-manager operand ledger (no game execution).

Requires capstone and pefile. The schema maps constructor-derived field values to
names; addresses, instruction bytes, encoding offsets and widths come from the PE.
--verify additionally scans all .text for event field references and tracks
manager-pointer provenance, rejecting unknown auxiliary-via-manager operands.
"""
from pathlib import Path
import argparse, hashlib, os, re
import capstone as cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG
import pefile

ROOT = Path(__file__).resolve().parents[1]
STYLE = {0x118d8:'bitmap_via_styles',0x188:'node_ring_read',0x134:'node_ring',
 0x190:'node_bitmap',0x18c:'node_ring_count',0x194:'node_count',0x184:'node_ring_write',
 0x1a4:'count_or_styles_via_map',0x11a7c:'bitmap_via_map',0x1a8:'styles',
 0x11a8c:'auxiliary',0x11a80:'bitmap',0x11a84:'iterator_index',0x11a88:'iterator_owner'}
EVENT = {0x15d20:'built',0x15da8:'ring',0x16e4c:'write',0x16e50:'read',
 0x16e54:'free',0x16e58:'used',0x16ee0:'live',0x16ee4:'ids',0x17f84:'mask',0x17f88:'shift'}
EVENT_SPANS = [(0x4f58d0,0x4f5969),(0x4f61c0,0x4f6260),(0x4f75e0,0x4f7642),
 (0x4f78e0,0x4f7985),(0x4f8e50,0x4f8e96),(0x4f8fd0,0x4f9032),
 (0x4faac0,0x4fab7e),(0x4facd0,0x4fad6c),(0x4faea0,0x4fbd36)]
PARSER_EVENT_OFFSET = 0x2904
PARSER_ALLOCATIONS = {0x4f7f2f, 0x5010ed}

def generate(exe, verify=False):
 p=pefile.PE(str(exe)); data=Path(exe).read_bytes()
 assert hashlib.sha256(data).hexdigest()=='146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b', 'wrong retail executable'
 md=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_32); md.detail=True
 def decode(lo,hi): return md.disasm(p.get_data(lo-0x400000,hi-lo),lo)
 def row(i,off,width,value,field):
  assert int.from_bytes(i.bytes[off:off+width],'little')==value
  return (i.address,i.bytes.hex(),off,width,value,field)
 styles=[]; events=[]
 for lo,hi in [(0x4fe9c0,0x4feabb),(0x4feb10,0x4feb70),(0x4ff740,0x4ffe00)]:
  for i in decode(lo,hi):
   for o in i.operands:
    if o.type==X86_OP_MEM and o.mem.disp in STYLE:
     styles.append(row(i,i.disp_offset,i.disp_size,o.mem.disp,STYLE[o.mem.disp]))
    if o.type==X86_OP_IMM:
     f = 'capacity' if o.imm==19 and i.mnemonic in ('cmp','mov') else 'last_index' if o.imm==18 and i.mnemonic=='mov' else 'manager_size' if o.imm==0x29a18 else None
     if o.imm==0x1a4 and i.mnemonic=='add': f='count_or_styles_via_map'
     if f: styles.append(row(i,i.imm_offset,i.imm_size,o.imm,f))
 assert len(styles)==72, f'style rows {len(styles)} (expected 72)'
 for lo,hi in EVENT_SPANS:
  for i in decode(lo,hi):
   for o in i.operands:
    if o.type==X86_OP_MEM and o.mem.disp in EVENT:
     events.append(row(i,i.disp_offset,i.disp_size,o.mem.disp,EVENT[o.mem.disp]))
    if o.type==X86_OP_IMM:
     # Bitmap clear counts are MOV ECX,34. A MOV [ESP+1c],34 in
     # the allocator is an EH unwind state, unrelated to event capacity.
     bitmap_count=o.imm==34 and i.mnemonic=='mov' and i.operands[0].type==X86_OP_REG and i.operands[0].reg==cs.x86.X86_REG_ECX
     f='capacity' if o.imm==1064 else 'last_index' if o.imm==1063 else 'words' if bitmap_count else None
     if f: events.append(row(i,i.imm_offset,i.imm_size,o.imm,f))
 assert {r[0] for r in events if r[-1]=='words'}=={0x4f6253,0x4f78e9,0x4f7908}
 # The event template is also the tail of both loaders' temporary owner. Its
 # prefix stays fixed; every heap allocation must grow with the shared layout.
 owners=[]
 for va in sorted(PARSER_ALLOCATIONS):
  i=next(decode(va,va+5))
  assert i.mnemonic=='push' and i.operands[0].imm==0x1a890
  owners.append(row(i,i.imm_offset,i.imm_size,0x1a890,'parser_size'))
 events += owners
 owner_guards=[(va,p.get_data(va-0x400000,size).hex(),what) for va,size,what in [
  (0x4f7ea0,0x44,'temporary owner constructor and event offset'),
  (0x4f7f2d,0x1a,'event loader allocation and constructor'),
  (0x5010eb,0x1a,'style loader allocation and constructor'),
  (0x4fb5ef,8,'event constructor EH state remains retail')]]
 cave_rows=[]
 for va,size,what in [(0x4feab5,6,'style destructor bitmap clear'),
  (0x4ff7c7,10,'node constructor bitmap clear'),(0x4ffc35,6,'node reset bitmap clear'),
  (0x4ffce8,6,'manager bitmap clear'),(0x4ffb93,8,'style refusal epilogue'),
  (0x4faec5,7,'event refusal epilogue')]:
  instructions=list(decode(va,va+size))
  assert sum(i.size for i in instructions)==size, f'cave splits instruction at {va:x}'
  cave_rows.append((va,p.get_data(va-0x400000,size).hex(),what))
 aux=[]
 # The three auxiliary entry displacements are existing style rows, not duplicate writes.
 for r in styles:
  if r[-1]=='auxiliary': aux.append(r)
 if verify:
  # Scan whole executable .text, including functions outside the initial census.
  section=next(s for s in p.sections if s.Name.startswith(b'.text'))
  lite=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_32); lite.skipdata=True
  found=set(); tags={}; reached=set(); unknown=[]
  calls={0x4f78e0:set(),0x4f7ea0:set()};allocations=set();owner_offsets=set();size_refs=set()
  seeds={0x4ffc60:0,0x4ffcc0:0,0x4ffd20:0}
  for va,size,mnem,ops in lite.disasm_lite(section.get_data(),0x400000+section.VirtualAddress):
   if va in seeds: tags={cs.x86.X86_REG_ECX:seeds[va]}
   # Detail decode only candidate instructions and pointer-flow instructions.
   if mnem not in ('mov','lea','add','sub','call','jmp','push','ret','int3') and not any(v.to_bytes(4,'little') in p.get_data(va-0x400000,size) for v in (*EVENT,0x1a890,0x17f8c,PARSER_EVENT_OFFSET)): continue
   i=next(decode(va,va+size),None)
   if i is None: continue
   if mnem in ('call','jmp') and i.operands and i.operands[0].type==X86_OP_IMM and i.operands[0].imm in calls:
    calls[i.operands[0].imm].add(va)
   if mnem=='push' and i.operands and i.operands[0].type==X86_OP_IMM and i.operands[0].imm==0x1a890: allocations.add(va)
   if any(o.type==X86_OP_IMM and o.imm in (0x1a890,0x17f8c) for o in i.operands):size_refs.add(va)
   if any(o.type==X86_OP_MEM and o.mem.disp==PARSER_EVENT_OFFSET for o in i.operands):owner_offsets.add(va)
   if va in seeds: tags={cs.x86.X86_REG_ECX:seeds[va]}
   for o in i.operands:
    if o.type!=X86_OP_MEM: continue
    if o.mem.disp in EVENT: found.add((va,i.disp_offset))
    if o.mem.base in tags and 0x11a8c <= o.mem.disp < 0x29a18 and tags[o.mem.base]==0:
     reached.add(va)
     if not any(r[0]==va for r in aux): unknown.append((hex(va),i.mnemonic,i.op_str))
   if mnem in ('ret','int3'): tags={}; continue
   if mnem=='call':
    target=i.operands[0].imm if i.operands and i.operands[0].type==X86_OP_IMM else 0
    for reg in (cs.x86.X86_REG_EAX,cs.x86.X86_REG_ECX,cs.x86.X86_REG_EDX): tags.pop(reg,None)
    if target==0x4ffe20: tags[cs.x86.X86_REG_EAX]=0
    continue
   if mnem in ('push','jmp'): continue
   if not i.operands or i.operands[0].type!=X86_OP_REG: continue
   dst=i.operands[0].reg; value=None
   if len(i.operands)>1:
    src=i.operands[1]
    if mnem=='mov' and src.type==X86_OP_REG: value=tags.get(src.reg)
    if mnem=='mov' and src.type==X86_OP_MEM and src.mem.base==0 and src.mem.disp==0x78a800: value=0
    if mnem=='lea' and src.type==X86_OP_MEM and src.mem.base in tags: value=tags[src.mem.base]+src.mem.disp
    if mnem in ('add','sub') and dst in tags and src.type==X86_OP_IMM: value=tags[dst]+src.imm*(1 if mnem=='add' else -1)
   tags.pop(dst,None)
   if value is not None: tags[dst]=value
  assert found=={(r[0],r[2]) for r in events if r[-1] in EVENT.values()}, f'event references outside ledger: {found-{(r[0],r[2]) for r in events}}'
  assert not unknown, f'unclassified auxiliary-via-manager: {unknown}'
  assert reached=={r[0] for r in aux}, (reached,aux)
  # Complete constructor ownership closure: no other direct entry, stored code
  # pointer, allocation of this type, or auxiliary-via-owner displacement.
  assert calls=={0x4f78e0:{0x4f7edb,0x4ffd0b},0x4f7ea0:{0x4f7f42,0x501100}}, calls
  assert allocations==PARSER_ALLOCATIONS, allocations
  assert size_refs==PARSER_ALLOCATIONS, f'unclassified owner/pool size: {size_refs}'
  assert owner_offsets=={0x4f7ecf,0x4f81fa,0x5011e4,0x50160e,0x5017fe},owner_offsets
  for target in calls:
   assert not any(target.to_bytes(4,'little') in s.get_data() for s in p.sections), f'stored constructor pointer {target:x}'
 def array(name,typ,rows):
  return f'inline constexpr std::array<site<{typ}>, {len(rows)}> {name}{{{{\n'+''.join(f'\t{{0x{va:x}, "{b}", {off}, {n}, 0x{v:x}, {typ}::{f}}},\n' for va,b,off,n,v,f in rows)+'}};\n'
 header='// Generated by tools/w3_style_manager_ledger.py; do not edit.\n'+array('style_sites','style_field',styles)+'\n'+array('event_sites','event_field',events)
 header+='\ninline constexpr std::array<guard, 6> style_manager_cave_guards{{\n'+''.join(f'\t{{0x{va:x}, \"{b}\", \"{what}\"}},\n' for va,b,what in cave_rows)+'}};\n'
 header+='\ninline constexpr std::array<guard, 4> event_owner_guards{{\n'+''.join(f'\t{{0x{va:x}, "{b}", "{what}"}},\n' for va,b,what in owner_guards)+'}};\n'
 return header, styles,events,aux

def main():
 a=argparse.ArgumentParser(); a.add_argument('--exe',default=os.environ.get('XML2_FIX_TEST_EXE')); a.add_argument('--check',action='store_true'); a.add_argument('--verify',action='store_true'); opts=a.parse_args()
 if not opts.exe: a.error('set XML2_FIX_TEST_EXE or pass --exe')
 header,s,e,aux=generate(opts.exe,opts.verify); path=ROOT/'src/w3_style_manager_sites.hpp'
 if opts.check: assert path.read_text()==header, 'generated header is stale'
 else: path.write_text(header,encoding='utf-8',newline='\n')
 print(f'PASS ledger: {len(s)} style rows / {len(e)} event rows / {len(aux)} auxiliary-via-manager rows; unclassified 0')
if __name__=='__main__': main()
