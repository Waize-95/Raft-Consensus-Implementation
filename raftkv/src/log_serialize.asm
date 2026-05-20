; ==============================================================================
; clean_log_serialize.asm
; 16-bit MS-DOS Real Mode Style Assembly
; 
; Translates the C++ function logic for:
; vector<u_int8_t> SerializeLogEntry(const LogEntry& log)
; 
; NOTE: In modern 64-bit C++, term and index are 64-bit integers.
; Since this is a 16-bit MS-DOS simulation, we represent parsing 
; large 64-bit values using block memory moves and loops.
; ==============================================================================

org 100h
jmp start

; --- Dummy LogEntry Data ---
log_term    dq 0x0000000000000005 ; 64-bit term (Term 5)
log_index   dq 0x0000000000000010 ; 64-bit index (Index 16 = 10 hex)
; Note: We aren't implementing the external CRC32 network call or the 
; external command serialization here to keep the MS-DOS routine beautiful and standalone.

buffer      times 256 db 0

; ==============================================================================
; Routine: serialize_log
; Inputs:
;   [bp+4]  = Pointer to output byte buffer
;   [bp+6]  = Pointer to 64-bit Term variable
;   [bp+8]  = Pointer to 64-bit Index variable
; ==============================================================================
serialize_log:
    push bp
    mov bp, sp
    push cx
    push si
    push di
    push bx

    mov di, [bp+4]      ; Output buffer

    ; 1. Serialize 64-bit Term (8 bytes, Big Endian)
    ; Since Intel x86 is Little Endian natively, we must read the 8 bytes 
    ; backwards from memory to put them into the buffer in network order (big-endian).
    mov cx, 8
    mov si, [bp+6]      ; Point SI to the start of the 64-bit Term
    add si, 7           ; Jump to exactly the highest byte
write_term_loop:
    mov al, [si]
    mov [di], al
    dec si              ; Go backward in memory (Little Endian conversion)
    inc di              ; Go forward in buffer
    loop write_term_loop

    ; 2. Serialize 64-bit Index (8 bytes, Big Endian)
    mov cx, 8
    mov si, [bp+8]      ; Point SI to 64-bit Index
    add si, 7
write_index_loop:
    mov al, [si]
    mov [di], al
    dec si
    inc di
    loop write_index_loop

    ; 3. [Pseudo-code] At this point we would call `serialize_command` 
    ; and append it to our buffer!
    
    ; 4. [Pseudo-code] We would then compute the CRC32 checksum over the 
    ; bytes written so far and append those 4 bytes.

done_serialize_log:
    pop bx
    pop di
    pop si
    pop cx
    mov sp, bp
    pop bp
    ret 6               ; Clear 3 parameters

start:
    ; Push addresses 
    mov ax, log_index
    push ax
    mov ax, log_term
    push ax
    mov ax, buffer
    push ax

    call serialize_log

exit:
    mov ax, 4C00h
    int 21h
