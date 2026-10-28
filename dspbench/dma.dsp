; The DSP's DMA alone, for the bus tests (bench.cpp): a loop of transfers of
; one size between high work RAM and data RAM, each waited for, from the
; same buffer every time. Started at read, write or none (the same loop with
; no transfer: what the loop itself costs).
;
; MD0  0 the transfers  1 the buffer's address / 4  2 the longwords a transfer

read:   mov #1,ct0
        mov m0,ra0
        mov #2,ct0
        mov #0,ct1
        dma d0,mc1,m0
rw:     jmp rw,t0
        nop
        mov #0,ct0
        mov m0,a | mov #1,pl
        sub | mov all,mc0
        jmp read,nz
        nop
        end

write:  mov #1,ct0
        mov m0,wa0
        mov #2,ct0
        mov #0,ct1
        dma mc1,d0,m0
ww:     jmp ww,t0
        nop
        mov #0,ct0
        mov m0,a | mov #1,pl
        sub | mov all,mc0
        jmp write,nz
        nop
        end

none:   mov #1,ct0
        mov m0,ra0
        mov #2,ct0
        mov #0,ct1
        nop
nw:     jmp nw,t0
        nop
        mov #0,ct0
        mov m0,a | mov #1,pl
        sub | mov all,mc0
        jmp none,nz
        nop
        end
