
org 100h
jmp start


op_code     db 1        ; OP_PUT
k_len       dw 4        ; keys length
v_len       dw 5        ; value length
key_str     db 'name'   ; "name"
val_str     db 'alice'  ; "alice"

buffer      times 256 db 0  

; Routine: serialize_command
; Purpose: Serializes a command into a byte buffer
; Inputs:
;   [bp+4]  = Pointer to output byte buffer
;   [bp+6]  = Operation (1 byte, stored in low byte of word)
;   [bp+8]  = Key Length (16-bit word)
;   [bp+10] = Value Length (16-bit word)
;   [bp+12] = Pointer to Key string
;   [bp+14] = Pointer to Value string
; Returns: AX = Total bytes written to buffer

serialize_command:
    push bp
    mov bp, sp
    push cx
    push si
    push di
    push bx

    ; Load output buffer into Destination Index (di)
    mov di, [bp+4]      
    mov bx, di          ; Save original start of buffer in bx to calculate length later

    ; 1. Serialize operation (1 byte)
    mov ax, [bp+6]
    mov [di], al        ; Write the single byte
    inc di              ; Move buffer pointer forward

    ; 2. Serialize key_len (2 bytes, Big Endian style)
    mov ax, [bp+8]
    xchg ah, al         ; Swap high and low byte to write big-endian!
    mov [di], ah        ; Write high byte
    inc di
    mov [di], al        ; Write low byte
    inc di

    ; 3. Serialize val_len (2 bytes, Big Endian style)
    mov ax, [bp+10]
    xchg ah, al         ; Swap
    mov [di], ah
    inc di
    mov [di], al
    inc di

    ; 4. Append 'key' string bytes
    mov cx, [bp+8]      ; Loop counter = key_len
    mov si, [bp+12]     ; Source Index = memory address of key string
    jcxz copy_val       ; If length is 0, skip to the next section
copy_key_loop:
    mov al, [si]        ; Read 1 byte from source string
    mov [di], al        ; Write 1 byte to output buffer
    inc si
    inc di
    loop copy_key_loop  ; Decrement CX, loop if NOT zero

copy_val:
    ; 5. Append 'val' string bytes
    mov cx, [bp+10]     ; Loop counter = val_len
    mov si, [bp+14]     ; Source Index = memory address of val string
    jcxz done_serialize
copy_val_loop:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop copy_val_loop

done_serialize:
    ; Calculate total bytes written (current DI - start BX)
    mov ax, di
    sub ax, bx

    ; Clean up stack and return
    pop bx
    pop di
    pop si
    pop cx
    mov sp, bp
    pop bp
    ret 12              ; Clear 6 parameters * 2 bytes from the stack

; ==============================================================================
; Start of imaginary program
; ==============================================================================
start:
    ; Push arguments onto the stack in reverse order
    mov ax, val_str
    push ax
    mov ax, key_str
    push ax
    mov ax, [v_len]
    push ax
    mov ax, [k_len]
    push ax
    mov al, [op_code]
    cbw                 ; Convert byte to word
    push ax
    mov ax, buffer
    push ax
    
    call serialize_command
    ; AX now contains the number of bytes pushed into 'buffer' (14 bytes)

exit:
    mov ax, 4C00h       ; DOS exit interrupt
    int 21h
