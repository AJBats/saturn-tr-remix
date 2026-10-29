; Version 2 of meshlight.dsp (page_meshes.cpp): the same vertices, rotated and
; lit on the SCU DSP from the game's own data, a list of meshes a run. What
; changed, from what pages 1 to 4 measured:
;   - no waits on its transfers: the next batch's records and normals come
;     in while this batch is worked, its output goes out while the next is
;     unpacked, and the next job's record comes in during its last batch;
;     no pass touches a bank a transfer is using (page 4: it would stall)
;   - a mesh's last batch runs its vertices only, not a whole 8
;   - each pass pipelined: the multiplier, the ALU and the buses at work on
;     different vertices at once
;   - the light's shift and the ambient in one step: t = ALH of
;     ambient << 16 + 0xFFFF - (n . l) * range * 4, which is
;     ambient - ((n . l) * range >> 14) exactly
;
; The list (page_meshes.cpp MeshLightJob): each job 22 longwords,
;   0 vertices (0 ends the list)  2 the vertices' address / 4
;   4..21 the MeshXform meshxform.s's setup writes (render.h)
; and the normals at the MeshXform's normals. So that the SH-2 writes no more
; than it must for each, the outputs follow each other from where the SH-2
; sets MD0 50 at the start. Each vertex comes out as meshlight.dsp's:
; x >> 14 << 16 | y >> 14 & 0xFFFF, z >> 14 << 16 | shade << 8; a batch's
; output is 16 longwords whatever its vertices (room for whole batches).
;
; Data RAM. MD0, the constants (* set once by the SH-2, page_meshes.cpp):
;   0..4   4 m0, 4 m1, 4 m2, 4 tx, *0xFFFF0000      the x row
;   5..9   4 m3, 4 m4, 4 m5, 4 ty, *0xFFFF          the y row
;   10..14 4 m6, 4 m7, 4 m8, 4 tz, *0xFFFF0000      the z row
;   15..22 lx, ly, lz, range as 16 bits << 8, *-4, ambient << 16 | 0xFFFF,
;          *0xFFFFE000, *0x1F00                     the light
;   23..36 the batch's low halves << 16 (23 and 24 the pipeline's)
;   37 *65536  38 *256  39 *0xFF000000  40 *0xFFFF  41 *0x8000  42 *4
;   46 the next job / 4 (the SH-2 sets it to start)  47 the job's vertices
;   left  48 the next batch's records / 4  49 its normals / 4  50 this
;   batch's output / 4 (the SH-2 sets it to start)  51 *a scratch output / 4,
;   where the start sends the output it hasn't got  52 this batch's
;   vertices - 1
; MD1: 1..25 the batch's x y z a vertex (1 the pipeline's), 26..49 its
;   normals' x y z a vertex, each byte at the top of a longword
; MD2: 0..11 the batch's MeshVertex records, 12..19 its normals
; MD3: 0..15 the batch's output, 16..37 the next job's record

        ; the first job's record, then its first batch's records and normals
start:  mov #46,ct0
        mov #16,ct3
        mov m0,ra0
        dma d0,mc3,22
        mov m0,a | mov #22,pl
        add | mov all,mc0               ; the job after
        mov #51,ct0
        mov m0,wa0                      ; nothing to send yet
sw0:    jmp sw0,t0
        nop
        mov #16,ct3
        mov m3,a | mov #0,pl
        or | mov #18,ct3
        jmp finish,z                    ; no job
        mov #48,ct0
        mov #0,ct2
        mov m3,ra0
        dma d0,mc2,12
        mov m3,a | mov #12,pl
        add | mov all,mc0               ; the next batch's records
        mov #34,ct3                     ; the MeshXform's normals
sw1:    jmp sw1,t0
        nop
        mov m3,a | mov #12,ct2
        sr | mov alu,a
        sr | mov alu,a | mov all,ra0    ; / 4
        dma d0,mc2,8
        mov #8,pl
        add | mov all,mc0               ; the next batch's normals
sw2:    jmp sw2,t0
        nop

        ; a job: its record in MD3 16..37, its first batch fetched
newjob: mov #16,ct3
        mov m3,a | mov #0,pl
        or | mov #47,ct0
        jmp finish,z                    ; the list's end
        mov mc3,mc0                     ; the vertices left

        ; the rows, each half sign extended and times 4, into MD1 0..9
        mov #20,ct3
        mov #42,ct0
        mov m0,y | mov #40,ct0          ; 4
        mov #0,ct1
        mvi 4,lop
        mov #rows,top
rows:   mov m3,p | clr a
        ad2 | mov alh,rx                ; the high half
        mov mul,p | clr a
        ad2 | mov all,mc1 | mov mc3,a | mov mc0,p      ; 0xFFFF
        and | mov alu,a | mov m0,p      ; 0x8000
        xor | mov alu,a | mov #40,ct0
        sub | mov all,rx                ; the low half
        mov mul,p | clr a
        btm
        ad2 | mov all,mc1
        mov #0,ct1
        mov #0,ct0
        mov mc1,mc0
        mov mc1,mc0
        mov mc1,mc0
        mov #5,ct0
        mov mc1,mc0
        mov mc1,mc0
        mov mc1,mc0
        mov #10,ct0
        mov mc1,mc0
        mov mc1,mc0
        mov mc1,mc0

        ; the translation times 4
        mov #25,ct3
        mov mc3,x | mov #3,ct0
        mov mul,p | clr a
        ad2 | mov mc3,x | mov all,mc0
        mov mul,p | clr a | mov #8,ct0
        ad2 | mov mc3,x | mov all,mc0
        mov mul,p | clr a | mov #13,ct0
        ad2 | mov all,mc0

        ; the light: its direction, the range as 16 bits << 8, the ambient
        mov #30,ct3
        mov #15,ct0
        mov mc3,mc0
        mov mc3,mc0
        mov mc3,mc0
        mov mc3,x | mov #37,ct0         ; the range
        mov mc0,y | clr a               ; 65536
        mov mul,p | mov mc0,y           ; 256
        ad2 | mov all,rx                ; range << 16
        mov mul,p | clr a | mov #18,ct0
        ad2 | mov alh,mc0
        mov #29,ct3
        mov mc3,x | mov #37,ct0         ; the ambient
        mov m0,y | mov #40,ct0          ; 65536
        mov mul,p | mov m0,a | mov #20,ct0      ; 0xFFFF
        ad2 | mov all,mc0
        mov #0,ct3
        dma mc3,d0,16                   ; the last job's last output

        ; a batch: the records' low halves << 16 into MD0 25..36
batch:  mov #37,ct0
        mov m0,y | clr a | mov #23,ct0  ; 65536
        mov #0,ct2
        mvi 13,lop
        lps
        ad2 | mov mul,p | mov mc2,x | clr a | mov all,mc0

        ; the vertices' x y z into MD1 2..25: each record's high half, then
        ; its low half
        mov #0,ct2
        mov #25,ct0
        mov #1,ct1
        mvi 3,lop
        mov #u2,top | clr a
u2:     ad2 | mov mc2,p | clr a | mov alh,mc1
        ad2 | mov mc0,p | clr a | mov alh,mc1
        ad2 | mov mc2,p | clr a | mov alh,mc1
        ad2 | mov mc0,p | clr a | mov alh,mc1
        ad2 | mov mc2,p | clr a | mov alh,mc1
        btm
        ad2 | mov mc0,p | clr a | mov alh,mc1
        ad2 | mov alh,mc1

        ; the normals' bytes, each at the top of a longword, into MD1 26..49
        mov #39,ct0
        mov m0,p | mov #12,ct2          ; 0xFF000000
        mov mc2,a | mov #26,ct1
        mvi 7,lop
        mov #nb,top
nb:     and | mov all,mc1
        rl8 | mov alu,a
        and | mov all,mc1
        rl8 | mov alu,a
        btm
        and | mov all,mc1 | mov mc2,a

        ; the job's last batch?
        mov #47,ct0
        mov m0,a | mov #8,pl
        sub
        jmp last,zs
        mov #52,ct0
        mov #7,mc0                      ; 8 vertices
        mov #48,ct0
        mov #0,ct2
        mov m0,ra0
        dma d0,mc2,12                   ; the next batch's records
        mov m0,a | mov #12,pl
        add | mov all,mc0
        jmp light
        nop

        ; its last: the vertices left; the next job's record, and its first
        ; batch's records
last:   mov #1,pl
        sub | mov all,mc0
        mov #46,ct0
        mov #16,ct3
        mov m0,ra0
        dma d0,mc3,22
        mov m0,a | mov #22,pl
        add | mov all,mc0               ; the job after
lw:     jmp lw,t0
        nop
        mov #16,ct3
        mov m3,a | mov #0,pl
        or | mov #18,ct3
        jmp light,z                     ; the list's end
        mov #48,ct0
        mov #0,ct2
        mov m3,ra0
        dma d0,mc2,12
        mov m3,a | mov #12,pl
        add | mov all,mc0
        mov #34,ct3
        mov m3,a
        sr | mov alu,a
        sr | mov all,mc0                ; its normals / 4, fetched after the light

        ; the shades << 8 into the output's odd longwords
light:  mov #52,ct0
        mov m0,lop
        mov #lt,top
        mov #26,ct1
        mov #15,ct0
        mov #0,ct3
        mov mc1,x | mov mc0,y
        mov mul,p | mov mc1,x | clr a | mov mc0,y
lt:     ad2 | mov mul,p | mov mc1,x | mov alu,a | mov mc0,y | mov mc3,wa0      ; past the even longword
        ad2 | mov mul,p | mov alu,a | mov mc0,y         ; range << 8
        ad2 | clr a | mov alh,rx                        ; (n . l) << 8
        mov mul,p | mov mc0,y                           ; -4
        ad2 | mov mc0,a | mov alh,rx                    ; (n . l) * range; the ambient's
        mov mul,p
        ad2 | mov mc0,a | mov alh,pl                    ; t; 0xFFFFE000
        and | mov m0,a | mov #15,ct0                    ; 0x1F00
        jmp clamp,nz                                    ; t outside 0 .. 0x1FFF
        mov mc1,x | mov mc0,y
ltback: btm
        and | mov mul,p | mov mc1,x | clr a | mov mc0,y | mov all,mc3

        ; the next batch's normals
        mov #49,ct0
        mov m0,a | mov #0,pl
        or | mov m0,ra0
        jmp xrow,z
        mov #12,ct2
        dma d0,mc2,8
        mov #8,pl
        add | mov all,mc0

        ; x * 4 & 0xFFFF0000 into the even longwords
xrow:   mov #52,ct0
        mov m0,lop
        mov #xr,top
        mov #2,ct1
        mov #0,ct0
        mov #63,ct3
        mov mc1,x | mov mc0,y
xr:     mov mul,p | mov mc1,x | clr a | mov mc0,y
        ad2 | mov mul,p | mov mc1,x | mov alu,a | mov mc0,y | mov mc3,wa0      ; past the odd longword
        ad2 | mov mul,p | mov alu,a
        ad2 | mov mc0,p | mov alu,a                     ; tx
        ad2 | mov m0,p | mov alu,a | mov #0,ct0         ; 0xFFFF0000
        btm
        and | mov mc1,x | mov mc0,y | mov all,mc3

        ; with y * 4 >> 16 & 0xFFFF
        mov #52,ct0
        mov m0,lop
        mov #yr,top
        mov #2,ct1
        mov #5,ct0
        mov #63,ct3
        mov mc1,x | mov mc0,y
yr:     mov mul,p | mov mc1,x | clr a | mov mc0,y
        ad2 | mov mul,p | mov mc1,x | mov alu,a | mov mc0,y | mov mc3,wa0
        ad2 | mov mul,p | mov alu,a
        ad2 | mov mc0,p | mov alu,a                     ; ty
        ad2 | mov m0,a | mov alh,pl                     ; 0xFFFF
        and | mov m3,p | mov alu,a | mov #5,ct0         ; x's half
        btm
        or | mov mc1,x | mov mc0,y | mov all,mc3

        ; z * 4 & 0xFFFF0000 with the shade into the odd longwords
        mov #52,ct0
        mov m0,lop
        mov #zr,top
        mov #2,ct1
        mov #10,ct0
        mov #0,ct3
        mov mc1,x | mov mc0,y
zr:     mov mul,p | mov mc1,x | clr a | mov mc0,y
        ad2 | mov mul,p | mov mc1,x | mov alu,a | mov mc0,y | mov mc3,wa0
        ad2 | mov mul,p | mov alu,a
        ad2 | mov mc0,p | mov alu,a                     ; tz
        ad2 | mov m0,p | mov alu,a | mov #10,ct0        ; 0xFFFF0000
        and | mov m3,p | mov alu,a                      ; the shade
        btm
        or | mov mc1,x | mov mc0,y | mov all,mc3

        ; the output, sent now, or after the next job's setup when this was
        ; its job's last batch (the setup reads MD3)
        mov #50,ct0
        mov m0,a | mov m0,wa0
        mov #16,pl
        add | mov all,mc0
        mov #47,ct0
        mov m0,a | mov #8,pl
        sub | mov all,mc0
        jmp newjob,zs
        mov #0,ct3
        dma mc3,d0,16
        jmp batch
        nop

        ; the end: the last output sent
finish: mov #0,ct3
        dma mc3,d0,16
fend:   jmp fend,t0
        nop
        end

        ; the shade's clamp, back to where it left: all ones above 0x1FFF
        ; (& 0x1F00 = 0x1F00), 0 below 0
clamp:  or                              ; t's sign
        jmp ltback,ns
        mov #-1,pl
        jmp ltback
        mov #0,pl
