"""TR1: exact generated R2 ledger identity without embedding retail function bodies."""
import collections
import hashlib
import re
import sys
from pathlib import Path
import pefile
R=Path(__file__).resolve().parents[1]
text=Path(sys.argv[1]).read_text(encoding='utf-8')
body=next(b for b in re.findall(r'```ledger\n(.*?)```',text,re.S) if '# row R2' in b)
assert hashlib.sha256(body.encode()).hexdigest()=='5111dd02726c6b3fc120b42ccf414fe4f33fad6a3b9cb1d96a3635df59bd0fb0'
rows=[s.split() for s in body.splitlines() if s and not s.startswith('#')]
assert len(rows)==275
p=R/'src/resident_stats_sites.hpp'
assert p.exists(),'TR1 generated ResidentStats table missing'
out=p.read_text(encoding='utf-8')
assert re.findall(r'// row SHA-256: ([0-9a-f]+)',out)==[hashlib.sha256(' '.join(r).encode()).hexdigest() for r in rows]
image=pefile.PE(sys.argv[2]).get_memory_mapped_image()
for va,k,role,field,off,size,old,new,hx in rows:
 b=bytes.fromhex(hx);a=int(va,16)-0x400000
 assert image[a:a+len(b)]==b,va
 if k in ('disp32','imm32','imm8','incave'):assert int.from_bytes(b[int(off):int(off)+int(size)],'little')==int(old,16)
 if k=='replace':assert hx not in out,'whole retail body must remain outside public source'
assert collections.Counter(r[1] for r in rows)=={'disp32':234,'imm32':9,'incave':8,'cave':16,'rewrite':3,'replace':5}
print('PASS TR1: 275 generated rows, pinned R2 hash, every retail span/operand, five body-free function hashes')
