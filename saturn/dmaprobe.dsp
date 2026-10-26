; The transfers xform.dsp makes, without its arithmetic: debug.cpp times this
; against xform.dsp to tell the DSP's DMA from its computing. Data RAM as
; xform.dsp's (MD3 0..3); what it writes out is MD2 as it stands.

        mov #2,ct3
        mov mc3,ra0
        mov mc3,wa0
batch:  mov #0,ct1
        dma d0,mc1,16
wait1:  jmp wait1,t0
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
