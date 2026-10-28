"""Regenerate short font guards and region hashes from an owned retail PE.

Usage: python tools/generate_lc_font_sites.py <XMen2.exe>
This reads the game file and writes only src/lc_fonts_sites.hpp. No game runs.
"""
from pathlib import Path
import hashlib,sys
import pefile
root=Path(__file__).resolve().parents[1]
pe=pefile.PE(sys.argv[1]);data=pe.get_memory_mapped_image();base=pe.OPTIONAL_HEADER.ImageBase
def raw(va,n):return data[va-base:va-base+n]
sites=[(0x598142,5,1,'map_pc'),(0x598157,5,1,'map_hd'),
       (0x597c3f,10,2,'manager'),(0x597c49,5,1,'slots'),(0x597c4e,5,1,'count'),
       (0x597c68,10,2,'counter'),(0x597c7c,5,1,'manager'),
       (0x597194,6,2,'current'),(0x598116,6,2,'counter_offset'),
       (0x59812a,5,1,'count'),(0x598134,6,2,'counter_offset'),(0x59813c,6,2,'current'),
       (0x5981e6,7,3,'names'),(0x5981fc,3,2,'count8'),(0x598217,3,2,'count8'),
       (0x5d7e79,47,0,'style'),(0x5c61d0,6,0,'render'),
       (0x5c636e,5,0,'scale_draw'),(0x5c62f5,5,0,'scale_clip')]
regions=[(0x596a70,0x596acd),(0x596af0,0x596df0),(0x596df0,0x597000),
         (0x597190,0x5972b7),(0x597c20,0x597c82),(0x597c90,0x598010),
         (0x5980f0,0x598e50),(0x5d7cb0,0x5d7f02),(0x5c61d0,0x5c6399),
         (0x5c6c30,0x5c6e30),(0x5ee580,0x5ee597),(0x564b70,0x564ba0)]
rows=['// Generated interoperability guards; no game payloads.','inline const site sites[]{']
for va,n,offset,kind in sites:rows.append(f' {{0x{va:x},"{raw(va,n).hex()}",{offset},kind::{kind}}},')
rows+=['};','inline const region_guard regions[]{']
for lo,hi in regions:rows.append(f' {{0x{lo:x},0x{hi:x},"{hashlib.sha256(raw(lo,hi-lo)).hexdigest()}"}},')
rows+=['};']
(root/'src/lc_fonts_sites.hpp').write_text('\n'.join(rows)+'\n',encoding='utf-8',newline='\n')
