; Room vertices to view space on the SCU DSP: for each vertex (one longword,
; bytes x y z g), z' = tz + m20 x + m21 y + m22 z and the same for x' and y',
; the matrix rows times 4 (xform.s unpacks a vertex times 4). In batches of
; 16: 16 vertices read into MD1, 64 longwords (g z' x' y' each) written out
; of MD2, the records xform.s's transformRoomDspAsm reads.
;
; Data RAM, set by the SH-2 (rooms.cpp):
;   MD0  0..11  m20 m21 m22 tz  m00 m01 m02 tx  m10 m11 m12 ty
;   MD3  0      batches
;        1      1
;        2      the vertices' address / 4
;        3      the output's address / 4
;        4      0xFF
; MD3 5..7 hold the vertex being transformed.

        mov #2,ct3
        mov mc3,ra0
        mov mc3,wa0
batch:  mov #0,ct1
        dma d0,mc1,16
wait1:  jmp wait1,t0
        nop
        mov #0,ct1
        mov #0,ct2
        mvi 15,lop
        mov #vertex,top
vertex: mov mc1,a | mov #4,ct3          ; the vertex
        mov mc3,p                       ; the byte mask
        and | mov all,mc2               ; g
        rl8 | mov alu,a
        and | mov all,mc3               ; x
        rl8 | mov alu,a
        and | mov all,mc3               ; y
        rl8 | mov alu,a
        and | mov all,mc3               ; z
        mov #0,ct0
        mov #5,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a
        add | mov alu,a | mov mc0,p
        add | mov all,mc2               ; z'
        mov #5,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a
        add | mov alu,a | mov mc0,p
        add | mov all,mc2               ; x'
        mov #5,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a
        add | mov alu,a | mov mc0,p
        add | mov all,mc2               ; y'
        btm
        nop
        mov #0,ct2
        dma mc2,d0,64
wait2:  jmp wait2,t0
        nop
        mov #0,ct3
        mov m3,a
        mov #1,ct3
        mov m3,p
        mov #0,ct3
        sub | mov alu,a | mov all,mc3   ; a batch less
        jmp batch,nz
        nop
        end
