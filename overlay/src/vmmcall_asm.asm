; vmmcall_asm.asm — MASM x64
; Calling convention Windows x64 :
;   RCX = cmd (CMD_FN_SNAPSHOT)
;   RDX = buf ptr
;   R8  = sz
; On mappe :  RAX = cmd, RDI = buf, RSI = sz, puis VMMCALL
; Le hyperviseur lit RDI/RSI depuis vcpu.registers()
; RAX retour = résultat du hyperviseur (1=ok, 0=fail)

.code

PUBLIC _vmmcall_snapshot_asm

_vmmcall_snapshot_asm PROC
    ; Sauvegarder RDI et RSI (non-volatile sur Win64)
    push    rdi
    push    rsi

    ; Charger args
    mov     rax, rcx        ; cmd
    mov     rdi, rdx        ; buf ptr
    mov     rsi, r8         ; sz

    ; VMMCALL → VMEXIT intercepté par Pulzar
    db      0Fh, 01h, 0D9h  ; VMMCALL

    ; RAX = résultat retourné par le hyperviseur

    pop     rsi
    pop     rdi
    ret

_vmmcall_snapshot_asm ENDP

END
