; The game's mesh vertices rotated and lit on the SCU DSP, a list of meshes
; a run, as branch meshlight's saturn/meshlight.dsp has it (page_meshes.cpp):
; the part of saturn/meshxform.s's shaded fast path that is multiplies and
; adds, as Sonic R gives its DSP. The master projects what comes out
; (meshproject.s): its divide and the frame's centre. For meshes lit by
; normals and wholly inside the frame, which take no clamps or clip flags.
;
; Each vertex comes out as the two longwords of a Vertex:
;   x >> 14 << 16 | y >> 14 & 0xFFFF    the view position as meshxform.s keeps
;                                       it, to be projected in place
;   z >> 14 << 16 | shade << 8          final: the clip byte 0
; In batches of 8: their MeshVertex records (int16 x, y, z: 12 longwords)
; read into MD1 and unpacked into MD2, their 8 normals into MD2 after them,
; 16 longwords written out of MD1. A mesh's last batch may run past its
; vertices: the output has room for whole batches.
;
; The list (page_meshes.cpp MeshLightJob): each job 22 longwords, which the DSP
; reads itself, so that one start runs every mesh queued:
;   0..3   batches  the normals' address / 4  the vertices' / 4  the output's / 4
;                                                               to MD3 0..3
;   4..21  the MeshXform meshxform.s's setup writes (render.h)  to MD2 32..49
; and a job of no batches ends it. From the MeshXform, into MD0: the rows
; times 4 at 0..8, the translation times 4 at 9..11, lx ly lz at 12..14, the
; range as 16 bits << 8 at 15, the ambient at 17.
;
; The rows come out 4 times over, across 48 bits: the low 32 bits of x * 4
; hold x >> 14 in their top half whatever the sum's size, as meshxform.s's
; 32-bit sum does, and its upper 32 bits hold y >> 14 in their bottom half.
;
; The light: ambient - ((n . l) * range >> 14), each normal byte masked at the
; top of a longword so that (n . l) comes out << 8; then >> 8 within 0 .. 31,
; which is the value within 0 .. 0x1FFF and 0x1F00. meshxform.s multiplies
; (n . l) as 16 bits: a light 128 long and normals 127 long keep it there.
;
; Data RAM, set once by the SH-2 (page_meshes.cpp meshLightLoad):
;   MD0  16 4  18 0x1FFF  19 0x1F00  20 0x1FFF  21 0xFFFF  22 0x8000
;        23 0xFF000000
;   MD1  48 0xFFFF  49 0x8000  50 256
;   MD3  10 0xFFFF0000  11 0xFFFF  13 4  40 the next job's address / 4
; MD3 16..39 hold the batch's normals, a longword each byte.

        ; the next job: a wait's delay slot may not touch the CT its DMA
        ; writes through
job:    mov #40,ct3
        mov m3,ra0
        mov #0,ct3
        dma d0,mc3,4                    ; batches .. the output's address
jw0:    jmp jw0,t0
        mov #32,ct2
        mov #0,ct3
        mov m3,a | mov #0,pl
        or
        jmp jend,z                      ; no batches: the list's end
        nop
        dma d0,mc2,18                   ; the MeshXform
jw1:    jmp jw1,t0
        mov #40,ct3
        mov m3,a | mov #22,pl
        add | mov all,mc3               ; the job after
        mov #3,ct3
        mov mc3,wa0                     ; the output

        ; the rows' int16 pairs, each half times 4 (the pad's under tx's)
        mov #13,ct3
        mov m3,y | mov #32,ct2          ; 4
        mov #0,ct0
        mov #48,ct1
        mvi 4,lop
        mov #rows,top
rows:   mov m2,p | clr a
        ad2 | mov alh,rx                ; the high half
        mov mul,p | clr a
        ad2 | mov all,mc0 | mov mc2,a | mov mc1,p       ; 0xFFFF
        and | mov alu,a | mov m1,p      ; 0x8000
        xor | mov alu,a | mov #48,ct1
        sub | mov all,rx                ; the low half
        mov mul,p | clr a
        btm
        ad2 | mov all,mc0

        ; the translation times 4
        mov #9,ct0
        mov mc2,x
        mov mul,p | clr a | mov mc2,x
        ad2 | mov all,mc0 | mov mul,p | mov mc2,x | clr a
        ad2 | mov all,mc0 | mov mul,p | clr a
        ad2 | mov all,mc0

        ; the ambient, the light's direction, its range as 16 bits << 8
        mov #41,ct2
        mov #17,ct0
        mov mc2,mc0
        mov #12,ct0
        mov mc2,mc0
        mov mc2,mc0
        mov mc2,mc0
        mov mc2,a | mov mc1,p           ; 0xFFFF
        and | mov alu,a | mov mc1,p     ; 0x8000
        xor | mov alu,a
        sub | mov all,rx | mov mc1,y    ; 256
        mov mul,p | clr a
        ad2 | mov all,mc0

batch:  mov #2,ct3
        mov m3,ra0
        mov #0,ct1
        dma d0,mc1,12
bw:     jmp bw,t0
        nop
        mov m3,a | mov #12,pl
        add | mov all,mc3               ; the next batch's vertices

        ; each longword's halves, high then low, sign extended: x y z of
        ; one vertex after another
        mov #0,ct1
        mov #0,ct2
        mvi 11,lop
        mov #unpack,top
unpack: mov m1,p | clr a | mov #21,ct0
        ad2 | mov alh,mc2               ; the high half
        mov mc1,a | mov mc0,p           ; 0xFFFF
        and | mov alu,a | mov mc0,p     ; 0x8000
        xor | mov alu,a
        btm
        sub | mov all,mc2               ; the low half

        ; the batch's normals
        mov #1,ct3
        mov m3,ra0
        mov #24,ct2
        dma d0,mc2,8
nw:     jmp nw,t0
        mov #23,ct0
        mov m3,a | mov #8,pl
        add | mov all,mc3               ; the next batch's
        mov mc0,p | mov #16,ct3         ; the mask
        mov #24,ct2
        mvi 7,lop
        mov mc2,a | mov #nbyte,top

        ; each normal's x, y, z at the top of a longword
nbyte:  and | mov all,mc3
        rl8 | mov alu,a
        and | mov all,mc3
        rl8 | mov alu,a
        btm
        and | mov all,mc3 | mov mc2,a

        ; the shades << 8, into the output's odd longwords
        mov #16,ct3
        mov #1,ct1
        mov #12,ct0
        mvi 7,lop
        mov #ndot,top
ndot:   mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        ad2 | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        ad2 | mov mul,p | mov alu,a
        ad2 | mov alh,rx | mov mc0,y    ; (n . l) << 8, range << 8
        mov mul,p | clr a | mov mc0,y   ; 4
        ad2 | mov alh,rx                ; (n . l) * range
        mov mul,p | clr a
        ad2 | mov alh,pl | mov mc0,a    ; >> 14, ambient
        sub | mov alu,a
        jmp lneg,s
        mov mc0,p                       ; 0x1FFF
        sub
        jmp lbig,nzs
        mov mc0,p                       ; 0x1F00
lok:    and | mov all,mc1
        btm
        mov mc1,y | mov #12,ct0         ; past the next even longword

        ; x * 4 & 0xFFFF0000 into each even longword
        mov #0,ct0
        mov #10,ct3
        mov #63,ct1
        mov #0,ct2
        mvi 7,lop
        mov #xrow,top
xrow:   mov mc2,x | mov mc0,y | mov mc1,pl      ; past the odd longword
        mov mul,p | mov mc2,x | clr a | mov mc0,y
        ad2 | mov mul,p | mov mc2,x | mov alu,a | mov mc0,y
        ad2 | mov mul,p | mov alu,a | mov #9,ct0
        ad2 | mov alu,a | mov mc0,p             ; tx
        ad2 | mov alu,a | mov m3,p | mov #0,ct0 ; 0xFFFF0000
        btm
        and | mov all,mc1

        ; with y * 4 >> 16 & 0xFFFF
        mov #11,ct3
        mov #63,ct1
        mov #0,ct2
        mov #3,ct0
        mvi 7,lop
        mov #yrow,top
yrow:   mov mc2,x | mov mc0,y | mov mc1,pl      ; past the odd longword
        mov mul,p | mov mc2,x | clr a | mov mc0,y
        ad2 | mov mul,p | mov mc2,x | mov alu,a | mov mc0,y
        ad2 | mov mul,p | mov alu,a | mov #10,ct0
        ad2 | mov alu,a | mov mc0,p             ; ty
        ad2 | mov alh,pl | mov m3,a             ; 0xFFFF
        and | mov alu,a | mov m1,p | mov #3,ct0 ; x's half
        btm
        or | mov all,mc1

        ; z * 4 & 0xFFFF0000 with the shade into each odd longword
        mov #10,ct3
        mov #0,ct1
        mov #0,ct2
        mov #6,ct0
        mvi 7,lop
        mov #zrow,top
zrow:   mov mc2,x | mov mc0,y | mov mc1,pl      ; past the even longword
        mov mul,p | mov mc2,x | clr a | mov mc0,y
        ad2 | mov mul,p | mov mc2,x | mov alu,a | mov mc0,y
        ad2 | mov mul,p | mov alu,a | mov #11,ct0
        ad2 | mov alu,a | mov mc0,p             ; tz
        ad2 | mov all,pl | mov m3,a             ; 0xFFFF0000
        and | mov alu,a | mov m1,p | mov #6,ct0 ; the shade
        btm
        or | mov all,mc1

        mov #0,ct1
        dma mc1,d0,16
ow:     jmp ow,t0
        mov #0,ct3
        mov m3,a | mov #1,pl
        sub | mov all,mc3               ; a batch less
        jmp batch,nz
        nop
        jmp job
        nop
jend:   end

        ; the shade's clamps, each back to where it left
lneg:   jmp lok
        clr a
lbig:   jmp lok
        mov mc0,a                       ; 0x1FFF
