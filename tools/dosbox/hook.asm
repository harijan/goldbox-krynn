; HOOK.COM: a TSR that watches INT 16h for tools/dosbox/cokbox.py.
; nasm -f bin -o HOOK.COM hook.asm; cokbox.py carries the bytes (HOOK_COM).
;
; It counts the keyboard reads, so that the host, reading guest memory, can
; tell when the game waits for a key: INT 16h function 0 or 10h (Crt's
; ReadKey) entered and not yet returned, or function 1 or 11h (KeyPressed)
; finding no key. The host finds the block by its signature, 16-byte aligned
; at PSP:0110.
;
; HOOK [N]: keep N (hex) more paragraphs resident, to move the programs run
; after it up in memory (a chosen PSP for the game).

        org 100h
start:  jmp install
        times 0x10 - ($ - $$) db 0
sig:    db 'COKHOOK1'           ; PSP:0110
waiting dw 0                    ; +8: 1 while function 0/10h waits in the BIOS
reads   dw 0                    ; +10: function 0/10h calls returned
polls   dw 0                    ; +12: function 1/11h calls that found no key
lastds  dw 0                    ; +14: DS of the last function 0/1 caller
retip   dw 0                    ; +16: return address of the last function 0/10h
retcs   dw 0                    ; +18
old16   dd 0                    ; +20: the previous INT 16h
calls   dw 0                    ; +24: function 1/11h calls, key or not
callss  dw 0                    ; +26: SS, SP (pointing at the return
callsp  dw 0                    ; +28: address) and BP of the last function
callbp  dw 0                    ; +30: 0/10h call, for walking its frames
pollip  dw 0                    ; +32: where the last function 1/11h call that
pollcs  dw 0                    ; +34: found no key returns to past Crt's
                                ;      KeyPressed (its caller's call site)
pollss  dw 0                    ; +36: SS and BP there, for walking the
pollbp  dw 0                    ; +38: frames

handler:
        cmp ah, 0
        je .read
        cmp ah, 10h
        je .read
        cmp ah, 1
        je .poll
        cmp ah, 11h
        je .poll
        jmp far [cs:old16]
.read:
        mov [cs:lastds], ds
        mov [cs:callss], ss
        mov [cs:callsp], sp
        mov [cs:callbp], bp
        push bp
        mov bp, sp
        push word [bp+2]
        pop word [cs:retip]
        push word [bp+4]
        pop word [cs:retcs]
        pop bp
        mov word [cs:waiting], 1
        pushf
        call far [cs:old16]
        mov word [cs:waiting], 0
        inc word [cs:reads]
        iret
.poll:
        mov [cs:lastds], ds
        inc word [cs:calls]
        pushf
        call far [cs:old16]
        jnz .key
        inc word [cs:polls]
        push bp
        mov bp, sp              ; IP, CS, flags, then KeyPressed's return
        push word [bp+8]
        pop word [cs:pollip]
        push word [bp+10]
        pop word [cs:pollcs]
        push word [bp]
        pop word [cs:pollbp]
        mov [cs:pollss], ss
        pop bp
        cmp ax, ax              ; ZF again: no key (INC cleared it)
.key:   sti
        retf 2
resident_end:

install:
        ; N from the command tail, in hex
        xor bx, bx
        mov si, 81h
.skip:  lodsb
        cmp al, ' '
        je .skip
.digit: cmp al, '0'
        jb .done
        cmp al, '9'
        jbe .dec
        or al, 20h
        cmp al, 'a'
        jb .done
        cmp al, 'f'
        ja .done
        sub al, 'a' - 10
        jmp .add
.dec:   sub al, '0'
.add:   shl bx, 1
        shl bx, 1
        shl bx, 1
        shl bx, 1
        or bl, al
        lodsb
        jmp .digit
.done:  push bx
        mov ax, 3516h
        int 21h
        mov [old16], bx
        mov [old16+2], es
        mov dx, handler
        mov ax, 2516h
        int 21h
        mov dx, msg
        mov ah, 9
        int 21h
        pop dx
        add dx, (resident_end - $$ + 100h + 15) / 16
        mov ax, 3100h
        int 21h
msg:    db 'COKHOOK installed', 13, 10, '$'
