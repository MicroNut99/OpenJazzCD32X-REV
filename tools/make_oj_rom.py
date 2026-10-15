#!/usr/bin/env python3
"""make_oj_rom.py - the OpenJazz engine as a 32X cartridge image for the CD32X loader.

  python3 tools/make_oj_rom.py tools/mars_boot.bin mars/openjazz32x.elf mars/openjazz32x.bin OUT.32X

Tyrian 32X T3j's make_tyrian_cart.py, minus the 68000 program and the ROM file system
(OpenLara's loader starts only the SH2s; the 68000 stays in the Sub-CPU's listener):
  0x000000  standard 32X boot block (mars_boot.bin), patched: titles, ROM end, the
            32X header's SH2 values (0x3D4 source, 0x3D8 destination, 0x3DC length,
            0x3E0/0x3E4 entries, 0x3E8/0x3EC VBRs - what cdloader_sh2.c reads), checksum,
            module name "OpenJazz 32X" at 0x3C0, marker "CDB2" at 0x3BC
  0x008000  openjazz32x.bin: .sdcode + .data (copied to SDRAM), then .text (runs in the cart)
The file is cut after the program (rounded up to the loader's 32 KB chunks): the loader
copies only what the file holds, so the boot stays short.
"""
import struct
import sys

SH2_OFF = 0x8000
CHUNK = 0x8000
ENGINE_END = 0x100000      # graphics area starts here (port/plat.h PLAT_CART_DATA_OFFSET)


def elf_symbols(path):
    d = open(path, "rb").read()
    assert d[:4] == b"\x7fELF" and d[4] == 1, "not a 32-bit ELF file: " + path
    e = ">" if d[5] == 2 else "<"
    shoff, = struct.unpack_from(e + "I", d, 0x20)
    shentsize, shnum = struct.unpack_from(e + "HH", d, 0x2E)
    sections = [struct.unpack_from(e + "IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    syms = {}
    for s in sections:
        if s[1] != 2:
            continue
        strtab = sections[s[6]]
        for off in range(s[4], s[4] + s[5], 16):
            name_off, value = struct.unpack_from(e + "II", d, off)
            start = strtab[4] + name_off
            name = d[start:d.index(b"\0", start)].decode("ascii", "replace")
            if name:
                syms.setdefault(name, value)
    return syms


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    boot_path, elf_path, sh2_path, out = sys.argv[1:5]
    boot = bytearray(open(boot_path, "rb").read())
    assert len(boot) == 0x800 and boot[0x100:0x108] == b"SEGA 32X", "bad mars_boot.bin"
    sh2 = open(sh2_path, "rb").read()

    sym = elf_symbols(elf_path)
    def need(name):
        if name not in sym:
            sys.exit("*** symbol %s missing from %s" % (name, elf_path))
        return sym[name]

    size = need("__sdram_load_size")
    ment, sent = need("pri_start"), need("sec_start")
    mvbr, svbr = need("pri_vbr"), need("sec_vbr")
    prog_end = need("__rom_program_end") - 0x02000000
    heap_start, heap_end = need("__heap_start"), need("__heap_end")
    for name, v in (("pri_start", ment), ("sec_start", sent), ("pri_vbr", mvbr), ("sec_vbr", svbr)):
        if v >> 24 != 0x06:
            sys.exit("*** %s = 0x%08X is not in SDRAM (crt0 must be in .sdcode)" % (name, v))
    # the CD32X loader's own limits (cdloader_sh2.c: error 4DE3)
    if size % 4 or size == 0 or size > 0x30000:
        sys.exit("*** SDRAM load size %d outside the loader's limit (0x30000)" % size)
    if SH2_OFF + len(sh2) != prog_end:
        sys.exit("*** %s (%d bytes) does not end at __rom_program_end (0x%X)" % (sh2_path, len(sh2), prog_end))
    if prog_end > ENGINE_END:
        sys.exit("*** engine ends at 0x%X, past 0x%X where the graphics area starts" % (prog_end, ENGINE_END))

    rom_len = (prog_end + CHUNK - 1) // CHUNK * CHUNK
    rom = bytearray(b"\xFF" * rom_len)
    rom[0:0x800] = boot

    def put_str(off, text, n):
        rom[off:off + n] = text.encode("ascii")[:n].ljust(n, b" ")

    put_str(0x120, "OPENJAZZ 32X CD", 48)
    put_str(0x150, "OPENJAZZ 32X CD", 48)
    rom[0x1A0:0x1A8] = struct.pack(">II", 0, rom_len - 1)
    rom[0x1B0:0x1BC] = b" " * 12
    rom[0x3BC:0x3C0] = b"CDB2"                                # prep_openlara_cd.py's marker
    put_str(0x3C0, "OpenJazz 32X", 16)
    rom[0x3D0:0x3F0] = struct.pack(">8I", 0, SH2_OFF, 0, size, ment, sent, mvbr, svbr)
    rom[SH2_OFF:SH2_OFF + len(sh2)] = sh2

    ck = 0
    for i in range(0x200, rom_len, 2):
        ck = (ck + (rom[i] << 8 | rom[i + 1])) & 0xFFFF
    rom[0x18E:0x190] = struct.pack(">H", ck)
    open(out, "wb").write(rom)

    print("  SH2 program  at 0x%06X, %7d bytes (%d copied to SDRAM), master 0x%08X slave 0x%08X"
          % (SH2_OFF, len(sh2), size, ment, sent))
    print("  engine ends  at 0x%06X (graphics area from 0x%06X: %d bytes to spare)"
          % (prog_end, ENGINE_END, ENGINE_END - prog_end))
    print("  SDRAM heap   0x%08X - 0x%08X = %d bytes for malloc" % (heap_start, heap_end, heap_end - heap_start))
    print("written %s (%d bytes = %d chunks of 32 KB, checksum 0x%04X)" % (out, rom_len, rom_len // CHUNK, ck))


if __name__ == "__main__":
    main()
