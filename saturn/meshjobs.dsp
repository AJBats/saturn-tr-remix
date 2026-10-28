; Mesh vertices to the screen on the SCU DSP, a list of meshes a run: as
; meshxform.s's transformMeshAsm does it on its full path (view space, the
; near and far planes, the divide by a table lookup, the frame flags) for
; meshes lit by normals or flat. Each vertex is written as the two longwords
; of a Vertex: x << 16 | y, then z << 16 | shade << 8 | clip. In batches of
; 8: their MeshVertex records (int16 x, y, z: 12 longwords) read into MD1 and
; unpacked into MD2, their 8 normals into MD2 after them, 16 longwords
; written out of MD1. A mesh's last batch may run past its vertices: the
; output has room for whole batches.
;
; The list (rooms.cpp DspMeshJob): each job 23 longwords, which the DSP
; reads itself, so that one start runs every mesh queued:
;   0..11  m00 m01 m02 tx  m10 m11 m12 ty  m20 m21 m22 tz   to MD0 0..11
;   12..17 lx ly lz  range << 8  4  ambient                  to MD0 32..37
;   18..22 batches  normals' longwords a batch (8, or 0 for a flat mesh, which
;          reads the same 8 zero normals each batch)  the vertices' address / 4
;          the output's / 4  the normals' / 4               to MD3 0..4
; and a job of no batches ends it.
;
; The projected x (and y) is clamped once, to what VDP1 can address less the
; centre, as project.dsp does for xform.s's two clamps: the wider can't
; change what the narrower leaves, nor which vertices are off the frame.
; x' and y' >> 14 are kept whole where meshxform.s keeps 16 bits: they differ
; only for vertices over 32767 units to the side of the view. A vertex is
; off the frame when it is off any side of it.
;
; The light, each batch's shades << 8 written ahead into MD1's odd longwords:
; ambient - ((n . l) * range >> 14), each normal byte masked at the top of a
; longword so that (n . l) comes out << 8; then >> 8 within 0 .. 31, which is
; the value within 0 .. 0x1FFF and 0x1F00. A flat mesh's normals are zero and
; its ambient its shade << 8.
;
; Data RAM, set once by the SH-2 (rooms.cpp meshDspInit):
;   MD0  12 VIEW_MIN_F  13 VIEW_MAX_F  14 4  15 4096  16 the table's address / 4
;        17 1023-halfW  18 -1024-halfW  19 halfW
;        20 1023-halfH  21 -1024-halfH  22 halfH
;        23 0  24 FRAME_WIDTH  25 0  26 FRAME_HEIGHT
;        27 0xFFFF  28 0x8000  29 65536  30 1
;        31 0xFF000000  38 0x1FFF  39 0x1F00  40 0x1FFF
;        41 the table's entry for this vertex (by DMA)
;   MD3  40 the next job's address / 4
; The table holds divTable << 4 as longwords, by z >> 18. The arrays must be
; longword aligned.
; MD3 5..14 hold the vertex being projected: 5 x 6 y 7 z 8 clip 9 x' 10 y'
; 11 z' 12 z >> 14 13 sx 14 sy; 16..39 the batch's normals, a longword each
; byte.

        ; the next job: a wait's delay slot may not touch the CT its DMA
        ; writes through
job:    mov #40,ct3
        mov m3,ra0
        mov #0,ct0
        dma d0,mc0,12                   ; the matrix
jw0:    jmp jw0,t0
        mov #0,ct3
        mov #32,ct0
        dma d0,mc0,6                    ; the light
jw1:    jmp jw1,t0
        nop
        dma d0,mc3,5                    ; batches .. the normals' address
jw2:    jmp jw2,t0
        nop
        mov #40,ct3
        mov m3,a | mov #23,pl
        add | mov all,mc3               ; the job after
        mov #0,ct3
        mov m3,a | mov #0,pl
        or | mov #3,ct3
        jmp jend,z                      ; no batches: the list's end
        mov mc3,wa0                     ; the output

batch:  mov #2,ct3
        mov m3,ra0
        mov #0,ct1
        dma d0,mc1,12
bw:     jmp bw,t0
        mov #30,ct0
        mov m3,a | mov #12,pl
        add | mov all,mc3               ; the next batch's vertices

        ; each longword's halves, high then low, sign extended: x y z of
        ; one vertex after another
        mov m0,y | mov #0,ct1           ; 1
        mov #0,ct2
        mvi 11,lop
        mov #unpack,top
unpack: mov m1,x | clr a
        mov mul,p | mov #27,ct0
        ad2 | mov alh,mc2               ; the high half
        mov mc1,a | mov mc0,p           ; 0xFFFF
        and | mov alu,a | mov mc0,p     ; 0x8000
        xor | mov alu,a
        btm
        sub | mov all,mc2               ; the low half

        ; the batch's normals
        mov #4,ct3
        mov m3,ra0
        mov #24,ct2
        dma d0,mc2,8
lw:     jmp lw,t0
        mov #1,ct3
        mov m3,p | mov #4,ct3           ; their longwords a batch
        mov m3,a | mov #31,ct0
        add | mov all,mc3               ; the next batch's
        mov mc0,p | mov #16,ct3         ; the mask
        mov #24,ct2

        ; each normal's x, y, z at the top of a longword, then the shades
        mvi 7,lop
        mov mc2,a | mov #nbyte,top
nbyte:  and | mov all,mc3
        rl8 | mov alu,a
        and | mov all,mc3
        rl8 | mov alu,a
        btm
        and | mov all,mc3 | mov mc2,a
        mov #16,ct3
        mov #1,ct1
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

        ; >> 8 within 0 .. 31, << 8
        jmp lneg,s
        mov mc0,p                       ; 0x1FFF
        sub
        jmp lbig,nzs
        mov mc0,p                       ; 0x1F00
lok:    and | mov all,mc1
        btm
        mov mc1,y | mov #32,ct0         ; past the next x << 16 | y

        mov #0,ct2
        mov #0,ct1
        mvi 7,lop
        mov #vertex,top

vertex: mov #5,ct3
        mov mc2,mc3                     ; x
        mov mc2,mc3                     ; y
        mov mc2,mc3                     ; z
        mov #0,mc3                      ; clip
        mov #0,ct0
        mov #5,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a | mov #9,ct3
        add | mov alu,a | mov mc0,p
        add | mov all,mc3               ; x'
        mov #5,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a | mov #10,ct3
        add | mov alu,a | mov mc0,p
        add | mov all,mc3               ; y'
        mov #5,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a | mov #11,ct3
        add | mov alu,a | mov mc0,p
        add | mov alu,a | mov all,mc3   ; z'

        ; the near and far planes
        mov mc0,p                       ; VIEW_MIN_F
        sub
        jmp znear,zs
        mov mc0,p                       ; VIEW_MAX_F
        sub
        jmp zfar,ns
        nop
zdone:  mov all,rx                      ; z' within them
        mov mc0,y | mov #12,ct3         ; 4
        mov mul,p | clr a
        ad2 | mov alh,mc3               ; z >> 14
        mov #12,ct3
        mov m3,x | mov mc0,y            ; 4096
        mov mul,p | clr a
        ad2 | mov alh,pl | mov mc0,a    ; z >> 18, the table's address
        add | mov all,ra0
        mov #41,ct0
        dma d0,mc0,1                    ; its entry
dw:     jmp dw,t0
        nop

        ; x' and y' >> 14, then times the table's entry >> 16: the
        ; projected x, y
        mov #9,ct3
        mov #14,ct0
        mov mc3,x | mov m0,y            ; 4
        mov mul,p | mov mc3,x | clr a | mov #9,ct3
        ad2 | mov mul,p | mov alh,mc3   ; x >> 14
        ad2 | mov alh,mc3               ; y >> 14
        mov #41,ct0
        mov #9,ct3
        mov mc3,x | mov m0,y
        mov mul,p | mov mc3,x | clr a | mov #13,ct3
        ad2 | mov mul,p | mov alh,mc3   ; x
        ad2 | mov alh,mc3               ; y

        ; within what VDP1 can address, then the centre added
        mov #17,ct0
        mov #13,ct3
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

        ; which sides of the frame it is off
        mov #13,ct3
        mov m3,a | mov mc0,p            ; 0
        sub
        jmp vleft,s
        nop
vl:     mov m3,a | mov mc0,p            ; FRAME_WIDTH
        sub
        jmp vright,nzs
        mov #14,ct3
vr:     mov m3,a | mov mc0,p            ; 0
        sub
        jmp vtop,s
        nop
vt:     mov m3,a | mov mc0,p            ; FRAME_HEIGHT
        sub
        jmp vbot,nzs
        mov #27,ct0

        ; x << 16 | y, z << 16 | shade << 8 | clip
vb:     mov #14,ct3
        mov m3,a | mov mc0,p            ; 0xFFFF
        and | mov all,mc3               ; y & 0xFFFF
        mov #29,ct0
        mov #13,ct3
        mov m3,x | mov m0,y             ; x, 65536
        mov mul,p | clr a | mov #12,ct3
        add | mov alu,a | mov m3,x | mov #14,ct3
        mov m3,p | mov #8,ct3
        or | mov mul,p | mov m3,a | mov all,mc1
        add | mov alu,a | mov m1,p      ; the shade << 8
        btm
        add | mov all,mc1

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

        ; the rare cases, each back to where it left
lneg:   jmp lok
        clr a
lbig:   jmp lok
        mov mc0,a                       ; 0x1FFF
znear:  mov #12,ct0
        jmp zplane
        mov m0,a | mov #8,ct3           ; VIEW_MIN_F
zfar:   mov #13,ct0
        mov m0,a | mov #8,ct3           ; VIEW_MAX_F
zplane: mov #32,mc3                     ; CLIP_PLANE
        mov #11,ct3
        mov all,mc3
        jmp zdone
        mov #14,ct0
sxhi:   mov #17,ct0
        mov m0,p | mov #19,ct0
        jmp sxok
        mov mc0,a                       ; halfW
sxlo:   mov #18,ct0
        jmp sxok
        mov m0,p | mov #20,ct0
syhi:   mov #20,ct0
        mov m0,p | mov #22,ct0
        jmp syok
        mov mc0,a                       ; halfH
sylo:   mov #21,ct0
        jmp syok
        mov m0,p | mov #23,ct0
vleft:  mov #8,ct3
        mov m3,a | mov #3,pl
        or | mov all,mc3                ; CLIP_FRAME | CLIP_LEFT
        jmp vl
        mov #13,ct3
vright: mov #8,ct3
        mov m3,a | mov #5,pl
        or | mov all,mc3                ; CLIP_FRAME | CLIP_RIGHT
        jmp vr
        mov #14,ct3
vtop:   mov #8,ct3
        mov m3,a | mov #9,pl
        or | mov all,mc3                ; CLIP_FRAME | CLIP_TOP
        jmp vt
        mov #14,ct3
vbot:   mov #8,ct3
        mov m3,a | mov #17,pl
        or | mov all,mc3                ; CLIP_FRAME | CLIP_BOTTOM
        jmp vb
        nop
