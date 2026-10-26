; Room vertices to the screen on the SCU DSP, as xform.s's transformRoomAsm
; does it for rooms not under water: view space, the near and far planes,
; the divide (a table lookup), the frame and viewport flags and fog. Each
; vertex is written as the two longwords of a Vertex: x << 16 | y, then
; z << 16 | shade << 8 | clip. In batches of 16: 16 vertices read into MD1,
; 32 longwords written out of MD2.
;
; One clamp of the projected x (and y) to what VDP1 can address, less the
; centre, stands for xform.s's two (to +-2047, then to -1024..1023 once off
; the frame): the wider can't change what the narrower leaves, nor which
; vertices are off the frame.
;
; Data RAM, set by the SH-2 (rooms.cpp):
;   MD0  0..11  m00 m01 m02 tx  m10 m11 m12 ty  m20 m21 m22 tz (m times 4)
;        12 zMin  13 zMax  14 256  15 16  16 the table's address / 4
;        17 0x7FFF00  18 -0x7FFF00
;        19 1023-halfW  20 -1024-halfW  21 halfW
;        22 1023-halfH  23 -1024-halfH  24 halfH
;        25 FRAME_WIDTH  26 FRAME_HEIGHT  27 x0  28 x1  29 y0  30 y1
;        31 FOG_MIN  32 8191  33 65536  34 0xFFFF  35 0xFF00
;        40 the table's entry for this vertex (by DMA)
;   MD3  0 batches  1 1  2 the vertices' address / 4  3 the output's / 4
;        4 0xFF
; The table holds divTable << 4 as longwords, by z >> 12.
; MD3 5..18 hold the vertex being projected: 5 x 6 y 7 z 8 g 9 clip
; 10 x' 11 y' 12 z' 13 z >> 8 16 sx 17 sy 18 shade.

        mov #3,ct3
        mov mc3,wa0
batch:  mov #2,ct3
        mov m3,ra0
        mov #0,ct1
        dma d0,mc1,16
bw:     jmp bw,t0
        mov #0,ct2
        mov m3,a | mov #16,pl
        add | mov all,mc3               ; the next batch's vertices
        mov #0,ct1
        mvi 15,lop
        mov #vertex,top

vertex: mov mc1,a | mov #4,ct3          ; bytes x y z g
        mov mc3,p                       ; 0xFF
        rl8 | mov alu,a
        and | mov all,mc3               ; x
        rl8 | mov alu,a
        and | mov all,mc3               ; y
        rl8 | mov alu,a
        and | mov all,mc3               ; z
        rl8 | mov alu,a
        and | mov all,mc3               ; g
        mov #0,mc3                      ; clip
        mov #0,ct0
        mov #5,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a | mov #10,ct3
        add | mov alu,a | mov mc0,p
        add | mov all,mc3               ; x'
        mov #5,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a | mov #11,ct3
        add | mov alu,a | mov mc0,p
        add | mov all,mc3               ; y'
        mov #5,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a | mov #12,ct3
        add | mov alu,a | mov mc0,p
        add | mov alu,a | mov all,mc3   ; z'

        ; the near and far planes
        mov mc0,p                       ; zMin
        sub
        jmp znear,zs
        mov mc0,p                       ; zMax
        sub
        jmp zfar,ns
        nop
zdone:  mov all,rx                      ; z' within them
        mov mc0,y | mov #13,ct3         ; 256
        mov mul,p | clr a | mov mc0,y   ; 16
        ad2 | mov mul,p | mov alh,mc3   ; z >> 8
        ad2 | mov alh,pl | mov mc0,a    ; z >> 12, the table's address
        add | mov all,ra0
        mov #40,ct0
        dma d0,mc0,1                    ; its entry
dw:     jmp dw,t0
        nop

        ; x', y' within +-0x7FFF00
        mov #10,ct3
        mov #17,ct0
        mov m3,a | mov mc0,p
        sub
        jmp xhi,nzs
        mov mc0,p
        sub
        jmp xlo,s
        mov #11,ct3
xok:    mov m3,a | mov #17,ct0
        mov mc0,p
        sub
        jmp yhi,nzs
        mov mc0,p
        sub
        jmp ylo,s
        mov #14,ct0
        ; >> 8, then times the table's entry >> 16: the projected x, y
yok:    mov #10,ct3
        mov mc3,x | mov m0,y            ; 256
        mov mul,p | mov mc3,x | clr a | mov #10,ct3
        ad2 | mov mul,p | mov alh,mc3   ; x >> 8
        ad2 | mov alh,mc3               ; y >> 8
        mov #40,ct0
        mov #10,ct3
        mov mc3,x | mov m0,y
        mov mul,p | mov mc3,x | clr a | mov #16,ct3
        ad2 | mov mul,p | mov alh,mc3   ; x
        ad2 | mov alh,mc3               ; y

        ; within what VDP1 can address, then the centre added
        mov #19,ct0
        mov #16,ct3
        mov m3,p | mov mc0,a            ; 1023-halfW
        sub
        jmp sxhi,s
        mov mc0,a                       ; -1024-halfW
        sub
        jmp sxlo,nzs
        mov mc0,a                       ; halfW
sxok:   add | mov all,mc3
        mov m3,p | mov mc0,a            ; 1023-halfH
        sub
        jmp syhi,s
        mov mc0,a
        sub
        jmp sylo,nzs
        mov mc0,a                       ; halfH
syok:   add | mov all,mc3

        ; off the frame
        mov #16,ct3
        mov m3,p | mov mc0,a            ; FRAME_WIDTH
        sub
        jmp offf,c                      ; x beyond it, unsigned
        mov #17,ct3
        mov m3,p | mov mc0,a            ; FRAME_HEIGHT
        sub
        jmp offf,c
        mov #16,ct3

        ; the viewport
fdone:  mov m3,a | mov mc0,p            ; x0
        sub
        jmp vleft,s
        nop
vl:     mov m3,a | mov mc0,p            ; x1
        sub
        jmp vright,nzs
        mov #17,ct3
vr:     mov m3,a | mov mc0,p            ; y0
        sub
        jmp vtop,s
        nop
vt:     mov m3,a | mov mc0,p            ; y1
        sub
        jmp vbot,nzs
        nop

        ; shade: g << 8, plus z >> 8 past FOG_MIN, at most 8191
vb:     mov #8,ct3
        mov m3,a | mov #18,ct3
        rl8 | mov alu,a | mov all,mc3
        mov #13,ct3
        mov m3,a | mov mc0,p | mov #18,ct3
        sub | mov alu,a
        jmp fogon,nzs
        nop

        ; x << 16 | y, z << 16 | shade << 8 | clip
shdone: mov #34,ct0
        mov #17,ct3
        mov m3,a | mov mc0,p            ; 0xFFFF
        and | mov all,mc3
        mov m3,a | mov mc0,p            ; 0xFF00
        and | mov all,mc3
        mov #33,ct0
        mov #16,ct3
        mov m3,x | mov m0,y             ; x, 65536
        mov mul,p | clr a | mov #13,ct3
        add | mov alu,a | mov m3,x | mov #17,ct3
        mov m3,p | mov #9,ct3
        or | mov mul,p | mov m3,a | mov all,mc2
        add | mov alu,a | mov #18,ct3
        mov m3,p
        add | mov all,mc2
        btm
        nop

        mov #0,ct2
        dma mc2,d0,32
ow:     jmp ow,t0
        nop
        mov #0,ct3
        mov m3,a | mov #1,pl
        sub | mov all,mc3               ; a batch less
        jmp batch,nz
        nop
        end

        ; the rare cases, each back to where it left
znear:  mov #12,ct0
        jmp zplane
        mov m0,a | mov #9,ct3           ; zMin
zfar:   mov #13,ct0
        mov m0,a | mov #9,ct3           ; zMax
zplane: mov #32,mc3                     ; CLIP_PLANE
        mov #12,ct3
        mov all,mc3
        jmp zdone
        mov #14,ct0
xhi:    mov #17,ct0
        jmp xclamp
        mov m0,a | mov #10,ct3
xlo:    mov #18,ct0
        mov m0,a | mov #10,ct3
xclamp: mov all,mc3
        jmp xok
        mov #11,ct3
yhi:    mov #17,ct0
        jmp yclamp
        mov m0,a | mov #11,ct3
ylo:    mov #18,ct0
        mov m0,a | mov #11,ct3
yclamp: mov all,mc3
        jmp yok
        mov #14,ct0
sxhi:   mov #19,ct0
        mov m0,p | mov #21,ct0
        jmp sxok
        mov mc0,a                       ; halfW
sxlo:   mov #20,ct0
        jmp sxok
        mov m0,p | mov #22,ct0
syhi:   mov #22,ct0
        mov m0,p | mov #24,ct0
        jmp syok
        mov mc0,a                       ; halfH
sylo:   mov #23,ct0
        jmp syok
        mov m0,p | mov #25,ct0
offf:   mov #9,ct3
        mov m3,a | mov #1,pl
        or | mov all,mc3                ; CLIP_FRAME
        mov #27,ct0
        jmp fdone
        mov #16,ct3
vleft:  mov #9,ct3
        mov m3,a | mov #2,pl
        or | mov all,mc3                ; CLIP_LEFT
        jmp vl
        mov #16,ct3
vright: mov #9,ct3
        mov m3,a | mov #4,pl
        or | mov all,mc3                ; CLIP_RIGHT
        jmp vr
        mov #17,ct3
vtop:   mov #9,ct3
        mov m3,a | mov #8,pl
        or | mov all,mc3                ; CLIP_TOP
        jmp vt
        mov #17,ct3
vbot:   mov #9,ct3
        mov m3,a | mov #16,pl
        or | mov all,mc3                ; CLIP_BOTTOM
        jmp vb
        nop
fogon:  mov m3,p                        ; g << 8
        add | mov alu,a
        mov mc0,p                       ; 8191
        sub
        jmp fogmax,nzs
        mov all,mc3
        jmp shdone
        nop
fogmax: mov #32,ct0
        mov m0,a | mov #18,ct3
        jmp shdone
        mov all,mc3
