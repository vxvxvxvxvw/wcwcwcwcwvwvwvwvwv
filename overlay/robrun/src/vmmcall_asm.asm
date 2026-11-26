; vmmcall_asm.asm  —  ml64 /c vmmcall_asm.asm
; Appelé depuis hypercall.cpp via extern "C" _rr_vmmcall(cmd, buf, sz, module_base)
; Calling convention x64 Windows :
;   rcx = cmd, rdx = buf, r8 = sz, r9 = module_base
; Le hyperviseur lit les registres guest directement.

PUBLIC _rr_vmmcall

.CODE

_rr_vmmcall PROC
    mov  rax, rcx       ; cmd         → rax  (ABI hv: RAX=cmd)
    mov  rbx, rdx       ; buf_va      → rbx  (ABI hv: RBX=buf_va)
    mov  rcx, r8        ; buf_sz      → rcx  (ABI hv: RCX=buf_sz)
    mov  rdx, r9        ; module_base → rdx  (ABI hv: RDX=module_base, 0=scan LDR)
    vmmcall             ; VMEXIT SVM → try_serve_robrun
    ret                 ; rax = 1 si ok, 0 si erreur
_rr_vmmcall ENDP

END
