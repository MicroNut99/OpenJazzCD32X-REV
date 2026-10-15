/*
 * OpenJazz CD32X - SH2 memory layout. Tyrian 32X T3j's tyrian.ld, plus what C++ needs:
 *   - .eh_frame and .gcc_except_table are KEPT (tyrian.ld discarded .eh_frame: Tyrian is C);
 *     plat_mars.c registers .eh_frame with the unwinder (__register_frame_info) before main
 *   - .init_array / .ctors (C++ global constructors) are collected; plat_mars.c runs them
 *
 * Cart (SH2 view 0x02000000 = the RAM cart the loader filled; cached)
 *   0x02008000  .sdcode + .data load image  -> copied to SDRAM by the loader
 *   then        .text / .rodata / .eh_frame -> run / read in place from the cart
 *   the engine must end before cart 0x100000 (graphics area, port/plat.h)
 * SDRAM 0x06000000 - 0x0603FFFF (256 KB)
 *   .sdcode (crt0: vectors, interrupt handlers), .data, .bss
 *   heap        __heap_start .. __heap_end (malloc: port/mars_syscalls.c)
 *   0x0603B800  master stack, 16 KB (grows down from 0x0603F800)
 *   0x0603F800  slave stack, 2 KB (grows down from 0x06040000)
 * The 32X header (cart 0x3D4) gets: source = 0x8000, destination = 0,
 * length = __sdram_load_size, entries = pri_start / sec_start, VBRs (tools/make_oj_rom.py).
 */
OUTPUT_ARCH(sh)
ENTRY(pri_start)

ROM_BASE      = 0x02000000;
SH2_ROM_OFF   = 0x00008000;
SDRAM_BASE    = 0x06000000;
STACKS_BOTTOM = 0x0603B800;

MEMORY
{
    rom   (rx) : ORIGIN = 0x02008000, LENGTH = 0x000F8000
    sdram (wx) : ORIGIN = 0x06000000, LENGTH = 0x0003B800
}

SECTIONS
{
    /* ---- copied into SDRAM by the loader ---- */
    .sdcode SDRAM_BASE : AT(ROM_BASE + SH2_ROM_OFF)
    {
        __sdram_start = .;
        KEEP(*(.sdcode))
        *(.sdcode.*)
        . = ALIGN(4);
    } > sdram

    .data : AT(ROM_BASE + SH2_ROM_OFF + SIZEOF(.sdcode))
    {
        __data_start = .;
        *(.data) *(.data.*)
        *(.sdata) *(.sdata.*)
        . = ALIGN(16);
        __data_end = .;
    } > sdram

    __sdram_load_size = SIZEOF(.sdcode) + SIZEOF(.data);

    /* ---- runs / is read from the cart ---- */
    .text ROM_BASE + SH2_ROM_OFF + __sdram_load_size :
    {
        __text_start = .;
        *(.text) *(.text.*)
        *(.rodata) *(.rodata.*)
        KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array))
        __init_array_end = .;
        __ctors_start = .; KEEP(*(.ctors)) __ctors_end = .;
        __dtors_start = .; KEEP(*(.dtors)) __dtors_end = .;
        . = ALIGN(4);
        *(.gcc_except_table) *(.gcc_except_table.*)
        . = ALIGN(4);
        __eh_frame_start = .;
        KEEP(*(.eh_frame))
        LONG(0)                        /* end of the frame list (what crtend.o adds) */
        . = ALIGN(16);
        __text_end = .;
    } > rom

    __rom_program_end = __text_end;

    /* ---- zero-filled, in SDRAM ---- */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss) *(.bss.*)
        *(.sbss) *(.sbss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > sdram

    __heap_start = ALIGN(__bss_end, 16);
    __heap_end   = STACKS_BOTTOM;

    ASSERT(__heap_start < __heap_end, "OpenJazz 32X: .data + .bss do not fit in SDRAM")
    ASSERT(__sdram_load_size <= 0x30000, "OpenJazz 32X: SDRAM image too big for the CD32X loader")

    /DISCARD/ : { *(.comment) *(.note*) }
}
