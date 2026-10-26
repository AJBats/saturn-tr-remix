; Room vertices to view space on the SCU DSP: for each vertex (one longword,
; bytes x y z g), x' = tx + m00 x + m01 y + m02 z and so on, the matrix rows
; times 4 (xform.s unpacks a vertex times 4). In batches of 16: 16 vertices
; read into MD1, 48 longwords (x' y' z' each) written out of MD2.
;
; Data RAM, set by the SH-2 (dsp.cpp):
;   MD0  0..11  m00 m01 m02 tx  m10 m11 m12 ty  m20 m21 m22 tz
;   MD3  0      0xFF
;        4      batches
;        5      1
;        6      the vertices' address / 4
;        7      the output's address / 4
; MD3 1..3 hold the vertex being transformed.

        mov #6,ct3
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
vertex: mov mc1,a | mov #0,ct3          ; the vertex
        mov m3,p                        ; the byte mask
        rl8 | mov alu,a | mov #1,ct3
        and | mov all,mc3               ; x
        rl8 | mov alu,a
        and | mov all,mc3               ; y
        rl8 | mov alu,a
        and | mov all,mc3               ; z
        mov #0,ct0
        mov #1,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a
        add | mov alu,a | mov mc0,p
        add | mov all,mc2               ; x'
        mov #1,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a
        add | mov alu,a | mov mc0,p
        add | mov all,mc2               ; y'
        mov #1,ct3
        mov mc3,x | mov mc0,y
        mov mul,p | mov mc3,x | clr a | mov mc0,y
        add | mov mul,p | mov mc3,x | mov alu,a | mov mc0,y
        add | mov mul,p | mov alu,a
        add | mov alu,a | mov mc0,p
        add | mov all,mc2               ; z'
        btm
        nop
        mov #0,ct2
        dma mc2,d0,48
wait2:  jmp wait2,t0
        nop
        mov #4,ct3
        mov m3,a
        mov #5,ct3
        mov m3,p
        mov #4,ct3
        sub | mov alu,a | mov all,mc3   ; a batch less
        jmp batch,nz
        nop
        end
