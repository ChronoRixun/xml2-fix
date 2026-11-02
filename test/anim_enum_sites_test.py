"""Independent row-for-row check against the approved design and read-only evidence."""
import json, re, sys
from pathlib import Path
R=Path(__file__).resolve().parents[1]
design=Path(sys.argv[1]) if len(sys.argv)>1 else R.parent/'anim-enum-design'
rows=json.loads((R/'test/anim_enum_sites.json').read_text())
assert len(rows)==88
assert len([x for x in rows if x['kind']=='operand'])==81
assert len([x for x in rows if x['kind']=='bound'])==5
assert len([x for x in rows if x['kind']=='hook'])==2
# Independently transcribed ledger, grouped in the design's row order. C7's nine
# operands are explicitly expanded; C8's final instruction changes two operands.
expected={
'C1':[0x42196d,0x421972,0x421978],
'C2':[0x425f04,0x425f09,0x425f0f],
'C3':[0x425f3b,0x425f8e,0x425f94],
'C4':[0x425fd8,0x425fde,0x426004,0x42600a],
'C5':[0x42612b],
'C6':[0x426142,0x426148,0x426173,0x426179,0x4261a4,0x4261aa,0x4261d5,0x4261db,0x426206,0x42620c,0x426237,0x42623d,0x426268,0x42626e,0x426299,0x42629f],
'C7':[0x426165,0x426196,0x4261c7,0x4261f8,0x426229,0x42625a,0x42628b,0x4262bc,0x4262c2,0x4262c8,0x4262d4,0x4262da,0x4262e0,0x4262ec,0x4262f2,0x4262f8,0x426304],
'C8':[0x425e5b,0x425e6e,0x425e64,0x425e73,0x425e79,0x425e7f],
'C9':[0x425c32], 'C10':[0x425b7a,0x425b82], 'C11':[0x42ead0],
'C12':[0x42f0aa,0x42f0af,0x42f0b5,0x42f0ce,0x42f0d6],
'C13':[0x430cdb,0x430ce0,0x430ce6,0x430cfe,0x430d06],
'C14':[0x4310ff,0x431104,0x43110a,0x43111d],
'T1':[0x425b96,0x425bac], 'T2':[0x42633f,0x4263b4,0x4263ca],
'T3':[0x42675e,0x42676e,0x42678b,0x426764],
'T4':[0x426060,0x426099,0x426078,0x4260a1], 'T5':[0x427c3a,0x427c47],
'M1':[0x427c30], 'M2':[0x4265c0]}
assert {k:[x['va'] for x in rows if x['row']==k] for k in expected}==expected
assert len({x['va'] for x in rows})==88
# Every real cache/table absolute reference in the broad audit is accounted for,
# with the three intentionally-retained CRT constructor/destructor operands.
absrefs=(design/'evidence/absrefs.txt').read_text()
audit=set()
for line in absrefs.splitlines():
 vals=[int(x,16) for x in re.findall(r'0x[0-9a-f]+',line)]
 if any(0x70ba58<=x<0x70f938 for x in vals):audit.add(int(line.split()[0],16))
assert audit <= {x['va'] for x in rows}|{0x67a050,0x67a070,0x67daa0}
print('PASS design ledger row-for-row: 81 in-place edits, 5 caves, 2 hooks; absolute-reference coverage')

# Native initializer order differs from ID order; explicitly retain every ID.
names=(R/'src/anim_enum_names.hpp').read_text()
tables=json.loads((design/'evidence/enum_tables.json').read_text())
native=re.findall(r'"(ea_[a-z0-9_]+)"',names.split('native_names{{',1)[1].split('}};',1)[0])
builtins=re.findall(r'"(ea_[a-z0-9_]+)"',names.split('builtin_names{{',1)[1].split('}};',1)[0])
assert native==[n for n,i in sorted(tables['pc'],key=lambda t:t[1])]
assert builtins==[n for i,n in tables['xml1_missing_on_pc']]
print('PASS native IDs unchanged and XML1 extension ordering agrees with audited tables')
