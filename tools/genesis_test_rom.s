| Genesis test ROM for genbg_md.s (the REV Genesis plane code), built by tools/genesis_test_rom.sh:
| COMM4 / COMM6 / the bank register are work RAM here (0xFFF010 / 0xFFF012 / 0xFFF014); the level
| file is at ROM 0x80000. The camera moves like the 32X's (a step every 5 vblanks), then jumps.
        .equ    CAMX, 0xFFF010
        .equ    CAMY, 0xFFF012
        .equ    FRAMES, 0xFFF100
        .equ    RESULT, 0xFFF104
        .equ    STEP, 0xFFF108
        .text
        .org    0
        .long   0x00FFFE00
        .long   start
        .rept   28
        .long   def
        .endr
        .long   vbl
        .rept   33
        .long   def
        .endr
        .org    0x100
        .ascii  "SEGA GENESIS    "
        .org    0x200
def:    rte
start:
        move.w  #0x2700,sr
        move.b  0xA10001,d0
        andi.b  #0x0F,d0
        beq.b   1f
        move.l  #0x53454741,0xA14000
1:      lea     0xC00004,a5
        move.w  #0x8004,(a5)
        move.w  #0x8134,(a5)
        move.w  #0x8230,(a5)
        move.w  #0x8C81,(a5)
        move.w  #0x8F02,(a5)
        move.w  #0x9001,(a5)
        lea     0xFF8000,a0             /* clear the top of work RAM */
        move.w  #0x2000-1,d0
2:      clr.l   (a0)+
        dbra    d0,2b
        move.l  #0x80000,-(sp)
        jsr     genbg_load
        addq.l  #4,sp
        move.l  d0,RESULT
        move.w  #200,CAMX
        move.w  #700,CAMY
        move.l  #(200<<16)+700,-(sp)
        jsr     genbg_start
        addq.l  #4,sp
        move.l  d0,RESULT+4
        move.w  #0x2000,sr
        moveq   #0,d7                   /* the camera path: a step every 5 frames like the 32X */
3:      move.l  FRAMES,d0
4:      cmp.l   FRAMES,d0
        beq.b   4b
        addq.w  #1,d7
        cmpi.w  #5,d7
        blo.b   3b
        moveq   #0,d7
        addq.w  #1,STEP
        move.w  STEP,d0
        cmpi.w  #2,d0                   /* the measure starts here: the vblank that was held */
        bne.b   8f                      /* back during genbg_start ran late */
        clr.w   0xFFEECE
8:      cmpi.w  #30,d0
        bhi.b   5f
        addi.w  #37,CAMX                /* right and down */
        addi.w  #13,CAMY
        bra.b   3b
5:      cmpi.w  #50,d0
        bhi.b   6f
        subi.w  #29,CAMX                /* left and up */
        subi.w  #21,CAMY
        bra.b   3b
6:      cmpi.w  #51,d0
        bne.b   7f
        addi.w  #2100,CAMX              /* a jump */
        move.l  #((10<<8)+60)*65536+7,-(sp) /* and a destroyed block: tile (60, 10) -> 7 */
        jsr     genbg_cell
        addq.l  #4,sp
7:      bra.b   3b

vbl:    movem.l d0-d1/a0-a1,-(sp)
        bsr.w   genbg_vblank
        addq.l  #1,FRAMES
        movem.l (sp)+,d0-d1/a0-a1
        rte

font_data:
        .space  0x6B*8*4
        .include "genbg_test.s"
