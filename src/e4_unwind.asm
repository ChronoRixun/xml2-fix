; Authored x86 SEH observer thunk. No game instructions or exception recovery.
.386
.MODEL FLAT
EXTERN _e4_unwind_callback:PROC
.CODE
PUBLIC _e4_unwind_handler
.safeseh _e4_unwind_handler
_e4_unwind_handler PROC
    jmp _e4_unwind_callback
_e4_unwind_handler ENDP
END
