; The union host's code-class block right after its headers (.text$a sorts before the compiler's .text$mn): the retail
; XMen2.exe image (0x401000..0xa744c6) is copied over it at run time. Keep the size in step with union_host.cpp.
.386
.MODEL FLAT
union_seg SEGMENT PARA PUBLIC 'CODE' ALIAS('.text$a')
PUBLIC _union_space
_union_space DB 700000h DUP(0)
union_seg ENDS
END
