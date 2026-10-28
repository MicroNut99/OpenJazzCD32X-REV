| ---------------------------------------------------------------------------------------------
| R9: the bonus level's floor on the Genesis (docs/R9_BONUS_FLOOR.md).
|
| The Sub-CPU's graphics chip draws the floor in two layers (320 x 85 each, 4 bits a pixel,
| cells in columns of 11) into Word RAM (2M mode) and queues them (bmd_queue). The 68000 copies
| them by DMA into video memory at the vblank the SH2 swaps its picture (COMM6 = 0x4000 | a bit
| that changes with every new floor, written with its swap request): the floor changes with the
| 32X picture. Plane A shows layer A (palette line 0) in front of plane B (layer B, line 1);
| where both are 0 the 32X's hole colour shows.
|
| The Genesis display is off from the vblank to screen line 126 (the 32X's sky covers those
| lines): a DMA while the display is off runs at the vblank's speed (some 200 bytes a line
| instead of 18), so the two 14 KB copies are done long before the floor's first line - one
| buffer per layer is enough. The H interrupt (line 126) switches the display on again.
| Included by hw_md.s inside the code the 68000 copies to 0xFF1000.
|
| Video memory while the bonus level runs:
|   0x0000 pattern 0 (empty)          0x0020-0x371F layer A, patterns 1-440
|   0x3800 pattern 448 (empty)        0x3820-0x6F1F layer B, patterns 449-888
|   0x8000 an empty 64 x 32 table (both planes until the first floor)
|   0xA000 plane A                    0xE000 plane B
|   0xDC00 sprite table (one empty)   0xF000 h-scroll
| Plane row r, column c (r < 11, c < 40) = the layer's first pattern + c * 11 + r; the floor's
| first line is screen line 127 (v-scroll 129: lines 0-126 show the planes' empty rows 16-31).
|
| Word RAM (68000 side, 0x200000): layer A at 0x230000, layer B at 0x234000, the file's header
| at 0x23A000.
| ---------------------------------------------------------------------------------------------

        .equ    BM_V,      0xFF7FF0     /* variables: free work RAM between the listener's code
                                           (0xFF1000-0xFF3Bxx) and genbg's tables (0xFF8000) */
        .equ    bmv_on,    0            /* w: 1 = the floor is shown, the vblank copies it */
        .equ    bmv_token, 2            /* w: the SH2's floor bit last copied (-1: none yet) */
        .equ    bmv_pend,  4            /* w: 1 = a floor waits in Word RAM */
        .equ    bmv_two,   6            /* w: 1 = two layers (the file's byte 79), 0 = plane A only */
        .if OL_MODE1
| MODE1-10 (EDCD, the cartridge boot from the Mega EverDrive PRO): the 68000 sees Word RAM at 0x600000
| (0x200000 is the cart there), and its level 4 interrupt goes through the RAM jump table the launcher
| builds (0xFFFD0C, edboot/edcd_md.s), not through the Mega CD's vector register.  Before this the EDCD
| build read the floor's colours and copied the floor from the cart, and the H interrupt that turns the
| display on at line 126 went to an rte: the bonus floor was never shown there.
        .equ    BM_IMG_A,  0x630000
        .equ    BM_IMG_B,  0x634000
        .equ    BM_HEAD,   0x63A000
        .else
        .equ    BM_IMG_A,  0x230000
        .equ    BM_IMG_B,  0x234000
        .equ    BM_HEAD,   0x23A000
        .endif

| d0.w = VRAM address -> VDP write command at 0xC00004 (uses d0-d1)
bm_vram_w:
        moveq   #0,d1
        move.w  d0,d1
        andi.w  #0x3FFF,d1
        swap    d1
        ori.l   #0x40000000,d1
        rol.w   #2,d0
        andi.w  #3,d0
        or.w    d0,d1
        move.l  d1,0xC00004
        rts

| d0.w = VRAM address, d2.w = words of 0
bm_clear:
        bsr.b   bm_vram_w
        subq.w  #1,d2
1:      move.w  #0,0xC00000
        dbra    d2,1b
        rts

| d0.w = name table address, d3.w = first name word (pattern, palette): one layer's 64 x 32 table
bm_names:
        bsr.b   bm_vram_w
        moveq   #0,d4                   /* row */
1:      moveq   #0,d5                   /* column */
2:      moveq   #0,d0
        cmpi.w  #11,d4
        bhs.b   3f
        cmpi.w  #40,d5
        bhs.b   3f
        move.w  d5,d0
        mulu.w  #11,d0
        add.w   d4,d0
        add.w   d3,d0
3:      move.w  d0,0xC00000
        addq.w  #1,d5
        cmpi.w  #64,d5
        blo.b   2b
        addq.w  #1,d4
        cmpi.w  #32,d4
        blo.b   1b
        rts

| int bmd_setup(void): the VDP for the floor, the colours from the header in Word RAM (the
| 68000 owns it), then Word RAM to the Sub-CPU. 1 = done.
bmd_setup:
        movem.l d2-d7/a2-a5,-(sp)
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        clr.w   BM_V+bmv_on
        clr.w   GB_V+gbv_on             /* the level's plane: off */
        lea     0xC00004,a5
        move.w  #0x8134,(a5)            /* display off while setting up */
        move.w  #0x8220,(a5)            /* plane A 0x8000 (empty) until the first floor */
        move.w  #0x8404,(a5)            /* plane B 0x8000 (empty) */
        move.w  #0x856E,(a5)            /* sprites 0xDC00 */
        move.w  #0x8D3C,(a5)            /* h-scroll 0xF000 */
        move.w  #0x8700,(a5)            /* backdrop: palette 0 colour 0 */
        move.w  #0x8B00,(a5)            /* full-screen h / v scroll */
        move.w  #0x9001,(a5)            /* 64 x 32 */
        move.w  #0x9100,(a5)
        move.w  #0x9200,(a5)
        move.w  #0x8C81,(a5)            /* H40 */
        move.w  #0x8F02,(a5)
        moveq   #0,d0                   /* pattern 0 and 448: empty */
        moveq   #16,d2
        bsr.w   bm_clear
        move.w  #0x3800,d0
        moveq   #16,d2
        bsr.w   bm_clear
        move.w  #0x8000,d0              /* the empty table */
        move.w  #2048,d2
        bsr.w   bm_clear
        move.w  #0xDC00,d0              /* one empty sprite */
        moveq   #4,d2
        bsr.w   bm_clear
        move.w  #0xF000,d0              /* h-scroll 0 */
        moveq   #2,d2
        bsr.w   bm_clear
        move.l  #0x40000010,(a5)        /* v-scroll: both planes 129 */
        move.w  #129,0xC00000
        move.w  #129,0xC00000
        move.w  #0xA000,d0              /* plane A: layer A, palette line 0 */
        moveq   #1,d3
        bsr.w   bm_names
        move.w  #0xE000,d0              /* plane B: layer B, palette line 1 */
        move.w  #0x2000+449,d3
        bsr.w   bm_names
        move.l  #0xC0000000,(a5)        /* CRAM 0-15: black, layer A's 15; 16-31: black, B's 15 */
        move.w  #0,0xC00000
        lea     BM_HEAD+14,a0
        moveq   #14,d0
1:      move.w  (a0)+,0xC00000
        dbra    d0,1b
        move.w  #0,0xC00000
        lea     BM_HEAD+46,a0
        moveq   #14,d0
2:      move.w  (a0)+,0xC00000
        dbra    d0,2b
        move.w  #-1,BM_V+bmv_token
        clr.w   BM_V+bmv_pend
        clr.w   BM_V+bmv_two
        cmpi.b  #2,BM_HEAD+79
        bne.b   3f
        move.w  #1,BM_V+bmv_two
3:
        lea     bmd_hint(pc),a0         /* the H interrupt: the Mega CD's level 4 vector register */
        .if OL_MODE1
        move.l  a0,0xFFFD0E             /* MODE1-10: the launcher's jump table, "jmp" at 0xFFFD0C */
        .else
        move.l  a0,d0                   /* (0xA12006, the low word of 0xFFxxxx), and the 32X's */
        move.w  d0,0xA12006             /* (0x70) for a console that takes it from there */
        move.l  a0,0x000070
        .endif
        move.w  #0x8A00+126,(a5)        /* at line 126 */
        move.w  #0x8014,(a5)            /* H interrupts on */
        move.w  #1,BM_V+bmv_on
        move.w  #0x8174,(a5)            /* display on */
        bset    #1,0xA12003             /* DMNA: Word RAM to the Sub-CPU */
        move.w  (sp)+,sr
        movem.l (sp)+,d2-d7/a2-a5
        moveq   #1,d0
        rts

| int bmd_queue(void): the floor in Word RAM is copied at the SH2's next swap (the 68000 owns
| Word RAM: the Sub-CPU gave it; the vblank gives it back)
bmd_queue:
        move.w  #1,BM_V+bmv_pend
        moveq   #1,d0
        rts

| d1.l = VRAM write command with DMA: 7040 words from (source registers set) and the first word
| by hand from (a0) - a DMA from Word RAM reads one word late, so its source is 2 bytes on.
| Uses d1, a0.
bm_dma_layer:
        move.l  d1,0xC00004             /* the 68000 waits here until the DMA is done */
        andi.l  #0xFFFFFF7F,d1
        move.l  d1,0xC00004
        move.w  (a0),0xC00000
        rts

| the vblank's copy of both layers, then Word RAM to the Sub-CPU. Uses d0-d1, a0.
bm_dma_now:
        move.w  #0x8F02,0xC00004
        move.w  #0x9380,0xC00004        /* 7040 words */
        move.w  #0x941B,0xC00004
        .if OL_MODE1
        move.w  #0x9501,0xC00004        /* MODE1-10 layer A: (0x630002) >> 1 = 0x318001 */
        move.w  #0x9680,0xC00004
        move.w  #0x9731,0xC00004
        .else
        move.w  #0x9501,0xC00004        /* layer A: (0x230002) >> 1 = 0x118001 */
        move.w  #0x9680,0xC00004
        move.w  #0x9711,0xC00004
        .endif
        move.l  #0x40200080,d1          /* VRAM 0x0020 */
        lea     BM_IMG_A,a0
        bsr.w   bm_dma_layer
        tst.w   BM_V+bmv_two
        beq.b   1f
        move.w  #0x9380,0xC00004
        move.w  #0x941B,0xC00004
        .if OL_MODE1
        move.w  #0x9501,0xC00004        /* MODE1-10 layer B: (0x634002) >> 1 = 0x31A001 */
        move.w  #0x96A0,0xC00004
        move.w  #0x9731,0xC00004
        .else
        move.w  #0x9501,0xC00004        /* layer B: (0x234002) >> 1 = 0x11A001 */
        move.w  #0x96A0,0xC00004
        move.w  #0x9711,0xC00004
        .endif
        move.l  #0x78200080,d1          /* VRAM 0x3820 */
        lea     BM_IMG_B,a0
        bsr.w   bm_dma_layer
1:      bset    #1,0xA12003             /* DMNA: Word RAM to the Sub-CPU */
        rts

| the H interrupt at line 126: the display on for the floor
bmd_hint:
        move.w  #0x8174,0xC00004
        rte

| int bmd_off(void): the floor no longer shown; the Genesis display off (the next level's
| genbg_load sets the VDP up again)
bmd_off:
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        clr.w   BM_V+bmv_on
        clr.w   BM_V+bmv_pend
        move.w  #0x8004,0xC00004        /* H interrupts off */
        move.w  #0x8134,0xC00004
        move.w  #0x8220,0xC00004
        move.w  #0x8404,0xC00004
        move.w  (sp)+,sr
        moveq   #1,d0
        rts

| the vblank: the display off until line 126; a new floor bit in COMM6 (with the SH2's swap) and
| a floor waiting: copy it now. Uses d0-d1, a0.
bmd_vblank:
        tst.w   BM_V+bmv_on
        bne.b   1f
        rts
1:      move.w  #0x8134,0xC00004        /* the display off until line 126 (the H interrupt) */
        tst.w   BM_V+bmv_pend
        beq.b   9f
        move.w  0xA15126,d0
        cmp.w   0xA15126,d0             /* the SH2 may have been writing: read again */
        beq.b   2f
        move.w  0xA15126,d0
2:      btst    #14,d0
        beq.b   9f
        andi.w  #1,d0
        cmp.w   BM_V+bmv_token,d0
        beq.b   9f
        tst.w   BM_V+bmv_token
        bpl.b   3f
        move.w  #0x8228,0xC00004        /* the first floor: plane A 0xA000, plane B 0xE000 */
        tst.w   BM_V+bmv_two            /* (one layer: plane B stays on the empty table) */
        beq.b   3f
        move.w  #0x8407,0xC00004
3:      move.w  d0,BM_V+bmv_token
        clr.w   BM_V+bmv_pend
        bsr.w   bm_dma_now
9:      rts
