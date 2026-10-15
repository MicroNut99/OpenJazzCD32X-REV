| ---------------------------------------------------------------------------------------------
| REV: the Genesis plane under the 32X picture (docs/GENESIS_PLANE.md).
|
| The level's background tiles are on plane A, its black cells on plane B (one solid pattern);
| the 32X draws the sky behind them (32X colours without bit 15) and the sprites, foreground
| tiles and panel in front (colours with bit 15). Included by hw_md.s inside the code the
| 68000 copies to 0xFF1000, so it runs from work RAM like the rest of the listener.
|
| The level's data (tools/genesis_convert.py, OJGB v3) is in the RAM cart (in the world pack);
| the 68000 reads it through the 32X bank window (0x900000 + offset in the 1 MB bank) while
| loading. The vblank handler never reads the cart: everything it needs is in work RAM.
|
| Video memory (64 KB):
|   0x0000-0x9FFF patterns 0-1279      0xA000-0xAFFF plane B (64 x 32)
|   0xB000-0xBFFF patterns 1280-1407   0xC000-0xCFFF the listener's text table (not shown)
|   0xD000-0xDBFF patterns 1408-1503   0xDC00        sprite table (one empty sprite)
|   0xE000-0xEFFF plane A (64 x 32)    0xF000        h-scroll table
|   0xF400-0xFFFF patterns 1504-1599
|
| Work RAM 0xFF8000-0xFFF000 (above the listener, below the stack): see the GB_ equates.
|
| Plane rows and columns: world cell (x, y) (8 x 8 pixels) is plane column x & 63, plane row
| y & 31. The window - the 64 x 32 world cells the planes hold - starts at gbv_cx / gbv_cy and
| follows the camera: target cx = (x >> 3) - 11, cy = (y >> 3) - 5. h-scroll = -x, v-scroll =
| y - 12 (the 32X view starts on screen line 12 and shows world row y there; plane line = world
| row & 255).
|
| Commands (cmd_table): 37 genbg_load(cart offset), 38 genbg_start(x << 16 | y),
| 39 genbg_off(), 40 genbg_cell(cell << 16 | tile)  - cell = y * 256 + x (tiles of 32 x 32)
| COMM4 = camera x, COMM6 = camera y (bits 0-10), fade (11-13 level, 14 towards white),
| hide (15: the Genesis display off - another screen is shown). Written by the SH2.
| ---------------------------------------------------------------------------------------------

        .equ    GB_MAP,   0xFF8000      /* 64 x 256 tile numbers (255 = nothing) */
        .equ    GB_BLACK, 0xFFC000      /* 64 x 32 bytes: black cells (bit 7 = column 0) */
        .equ    GB_TMAP,  0xFFC800      /* 32 bytes a tile: plane A words, rows of 4 */
        .equ    GB_CYC,   0xFFE800      /* the colour cycles as in the file */
        .equ    GB_CYCMAX,0x600
        .equ    GB_PAL,   0xFFEE00      /* 64 colours before fading */
        .equ    GB_HEAD,  0xFFEE80      /* the file header (48 bytes; +38 plane B mode, +40 texture words) */
        .equ    GB_V,     0xFFEEC0      /* variables */
        .equ    gbv_on,    0            /* w: 1 = a level is shown (vblank updates) */
        .equ    gbv_cx,    2            /* w: window's first world cell column */
        .equ    gbv_cy,    4            /* w: window's first world cell row */
        .equ    gbv_ntile, 6            /* w: tiles in GB_TMAP */
        .equ    gbv_ncyc,  8            /* w: colour cycle groups (0-8) */
        .equ    gbv_ok,    10           /* w: 1 = a level's data is loaded */
        .equ    gbv_sr,    12           /* w: dispatcher: status register around VDP commands */
        .equ    gbv_lines, 14           /* w: test: most lines one vblank pass took (from line 224) */
        .equ    gbv_cyc,   16           /* l x 8: the groups' positions (16.16) */
        .equ    GB_BUF,    0xFFEF00     /* 256 bytes: a row or column of name words */
        .equ    GB_VEND,   0xFFF000
        .equ    GB_BLACKW, 0x6001       /* plane B: pattern 1 (solid colour 15), palette 3 */
        .equ    GB_MAXTILES, 256
        .equ    GB_MAXPATS,  1600
        .equ    GB_COLS,   3            /* columns / rows per vblank while following the camera */
        .equ    GB_ROWS,   2

| ---------------------------------------------------------------------------------------------
| gb_copy: d6.l = cart offset (even), d7.l = words, a1 = destination; d5.w = 0: a1 is RAM
| (advances), 1: a1 is the VDP data port. Crosses 1 MB banks. Uses d0-d1, a0; d6 / d7 / a1
| updated. Leaves cart bank 0 selected.
gb_copy:
        tst.l   d7
        beq.b   9f
        move.l  d6,d0
        swap    d0
        lsr.w   #4,d0
        andi.w  #3,d0
        move.w  d0,0xA15104             /* the bank of this offset */
        move.l  d6,d0
        andi.l  #0xFFFFF,d0
        movea.l d0,a0
        adda.l  #0x900000,a0
        move.l  #0x100000,d1
        sub.l   d0,d1
        lsr.l   #1,d1                   /* words left in this bank */
        cmp.l   d7,d1
        bls.b   1f
        move.l  d7,d1
1:      sub.l   d1,d7
        add.l   d1,d6
        add.l   d1,d6
        subq.l  #1,d1                   /* d1 >= 0: count - 1 */
        tst.w   d5
        bne.b   4f
2:      move.w  (a0)+,(a1)+
        dbra    d1,2b                   /* low 16 bits; then the high part, 65536 at a time */
        sub.l   #0x10000,d1
        bpl.b   2b
        bra.b   gb_copy
4:      move.w  (a0)+,(a1)
        dbra    d1,4b
        sub.l   #0x10000,d1
        bpl.b   4b
        bra.b   gb_copy
9:      move.w  #0,0xA15104
        rts

| d0.w = VRAM address -> VDP write command (uses d0-d1)
gb_vram_w:
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

| d2.w = world cell x, d3.w = world cell y -> d0.w plane A word, d4.w plane B word
| (uses d1, a0; keeps d2, d3, d5-d7, a1-a5)
gb_cell:
        moveq   #0,d0
        move.w  #GB_BLACKW,d4
        cmpi.w  #1024,d2                /* outside the level (negative too): black */
        bhs.b   9f
        cmpi.w  #256,d3
        bhs.b   9f
        move.w  d3,d1                   /* black: bit 7 - ((x >> 2) & 7) of byte (y >> 2) * 32 + (x >> 5) */
        lsr.w   #2,d1
        lsl.w   #5,d1
        move.w  d2,d0
        lsr.w   #5,d0
        add.w   d0,d1
        lea     GB_BLACK,a0
        move.b  0(a0,d1.w),d1
        move.w  d2,d0
        lsr.w   #2,d0
        not.w   d0
        andi.w  #7,d0
        btst    d0,d1
        bne.b   1f
        moveq   #0,d4
1:      move.w  d3,d1                   /* tile number at (x >> 2, y >> 2) */
        lsr.w   #2,d1
        lsl.w   #8,d1
        move.w  d2,d0
        lsr.w   #2,d0
        add.w   d0,d1
        lea     GB_MAP,a0
        moveq   #0,d0
        move.b  0(a0,d1.w),d0
        cmp.w   GB_V+gbv_ntile,d0
        bhs.b   8f
        lsl.w   #5,d0                   /* 32 bytes a tile */
        move.w  d3,d1
        andi.w  #3,d1
        lsl.w   #3,d1                   /* a row of 4 cells: 8 bytes */
        add.w   d1,d0
        move.w  d2,d1
        andi.w  #3,d1
        add.w   d1,d1
        add.w   d1,d0
        lea     GB_TMAP,a0
        move.w  0(a0,d0.w),d0
        rts
8:      moveq   #0,d0
9:      rts

| d2.w = world cell x: that column for the window's 32 rows, both planes
| (uses d0-d1, d3-d7, a0-a3)
gb_column:
        move.w  d2,d5
        andi.w  #63,d5
        add.w   d5,d5                   /* the column's byte offset in a plane row */
        lea     GB_BUF,a1
        move.w  GB_V+gbv_cy,d6
        moveq   #0,d7                   /* plane row k = 0..31 */
        cmpi.w  #1024,d2
        blo.b   1f
20:     clr.w   (a1)+                   /* outside the level: nothing on A, black on B */
        move.w  #GB_BLACKW,62(a1)
        addq.w  #1,d7
        cmpi.w  #32,d7
        blo.b   20b
        bra.w   9f
1:      lea     GB_MAP,a0               /* a0: the map column (x >> 2) */
        move.w  d2,d0
        lsr.w   #2,d0
        adda.w  d0,a0
        lea     GB_BLACK,a3             /* a3: the black cells' byte column (x >> 5) */
        move.w  d2,d0
        lsr.w   #5,d0
        adda.w  d0,a3
        move.w  d2,d4                   /* d4: the black bit, 7 - ((x >> 2) & 7) */
        lsr.w   #2,d4
        not.w   d4
        andi.w  #7,d4
        lea     GB_TMAP,a2              /* a2: the column in a tile, (x & 3) * 2 */
        move.w  d2,d0
        andi.w  #3,d0
        add.w   d0,d0
        adda.w  d0,a2
2:      move.w  d7,d3                   /* world row = cy + ((k - cy) & 31) */
        sub.w   d6,d3
        andi.w  #31,d3
        add.w   d6,d3
        cmpi.w  #256,d3
        bhs.b   7f
        move.w  d3,d1
        lsr.w   #2,d1                   /* tile row */
        move.w  d1,d0
        lsl.w   #8,d0
        moveq   #0,d2
        move.b  0(a0,d0.w),d2           /* tile */
        lsl.w   #5,d1                   /* black cells: byte (y >> 2) * 32 */
        move.b  0(a3,d1.w),d1
        moveq   #0,d0
        cmp.w   GB_V+gbv_ntile,d2
        bhs.b   3f
        lsl.w   #5,d2
        move.w  d3,d0
        andi.w  #3,d0
        lsl.w   #3,d0
        add.w   d0,d2
        move.w  0(a2,d2.w),d0
3:      move.w  d0,(a1)+
        btst    d4,d1
        beq.b   4f
        move.w  #GB_BLACKW,62(a1)
        bra.b   8f
4:      clr.w   62(a1)
        bra.b   8f
7:      clr.w   (a1)+
        move.w  #GB_BLACKW,62(a1)
8:      addq.w  #1,d7
        cmpi.w  #32,d7
        blo.b   2b
9:      move.w  #0x8F80,0xC00004        /* auto-increment 128: down the column */
        move.w  d5,d0
        addi.w  #0xE000,d0
        bsr.w   gb_vram_w
        lea     GB_BUF,a1
        moveq   #31,d1
5:      move.w  (a1)+,0xC00000
        dbra    d1,5b
        tst.w   GB_HEAD+38              /* plane B is the texture: nothing to write there */
        bne.b   7f
        move.w  d5,d0
        addi.w  #0xA000,d0
        bsr.w   gb_vram_w
        moveq   #31,d1
6:      move.w  (a1)+,0xC00000
        dbra    d1,6b
7:      move.w  #0x8F02,0xC00004
        rts

| d3.w = world cell row: that row for the window's 64 columns, both planes
| (uses d0-d2, d4-d7, a0-a3)
gb_row:
        move.w  d3,d5
        andi.w  #31,d5
        lsl.w   #7,d5                   /* the row's byte offset in a plane */
        lea     GB_BUF,a1
        move.w  GB_V+gbv_cx,d6
        moveq   #0,d7                   /* plane column k = 0..63 */
        cmpi.w  #256,d3
        blo.b   1f
20:     clr.w   (a1)+                   /* outside the level: nothing on A, black on B */
        move.w  #GB_BLACKW,126(a1)
        addq.w  #1,d7
        cmpi.w  #64,d7
        blo.b   20b
        bra.w   9f
1:      move.w  d3,d0
        lsr.w   #2,d0
        lea     GB_BLACK,a3             /* a3: the black cells' row (y >> 2) * 32 */
        move.w  d0,d1
        lsl.w   #5,d1
        adda.w  d1,a3
        lea     GB_MAP,a0               /* a0: the map row (y >> 2) * 256 */
        lsl.w   #8,d0
        adda.w  d0,a0
        lea     GB_TMAP,a2              /* a2: the row in a tile, (y & 3) * 8 */
        move.w  d3,d0
        andi.w  #3,d0
        lsl.w   #3,d0
        adda.w  d0,a2
2:      move.w  d7,d2                   /* world column = cx + ((k - cx) & 63) */
        sub.w   d6,d2
        andi.w  #63,d2
        add.w   d6,d2
        cmpi.w  #1024,d2
        bhs.b   7f
        move.w  d2,d4
        lsr.w   #2,d4                   /* tile column */
        moveq   #0,d1
        move.b  0(a0,d4.w),d1           /* tile */
        moveq   #0,d0
        cmp.w   GB_V+gbv_ntile,d1
        bhs.b   3f
        lsl.w   #5,d1
        move.w  d2,d0
        andi.w  #3,d0
        add.w   d0,d0
        add.w   d0,d1
        move.w  0(a2,d1.w),d0
3:      move.w  d0,(a1)+
        move.w  d4,d0                   /* black: bit 7 - (tile column & 7) of byte column >> 3 */
        lsr.w   #3,d0
        move.b  0(a3,d0.w),d1
        not.w   d4
        andi.w  #7,d4
        btst    d4,d1
        beq.b   4f
        move.w  #GB_BLACKW,126(a1)
        bra.b   8f
4:      clr.w   126(a1)
        bra.b   8f
7:      clr.w   (a1)+
        move.w  #GB_BLACKW,126(a1)
8:      addq.w  #1,d7
        cmpi.w  #64,d7
        blo.b   2b
9:      move.w  d5,d0
        addi.w  #0xE000,d0
        bsr.w   gb_vram_w
        lea     GB_BUF,a1
        moveq   #63,d1
5:      move.w  (a1)+,0xC00000
        dbra    d1,5b
        tst.w   GB_HEAD+38              /* plane B is the texture: nothing to write there */
        bne.b   7f
        move.w  d5,d0
        addi.w  #0xA000,d0
        bsr.w   gb_vram_w
        moveq   #63,d1
6:      move.w  (a1)+,0xC00000
        dbra    d1,6b
7:      rts

| scroll: d6.w = camera x, d5.w = camera y (uses d0-d2). Plane A: h = -x, v = y - 12 (screen
| line 12 shows world row y). Plane B the same, or the parallax texture's own (GB_HEAD+38):
|   1 (P2DPaletteEffect, 8 x 8 texture, speed 1/8): tx = (8192 - x) >> 3, ty = (2048 - y) >> 3,
|     h = -(x + tx), v = y - 12 + ty
|   2 (P1DPaletteEffect, diagonal stripes, speed 1/2): p = (x + y) >> 1, h = p - x, v = y - 12
gb_scroll:
        move.l  #0x70000003,0xC00004    /* h-scroll (0xF000): plane A, then B */
        move.w  d6,d0
        neg.w   d0
        move.w  d0,0xC00000
        move.w  GB_HEAD+38,d1
        beq.b   1f
        cmpi.w  #1,d1
        bne.b   2f
        move.w  #8192,d2
        sub.w   d6,d2
        lsr.w   #3,d2
        add.w   d6,d2
        neg.w   d2
        move.w  d2,0xC00000
        bra.b   3f
2:      move.w  d6,d2
        add.w   d5,d2
        lsr.w   #1,d2
        sub.w   d6,d2
        move.w  d2,0xC00000
        bra.b   3f
1:      move.w  d0,0xC00000
3:      move.l  #0x40000010,0xC00004    /* v-scroll: plane A, then B */
        move.w  d5,d0
        subi.w  #12,d0
        move.w  d0,0xC00000
        cmpi.w  #1,d1
        bne.b   4f
        move.w  #2048,d2
        sub.w   d5,d2
        lsr.w   #3,d2
        add.w   d2,d0
4:      move.w  d0,0xC00000
        rts

| the whole window for gbv_cx / gbv_cy (uses d0-d7, a0-a1)
gb_fill:
        move.w  GB_V+gbv_cy,d3
        moveq   #31,d2
1:      move.w  d2,-(sp)
        move.w  d3,-(sp)
        bsr.w   gb_row
        move.w  (sp)+,d3
        move.w  (sp)+,d2
        addq.w  #1,d3
        dbra    d2,1b
        rts

| the 64 colours: GB_PAL with the colour cycles advanced one vblank, faded by d7 (COMM6) bits
| 11-14, into CRAM (uses d0-d5, a0-a5)
gb_cram:
        lea     GB_CYC+2,a0             /* the groups (the file's count word first) */
        lea     GB_V+gbv_cyc,a2
        lea     GB_PAL,a5
        move.w  GB_V+gbv_ncyc,d4
        subq.w  #1,d4
        bmi.b   7f
1:      move.w  2(a0),d2                /* amount */
        move.l  4(a0),d0                /* step per vblank (16.16) */
        add.l   (a2),d0                 /* position */
        moveq   #0,d1
        move.w  d2,d1
        swap    d1                      /* amount << 16 */
2:      tst.l   d0
        bpl.b   3f
        add.l   d1,d0
        bra.b   2b
3:      cmp.l   d1,d0
        blo.b   4f
        sub.l   d1,d0
        bra.b   3b
4:      move.l  d0,(a2)+
        swap    d0                      /* d0.w = whole part P */
        move.w  8(a0),d3                /* entries */
        lea     10(a0),a3               /* {k, CRAM byte address} */
        move.w  d3,d5
        lsl.w   #2,d5
        lea     0(a3,d5.w),a4           /* the group's base colours */
        subq.w  #1,d3
        bmi.b   6f
5:      move.w  (a3)+,d1                /* shown colour = base[(k + P) mod amount] */
        add.w   d0,d1
        cmp.w   d2,d1
        blo.b   55f
        sub.w   d2,d1
55:     add.w   d1,d1
        move.w  0(a4,d1.w),d1
        move.w  (a3)+,d5
        move.w  d1,0(a5,d5.w)
        dbra    d3,5b
6:      move.w  d2,d5                   /* next group after the colours */
        add.w   d5,d5
        lea     0(a4,d5.w),a0
        dbra    d4,1b
7:      move.l  #0xC0000000,0xC00004    /* CRAM 0 */
        move.w  d7,d0
        lsr.w   #8,d0
        lsr.w   #3,d0
        andi.w  #15,d0                  /* bit 3: towards white, bits 0-2: level */
        moveq   #63,d3
        move.w  d0,d1
        andi.w  #7,d1
        bne.b   8f
10:     move.w  (a5)+,0xC00000          /* no fade */
        dbra    d3,10b
        rts
8:      lsl.w   #3,d0
        lea     gb_fade(pc),a4
        adda.w  d0,a4                   /* 8 bytes: channel level 0-7 -> faded */
9:      move.w  (a5)+,d1
        moveq   #0,d4
        move.w  d1,d0                   /* blue: bits 9-11 */
        lsr.w   #8,d0
        lsr.w   #1,d0
        andi.w  #7,d0
        move.b  0(a4,d0.w),d4
        lsl.w   #4,d4
        move.w  d1,d0                   /* green: bits 5-7 */
        lsr.w   #5,d0
        andi.w  #7,d0
        or.b    0(a4,d0.w),d4
        lsl.w   #4,d4
        move.w  d1,d0                   /* red: bits 1-3 */
        lsr.w   #1,d0
        andi.w  #7,d0
        or.b    0(a4,d0.w),d4
        add.w   d4,d4
        move.w  d4,0xC00000
        dbra    d3,9b
        rts

| per level 0-7 (0 = none, 7 = all black / all white): channel level 0-7 -> faded level
gb_fade:
        .byte   0, 1, 2, 3, 4, 5, 6, 7
        .byte   0, 1, 2, 3, 3, 4, 5, 6
        .byte   0, 1, 1, 2, 3, 4, 4, 5
        .byte   0, 1, 1, 2, 2, 3, 3, 4
        .byte   0, 0, 1, 1, 2, 2, 3, 3
        .byte   0, 0, 1, 1, 1, 1, 2, 2
        .byte   0, 0, 0, 0, 1, 1, 1, 1
        .byte   0, 0, 0, 0, 0, 0, 0, 0
        .byte   0, 1, 2, 3, 4, 5, 6, 7
        .byte   1, 2, 3, 4, 4, 5, 6, 7
        .byte   2, 3, 3, 4, 5, 6, 6, 7
        .byte   3, 4, 4, 5, 5, 6, 6, 7
        .byte   4, 4, 5, 5, 6, 6, 7, 7
        .byte   5, 5, 6, 6, 6, 6, 7, 7
        .byte   6, 6, 6, 6, 7, 7, 7, 7
        .byte   7, 7, 7, 7, 7, 7, 7, 7

        .align  2
gb_areas:                               /* VRAM address, patterns: in the file's order */
        .word   0x0000, 1280
        .word   0xB000, 128
        .word   0xD000, 96
        .word   0xF400, 96


| ---------------------------------------------------------------------------------------------
| int genbg_load(cart offset): the level's data into video memory and work RAM. 1 = done,
| 0 = not an OJGB v3 file / too big. The Genesis display stays off until genbg_start.
genbg_load:
        movem.l d2-d7/a2-a5,-(sp)
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        move.l  46(sp),d3               /* sr 2 + 10 registers 40 + return 4 */
        clr.w   GB_V+gbv_on
        clr.w   GB_V+gbv_ok
        lea     0xC00004,a5
        move.w  #0x8134,(a5)            /* display off while loading */
        move.l  d3,d6                   /* the header */
        lea     GB_HEAD,a1
        moveq   #24,d7
        moveq   #0,d5
        bsr.w   gb_copy
        cmpi.l  #0x4F4A4742,GB_HEAD     /* 'OJGB' */
        bne.w   90f
        cmpi.w  #4,GB_HEAD+4
        bne.w   90f
        move.w  GB_HEAD+10,d0           /* tiles */
        cmpi.w  #GB_MAXTILES,d0
        bhi.w   90f
        move.w  d0,GB_V+gbv_ntile
        move.w  GB_HEAD+8,d4            /* patterns */
        cmpi.w  #GB_MAXPATS,d4
        bhi.w   90f
        moveq   #0,d7                   /* colour cycles: the rest of the file */
        move.w  GB_HEAD+36,d7
        lsl.l   #2,d7
        sub.l   GB_HEAD+32,d7
        bmi.w   90f
        cmpi.l  #GB_CYCMAX,d7
        bhi.w   90f
        /* the VDP: planes, tables, 64 x 32, H40, full-screen scroll, backdrop = colour 0 */
        move.w  #0x8238,(a5)            /* plane A 0xE000 */
        move.w  #0x8405,(a5)            /* plane B 0xA000 */
        move.w  #0x856E,(a5)            /* sprites 0xDC00 */
        move.w  #0x8D3C,(a5)            /* h-scroll 0xF000 */
        move.w  #0x8700,(a5)            /* backdrop: palette 0 colour 0 */
        move.w  #0x8B00,(a5)            /* full-screen h / v scroll */
        move.w  #0x9001,(a5)            /* 64 x 32 */
        move.w  #0x9100,(a5)            /* no window */
        move.w  #0x9200,(a5)
        move.w  #0x8C81,(a5)            /* H40, no shadow / highlight */
        move.w  #0x8F02,(a5)
        /* colour cycles */
        move.l  d3,d6
        add.l   GB_HEAD+32,d6
        lsr.l   #1,d7
        lea     GB_CYC,a1
        bsr.w   gb_copy
        move.w  GB_CYC,d0
        cmpi.w  #8,d0
        bhi.w   90f
        move.w  d0,GB_V+gbv_ncyc
        lea     GB_V+gbv_cyc,a0
        moveq   #7,d0
1:      clr.l   (a0)+
        dbra    d0,1b
        /* palettes */
        move.l  d3,d6
        add.l   GB_HEAD+12,d6
        lea     GB_PAL,a1
        moveq   #64,d7
        bsr.w   gb_copy
        /* patterns into the four areas */
        move.l  d3,d6
        add.l   GB_HEAD+16,d6
        lea     gb_areas(pc),a2
        moveq   #3,d2
2:      tst.w   d4
        beq.b   4f
        move.w  (a2)+,d0                /* VRAM address */
        move.w  (a2)+,d1                /* patterns there */
        cmp.w   d1,d4
        bhs.b   3f
        move.w  d4,d1
3:      sub.w   d1,d4
        move.w  d1,-(sp)
        bsr.w   gb_vram_w
        moveq   #0,d7
        move.w  (sp)+,d7
        lsl.l   #4,d7                   /* 16 words a pattern */
        lea     0xC00000,a1
        moveq   #1,d5
        bsr.w   gb_copy
        moveq   #0,d5
        dbra    d2,2b
4:      /* the tile table, the map, the black cells */
        move.l  d3,d6
        add.l   GB_HEAD+20,d6
        lea     GB_TMAP,a1
        moveq   #0,d7
        move.w  GB_V+gbv_ntile,d7
        lsl.l   #4,d7
        bsr.w   gb_copy
        move.l  d3,d6
        add.l   GB_HEAD+24,d6
        lea     GB_MAP,a1
        move.l  #8192,d7
        bsr.w   gb_copy
        move.l  d3,d6
        add.l   GB_HEAD+28,d6
        lea     GB_BLACK,a1
        move.l  #1024,d7
        bsr.w   gb_copy
        /* plane B: empty (black cells come with the columns / rows) or the parallax texture
           (mode 1: one pattern everywhere; mode 2: cell (u, v) = word (u + v) % 4) */
        move.w  #0xA000,d0
        bsr.w   gb_vram_w
        moveq   #0,d2                   /* plane row v */
50:     moveq   #0,d3                   /* plane column u */
51:     moveq   #0,d0
        tst.w   GB_HEAD+38
        beq.b   52f
        move.w  d2,d1
        add.w   d3,d1
        andi.w  #3,d1
        cmpi.w  #1,GB_HEAD+38
        bne.b   53f
        moveq   #0,d1
53:     add.w   d1,d1
        lea     GB_HEAD+40,a0
        move.w  0(a0,d1.w),d0
52:     move.w  d0,0xC00000
        addq.w  #1,d3
        cmpi.w  #64,d3
        blo.b   51b
        addq.w  #1,d2
        cmpi.w  #32,d2
        blo.b   50b
        move.w  #0xE000,d0
        bsr.w   gb_vram_w
        move.w  #2047,d0
6:      move.w  #0,0xC00000
        dbra    d0,6b
        move.w  #0xDC00,d0
        bsr.w   gb_vram_w
        moveq   #3,d0
7:      move.w  #0,0xC00000
        dbra    d0,7b
        move.w  #0xF000,d0
        bsr.w   gb_vram_w
        move.l  #0,0xC00000
        move.l  #0x40000010,(a5)
        move.l  #0,0xC00000
        move.w  #1,GB_V+gbv_ok
        moveq   #1,d0
        bra.b   99f
90:     moveq   #0,d0
99:     move.w  #0,0xA15104
        move.w  (sp)+,sr
        movem.l (sp)+,d2-d7/a2-a5
        rts

| int genbg_start(x << 16 | y): the window filled for that camera, scroll set, the vblank
| updates (and the display) on. 1 = done, 0 = no data loaded.
genbg_start:
        movem.l d2-d7/a2-a5,-(sp)
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        moveq   #0,d0
        tst.w   GB_V+gbv_ok
        beq.b   99f
        move.l  46(sp),d6
        move.w  d6,d7                   /* y */
        swap    d6                      /* x */
        move.w  d6,d0
        asr.w   #3,d0
        subi.w  #11,d0
        move.w  d0,GB_V+gbv_cx
        move.w  d7,d0
        asr.w   #3,d0
        subq.w  #5,d0
        move.w  d0,GB_V+gbv_cy
        movem.w d6-d7,-(sp)
        bsr.w   gb_fill
        movem.w (sp)+,d6-d7
        move.w  d7,d5
        bsr.w   gb_scroll
        moveq   #0,d7                   /* colours, not faded (the vblank fades them) */
        bsr.w   gb_cram
        move.w  #1,GB_V+gbv_on
        moveq   #1,d0
99:     move.w  (sp)+,sr
        movem.l (sp)+,d2-d7/a2-a5
        rts

| int genbg_off(void): updates off, the Genesis display off, the listener's own VDP set-up,
| font and text colours back (for its messages)
genbg_off:
        movem.l d2-d7/a2-a5,-(sp)
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        clr.w   GB_V+gbv_on
        lea     0xC00004,a5
        move.w  #0x8134,(a5)
        move.w  #0x8230,(a5)            /* the listener's tables (md_init_hw InitVDPRegs) */
        move.w  #0x832C,(a5)
        move.w  #0x8407,(a5)
        move.w  #0x8554,(a5)
        move.w  #0x8D2B,(a5)
        move.l  #0x6C000002,(a5)        /* h-scroll at 0xAC00: 0 */
        move.l  #0,0xC00000
        move.l  #0x40000010,(a5)
        move.l  #0,0xC00000
        bsr.w   md_font
        move.w  (sp)+,sr
        movem.l (sp)+,d2-d7/a2-a5
        moveq   #1,d0
        rts

| int genbg_cell(cell << 16 | tile): a tile of the map changed (destroyed block ...); its
| 4 x 4 cells on plane A if they are in the window
genbg_cell:
        movem.l d2-d7/a2-a5,-(sp)
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        move.l  46(sp),d6
        moveq   #0,d5
        move.w  d6,d5                   /* tile (255 = nothing) */
        swap    d6                      /* cell = y * 256 + x */
        andi.w  #0x3FFF,d6
        lea     GB_MAP,a0
        move.b  d5,0(a0,d6.w)
        tst.w   GB_V+gbv_on
        beq.b   99f
        move.w  d6,d2                   /* first world cell x, y */
        andi.w  #255,d2
        lsl.w   #2,d2
        move.w  d6,d3
        lsr.w   #8,d3
        lsl.w   #2,d3
        moveq   #3,d7                   /* 4 rows */
1:      moveq   #3,d6                   /* 4 columns */
        move.w  d3,d0                   /* in the window? rows cy .. cy + 31 */
        sub.w   GB_V+gbv_cy,d0
        cmpi.w  #32,d0
        bhs.b   4f
2:      move.w  d2,d0
        sub.w   GB_V+gbv_cx,d0
        cmpi.w  #64,d0
        bhs.b   3f
        bsr.w   gb_cell
        move.w  d0,d4                   /* plane A word */
        move.w  d3,d0
        andi.w  #31,d0
        lsl.w   #7,d0
        move.w  d2,d1
        andi.w  #63,d1
        add.w   d1,d1
        add.w   d1,d0
        addi.w  #0xE000,d0
        bsr.w   gb_vram_w
        move.w  d4,0xC00000
3:      addq.w  #1,d2
        dbra    d6,2b
        subq.w  #4,d2
4:      addq.w  #1,d3
        dbra    d7,1b
99:     move.w  (sp)+,sr
        movem.l (sp)+,d2-d7/a2-a5
        moveq   #1,d0
        rts

| int set_vsram(int word_address_value): CD32X ledger additions command 36 - one VSRAM word:
| arg = address << 16 | value (address 0 = plane A, 2 = plane B)
set_vsram:
        move.l  4(sp),d0
        move.l  d0,d1
        swap    d1
        andi.l  #0x7E,d1
        swap    d1
        ori.l   #0x40000010,d1
        move.w  sr,-(sp)
        move.w  #0x2700,sr
        move.l  d1,0xC00004
        move.w  d0,0xC00000
        move.w  (sp)+,sr
        moveq   #0,d0
        rts

| the listener's font into video memory 0 and its text colours (as md_init_hw)
md_font:
        lea     0xC00000,a3
        move.w  #0x8F02,4(a3)
        move.l  #0x40000000,4(a3)
        lea     font_data(pc),a0
        move.w  #0x6B*8-1,d2
1:      move.l  (a0)+,d0
        andi.l  #0x11111111,d0
        move.l  d0,(a3)
        dbra    d2,1b
        move.l  #0xC0000000,4(a3)
        move.l  #0x00000CCC,(a3)
        move.l  #0xC0200000,4(a3)
        move.l  #0x000000A0,(a3)
        move.l  #0xC0400000,4(a3)
        move.l  #0x0000000A,(a3)
        rts

| ---------------------------------------------------------------------------------------------
| vblank: scroll to the camera, the colours, then the columns / rows that came into view.
| Runs first in vblank_int (interrupt level 6). Everything it reads is in work RAM or a
| COMM register; it never touches the cart.
genbg_vblank:
        tst.w   GB_V+gbv_on
        bne.b   1f
        rts
1:      movem.l d0-d7/a0-a5,-(sp)
        move.w  0xA15124,d6             /* camera x */
        move.w  0xA15126,d7             /* camera y, fade, hide */
        cmp.w   0xA15124,d6             /* the SH2 may have been writing: read again */
        beq.b   2f
        move.w  0xA15124,d6
        move.w  0xA15126,d7
2:      lea     0xC00004,a5
        move.w  #0x8174,d0              /* the display: on, unless the SH2 shows another screen */
        tst.w   d7
        bpl.b   3f
        move.w  #0x8134,d0
3:      move.w  d0,(a5)
        move.w  d7,d5
        andi.w  #0x7FF,d5               /* camera y */
        bsr.w   gb_scroll
        movem.w d5-d6,-(sp)
        bsr.w   gb_cram                 /* d7: fade */
        movem.w (sp)+,d5-d6
        /* where the window should be */
        move.w  d6,d2
        asr.w   #3,d2
        subi.w  #11,d2                  /* target cx */
        move.w  d5,d3
        asr.w   #3,d3
        subq.w  #5,d3                   /* target cy */
        move.w  d2,d0                   /* far away (a jump): everything again */
        sub.w   GB_V+gbv_cx,d0
        bpl.b   4f
        neg.w   d0
4:      cmpi.w  #24,d0
        bhi.b   5f
        move.w  d3,d0
        sub.w   GB_V+gbv_cy,d0
        bpl.b   41f
        neg.w   d0
41:     cmpi.w  #16,d0
        bls.b   6f
5:      move.w  d2,GB_V+gbv_cx
        move.w  d3,GB_V+gbv_cy
        bsr.w   gb_fill
        bra.w   21f
6:      /* columns: one a vblank, GB_COLS when far behind (the window has 11 spare columns on
           either side, so a short lag never shows; few cells a vblank keep the interrupt short:
           the 68000 answers the Sub-CPU's commands - the SH2's pad request - only after it) */
        movem.w d2-d3,-(sp)
        moveq   #0,d4
        move.w  d2,d0
        sub.w   GB_V+gbv_cx,d0
        bpl.b   61f
        neg.w   d0
61:     cmpi.w  #7,d0
        blo.b   7f
        moveq   #GB_COLS-1,d4
7:      move.w  (sp),d2                 /* target cx */
        move.w  GB_V+gbv_cx,d0
        cmp.w   d2,d0
        beq.b   10f
        blt.b   8f
        subq.w  #1,d0                   /* left: the column cx - 1 */
        move.w  d0,GB_V+gbv_cx
        move.w  d0,d2
        bra.b   9f
8:      move.w  d0,d2                   /* right: the column cx + 64, then cx + 1 */
        addi.w  #64,d2
        addq.w  #1,d0
        move.w  d0,GB_V+gbv_cx
9:      move.w  d4,-(sp)
        bsr.w   gb_column
        move.w  (sp)+,d4
        dbra    d4,7b
10:     /* rows: one a vblank, GB_ROWS when behind by 3 or more (5 spare rows) */
        moveq   #0,d4
        move.w  2(sp),d0
        sub.w   GB_V+gbv_cy,d0
        bpl.b   101f
        neg.w   d0
101:    cmpi.w  #3,d0
        blo.b   11f
        moveq   #GB_ROWS-1,d4
11:     move.w  2(sp),d3                /* target cy */
        move.w  GB_V+gbv_cy,d0
        cmp.w   d3,d0
        beq.b   14f
        blt.b   12f
        subq.w  #1,d0                   /* up: the row cy - 1 */
        move.w  d0,GB_V+gbv_cy
        move.w  d0,d3
        bra.b   13f
12:     move.w  d0,d3                   /* down: the row cy + 32, then cy + 1 */
        addi.w  #32,d3
        addq.w  #1,d0
        move.w  d0,GB_V+gbv_cy
13:     move.w  d4,-(sp)
        bsr.w   gb_row
        move.w  (sp)+,d4
        dbra    d4,11b
14:     addq.l  #4,sp
20:     move.b  0xC00008,d0             /* test: the most lines a pass took since line 224 */
        subi.b  #0xE0,d0
        cmp.b   GB_V+gbv_lines+1,d0
        bls.b   21f
        move.b  d0,GB_V+gbv_lines+1
21:
        movem.l (sp)+,d0-d7/a0-a5
        rts
