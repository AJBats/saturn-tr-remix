; Whether the DSP's program waits on a data RAM bank its DMA is using
; (page_banks.cpp): a round is a 64-longword transfer out of MD3 or into it,
; then 160 instructions each reading a longword of MD2 (a bank the transfer
; doesn't use) or of MD3 (the one it does, wherever its counter is), then a
; wait for the transfer. none: the reads alone.
;
; MD0  0 rounds  1 the buffer's address / 4

none:   mvi 159,lop
        lps
        mov m2,x
        mov #0,ct0
        mov m0,a | mov #1,pl
        sub | mov all,mc0
        jmp none,nz
        nop
        end

outoth: mov #1,ct0
        mov m0,wa0
        mov #0,ct3
        dma mc3,d0,64
        mvi 159,lop
        lps
        mov m2,x
oow:    jmp oow,t0
        nop
        mov #0,ct0
        mov m0,a | mov #1,pl
        sub | mov all,mc0
        jmp outoth,nz
        nop
        end

outsam: mov #1,ct0
        mov m0,wa0
        mov #0,ct3
        dma mc3,d0,64
        mvi 159,lop
        lps
        mov m3,x
osw:    jmp osw,t0
        nop
        mov #0,ct0
        mov m0,a | mov #1,pl
        sub | mov all,mc0
        jmp outsam,nz
        nop
        end

inoth:  mov #1,ct0
        mov m0,ra0
        mov #0,ct3
        dma d0,mc3,64
        mvi 159,lop
        lps
        mov m2,x
iow:    jmp iow,t0
        nop
        mov #0,ct0
        mov m0,a | mov #1,pl
        sub | mov all,mc0
        jmp inoth,nz
        nop
        end

insam:  mov #1,ct0
        mov m0,ra0
        mov #0,ct3
        dma d0,mc3,64
        mvi 159,lop
        lps
        mov m3,x
isw:    jmp isw,t0
        nop
        mov #0,ct0
        mov m0,a | mov #1,pl
        sub | mov all,mc0
        jmp insam,nz
        nop
        end
